// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#define LOG_TAG "V4L2ComponentCommon"

#include <v4l2_codec2/v4l2/V4L2ComponentCommon.h>

#include <log/log.h>
#include <set>

namespace android {

const std::string V4L2ComponentName::kH264Encoder = "c2.v4l2.avc.encoder";
const std::string V4L2ComponentName::kHEVCEncoder = "c2.v4l2.hevc.encoder";

const std::string V4L2ComponentName::kH264Decoder = "c2.v4l2.avc.decoder";
const std::string V4L2ComponentName::kVP8Decoder = "c2.v4l2.vp8.decoder";
const std::string V4L2ComponentName::kVP9Decoder = "c2.v4l2.vp9.decoder";
const std::string V4L2ComponentName::kHEVCDecoder = "c2.v4l2.hevc.decoder";
const std::string V4L2ComponentName::kAV1Decoder = "c2.v4l2.av1.decoder";
const std::string V4L2ComponentName::kMPEG4Decoder = "c2.v4l2.mpeg4.decoder";
const std::string V4L2ComponentName::kH263Decoder = "c2.v4l2.h263.decoder";
const std::string V4L2ComponentName::kH264SecureDecoder = "c2.v4l2.avc.decoder.secure";
const std::string V4L2ComponentName::kVP8SecureDecoder = "c2.v4l2.vp8.decoder.secure";
const std::string V4L2ComponentName::kVP9SecureDecoder = "c2.v4l2.vp9.decoder.secure";
const std::string V4L2ComponentName::kHEVCSecureDecoder = "c2.v4l2.hevc.decoder.secure";

bool V4L2ComponentName::isValid(const std::string& name) {
    return name == kH264Encoder || name == kHEVCEncoder ||
           name == kH264Decoder || name == kVP8Decoder || name == kVP9Decoder ||
           name == kHEVCDecoder || name == kAV1Decoder || name == kMPEG4Decoder ||
           name == kH263Decoder || name == kH264SecureDecoder ||
	   name == kVP8SecureDecoder || name == kVP9SecureDecoder || name == kHEVCSecureDecoder;
}

bool V4L2ComponentName::isEncoder(const std::string& name) {
    ALOG_ASSERT(isValid(name));

    return name == kH264Encoder || name == kHEVCEncoder;
}

bool V4L2ComponentName::isDecoder(const std::string& name) {
    ALOG_ASSERT(isValid(name));
    static const std::set<std::string> kValidDecoders = {
            kH264Decoder, kH264SecureDecoder, kVP8Decoder,   kVP8SecureDecoder,
            kVP9Decoder,  kVP9SecureDecoder,  kHEVCDecoder,  kHEVCSecureDecoder,
            kAV1Decoder,  kMPEG4Decoder,      kH263Decoder
    };

    return kValidDecoders.find(name) != kValidDecoders.end();
}

std::optional<VideoCodec> V4L2ComponentName::getCodec(const std::string& name) {
    ALOG_ASSERT(isValid(name));
    static const std::map<std::string, VideoCodec> kNameToCodecs = {
            {kH264Decoder, VideoCodec::H264}, {kH264SecureDecoder, VideoCodec::H264},
            {kH264Encoder, VideoCodec::H264},

            {kVP8Decoder, VideoCodec::VP8},   {kVP8SecureDecoder, VideoCodec::VP8},

            {kVP9Decoder, VideoCodec::VP9},   {kVP9SecureDecoder, VideoCodec::VP9},

            {kHEVCDecoder, VideoCodec::HEVC}, {kHEVCSecureDecoder, VideoCodec::HEVC},
            {kHEVCEncoder, VideoCodec::HEVC},

            {kAV1Decoder, VideoCodec::AV1},

            {kMPEG4Decoder, VideoCodec::MPEG4},

            {kH263Decoder, VideoCodec::H263},
    };

    auto iter = kNameToCodecs.find(name);
    if (iter == kNameToCodecs.end()) {
        return std::nullopt;
    }
    return iter->second;
}

}
