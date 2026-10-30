// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#define LOG_TAG "VideoFrame"

#include <v4l2_codec2/components/VideoFrame.h>

#include <algorithm>
#include <limits>
#include <sys/stat.h>
#include <mutex>
#include <vector>

#include <C2AllocatorGralloc.h>
#include <log/log.h>
#include <v4l2_codec2/common/LibyuvImageConverter.h>

namespace android {

namespace {

bool isSupportedDetileLayout(const C2GraphicView& view, int width, int height) {
    if (view.error() != C2_OK || width <= 0 || height <= 0 ||
        width == std::numeric_limits<int>::max() ||
        height == std::numeric_limits<int>::max() ||
        view.width() < static_cast<uint32_t>(width) ||
        view.height() < static_cast<uint32_t>(height) ||
        view.crop().left != 0 || view.crop().top != 0 ||
        view.crop().width < static_cast<uint32_t>(width) ||
        view.crop().height < static_cast<uint32_t>(height)) {
        return false;
    }
    const C2PlanarLayout& layout = view.layout();
    if (layout.type != C2PlanarLayout::TYPE_YUV || layout.numPlanes != 3 ||
        (layout.rootPlanes != 2 && layout.rootPlanes != 3)) {
        return false;
    }
    uintptr_t starts[3];
    uintptr_t ends[3];
    const int chromaStep = layout.planes[C2PlanarLayout::PLANE_U].colInc;
    if (chromaStep != 1 && chromaStep != 2) {
        return false;
    }
    for (uint32_t i = 0; i < 3; ++i) {
        const C2PlaneInfo& plane = layout.planes[i];
        const uint32_t sampling = i == C2PlanarLayout::PLANE_Y ? 1 : 2;
        const int step = sampling == 1 ? 1 : chromaStep;
        const uint64_t columns = (static_cast<uint64_t>(width) + sampling - 1) / sampling;
        const uint64_t rows = (static_cast<uint64_t>(height) + sampling - 1) / sampling;
        const uint64_t rowBytes = (columns - 1) * step + 1;
        if (!view.data()[i] || plane.allocatedDepth != 8 || plane.bitDepth != 8 ||
            plane.rightShift != 0 || plane.colSampling != sampling ||
            plane.rowSampling != sampling || plane.colInc != step ||
            plane.rowInc <= 0 || plane.rowInc > std::numeric_limits<int>::max() ||
            static_cast<uint64_t>(plane.rowInc) < columns * step) {
            return false;
        }
        starts[i] = reinterpret_cast<uintptr_t>(view.data()[i]);
        const uint64_t span = (rows - 1) * plane.rowInc + rowBytes;
        if (span > std::numeric_limits<uintptr_t>::max() - starts[i]) {
            return false;
        }
        ends[i] = starts[i] + static_cast<uintptr_t>(span);
    }
    const auto overlaps = [&](uint32_t a, uint32_t b) {
        return starts[a] < ends[b] && starts[b] < ends[a];
    };
    if (overlaps(C2PlanarLayout::PLANE_Y, C2PlanarLayout::PLANE_U) ||
        overlaps(C2PlanarLayout::PLANE_Y, C2PlanarLayout::PLANE_V)) {
        return false;
    }
    if (chromaStep == 1) {
        return !overlaps(C2PlanarLayout::PLANE_U, C2PlanarLayout::PLANE_V);
    }
    return starts[C2PlanarLayout::PLANE_U] + 1 == starts[C2PlanarLayout::PLANE_V] &&
           layout.planes[C2PlanarLayout::PLANE_U].rowInc ==
                   layout.planes[C2PlanarLayout::PLANE_V].rowInc;
}

constexpr int kNV12PlaneGeometryCacheSlots = 32;

struct NV12PlaneGeometry : NV12WritableImage {
    bool valid = false;
};

bool readNV12PlaneGeometry(const C2GraphicView& view, size_t allocationSize,
                          int width, int height, NV12PlaneGeometry* geometry) {
    if (view.error() != C2_OK || width <= 0 || height <= 0 ||
        static_cast<uint32_t>(width) > view.crop().width ||
        static_cast<uint32_t>(height) > view.crop().height) {
        return false;
    }
    const C2PlanarLayout layout = view.layout();
    if (!allocationSize ||
        layout.type != C2PlanarLayout::TYPE_YUV || layout.numPlanes != 3 ||
        layout.planes[0].offset != 0 ||
        view.crop().left != 0 || view.crop().top != 0) {
        return false;
    }
    const uint8_t* const* data = view.data();
    if (!data) return false;
    uintptr_t addresses[3];
    for (uint32_t i = 0; i < 3; ++i) {
        const auto& plane = layout.planes[i];
        const uint32_t sampling = i == C2PlanarLayout::PLANE_Y ? 1 : 2;
        if (!data[i] || plane.rowInc <= 0 || plane.bitDepth != 8 ||
            plane.allocatedDepth != 8 || plane.rightShift != 0 ||
            plane.colSampling != sampling || plane.rowSampling != sampling ||
            (sampling == 1 ? plane.colInc != 1 :
                            (plane.colInc != 1 && plane.colInc != 2))) {
            return false;
        }
        addresses[i] = reinterpret_cast<uintptr_t>(data[i]);
    }
    if (layout.planes[1].colInc != layout.planes[2].colInc ||
        addresses[1] < addresses[0] || addresses[2] < addresses[0] ||
        addresses[1] - addresses[0] >= allocationSize ||
        addresses[2] - addresses[0] >= allocationSize) {
        return false;
    }
    geometry->strideY = layout.planes[0].rowInc;
    geometry->strideUV = layout.planes[1].rowInc;
    geometry->strideV = layout.planes[2].rowInc;
    geometry->offsetUV = addresses[1] - addresses[0];
    geometry->offsetV = addresses[2] - addresses[0];
    geometry->sizeY = std::min(geometry->offsetUV, geometry->offsetV);
    geometry->sizeUV = allocationSize - geometry->offsetUV;
    geometry->sizeV = allocationSize - geometry->offsetV;
    geometry->dmaBufSize = allocationSize;
    geometry->chromaStep = layout.planes[1].colInc;
    geometry->valid = true;
    return true;
}

struct NV12PlaneGeometryKey {
    dev_t device = 0;
    ino_t inode = 0;
    off_t size = 0;
    uint32_t width = 0, height = 0, format = 0, stride = 0;
    uint64_t usage = 0;

    bool operator==(const NV12PlaneGeometryKey& o) const {
        return device == o.device && inode == o.inode && size == o.size &&
               width == o.width && height == o.height && format == o.format &&
               stride == o.stride && usage == o.usage;
    }
};

struct NV12PlaneGeometryCacheEntry {
    NV12PlaneGeometryKey key;
    NV12PlaneGeometry geometry;
};

std::mutex g_nv12PlaneGeometryCacheMutex;
NV12PlaneGeometryCacheEntry g_nv12PlaneGeometryCache[kNV12PlaneGeometryCacheSlots];
int g_nv12PlaneGeometryCacheNext = 0;

bool lookupNV12PlaneGeometry(const NV12PlaneGeometryKey& key,
                            NV12PlaneGeometry* out) {
    std::lock_guard<std::mutex> lock(g_nv12PlaneGeometryCacheMutex);
    for (const NV12PlaneGeometryCacheEntry& entry : g_nv12PlaneGeometryCache) {
        if (entry.geometry.valid && entry.key == key) {
            *out = entry.geometry;
            return true;
        }
    }
    return false;
}

void storeNV12PlaneGeometry(const NV12PlaneGeometryKey& key,
                           const NV12PlaneGeometry& geometry) {
    std::lock_guard<std::mutex> lock(g_nv12PlaneGeometryCacheMutex);
    g_nv12PlaneGeometryCache[g_nv12PlaneGeometryCacheNext] =
            NV12PlaneGeometryCacheEntry{key, geometry};
    g_nv12PlaneGeometryCacheNext =
            (g_nv12PlaneGeometryCacheNext + 1) % kNV12PlaneGeometryCacheSlots;
}

}

std::unique_ptr<VideoFrame> VideoFrame::Create(std::shared_ptr<C2GraphicBlock> block) {
    if (!block) return nullptr;

    std::vector<int> fds;
    const C2Handle* const handle = block->handle();
    for (int i = 0; i < handle->numFds; i++) {
        fds.emplace_back(handle->data[i]);
    }

    return std::unique_ptr<VideoFrame>(new VideoFrame(std::move(block), std::move(fds)));
}

VideoFrame::VideoFrame(std::shared_ptr<C2GraphicBlock> block, std::vector<int> fds)
      : mGraphicBlock(std::move(block)), mFds(fds) {}

VideoFrame::~VideoFrame() = default;

const std::vector<int>& VideoFrame::getFDs() const {
    return mFds;
}

void VideoFrame::setVisibleRect(const Rect& visibleRect) {
    mVisibleRect = visibleRect;
}

const Rect& VideoFrame::getVisibleRect() const {
    return mVisibleRect;
}

void VideoFrame::setBitstreamId(int32_t bitstreamId) {
    mBitstreamId = bitstreamId;
}

int32_t VideoFrame::getBitstreamId() const {
    return mBitstreamId;
}

C2ConstGraphicBlock VideoFrame::getGraphicBlock() {
    return mGraphicBlock->share(C2Rect(mVisibleRect.width(), mVisibleRect.height()), C2Fence());
}

bool VideoFrame::detileFrom(ImageConverter* primaryConverter, ImageConverter* fallbackConverter,
                            const TiledMM21Image& src, int width, int height,
                            bool* primaryFailed) {
    if (primaryFailed) {
        *primaryFailed = false;
    }
    if (!mGraphicBlock) {
        return false;
    }

    if (primaryConverter) {
        const C2Handle* const handle = mGraphicBlock->handle();
        bool queried = false;
        struct stat info = {};
        if (handle && handle->numFds > 1 && width > 0 && height > 0 &&
            mGraphicBlock->crop().left == 0 && mGraphicBlock->crop().top == 0 &&
            static_cast<uint32_t>(width) <= mGraphicBlock->crop().width &&
            static_cast<uint32_t>(height) <= mGraphicBlock->crop().height &&
            fstat(handle->data[1], &info) == 0 && info.st_size > 0 &&
            static_cast<uint64_t>(info.st_size) <= std::numeric_limits<size_t>::max()) {
            NV12PlaneGeometry geometry;
            NV12PlaneGeometryKey key;
            key.device = info.st_dev;
            key.inode = info.st_ino;
            key.size = info.st_size;
            uint32_t generation = 0, igbpSlot = 0;
            uint64_t igbpId = 0;
            _UnwrapNativeCodec2GrallocMetadata(handle, &key.width, &key.height, &key.format,
                                               &key.usage, &key.stride, &generation, &igbpId,
                                               &igbpSlot);
            if (!lookupNV12PlaneGeometry(key, &geometry)) {
                C2Acquirable<C2GraphicView> writable = mGraphicBlock->map();
                C2GraphicView view = writable.get();
                if (readNV12PlaneGeometry(view, static_cast<size_t>(info.st_size),
                                          width, height, &geometry)) {
                    storeNV12PlaneGeometry(key, geometry);
                }
            }
            if (geometry.valid) {
                NV12WritableImage dst = geometry;
                dst.dmaBufFd = handle->data[1];
                queried = true;
                if (primaryConverter->convertMM21ToNV12(src, dst, width, height)) {
                    return true;
                }
            }
        }
        if (!queried) {
            ALOGE("Graphic block has no usable native handle for the zero-copy path");
        }
        if (primaryFailed) {
            *primaryFailed = true;
        }
    }

    if (!fallbackConverter) {
        return false;
    }

    C2Acquirable<C2GraphicView> writable = mGraphicBlock->map();
    C2GraphicView view = writable.get();
    if (view.error() != C2_OK) {
        ALOGE("Failed to map graphic block for detile, error=%d", view.error());
        return false;
    }

    const C2PlanarLayout layout = view.layout();
    if (!isSupportedDetileLayout(view, width, height)) {
        ALOGE("Unsupported graphic block layout for 8-bit detile");
        return false;
    }

    uint8_t* const* data = view.data();
    if (layout.planes[C2PlanarLayout::PLANE_U].colInc == 1) {
        return LibyuvImageConverter::convertMM21ToI420(
                src, data[C2PlanarLayout::PLANE_Y],
                static_cast<size_t>(layout.planes[C2PlanarLayout::PLANE_Y].rowInc),
                data[C2PlanarLayout::PLANE_U],
                static_cast<size_t>(layout.planes[C2PlanarLayout::PLANE_U].rowInc),
                data[C2PlanarLayout::PLANE_V],
                static_cast<size_t>(layout.planes[C2PlanarLayout::PLANE_V].rowInc),
                width, height);
    }
    NV12WritableImage dst;
    dst.y = data[C2PlanarLayout::PLANE_Y];
    dst.uv = data[C2PlanarLayout::PLANE_U];
    dst.strideY = static_cast<size_t>(layout.planes[C2PlanarLayout::PLANE_Y].rowInc);
    dst.strideUV = static_cast<size_t>(layout.planes[C2PlanarLayout::PLANE_U].rowInc);

    return fallbackConverter->convertMM21ToNV12(src, dst, width, height);
}

bool VideoFrame::detileFrom10(ImageConverter* converter, const TiledMT10Image& src, int width,
                              int height) {
    if (!converter || !mGraphicBlock) {
        return false;
    }

    C2Acquirable<C2GraphicView> writable = mGraphicBlock->map();
    C2GraphicView view = writable.get();
    if (view.error() != C2_OK) {
        ALOGE("Failed to map graphic block for 10-bit detile, error=%d", view.error());
        return false;
    }

    const C2PlanarLayout layout = view.layout();
    if (layout.numPlanes < 2) {
        ALOGE("Graphic block has %u planes, cannot detile 10-bit into it", layout.numPlanes);
        return false;
    }
    if (layout.planes[C2PlanarLayout::PLANE_U].colInc != 4) {
        ALOGE("Graphic block chroma colInc=%d is not interleaved 16-bit, cannot detile 10-bit "
              "into it",
              layout.planes[C2PlanarLayout::PLANE_U].colInc);
        return false;
    }

    uint8_t* const* data = view.data();
    P010WritableImage dst;
    dst.y = data[C2PlanarLayout::PLANE_Y];
    dst.uv = data[C2PlanarLayout::PLANE_U];
    dst.strideY = static_cast<size_t>(layout.planes[C2PlanarLayout::PLANE_Y].rowInc);
    dst.strideUV = static_cast<size_t>(layout.planes[C2PlanarLayout::PLANE_U].rowInc);

    return converter->convertMT10ToP010(src, dst, width, height);
}

bool VideoFrame::detileFrom10To8(ImageConverter* converter, const TiledMT10Image& src, int width,
                                 int height) {
    if (!converter || !mGraphicBlock) {
        return false;
    }

    C2Acquirable<C2GraphicView> writable = mGraphicBlock->map();
    C2GraphicView view = writable.get();
    if (view.error() != C2_OK) {
        ALOGE("Failed to map graphic block for 10-to-8-bit detile, error=%d", view.error());
        return false;
    }

    const C2PlanarLayout layout = view.layout();
    if (layout.numPlanes < 2) {
        ALOGE("Graphic block has %u planes, cannot detile 10-to-8-bit into it", layout.numPlanes);
        return false;
    }
    if (layout.planes[C2PlanarLayout::PLANE_U].colInc != 2) {
        ALOGE("Graphic block chroma colInc=%d is not interleaved 8-bit, cannot detile "
              "10-to-8-bit into it",
              layout.planes[C2PlanarLayout::PLANE_U].colInc);
        return false;
    }

    uint8_t* const* data = view.data();
    NV12WritableImage dst;
    dst.y = data[C2PlanarLayout::PLANE_Y];
    dst.uv = data[C2PlanarLayout::PLANE_U];
    dst.strideY = static_cast<size_t>(layout.planes[C2PlanarLayout::PLANE_Y].rowInc);
    dst.strideUV = static_cast<size_t>(layout.planes[C2PlanarLayout::PLANE_U].rowInc);

    return converter->convertMT10ToNV12(src, dst, width, height);
}

}
