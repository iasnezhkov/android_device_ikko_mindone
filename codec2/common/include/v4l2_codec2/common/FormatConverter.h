// Copyright 2019 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMMON_FORMAT_CONVERTER_H
#define ANDROID_V4L2_CODEC2_COMMON_FORMAT_CONVERTER_H

#include <limits>
#include <queue>
#include <vector>

#include <C2Buffer.h>
#include <ui/Size.h>
#include <utils/StrongPointer.h>

#include <v4l2_codec2/common/VideoPixelFormat.h>

namespace android {

class GraphicBuffer;

class ImplDefinedToRGBXMap {
public:
    ~ImplDefinedToRGBXMap();
    ImplDefinedToRGBXMap() = delete;

    static std::unique_ptr<ImplDefinedToRGBXMap> create(const C2ConstGraphicBlock& block);

    const uint8_t* addr() const { return mAddr; }
    int offset() const { return 0; }
    int rowInc() const { return mRowInc; }

private:
    ImplDefinedToRGBXMap(sp<GraphicBuffer> buf, uint8_t* addr, int rowInc);

    const sp<GraphicBuffer> mBuffer;
    const uint8_t* mAddr;
    const int mRowInc;
};

class FormatConverter {
public:
    ~FormatConverter() = default;

    FormatConverter(const FormatConverter&) = delete;
    FormatConverter& operator=(const FormatConverter&) = delete;

    static std::unique_ptr<FormatConverter> create(VideoPixelFormat outFormat,
                                                   const ui::Size& visibleSize, uint32_t inputCount,
                                                   const ui::Size& codedSize);

    c2_status_t convertBlock(uint64_t frameIndex, const C2ConstGraphicBlock& inputBlock,
                             C2ConstGraphicBlock* convertedBlock);

    c2_status_t returnBlock(uint64_t frameIndex);

    bool isReady() const { return !mAvailableQueue.empty(); }

private:

    static constexpr uint64_t kNoFrameAssociated = ~static_cast<uint64_t>(0);

    struct BlockEntry {

        BlockEntry(std::shared_ptr<C2GraphicBlock> block) : mBlock(std::move(block)) {}

        BlockEntry(uint64_t frameIndex) : mAssociatedFrameIndex(frameIndex) {}

        std::shared_ptr<C2GraphicBlock> mBlock;
        uint64_t mAssociatedFrameIndex = kNoFrameAssociated;
    };

    FormatConverter() = default;

    c2_status_t initialize(VideoPixelFormat outFormat, const ui::Size& visibleSize,
                           uint32_t inputCount, const ui::Size& codedSize);

    c2_status_t allocateBuffers(uint32_t count);

    std::vector<std::unique_ptr<BlockEntry>> mGraphicBlocks;

    std::queue<BlockEntry*> mAvailableQueue;

    std::unique_ptr<uint8_t[]> mTempPlaneU;
    std::unique_ptr<uint8_t[]> mTempPlaneV;

    VideoPixelFormat mOutFormat = VideoPixelFormat::UNKNOWN;

    ui::Size mVisibleSize;

    ui::Size mCodedSize;
};

}

#endif
