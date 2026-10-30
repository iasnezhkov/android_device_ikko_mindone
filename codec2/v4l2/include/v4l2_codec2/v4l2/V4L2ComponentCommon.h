// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_V4L2_V4L2_COMPONENT_COMMON_H
#define ANDROID_V4L2_CODEC2_V4L2_V4L2_COMPONENT_COMMON_H

#include <v4l2_codec2/common/VideoTypes.h>
#include <optional>
#include <string>

namespace android {

struct V4L2ComponentName {
    static const std::string kH264Encoder;
    static const std::string kHEVCEncoder;

    static const std::string kH264Decoder;
    static const std::string kVP8Decoder;
    static const std::string kVP9Decoder;
    static const std::string kHEVCDecoder;
    static const std::string kAV1Decoder;
    static const std::string kMPEG4Decoder;
    static const std::string kH263Decoder;
    static const std::string kH264SecureDecoder;
    static const std::string kVP8SecureDecoder;
    static const std::string kVP9SecureDecoder;
    static const std::string kHEVCSecureDecoder;

    static bool isValid(const std::string& name);

    static bool isEncoder(const std::string& name);

    static bool isDecoder(const std::string& name);

    static std::optional<VideoCodec> getCodec(const std::string& name);
};

}

#endif
