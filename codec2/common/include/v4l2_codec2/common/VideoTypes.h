// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMPONENTS_VIDEO_TYPES_H
#define ANDROID_V4L2_CODEC2_COMPONENTS_VIDEO_TYPES_H

#include <optional>
#include <string>

#include <C2Config.h>
#include <android/hardware/graphics/common/1.1/types.h>

namespace android {

enum class VideoCodec {
    H264,
    VP8,
    VP9,
    HEVC,
    AV1,
    MPEG4,
    H263,
};

constexpr std::initializer_list<VideoCodec> kAllCodecs = {
        VideoCodec::H264, VideoCodec::VP8,  VideoCodec::VP9,   VideoCodec::HEVC,
        VideoCodec::AV1,  VideoCodec::MPEG4, VideoCodec::H263};

const char* VideoCodecToString(VideoCodec codec);
const char* profileToString(C2Config::profile_t profile);

using HPixelFormat = ::android::hardware::graphics::common::V1_1::PixelFormat;
enum class HalPixelFormat : int32_t {
    UNKNOWN = 0x0,
    YCBCR_420_888 = static_cast<int32_t>(HPixelFormat::YCBCR_420_888),
    YV12 = static_cast<int32_t>(HPixelFormat::YV12),
    YCBCR_P010 = static_cast<int32_t>(HPixelFormat::YCBCR_P010),
    IMPLEMENTATION_DEFINED = static_cast<int32_t>(HPixelFormat::IMPLEMENTATION_DEFINED),

    NV12 = 0x3231564e,
};
const char* HalPixelFormatToString(HalPixelFormat format);

}

#endif
