// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMPONENTS_VIDEO_FRAME_H
#define ANDROID_V4L2_CODEC2_COMPONENTS_VIDEO_FRAME_H

#include <memory>
#include <vector>

#include <C2Buffer.h>

#include <ui/Rect.h>

#include <v4l2_codec2/common/ImageConverter.h>

namespace android {

class VideoFrame {
public:

    static std::unique_ptr<VideoFrame> Create(std::shared_ptr<C2GraphicBlock> block);
    ~VideoFrame();

    const std::vector<int>& getFDs() const;

    void setVisibleRect(const Rect& visibleRect);
    const Rect& getVisibleRect() const;

    void setBitstreamId(int32_t bitstreamId);
    int32_t getBitstreamId() const;

    C2ConstGraphicBlock getGraphicBlock();

    bool detileFrom(ImageConverter* primaryConverter, ImageConverter* fallbackConverter,
                    const TiledMM21Image& src, int width, int height, bool* primaryFailed);

    bool detileFrom10(ImageConverter* converter, const TiledMT10Image& src, int width, int height);

    bool detileFrom10To8(ImageConverter* converter, const TiledMT10Image& src, int width,
                         int height);

private:
    VideoFrame(std::shared_ptr<C2GraphicBlock> block, std::vector<int> fds);

    std::shared_ptr<C2GraphicBlock> mGraphicBlock;
    std::vector<int> mFds;
    Rect mVisibleRect;
    int32_t mBitstreamId = -1;
};

}

#endif
