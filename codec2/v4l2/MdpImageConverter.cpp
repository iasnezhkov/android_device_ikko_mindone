// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: BSD-3-Clause

#define LOG_TAG "MdpImageConverter"

#include <v4l2_codec2/v4l2/MdpImageConverter.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <new>

#include <log/log.h>

enum DP_COLOR_ENUM {
    DP_COLOR_420_BLKP = 13434956,
    DP_COLOR_NV12 = 0x4c084c,
    DP_COLOR_NV21 = 0x4c086c,
    DP_COLOR_I420 = 0x6c0848,
    DP_COLOR_YV12 = 0x6c0868,
};

enum DpInterlaceFormat {
    DP_INTERLACE_NONE = 0,
};

enum DP_PROFILE_ENUM { DP_PROFILE_BT601 = 0 };
enum DpSecure { DP_SECURE_NONE = 0 };

struct DpRect;

class DpBlitStream {
public:
    DpBlitStream();
    ~DpBlitStream();
    int setSrcConfig(int width, int height, DP_COLOR_ENUM color, DpInterlaceFormat interlace,
                      DpRect* roi);
    int setSrcBuffer(int fd, unsigned int* planeSizes, unsigned int planeCount);
    int setDstConfig(int width, int height, int yPitch, int uvPitch, DP_COLOR_ENUM color,
                     DP_PROFILE_ENUM profile, DpInterlaceFormat interlace, DpRect* roi,
                     DpSecure secure, bool dither);
    int setDstBuffer(int fd, unsigned int* planeSizes, unsigned int planeCount);
    int invalidate(struct timeval* tv);
};

namespace android {

namespace {
constexpr const char* kMdpDeviceNode = "/dev/mtk_mdp";
constexpr size_t kDpBlitStreamStorageSize = 8192;
}

struct MdpImageConverter::Impl {
    alignas(16) unsigned char storage[kDpBlitStreamStorageSize];
    DpBlitStream* stream = nullptr;
};

std::unique_ptr<MdpImageConverter> MdpImageConverter::Create() {
    const int probe = open(kMdpDeviceNode, O_RDONLY | O_CLOEXEC);
    if (probe < 0) {
        ALOGE("MDP device %s not accessible: %s", kMdpDeviceNode, strerror(errno));
        return nullptr;
    }
    close(probe);

    std::unique_ptr<MdpImageConverter> converter(new MdpImageConverter());
    if (!converter->init()) {
        return nullptr;
    }
    return converter;
}

MdpImageConverter::MdpImageConverter() = default;

MdpImageConverter::~MdpImageConverter() {
    if (mImpl && mImpl->stream) {
        mImpl->stream->~DpBlitStream();
    }
}

bool MdpImageConverter::init() {
    mImpl = std::make_unique<Impl>();
    mImpl->stream = new (mImpl->storage) DpBlitStream();
    return true;
}

bool MdpImageConverter::convertMM21ToNV12(const TiledMM21Image& src, const NV12WritableImage& dst,
                                          int width, int height) {
    if (!mImpl || !mImpl->stream || src.dmaBufFd < 0 || dst.dmaBufFd < 0 || width <= 0 ||
        height <= 0) {
        ALOGE("convertMM21ToNV12: invalid arguments (srcFd=%d dstFd=%d width=%d height=%d)",
              src.dmaBufFd, dst.dmaBufFd, width, height);
        return false;
    }

    if (!src.y || !src.uv || src.strideY > UINT32_MAX || src.strideUV > UINT32_MAX) {
        return false;
    }
    const uint64_t yBytes = static_cast<uint64_t>(src.strideY) * height;
    const uint64_t uvBytes = static_cast<uint64_t>(src.strideUV) * (height / 2);
    if (width % 16 || height % 32 || src.strideY != static_cast<size_t>(width) ||
        src.strideUV != src.strideY || yBytes > UINT32_MAX || uvBytes > UINT32_MAX ||
        yBytes + uvBytes > src.dmaBufSize ||
        reinterpret_cast<uintptr_t>(src.uv) < reinterpret_cast<uintptr_t>(src.y) ||
        reinterpret_cast<uintptr_t>(src.uv) - reinterpret_cast<uintptr_t>(src.y) != yBytes ||
        dst.offsetY != 0 ||
        dst.strideY < static_cast<size_t>(width) || dst.strideY > INT32_MAX ||
        dst.strideUV > INT32_MAX || dst.strideV != dst.strideUV ||
        (dst.chromaStep != 1 && dst.chromaStep != 2)) {
        return false;
    }
    const uint64_t chromaWidth = (static_cast<uint64_t>(width) + 1) / 2;
    const uint64_t chromaRows = (static_cast<uint64_t>(height) + 1) / 2;
    const uint64_t dstYBytes = static_cast<uint64_t>(dst.strideY) * height;
    const uint64_t dstChromaBytes = static_cast<uint64_t>(dst.strideUV) * chromaRows;
    const size_t first = std::min(dst.offsetUV, dst.offsetV);
    const size_t second = std::max(dst.offsetUV, dst.offsetV);
    if (dst.strideUV < chromaWidth * dst.chromaStep || first < dstYBytes ||
        first > UINT32_MAX || second >= dst.dmaBufSize ||
        dstYBytes > dst.sizeY || dstChromaBytes > UINT32_MAX) {
        return false;
    }
    unsigned int srcPlaneSizes[2] = {static_cast<unsigned int>(yBytes),
                                     static_cast<unsigned int>(uvBytes)};
    unsigned int dstPlaneSizes[3] = {static_cast<unsigned int>(first), 0, 0};
    unsigned int dstPlaneCount;
    DP_COLOR_ENUM dstColor;
    if (dst.chromaStep == 2) {
        if (second - first != 1 || dstChromaBytes > dst.dmaBufSize - first ||
            dstChromaBytes > (dst.offsetUV == first ? dst.sizeUV : dst.sizeV)) {
            return false;
        }
        dstPlaneCount = 2;
        dstPlaneSizes[1] = static_cast<unsigned int>(dstChromaBytes);
        dstColor = dst.offsetUV == first ? DP_COLOR_NV12 : DP_COLOR_NV21;
    } else {
        if (second - first < dstChromaBytes || second - first > UINT32_MAX ||
            dstChromaBytes > dst.dmaBufSize - second ||
            dstChromaBytes > dst.sizeUV || dstChromaBytes > dst.sizeV) {
            return false;
        }
        dstPlaneCount = 3;
        dstPlaneSizes[1] = static_cast<unsigned int>(second - first);
        dstPlaneSizes[2] = static_cast<unsigned int>(dstChromaBytes);
        dstColor = dst.offsetUV == first ? DP_COLOR_I420 : DP_COLOR_YV12;
    }
    int rc;

    mdpreplay::Config cfg;
    cfg.width = width;
    cfg.height = height;
    cfg.srcPlaneSize0 = srcPlaneSizes[0];
    cfg.srcPlaneSize1 = srcPlaneSizes[1];
    cfg.dstPlaneSize0 = dstPlaneSizes[0];
    cfg.dstPlaneSize1 = dstPlaneSizes[1];
    cfg.dstPlaneSize2 = dstPlaneSizes[2];
    cfg.dstPlaneCount = dstPlaneCount;
    cfg.dstColor = dstColor;
    cfg.dstStrideY = dst.strideY;
    cfg.dstStrideUV = dst.strideUV;

    if (!mEngine.permanentlyDisabled() && mEngine.configMatches(cfg) &&
        mEngine.Replay(src.dmaBufFd, dst.dmaBufFd)) {
        return true;
    }

    DpBlitStream* const stream = mImpl->stream;

    rc = stream->setSrcConfig(width, height, DP_COLOR_420_BLKP, DP_INTERLACE_NONE, nullptr);
    if (rc != 0) {
        ALOGE("DpBlitStream::setSrcConfig failed, rc=%d", rc);
        return false;
    }
    rc = stream->setSrcBuffer(src.dmaBufFd, srcPlaneSizes, 2);
    if (rc != 0) {
        ALOGE("DpBlitStream::setSrcBuffer failed, rc=%d", rc);
        return false;
    }
    rc = stream->setDstConfig(width, height, static_cast<int>(dst.strideY),
                              static_cast<int>(dst.strideUV), dstColor, DP_PROFILE_BT601,
                              DP_INTERLACE_NONE, nullptr, DP_SECURE_NONE, true);
    if (rc != 0) {
        ALOGE("DpBlitStream::setDstConfig failed, rc=%d", rc);
        return false;
    }
    rc = stream->setDstBuffer(dst.dmaBufFd, dstPlaneSizes, dstPlaneCount);
    if (rc != 0) {
        ALOGE("DpBlitStream::setDstBuffer failed, rc=%d", rc);
        return false;
    }

    if (mEngine.permanentlyDisabled()) {
        rc = stream->invalidate(nullptr);
    } else {
        mEngine.BeginRecord(cfg, src.dmaBufFd, dst.dmaBufFd);
        rc = stream->invalidate(nullptr);
        char reason[mdpreplay::kReasonBufSize];
        if (!mEngine.EndRecord(cfg, src.dmaBufFd, dst.dmaBufFd, rc == 0, reason, sizeof(reason))) {
            ALOGI("MDP replay capture not armed for %dx%d: %s", width, height, reason);
        } else {
            ALOGI("MDP replay capture armed for %dx%d, %d job(s)", width, height,
                  mEngine.jobCount());
        }
    }

    if (rc != 0) {
        ALOGE("DpBlitStream::invalidate failed, rc=%d", rc);
        return false;
    }
    return true;
}

}
