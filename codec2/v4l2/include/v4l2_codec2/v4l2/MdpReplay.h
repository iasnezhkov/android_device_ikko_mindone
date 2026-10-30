// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: BSD-3-Clause

#ifndef MDPREPLAY_MDP_REPLAY_H
#define MDPREPLAY_MDP_REPLAY_H

#include <cstdint>
#include <cstddef>

namespace mdpreplay {

using cmdqU32Ptr_t = uint64_t;

struct OpMeta {
    uint8_t op;
    uint16_t engine;
    union {
        uint16_t offset;
        uint16_t event;
        struct {
            uint8_t pipe_idx;
            uint8_t cpr_idx;
        };
    };
    union {
        uint32_t value;
        uint32_t readback_id;
        uint32_t fd;
        uint32_t sec_handle;
        struct {
            uint16_t from_offset;
            uint16_t from_engine;
        };
    };
    union {
        uint32_t mask;
        uint32_t fd_offset;
        uint32_t sec_index;
    };
};

struct HwMeta {
    uint16_t offset;
    uint16_t engine;
};

struct CmdqMetaBuf {
    uint64_t va;
    uint64_t size;
};

constexpr int kCmdqIspMetaCnt = 8;

struct CmdqSecIspMeta {
    CmdqMetaBuf ispBufs[kCmdqIspMetaCnt];
    uint64_t CqSecHandle;
    uint32_t CqSecSize;
    uint32_t CqDesOft;
    uint32_t CqVirtOft;
    uint64_t TpipeSecHandle;
    uint32_t TpipeSecSize;
    uint32_t TpipeOft;
    uint64_t BpciHandle;
    uint64_t LsciHandle;
    uint64_t LceiHandle;
    uint64_t DepiHandle;
    uint64_t DmgiHandle;
};

struct CmdqSecDataStruct {
    bool is_secure;
    uint32_t addrMetadataCount;
    cmdqU32Ptr_t addrMetadatas;
    uint32_t addrMetadataMaxCount;
    uint64_t enginesNeedDAPC;
    uint64_t enginesNeedPortSecurity;
    int32_t waitCookie;
    bool resetExecCnt;
    CmdqSecIspMeta ispMeta;
    uint64_t extension;
};

struct MdpSubmit {
    cmdqU32Ptr_t metas;
    uint32_t meta_count;
    uint32_t priority;
    uint64_t engine_flag;
    uint32_t prop_size;
    uint64_t prop_addr;
    uint64_t readback_ext;
    uint32_t read_count_v1;
    cmdqU32Ptr_t hw_metas_read_v1;
    uint64_t job_id;
    CmdqSecDataStruct secData;
};

struct MdpReadV1 {
    uint32_t count;
    cmdqU32Ptr_t ret_values;
};

struct MdpReadReadback {
    uint32_t count;
    cmdqU32Ptr_t ids;
    cmdqU32Ptr_t ret_values;
};

struct MdpWait {
    uint64_t job_id;
    MdpReadV1 read_v1_result;
    MdpReadReadback read_result;
};

struct MdpIovaUnmap {
    int32_t fd;
};

struct MdpIovaCacheCtl {
    int32_t enable;
};

constexpr int kPmqosIspPortNum = 16;
constexpr int kPmqosMdpPortNum = 4;

struct MdpPmqos {
    uint32_t isp_total_datasize;
    uint32_t isp_total_pixel;
    uint32_t mdp_total_datasize;
    uint32_t mdp_total_pixel;
    uint32_t qos2_isp_bandwidth[kPmqosIspPortNum];
    uint32_t qos2_isp_total_pixel[kPmqosIspPortNum];
    uint32_t qos2_isp_port[kPmqosIspPortNum];
    uint32_t qos2_isp_count;
    uint32_t qos2_mdp_bandwidth[kPmqosMdpPortNum];
    uint32_t qos2_mdp_total_pixel[kPmqosMdpPortNum];
    uint32_t qos2_mdp_port[kPmqosMdpPortNum];
    uint32_t qos2_mdp_count;
    uint64_t tv_sec;
    uint64_t tv_usec;
    uint64_t ispMetString;
    uint32_t ispMetStringSize;
    uint64_t mdpMetString;
    uint32_t mdpMetStringSize;
    uint64_t mdpMMpathString;
    uint32_t mdpMMpathStringSize;
};

static_assert(sizeof(MdpSubmit) == 336,
              "MdpSubmit must be 336 bytes to match CMDQ_IOCTL_ASYNC_EXEC's own _IOW size field");
static_assert(sizeof(OpMeta) == 16, "OpMeta must be 16 bytes, one CMDQ command list entry");
static_assert(sizeof(MdpWait) == 48,
              "MdpWait must be 48 bytes to match CMDQ_IOCTL_ASYNC_WAIT's own _IOR size field");
static_assert(sizeof(CmdqSecDataStruct) == 264, "CmdqSecDataStruct must be 264 bytes");

constexpr uint8_t kOpWrite = 0;
constexpr uint8_t kOpRead = 1;
constexpr uint8_t kOpPoll = 2;
constexpr uint8_t kOpWait = 3;
constexpr uint8_t kOpWaitNoClear = 4;
constexpr uint8_t kOpClear = 5;
constexpr uint8_t kOpSet = 6;
constexpr uint8_t kOpAcquire = 7;
constexpr uint8_t kOpWriteFd = 8;
constexpr uint8_t kOpWriteFromReg = 9;
constexpr uint8_t kOpWriteSec = 10;
constexpr uint8_t kOpReadback = 11;
constexpr uint8_t kOpWriteRdma = 12;
constexpr uint8_t kOpWriteFdRdma = 13;
constexpr uint8_t kOpNop = 14;
constexpr uint8_t kOpWriteSecFd = 15;

constexpr unsigned int kCmdqIoctlMagic = 'x';

constexpr unsigned int kIocNrbits = 8;
constexpr unsigned int kIocTypebits = 8;
constexpr unsigned int kIocSizebits = 14;
constexpr unsigned int kIocNrshift = 0;
constexpr unsigned int kIocTypeshift = kIocNrshift + kIocNrbits;
constexpr unsigned int kIocSizeshift = kIocTypeshift + kIocTypebits;
constexpr unsigned int kIocDirshift = kIocSizeshift + kIocSizebits;
constexpr unsigned int kIocWrite = 1;
constexpr unsigned int kIocRead = 2;

constexpr unsigned int MakeIoc(unsigned int dir, unsigned int type, unsigned int nr, unsigned int size) {
    return (dir << kIocDirshift) | (type << kIocTypeshift) | (nr << kIocNrshift) |
           (size << kIocSizeshift);
}

constexpr unsigned int kCmdqIoctlAsyncExec =
        MakeIoc(kIocWrite, kCmdqIoctlMagic, 20, sizeof(MdpSubmit));
constexpr unsigned int kCmdqIoctlAsyncWait =
        MakeIoc(kIocRead, kCmdqIoctlMagic, 21, sizeof(MdpWait));
constexpr unsigned int kCmdqIoctlIovaUnmap =
        MakeIoc(kIocWrite, kCmdqIoctlMagic, 26, sizeof(MdpIovaUnmap));
constexpr unsigned int kCmdqIoctlIovaCache =
        MakeIoc(kIocWrite, kCmdqIoctlMagic, 27, sizeof(MdpIovaCacheCtl));

constexpr int kMaxJobs = 4;
constexpr int kMaxMetasPerJob = 512;
constexpr int kMaxHwMeta = 64;
constexpr int kMaxTrackedFds = 16;
constexpr size_t kReasonBufSize = 256;
constexpr int kMaxConsecutiveReplayFailures = 3;

struct Config {
    int width = 0;
    int height = 0;
    uint32_t srcPlaneSize0 = 0;
    uint32_t srcPlaneSize1 = 0;
    uint32_t dstPlaneSize0 = 0;
    uint32_t dstPlaneSize1 = 0;
    uint32_t dstPlaneSize2 = 0;
    uint32_t dstPlaneCount = 0;
    uint32_t dstColor = 0;
    uint32_t dstStrideY = 0;
    uint32_t dstStrideUV = 0;

    bool operator==(const Config &other) const {
        return width == other.width && height == other.height &&
               srcPlaneSize0 == other.srcPlaneSize0 && srcPlaneSize1 == other.srcPlaneSize1 &&
               dstPlaneSize0 == other.dstPlaneSize0 && dstPlaneSize1 == other.dstPlaneSize1 &&
               dstPlaneSize2 == other.dstPlaneSize2 && dstPlaneCount == other.dstPlaneCount &&
               dstColor == other.dstColor && dstStrideY == other.dstStrideY &&
               dstStrideUV == other.dstStrideUV;
    }
    bool operator!=(const Config &other) const { return !(*this == other); }
};

class MdpReplayEngine {
public:
    MdpReplayEngine();
    ~MdpReplayEngine();
    MdpReplayEngine(const MdpReplayEngine &) = delete;
    MdpReplayEngine &operator=(const MdpReplayEngine &) = delete;

    void BeginRecord(const Config &cfg, int srcFd, int dstFd);

    void OnAsyncExec(int fd, const void *submitArg);
    void OnAsyncWait();

    bool EndRecord(const Config &cfg, int srcFd, int dstFd, bool invalidateOk, char *reason,
                    size_t reasonLen);

    bool Replay(int srcFd, int dstFd);

    bool ready() const { return ready_; }
    bool permanentlyDisabled() const { return permanentlyDisabled_; }
    bool configMatches(const Config &cfg) const { return ready_ && cfg_ == cfg; }
    bool iovaCacheEnabled() const { return cacheEnabled_; }
    bool iovaUnmapSupported() const { return iovaUnmapSupported_; }
    int jobCount() const { return jobCount_; }

private:
    struct CapturedJob {
        MdpSubmit submit{};
        OpMeta metas[kMaxMetasPerJob]{};
        uint32_t metaCount = 0;
        uint8_t role[kMaxMetasPerJob]{};
        bool truncated = false;
        int unknownFdCount = 0;
        MdpPmqos propSnapshot{};
        bool hasPropSnapshot = false;
        HwMeta hwSnapshot[kMaxHwMeta]{};
        bool hasHwSnapshot = false;
    };

    bool ensureDevice(char *reason, size_t reasonLen);
    void classifyRoles(int srcFd, int dstFd);
    bool validateCapture(char *reason, size_t reasonLen);
    void onReplayFailure(const char *step);
    void trackFd(int fd);
    void teardown();

    int mdpFd_ = -1;
    bool deviceOpened_ = false;
    bool cacheEnabled_ = false;
    bool iovaUnmapSupported_ = false;

    CapturedJob jobs_[kMaxJobs];
    int jobCount_ = 0;
    bool jobOverflow_ = false;
    int waitSeen_ = 0;
    int mdpFdSeen_ = -1;
    int recordSrcFd_ = -1;
    int recordDstFd_ = -1;
    bool recording_ = false;

    Config cfg_{};
    bool ready_ = false;
    int consecutiveReplayFailures_ = 0;
    bool permanentlyDisabled_ = false;

    int trackedFds_[kMaxTrackedFds];
    int trackedFdCount_ = 0;

    OpMeta scratch_[kMaxJobs][kMaxMetasPerJob];
};

MdpReplayEngine *ActiveRecordingEngine();

}

#endif
