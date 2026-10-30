// Copyright 2021 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMPONENTS_VIDEO_ENCODER_H
#define ANDROID_V4L2_CODEC2_COMPONENTS_VIDEO_ENCODER_H

#include <stdint.h>
#include <memory>
#include <vector>

#include <base/callback.h>
#include <ui/Size.h>

#include <v4l2_codec2/common/Common.h>
#include <v4l2_codec2/common/VideoPixelFormat.h>
#include <v4l2_codec2/common/VideoTypes.h>

namespace android {

struct BitstreamBuffer;

class VideoEncoder {
public:

    static constexpr size_t kInputBufferCount = 2;
    static constexpr size_t kOutputBufferCount = 2;
    static constexpr VideoPixelFormat kInputPixelFormat = VideoPixelFormat::NV12;

    static constexpr uint32_t kPeakBitrateMultiplier = 2u;

    class InputFrame {
    public:
        InputFrame(std::vector<int>&& fds, const std::vector<VideoFramePlane>& planes,
                   VideoPixelFormat pixelFormat, uint64_t index, int64_t timestamp);
        ~InputFrame() = default;

        const std::vector<int>& fds() const { return mFds; }
        const std::vector<VideoFramePlane>& planes() const { return mPlanes; }
        VideoPixelFormat pixelFormat() const { return mPixelFormat; }
        uint64_t index() const { return mIndex; }
        int64_t timestamp() const { return mTimestamp; }

    private:
        const std::vector<int> mFds;
        std::vector<VideoFramePlane> mPlanes;
        VideoPixelFormat mPixelFormat;
        uint64_t mIndex = 0;
        int64_t mTimestamp = 0;
    };

    using FetchOutputBufferCB =
            ::base::RepeatingCallback<void(uint32_t, std::unique_ptr<BitstreamBuffer>* buffer)>;

    using InputBufferDoneCB = ::base::RepeatingCallback<void(uint64_t)>;
    using OutputBufferDoneCB = ::base::RepeatingCallback<void(
            size_t, int64_t, bool, std::unique_ptr<BitstreamBuffer> buffer)>;
    using DrainDoneCB = ::base::RepeatingCallback<void(bool)>;
    using ErrorCB = ::base::RepeatingCallback<void()>;

    virtual ~VideoEncoder() = default;

    virtual bool encode(std::unique_ptr<InputFrame> buffer) = 0;

    virtual void drain() = 0;

    virtual void flush() = 0;

    virtual bool setBitrate(uint32_t bitrate) = 0;

    virtual bool setPeakBitrate(uint32_t peakBitrate) = 0;

    virtual bool setFramerate(uint32_t framerate) = 0;

    virtual void requestKeyframe() = 0;

    virtual bool reconfigureInputFormat(VideoPixelFormat format,
                                        const std::vector<VideoFramePlane>& planes) = 0;

    virtual VideoPixelFormat inputFormat() const = 0;
    virtual const ui::Size& visibleSize() const = 0;
    virtual const ui::Size& codedSize() const = 0;
};

}

#endif
