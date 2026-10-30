// Copyright 2021 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include <v4l2_codec2/components/VideoEncoder.h>

namespace android {

VideoEncoder::InputFrame::InputFrame(std::vector<int>&& fds,
                                     const std::vector<VideoFramePlane>& planes,
                                     VideoPixelFormat pixelFormat, uint64_t index,
                                     int64_t timestamp)
      : mFds(std::move(fds)),
        mPlanes(planes),
        mPixelFormat(pixelFormat),
        mIndex(index),
        mTimestamp(timestamp) {}

}
