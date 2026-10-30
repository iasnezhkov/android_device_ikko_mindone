// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMPONENTS_VIDEO_FRAME_POOL_H
#define ANDROID_V4L2_CODEC2_COMPONENTS_VIDEO_FRAME_POOL_H

#include <atomic>
#include <memory>
#include <optional>
#include <queue>

#include <C2Buffer.h>
#include <base/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/sequenced_task_runner.h>
#include <base/threading/thread.h>
#include <ui/Size.h>

#include <v4l2_codec2/common/VideoTypes.h>
#include <v4l2_codec2/components/VideoFrame.h>

namespace android {

class VideoFramePool {
public:
    using FrameWithBlockId = std::pair<std::unique_ptr<VideoFrame>, uint32_t>;
    using GetVideoFrameCB = ::base::OnceCallback<void(std::optional<FrameWithBlockId>)>;

    static std::unique_ptr<VideoFramePool> Create(
            std::shared_ptr<C2BlockPool> blockPool, const size_t numBuffers, const ui::Size& size,
            HalPixelFormat pixelFormat, bool isSecure,
            scoped_refptr<::base::SequencedTaskRunner> taskRunner);
    ~VideoFramePool();

    bool getVideoFrame(GetVideoFrameCB cb);

private:

    VideoFramePool(std::shared_ptr<C2BlockPool> blockPool, const size_t maxBufferCount,
                   const ui::Size& size, HalPixelFormat pixelFormat, C2MemoryUsage memoryUsage,
                   scoped_refptr<::base::SequencedTaskRunner> taskRunner);
    bool initialize();
    void destroyTask();

    static void getVideoFrameTaskThunk(scoped_refptr<::base::SequencedTaskRunner> taskRunner,
                                       std::optional<::base::WeakPtr<VideoFramePool>> weakPool);
    void getVideoFrameTask();
    void onVideoFrameReady(std::optional<FrameWithBlockId> frameWithBlockId);

    bool shouldDropBuffer(uint32_t bufferId);

    static std::optional<uint32_t> getBufferIdFromGraphicBlock(C2BlockPool& blockPool,
                                                               const C2Block2D& block);

    std::shared_ptr<C2BlockPool> mBlockPool;

    size_t mMaxBufferCount;

    std::set<uint32_t> mBuffers;

    const ui::Size mSize;
    const HalPixelFormat mPixelFormat;
    const C2MemoryUsage mMemoryUsage;

    GetVideoFrameCB mOutputCb;
    size_t mFetchRetries = 0;
    size_t mFetchDelayUs = 256;

    scoped_refptr<::base::SequencedTaskRunner> mClientTaskRunner;
    ::base::Thread mFetchThread{"VideoFramePoolFetchThread"};
    scoped_refptr<::base::SequencedTaskRunner> mFetchTaskRunner;

    ::base::WeakPtr<VideoFramePool> mClientWeakThis;
    ::base::WeakPtr<VideoFramePool> mFetchWeakThis;
    ::base::WeakPtrFactory<VideoFramePool> mClientWeakThisFactory{this};
    ::base::WeakPtrFactory<VideoFramePool> mFetchWeakThisFactory{this};
};

}

#endif
