// Copyright 2019 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#define ATRACE_TAG ATRACE_TAG_VIDEO
#define LOG_TAG "FormatConverter"

#include <v4l2_codec2/common/FormatConverter.h>

#include <inttypes.h>

#include <memory>
#include <string>

#include <C2AllocatorGralloc.h>
#include <C2PlatformSupport.h>
#include <android/hardware/graphics/common/1.0/types.h>
#include <inttypes.h>
#include <libyuv.h>
#include <ui/GraphicBuffer.h>
#include <utils/Log.h>
#include <utils/Trace.h>

#include <v4l2_codec2/common/VideoTypes.h>

using android::hardware::graphics::common::V1_0::BufferUsage;

namespace android {

namespace {
bool isYuv420(const C2GraphicView& view, const ui::Size& size) {
    const C2PlanarLayout& layout = view.layout();
    if (view.error() != C2_OK || layout.type != C2PlanarLayout::TYPE_YUV ||
        layout.numPlanes != 3 || (layout.rootPlanes != 2 && layout.rootPlanes != 3) ||
        size.width <= 0 || size.height <= 0 ||
        view.width() < static_cast<uint32_t>(size.width) ||
        view.height() < static_cast<uint32_t>(size.height)) {
        return false;
    }
    for (uint32_t i = 0; i < 3; ++i) {
        const C2PlaneInfo& plane = layout.planes[i];
        const uint32_t sampling = i == C2PlanarLayout::PLANE_Y ? 1 : 2;
        const int width = (size.width + sampling - 1) / sampling;
        const int step = i == C2PlanarLayout::PLANE_Y || layout.rootPlanes == 3 ? 1 : 2;
        if (!view.data()[i] || plane.allocatedDepth != 8 || plane.bitDepth != 8 ||
            plane.rightShift != 0 || plane.colSampling != sampling ||
            plane.rowSampling != sampling || plane.colInc != step ||
            plane.rowInc < static_cast<int64_t>(width) * step) {
            return false;
        }
    }
    const uint8_t* u = view.data()[C2PlanarLayout::PLANE_U];
    const uint8_t* v = view.data()[C2PlanarLayout::PLANE_V];
    if (layout.rootPlanes == 3) {
        return u != v;
    }
    return (u + 1 == v || v + 1 == u) &&
           layout.planes[C2PlanarLayout::PLANE_U].rowInc ==
                   layout.planes[C2PlanarLayout::PLANE_V].rowInc;
}

}

ImplDefinedToRGBXMap::ImplDefinedToRGBXMap(sp<GraphicBuffer> buf, uint8_t* addr, int rowInc)
      : mBuffer(std::move(buf)), mAddr(addr), mRowInc(rowInc) {}

ImplDefinedToRGBXMap::~ImplDefinedToRGBXMap() {
    mBuffer->unlock();
}

std::unique_ptr<ImplDefinedToRGBXMap> ImplDefinedToRGBXMap::create(
        const C2ConstGraphicBlock& block) {
    uint32_t width, height, format, stride, igbpSlot, generation;
    uint64_t usage, igbpId;
    android::_UnwrapNativeCodec2GrallocMetadata(block.handle(), &width, &height, &format, &usage,
                                                &stride, &generation, &igbpId, &igbpSlot);

    if (format != HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
        ALOGE("The original format (=%u) is not IMPLEMENTATION_DEFINED", format);
        return nullptr;
    }

    native_handle_t* grallocHandle = android::UnwrapNativeCodec2GrallocHandle(block.handle());
    sp<GraphicBuffer> buf = new GraphicBuffer(grallocHandle, GraphicBuffer::CLONE_HANDLE, width,
                                              height, format, 1, usage, stride);
    native_handle_delete(grallocHandle);

    void* pointer = nullptr;
    int32_t status = buf->lock(GRALLOC_USAGE_SW_READ_OFTEN, &pointer);
    if (status != OK) {
        ALOGE("Failed to lock buffer as IMPLEMENTATION_DEFINED format");
        return nullptr;
    }

    uint8_t* addr = reinterpret_cast<uint8_t*>(pointer);
    int rowInc = static_cast<int>(stride * 4);
    ALOGD("Parsed input format IMPLEMENTATION_DEFINED to RGBX_8888");
    return std::unique_ptr<ImplDefinedToRGBXMap>(
            new ImplDefinedToRGBXMap(std::move(buf), addr, rowInc));
}

std::unique_ptr<FormatConverter> FormatConverter::create(VideoPixelFormat outFormat,
                                                         const ui::Size& visibleSize,
                                                         uint32_t inputCount,
                                                         const ui::Size& codedSize) {
    if (outFormat != VideoPixelFormat::I420 && outFormat != VideoPixelFormat::NV12) {
        ALOGE("Unsupported output format: %d", static_cast<int32_t>(outFormat));
        return nullptr;
    }

    std::unique_ptr<FormatConverter> converter(new FormatConverter);
    if (converter->initialize(outFormat, visibleSize, inputCount, codedSize) != C2_OK) {
        ALOGE("Failed to initialize FormatConverter");
        return nullptr;
    }
    return converter;
}

c2_status_t FormatConverter::initialize(VideoPixelFormat outFormat, const ui::Size& visibleSize,
                                        uint32_t inputCount, const ui::Size& codedSize) {
    ALOGV("initialize(out_format=%s, visible_size=%dx%d, input_count=%u, coded_size=%dx%d)",
          videoPixelFormatToString(outFormat).c_str(), visibleSize.width, visibleSize.height,
          inputCount, codedSize.width, codedSize.height);

    if (visibleSize.width <= 0 || visibleSize.height <= 0 ||
        codedSize.width < visibleSize.width || codedSize.height < visibleSize.height) {
        return C2_BAD_VALUE;
    }
    mOutFormat = outFormat;
    mVisibleSize = visibleSize;
    mCodedSize = codedSize;

    const size_t chromaSize = static_cast<size_t>((mVisibleSize.width + 1) / 2) *
                              ((mVisibleSize.height + 1) / 2);
    mTempPlaneU = std::make_unique<uint8_t[]>(chromaSize);
    mTempPlaneV = std::make_unique<uint8_t[]>(chromaSize);

    uint32_t requested_buffer_count = std::max(1u, inputCount);
    c2_status_t status = allocateBuffers(requested_buffer_count);
    if (status != C2_OK) {
        ALOGE("Failed to allocate buffers (error: %d)", status);
        return status;
    }

    return C2_OK;
}

c2_status_t FormatConverter::allocateBuffers(uint32_t count) {
    ALOGV("Allocating %u buffers (format: %s, visible size: %dx%d, coded size: %dx%d)", count,
          videoPixelFormatToString(mOutFormat).c_str(), mVisibleSize.width, mVisibleSize.height,
          mCodedSize.width, mCodedSize.height);

    HalPixelFormat halFormat;
    if (mOutFormat == VideoPixelFormat::I420) {
        halFormat = HalPixelFormat::YV12;
    } else if (mOutFormat == VideoPixelFormat::P010) {
        halFormat = HalPixelFormat::YCBCR_P010;
    } else {
        halFormat = HalPixelFormat::YCBCR_420_888;
    }

    std::shared_ptr<C2BlockPool> pool;
    c2_status_t status = GetCodec2BlockPool(C2BlockPool::BASIC_GRAPHIC, nullptr, &pool);
    if (status != C2_OK) {
        ALOGE("Failed to get basic graphic block pool (error: %d)", status);
        return C2_NO_MEMORY;
    }

    for (uint32_t i = 0; i < count; i++) {
        std::shared_ptr<C2GraphicBlock> block;
        status = pool->fetchGraphicBlock(mCodedSize.width, mCodedSize.height,
                                         static_cast<uint32_t>(halFormat),
                                         {(C2MemoryUsage::CPU_READ | C2MemoryUsage::CPU_WRITE),
                                          static_cast<uint64_t>(BufferUsage::VIDEO_ENCODER)},
                                         &block);
        if (status != C2_OK) {
            ALOGE("Failed to fetch graphic block (error: %d)", status);
            return C2_NO_MEMORY;
        }
        mGraphicBlocks.emplace_back(new BlockEntry(std::move(block)));
        mAvailableQueue.push(mGraphicBlocks.back().get());
    }

    return C2_OK;
}

c2_status_t FormatConverter::convertBlock(uint64_t frameIndex,
                                          const C2ConstGraphicBlock& inputBlock,
                                          C2ConstGraphicBlock* convertedBlock) {
    ATRACE_CALL();
    if (inputBlock.width() < static_cast<uint32_t>(mVisibleSize.width) ||
        inputBlock.height() < static_cast<uint32_t>(mVisibleSize.height)) {
        ALOGE("Input block is smaller than the conversion rectangle");
        return C2_BAD_VALUE;
    }
    const C2GraphicView& inputView = inputBlock.map().get();
    C2PlanarLayout inputLayout =
            inputView.error() == C2_OK ? inputView.layout() : C2PlanarLayout{};

    VideoPixelFormat inputFormat = VideoPixelFormat::UNKNOWN;
    std::unique_ptr<ImplDefinedToRGBXMap> idMap;
    if (inputLayout.type == C2PlanarLayout::TYPE_YUV) {
        if (inputLayout.rootPlanes == 3) {
            inputFormat = VideoPixelFormat::YV12;
        } else if (inputLayout.rootPlanes == 2) {
            const uint8_t* const* data = inputView.data();
            inputFormat = (data[C2PlanarLayout::PLANE_V] > data[C2PlanarLayout::PLANE_U])
                                  ? VideoPixelFormat::NV12
                                  : VideoPixelFormat::NV21;
        }
    } else if (inputLayout.type == C2PlanarLayout::TYPE_RGB) {
        inputFormat = VideoPixelFormat::ABGR;
    } else if (static_cast<uint32_t>(inputLayout.type) == 0u) {

        idMap = ImplDefinedToRGBXMap::create(inputBlock);
        if (!idMap) {
            ALOGE("Unable to parse RGBX_8888 from IMPLEMENTATION_DEFINED");
            return C2_CORRUPTED;
        }

        inputFormat = VideoPixelFormat::ABGR;
        inputLayout.type = C2PlanarLayout::TYPE_RGB;
    } else {
        ALOGE("Failed to determine input pixel format: %u", inputLayout.type);
        return C2_CORRUPTED;
    }

    if (!isReady()) {
        ALOGV("There is no available block for conversion");
        return C2_NO_MEMORY;
    }

    BlockEntry* entry = mAvailableQueue.front();
    std::shared_ptr<C2GraphicBlock> outputBlock = entry->mBlock;

    C2GraphicView outputView = outputBlock->map().get();
    if (!isYuv420(outputView, mVisibleSize)) {
        ALOGE("Unsupported output YUV layout for format conversion");
        return C2_CORRUPTED;
    }
    const C2PlanarLayout& outputLayout = outputView.layout();
    uint8_t* dstY = outputView.data()[C2PlanarLayout::PLANE_Y];
    uint8_t* dstU = outputView.data()[C2PlanarLayout::PLANE_U];
    uint8_t* dstV = outputView.data()[C2PlanarLayout::PLANE_V];
    const int dstStrideY = outputLayout.planes[C2PlanarLayout::PLANE_Y].rowInc;
    const int dstStrideU = outputLayout.planes[C2PlanarLayout::PLANE_U].rowInc;
    const int dstStrideV = outputLayout.planes[C2PlanarLayout::PLANE_V].rowInc;
    const int chromaWidth = (mVisibleSize.width + 1) / 2;
    const int chromaHeight = (mVisibleSize.height + 1) / 2;
    const bool planar = outputLayout.rootPlanes == 3;
    uint8_t* targetU = planar ? dstU : mTempPlaneU.get();
    uint8_t* targetV = planar ? dstV : mTempPlaneV.get();
    const int targetStrideU = planar ? dstStrideU : chromaWidth;
    const int targetStrideV = planar ? dstStrideV : chromaWidth;

    int result;
    if (inputLayout.type == C2PlanarLayout::TYPE_YUV) {
        if (!isYuv420(inputView, mVisibleSize)) {
            ALOGE("Unsupported input YUV layout for format conversion");
            return C2_BAD_VALUE;
        }
        result = libyuv::Android420ToI420(
                inputView.data()[C2PlanarLayout::PLANE_Y],
                inputLayout.planes[C2PlanarLayout::PLANE_Y].rowInc,
                inputView.data()[C2PlanarLayout::PLANE_U],
                inputLayout.planes[C2PlanarLayout::PLANE_U].rowInc,
                inputView.data()[C2PlanarLayout::PLANE_V],
                inputLayout.planes[C2PlanarLayout::PLANE_V].rowInc,
                inputLayout.planes[C2PlanarLayout::PLANE_U].colInc,
                dstY, dstStrideY, targetU, targetStrideU, targetV, targetStrideV,
                mVisibleSize.width, mVisibleSize.height);
    } else if (inputLayout.type == C2PlanarLayout::TYPE_RGB) {
        const uint8_t* srcRGB = idMap ? idMap->addr() : inputView.data()[C2PlanarLayout::PLANE_R];
        const int srcStrideRGB = idMap ? idMap->rowInc()
                                      : inputLayout.planes[C2PlanarLayout::PLANE_R].rowInc;
        if (!srcRGB || srcStrideRGB < static_cast<int64_t>(mVisibleSize.width) * 4 ||
            (!idMap && (inputView.error() != C2_OK ||
                        inputView.width() < static_cast<uint32_t>(mVisibleSize.width) ||
                        inputView.height() < static_cast<uint32_t>(mVisibleSize.height) ||
                        inputLayout.planes[C2PlanarLayout::PLANE_R].colInc != 4))) {
            ALOGE("Unsupported input RGB layout for format conversion");
            return C2_BAD_VALUE;
        }
        result = libyuv::ABGRToI420(srcRGB, srcStrideRGB, dstY, dstStrideY,
                                  targetU, targetStrideU, targetV, targetStrideV,
                                  mVisibleSize.width, mVisibleSize.height);
    } else {
        ALOGE("Unsupported input layout type");
        return C2_BAD_VALUE;
    }
    if (result != 0) {
        ALOGE("Input format conversion failed: %d", result);
        return C2_CORRUPTED;
    }
    if (!planar) {
        if (dstU + 1 == dstV) {
            libyuv::MergeUVPlane(targetU, targetStrideU, targetV, targetStrideV,
                                dstU, dstStrideU, chromaWidth, chromaHeight);
        } else {
            libyuv::MergeUVPlane(targetV, targetStrideV, targetU, targetStrideU,
                                dstV, dstStrideV, chromaWidth, chromaHeight);
        }
    }

    ALOGV("convertBlock(frame_index=%" PRIu64 ", format=%s)", frameIndex,
          videoPixelFormatToString(inputFormat).c_str());
    entry->mAssociatedFrameIndex = frameIndex;
    mAvailableQueue.pop();

    *convertedBlock =
            outputBlock->share(C2Rect(mVisibleSize.width, mVisibleSize.height), C2Fence());
    return C2_OK;
}

c2_status_t FormatConverter::returnBlock(uint64_t frameIndex) {
    ALOGV("returnBlock(frame_index=%" PRIu64 ")", frameIndex);

    auto iter = std::find_if(mGraphicBlocks.begin(), mGraphicBlocks.end(),
                             [frameIndex](const std::unique_ptr<BlockEntry>& be) {
                                 return be->mAssociatedFrameIndex == frameIndex;
                             });
    if (iter == mGraphicBlocks.end()) {
        ALOGE("Failed to find graphic block by converted/zero-copied frame index: %" PRIu64 "",
              frameIndex);
        return C2_BAD_INDEX;
    }

    if ((*iter)->mBlock) {

        (*iter)->mAssociatedFrameIndex = kNoFrameAssociated;
        mAvailableQueue.push(iter->get());
    } else {

        mGraphicBlocks.erase(iter);
    }
    return C2_OK;
}

}
