// Copyright 2021 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_V4L2_V4L2_ENCODER_H
#define ANDROID_V4L2_CODEC2_V4L2_V4L2_ENCODER_H

#include <stdint.h>
#include <memory>
#include <optional>
#include <queue>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <base/sequenced_task_runner.h>
#include <ui/Size.h>

#include <v4l2_codec2/common/Common.h>
#include <v4l2_codec2/components/VideoEncoder.h>

namespace android {

struct BitstreamBuffer;
struct VideoFramePlane;
class V4L2Device;
class V4L2Queue;

class V4L2Encoder : public VideoEncoder {
public:
    static std::unique_ptr<VideoEncoder> create(
            C2Config::profile_t profile, std::optional<uint8_t> level, const ui::Size& visibleSize,
            uint32_t stride, uint32_t keyFramePeriod, C2Config::bitrate_mode_t bitrateMode,
            uint32_t bitrate, std::optional<uint32_t> peakBitrate,
            FetchOutputBufferCB fetchOutputBufferCb, InputBufferDoneCB inputBufferDoneCb,
            OutputBufferDoneCB outputBufferDoneCb, DrainDoneCB drainDoneCb, ErrorCB errorCb,
            scoped_refptr<::base::SequencedTaskRunner> taskRunner);
    ~V4L2Encoder() override;

    bool encode(std::unique_ptr<InputFrame> frame) override;
    void drain() override;
    void flush() override;

    bool setBitrate(uint32_t bitrate) override;
    bool setPeakBitrate(uint32_t peakBitrate) override;
    bool setFramerate(uint32_t framerate) override;
    void requestKeyframe() override;

    bool reconfigureInputFormat(VideoPixelFormat format,
                                const std::vector<VideoFramePlane>& planes) override;

    VideoPixelFormat inputFormat() const override;
    const ui::Size& visibleSize() const override { return mVisibleSize; }
    const ui::Size& codedSize() const override { return mInputCodedSize; }

private:

    enum class State {
        UNINITIALIZED,
        WAITING_FOR_INPUT_FRAME,
        WAITING_FOR_V4L2_BUFFER,
        ENCODING,
        DRAINING,
        ERROR,
    };

    struct EncodeRequest {
        EncodeRequest(std::unique_ptr<InputFrame> video_frame)
              : video_frame(std::move(video_frame)) {}
        ~EncodeRequest() = default;
        EncodeRequest(EncodeRequest&&) = default;
        EncodeRequest& operator=(EncodeRequest&&) = default;

        std::unique_ptr<InputFrame> video_frame;
        bool end_of_stream = false;
    };

    V4L2Encoder(scoped_refptr<::base::SequencedTaskRunner> taskRunner,
                FetchOutputBufferCB fetchOutputBufferCb, InputBufferDoneCB mInputBufferDoneCb,
                OutputBufferDoneCB mOutputBufferDoneCb, DrainDoneCB drainDoneCb, ErrorCB errorCb);

    bool initialize(C2Config::profile_t outputProfile, std::optional<uint8_t> level,
                    const ui::Size& visibleSize, uint32_t stride, uint32_t keyFramePeriod,
                    C2Config::bitrate_mode_t bitrateMode, uint32_t bitrate,
                    std::optional<uint32_t> peakBitrate);

    void handleEncodeRequest();

    void handleFlushRequest();

    void handleDrainRequest();

    void onDrainDone(bool done);

    bool configureInputFormat(VideoPixelFormat inputFormat, uint32_t stride);

    bool configureOutputFormat(C2Config::profile_t outputProfile);

    bool configureDevice(C2Config::profile_t outputProfile,
                         std::optional<const uint8_t> outputH264Level);

    bool configureH264(C2Config::profile_t outputProfile,
                       std::optional<const uint8_t> outputH264Level);

    bool configureBitrateMode(C2Config::bitrate_mode_t bitrateMode);

    bool startDevicePoll();

    bool stopDevicePoll();

    void onPollError();

    void serviceDeviceTask(bool event);

    bool enqueueInputBuffer(std::unique_ptr<InputFrame> frame);

    bool enqueueOutputBuffer();

    bool dequeueInputBuffer();

    bool dequeueOutputBuffer();

    bool createInputBuffers();

    bool createOutputBuffers();

    void destroyInputBuffers();

    void destroyOutputBuffers();

    void onError();

    void setState(State state);

    static const char* stateToString(State state);

    std::queue<EncodeRequest> mEncodeRequests;

    ui::Size mVisibleSize;

    ui::Size mInputCodedSize;

    std::optional<VideoFrameLayout> mInputLayout;

    uint32_t mOutputBufferSize = 0;

    uint32_t mKeyFramePeriod = 0;

    uint32_t mKeyFrameCounter = 0;

    bool mInjectParamsBeforeIDR = false;

    std::vector<uint8_t> mCachedSPS;
    std::vector<uint8_t> mCachedPPS;

    scoped_refptr<V4L2Device> mDevice;
    scoped_refptr<V4L2Queue> mInputQueue;
    scoped_refptr<V4L2Queue> mOutputQueue;

    std::vector<std::unique_ptr<InputFrame>> mInputBuffers;

    std::vector<std::unique_ptr<BitstreamBuffer>> mOutputBuffers;

    FetchOutputBufferCB mFetchOutputBufferCb;
    InputBufferDoneCB mInputBufferDoneCb;
    OutputBufferDoneCB mOutputBufferDoneCb;
    DrainDoneCB mDrainDoneCb;
    ErrorCB mErrorCb;

    State mState = State::UNINITIALIZED;

    scoped_refptr<::base::SequencedTaskRunner> mTaskRunner;

    ::base::WeakPtr<V4L2Encoder> mWeakThis;
    ::base::WeakPtrFactory<V4L2Encoder> mWeakThisFactory{this};
};

}

#endif
