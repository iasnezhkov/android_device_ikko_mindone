// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMMON_COMMON_H
#define ANDROID_V4L2_CODEC2_COMMON_COMMON_H

#include <inttypes.h>

#include <optional>
#include <string>
#include <vector>

#include <ui/Rect.h>
#include <ui/Size.h>

#include <v4l2_codec2/common/VideoPixelFormat.h>
#include <v4l2_codec2/common/VideoTypes.h>

namespace android {

struct VideoFramePlane {
    uint32_t mStride = 0;
    size_t mOffset = 0;
    size_t mSize = 0;
};

struct VideoFrameLayout {
    VideoPixelFormat mFormat = VideoPixelFormat::UNKNOWN;
    android::ui::Size mCodedSize;
    std::vector<VideoFramePlane> mPlanes;
    bool mMultiPlanar = false;
};

struct SupportedProfile {
    C2Config::profile_t profile = C2Config::PROFILE_UNUSED;
    ui::Size min_resolution;
    ui::Size max_resolution;
    uint32_t max_framerate_numerator = 0;
    uint32_t max_framerate_denominator = 0;
    bool encrypted_only = false;
};
using SupportedProfiles = std::vector<SupportedProfile>;

struct SupportedCapabilities {
    VideoCodec codec;
    SupportedProfiles supportedProfiles;
    C2Config::profile_t defaultProfile = C2Config::PROFILE_UNUSED;
    std::vector<C2Config::level_t> supportedLevels;
    C2Config::level_t defaultLevel = C2Config::LEVEL_UNUSED;
};

bool contains(const Rect& rect1, const Rect& rect2);

std::string toString(const Rect& rect);

std::optional<int> getArea(const ui::Size& size);

bool isEmpty(const ui::Size& size);

std::string toString(const ui::Size& size);

bool isValidProfileForCodec(VideoCodec codec, C2Config::profile_t profile);

}

#endif
