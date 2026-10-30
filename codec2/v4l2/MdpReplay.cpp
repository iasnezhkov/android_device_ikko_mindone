// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: BSD-3-Clause

#include <v4l2_codec2/v4l2/MdpReplay.h>

#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <log/log.h>

namespace mdpreplay {

namespace {

thread_local MdpReplayEngine *g_activeEngine = nullptr;

constexpr char kMdpDeviceNode[] = "/dev/mtk_mdp";

}

MdpReplayEngine *ActiveRecordingEngine() {
    return g_activeEngine;
}

MdpReplayEngine::MdpReplayEngine() {
    for (int i = 0; i < kMaxTrackedFds; i++) {
        trackedFds_[i] = -1;
    }
}

MdpReplayEngine::~MdpReplayEngine() {
    teardown();
}

void MdpReplayEngine::BeginRecord(const Config &cfg, int srcFd, int dstFd) {
    (void)cfg;
    jobCount_ = 0;
    jobOverflow_ = false;
    waitSeen_ = 0;
    mdpFdSeen_ = -1;
    consecutiveReplayFailures_ = 0;
    permanentlyDisabled_ = false;
    recordSrcFd_ = srcFd;
    recordDstFd_ = dstFd;
    ready_ = false;
    recording_ = true;
    g_activeEngine = this;
}

void MdpReplayEngine::OnAsyncExec(int fd, const void *submitArg) {
    if (!recording_) {
        return;
    }
    if (mdpFdSeen_ < 0) {
        mdpFdSeen_ = fd;
    } else if (mdpFdSeen_ != fd) {
        fprintf(stderr, "mdpreplay: CMDQ_IOCTL_ASYNC_EXEC seen on fd %d, first was %d\n", fd,
                mdpFdSeen_);
    }
    if (jobCount_ >= kMaxJobs) {
        jobOverflow_ = true;
        return;
    }

    const MdpSubmit *submit = reinterpret_cast<const MdpSubmit *>(submitArg);
    CapturedJob &job = jobs_[jobCount_];
    job = CapturedJob();
    job.submit = *submit;

    uint32_t copyCount = submit->meta_count;
    if (copyCount > kMaxMetasPerJob) {
        job.truncated = true;
        copyCount = kMaxMetasPerJob;
    }
    job.metaCount = copyCount;
    if (copyCount && submit->metas) {
        memcpy(job.metas, reinterpret_cast<const void *>(static_cast<uintptr_t>(submit->metas)),
               static_cast<size_t>(copyCount) * sizeof(OpMeta));
    }

    if (submit->prop_size && submit->prop_addr) {
        memcpy(&job.propSnapshot,
               reinterpret_cast<const void *>(static_cast<uintptr_t>(submit->prop_addr)),
               sizeof(job.propSnapshot));
        job.hasPropSnapshot = true;
    }

    if (submit->read_count_v1 && submit->hw_metas_read_v1) {
        uint32_t hwCount = submit->read_count_v1;
        if (hwCount > kMaxHwMeta) {
            hwCount = kMaxHwMeta;
        }
        memcpy(job.hwSnapshot,
               reinterpret_cast<const void *>(static_cast<uintptr_t>(submit->hw_metas_read_v1)),
               static_cast<size_t>(hwCount) * sizeof(HwMeta));
        job.hasHwSnapshot = true;
    }

    jobCount_++;
}

void MdpReplayEngine::OnAsyncWait() {
    if (!recording_) {
        return;
    }
    waitSeen_++;
}

void MdpReplayEngine::classifyRoles(int srcFd, int dstFd) {
    for (int j = 0; j < jobCount_; j++) {
        CapturedJob &job = jobs_[j];
        job.unknownFdCount = 0;
        for (uint32_t i = 0; i < job.metaCount; i++) {
            OpMeta &m = job.metas[i];
            if (m.op != kOpWriteFd && m.op != kOpWriteFdRdma) {
                job.role[i] = 0;
                continue;
            }
            if (static_cast<int>(m.fd) == srcFd) {
                job.role[i] = 1;
            } else if (static_cast<int>(m.fd) == dstFd) {
                job.role[i] = 2;
            } else {
                job.role[i] = 3;
                job.unknownFdCount++;
            }
        }
    }
}

bool MdpReplayEngine::validateCapture(char *reason, size_t reasonLen) {
    if (jobOverflow_) {
        snprintf(reason, reasonLen,
                 "invalidate() issued more than %d CMDQ_IOCTL_ASYNC_EXEC calls, capture is "
                 "incomplete",
                 kMaxJobs);
        return false;
    }
    for (int j = 0; j < jobCount_; j++) {
        if (jobs_[j].truncated) {
            snprintf(reason, reasonLen,
                     "job %d has meta_count > %d, capture is incomplete", j, kMaxMetasPerJob);
            return false;
        }
    }
    int unknown = 0;
    for (int j = 0; j < jobCount_; j++) {
        unknown += jobs_[j].unknownFdCount;
    }
    if (unknown != 0) {
        snprintf(reason, reasonLen,
                 "%d op_meta entries reference a dmabuf fd that is neither the record-time src "
                 "nor dst fd",
                 unknown);
        return false;
    }
    return true;
}

bool MdpReplayEngine::ensureDevice(char *reason, size_t reasonLen) {
    if (deviceOpened_) {
        return true;
    }

    mdpFd_ = open(kMdpDeviceNode, O_RDWR | O_CLOEXEC);
    if (mdpFd_ < 0) {
        mdpFd_ = open(kMdpDeviceNode, O_RDONLY | O_CLOEXEC);
    }
    if (mdpFd_ < 0) {
        snprintf(reason, reasonLen, "open %s failed: %s", kMdpDeviceNode, strerror(errno));
        return false;
    }
    deviceOpened_ = true;

    MdpIovaCacheCtl cacheReq;
    cacheReq.enable = 1;
    long rc = syscall(__NR_ioctl, mdpFd_, kCmdqIoctlIovaCache, &cacheReq);
    cacheEnabled_ = (rc == 0);

    MdpIovaUnmap probe;
    probe.fd = -1;
    long rc2 = syscall(__NR_ioctl, mdpFd_, kCmdqIoctlIovaUnmap, &probe);
    iovaUnmapSupported_ = (rc2 == 0 || errno == EINVAL);

    return true;
}

bool MdpReplayEngine::EndRecord(const Config &cfg, int srcFd, int dstFd, bool invalidateOk,
                                 char *reason, size_t reasonLen) {
    recording_ = false;
    g_activeEngine = nullptr;

    if (!invalidateOk) {
        snprintf(reason, reasonLen, "DpBlitStream::invalidate() returned nonzero");
        ready_ = false;
        return false;
    }
    if (jobCount_ == 0) {
        snprintf(reason, reasonLen,
                 "the interposed ioctl() never observed CMDQ_IOCTL_ASYNC_EXEC during "
                 "invalidate(); MdpReplayInterpose.cpp may not be linked into this process's "
                 "-rdynamic executable, or libdpframework.so landed in a different linker "
                 "namespace");
        ready_ = false;
        return false;
    }

    classifyRoles(srcFd, dstFd);
    if (!validateCapture(reason, reasonLen)) {
        ready_ = false;
        return false;
    }
    if (!ensureDevice(reason, reasonLen)) {
        ready_ = false;
        return false;
    }

    cfg_ = cfg;
    ready_ = true;
    return true;
}

bool MdpReplayEngine::Replay(int srcFd, int dstFd) {
    if (!ready_ || permanentlyDisabled_) {
        return false;
    }

    uint64_t jobIds[kMaxJobs];

    for (int j = 0; j < jobCount_; j++) {
        CapturedJob &job = jobs_[j];
        for (uint32_t i = 0; i < job.metaCount; i++) {
            scratch_[j][i] = job.metas[i];
            if (job.role[i] == 1) {
                scratch_[j][i].fd = static_cast<uint32_t>(srcFd);
            } else if (job.role[i] == 2) {
                scratch_[j][i].fd = static_cast<uint32_t>(dstFd);
            }
        }

        MdpSubmit submit = job.submit;
        submit.metas = static_cast<cmdqU32Ptr_t>(reinterpret_cast<uintptr_t>(scratch_[j]));
        submit.meta_count = job.metaCount;
        submit.job_id = 0;
        submit.prop_addr = job.hasPropSnapshot
                                    ? static_cast<cmdqU32Ptr_t>(
                                              reinterpret_cast<uintptr_t>(&job.propSnapshot))
                                    : 0;
        submit.hw_metas_read_v1 =
                job.hasHwSnapshot
                        ? static_cast<cmdqU32Ptr_t>(reinterpret_cast<uintptr_t>(job.hwSnapshot))
                        : 0;

        long rc = syscall(__NR_ioctl, mdpFd_, kCmdqIoctlAsyncExec, &submit);
        if (rc < 0) {
            onReplayFailure("ASYNC_EXEC");
            return false;
        }
        jobIds[j] = submit.job_id;
    }

    for (int j = 0; j < jobCount_; j++) {
        MdpWait wait;
        memset(&wait, 0, sizeof(wait));
        wait.job_id = jobIds[j];
        long rc = syscall(__NR_ioctl, mdpFd_, kCmdqIoctlAsyncWait, &wait);
        if (rc < 0) {
            onReplayFailure("ASYNC_WAIT");
            return false;
        }
    }

    consecutiveReplayFailures_ = 0;
    trackFd(srcFd);
    trackFd(dstFd);
    return true;
}

void MdpReplayEngine::trackFd(int fd) {
    for (int i = 0; i < trackedFdCount_; i++) {
        if (trackedFds_[i] == fd) {
            return;
        }
    }
    if (trackedFdCount_ < kMaxTrackedFds) {
        trackedFds_[trackedFdCount_++] = fd;
        return;
    }
    for (int i = 1; i < kMaxTrackedFds; i++) {
        trackedFds_[i - 1] = trackedFds_[i];
    }
    trackedFds_[kMaxTrackedFds - 1] = fd;
}

void MdpReplayEngine::onReplayFailure(const char *step) {
    consecutiveReplayFailures_++;
    if (consecutiveReplayFailures_ >= kMaxConsecutiveReplayFailures && !permanentlyDisabled_) {
        permanentlyDisabled_ = true;
        ready_ = false;
        ALOGW("MdpReplayEngine: %d consecutive %s failures, disabling replay for this shape "
              "(falling back to per-frame DpBlitStream)",
              consecutiveReplayFailures_, step);
    }
}

void MdpReplayEngine::teardown() {
    if (!deviceOpened_) {
        return;
    }
    if (iovaUnmapSupported_) {
        for (int i = 0; i < trackedFdCount_; i++) {
            MdpIovaUnmap req;
            req.fd = trackedFds_[i];
            syscall(__NR_ioctl, mdpFd_, kCmdqIoctlIovaUnmap, &req);
        }
    }
    close(mdpFd_);
    mdpFd_ = -1;
    deviceOpened_ = false;
    ready_ = false;
    trackedFdCount_ = 0;
}

}
