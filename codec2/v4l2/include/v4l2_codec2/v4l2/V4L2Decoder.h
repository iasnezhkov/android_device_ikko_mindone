// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_V4L2_V4L2_DECODER_H
#define ANDROID_V4L2_CODEC2_V4L2_V4L2_DECODER_H

#include <stdint.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <base/callback.h>
#include <base/memory/weak_ptr.h>

#include <ui/GraphicBuffer.h>
#include <ui/Rect.h>
#include <ui/Size.h>
#include <v4l2_codec2/common/Fourcc.h>
#include <v4l2_codec2/common/ImageConverter.h>
#include <v4l2_codec2/common/VideoTypes.h>
#include <v4l2_codec2/components/VideoDecoder.h>
#include <v4l2_codec2/components/VideoFrame.h>
#include <v4l2_codec2/components/VideoFramePool.h>
#include <v4l2_codec2/plugin_store/DmabufHelpers.h>
#include <v4l2_codec2/v4l2/MdpImageConverter.h>
#include <v4l2_codec2/v4l2/TiledCaptureBuffer.h>
#include <v4l2_codec2/v4l2/V4L2Device.h>

namespace android {

constexpr uint32_t kMt21S10TFourcc = v4l2_fourcc('M', 'T', 'S', 'T');
constexpr uint32_t kMt21S10RFourcc = v4l2_fourcc('M', 'T', 'S', 'R');

constexpr std::initializer_list<uint32_t> kSupportedOutputFourccs = {
        Fourcc::YU12, Fourcc::YV12, Fourcc::YM12, Fourcc::YM21,
        Fourcc::NV12, Fourcc::NV21, Fourcc::NM12, Fourcc::NM21,
        Fourcc::P010, kMt21S10TFourcc, kMt21S10RFourcc,
};

class V4L2Decoder : public VideoDecoder {
public:
    static std::unique_ptr<VideoDecoder> Create(
            uint32_t debugStreamId, const VideoCodec& codec, const size_t inputBufferSize,
            const size_t minNumOutputBuffers, GetPoolCB getPoolCB, OutputCB outputCb,
            ErrorCB errorCb, scoped_refptr<::base::SequencedTaskRunner> taskRunner, bool isSecure,
            uint32_t requestedPixelFormat);
    ~V4L2Decoder() override;

    void decode(std::unique_ptr<ConstBitstreamBuffer> buffer, DecodeCB decodeCb) override;
    void drain(DecodeCB drainCb) override;
    void flush() override;

private:
    static constexpr size_t kNumInputBuffers = 16;

    enum class State {
        Idle,
        Decoding,
        Draining,
        Error,
    };
    static const char* StateToString(State state);

    struct DecodeRequest {
        DecodeRequest(std::unique_ptr<ConstBitstreamBuffer> buffer, DecodeCB decodeCb)
              : buffer(std::move(buffer)), decodeCb(std::move(decodeCb)) {}
        DecodeRequest(DecodeRequest&&) = default;
        ~DecodeRequest() = default;

        std::unique_ptr<ConstBitstreamBuffer> buffer;
        DecodeCB decodeCb;
    };

    V4L2Decoder(uint32_t debugStreamId, scoped_refptr<::base::SequencedTaskRunner> taskRunner);
    bool start(const VideoCodec& codec, const size_t inputBufferSize,
               const size_t minNumOutputBuffers, GetPoolCB getPoolCb, OutputCB outputCb,
               ErrorCB errorCb, bool isSecure, uint32_t requestedPixelFormat);
    bool setupInputFormat(const uint32_t inputPixelFormat, const size_t inputBufferSize);

    bool setupInitialOutput();

    bool setupMinimalOutputFormat();

    bool startOutputQueue(size_t minOutputBuffersCount, enum v4l2_memory memory);

    void pumpDecodeRequest();
    bool queueInputBuffer(V4L2WritableBufferRef inputBuffer,
                          const ConstBitstreamBuffer& buffer, size_t allocationSize);

    void serviceDeviceTask(bool event);
    bool dequeueResolutionChangeEvent();
    bool changeResolution();
    bool setupOutputFormat(const ui::Size& size, const uint32_t requestedpixfmt);

    void tryFetchVideoFrame();
    void onVideoFrameReady(std::optional<VideoFramePool::FrameWithBlockId> frameWithBlockId);

    std::optional<size_t> getNumOutputBuffers();
    std::optional<struct v4l2_format> getFormatInfo();
    Rect getVisibleRect(const ui::Size& codedSize);
    bool sendV4L2DecoderCmd(bool start);
    bool tryV4L2DecoderCmd(bool start);

    void setState(State newState);
    void onError();

    uint32_t mDebugStreamId;

    std::unique_ptr<VideoFramePool> mVideoFramePool;

    scoped_refptr<V4L2Device> mDevice;
    scoped_refptr<V4L2Queue> mInputQueue;
    scoped_refptr<V4L2Queue> mOutputQueue;

    bool mInitialEosPending = false;

    std::queue<DecodeRequest> mDecodeRequests;
    std::map<int32_t, DecodeCB> mPendingDecodeCbs;

    bool mPendingDRC = false;

    bool mIsSecure;
    VideoCodec mCodec;

    unique_id_t mLastDmaBufferId[kNumInputBuffers];

    size_t mNextInputBufferId = 0;
    size_t mInputBufferSize = 0;
    TiledCaptureBuffer mInputStaging[kNumInputBuffers];

    size_t mMinNumOutputBuffers = 0;
    GetPoolCB mGetPoolCb;
    OutputCB mOutputCb;
    DecodeCB mDrainCb;
    ErrorCB mErrorCb;

    ui::Size mCodedSize;
    Rect mVisibleRect;

    std::unique_ptr<ImageConverter> mImageConverter;
    std::unique_ptr<ImageConverter> mMdpImageConverter;
    bool mMdpFailed = false;
    TiledCaptureBufferPool mTiledCaptureBuffers;
    uint32_t mCaptureStrideY = 0;
    bool mCaptureIsMt10 = false;
    bool mCaptureMt10Raster = false;
    uint32_t mRequestedPixelFormat = static_cast<uint32_t>(HalPixelFormat::YCBCR_420_888);
    bool mOutputIsDownconvertedTo8Bit = false;

    std::map<size_t, std::unique_ptr<VideoFrame>> mFrameAtDevice;

    std::queue<std::pair<size_t, std::unique_ptr<VideoFrame>>> mReuseFrameQueue;

    std::map<size_t, size_t> mBlockIdToV4L2Id;

    State mState = State::Idle;

    scoped_refptr<::base::SequencedTaskRunner> mTaskRunner;

    ::base::WeakPtr<V4L2Decoder> mWeakThis;
    ::base::WeakPtrFactory<V4L2Decoder> mWeakThisFactory{this};
};

}

#endif
