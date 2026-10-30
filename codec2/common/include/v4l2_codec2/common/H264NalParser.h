// Copyright 2021 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMMON_H264NALPARSER_H
#define ANDROID_V4L2_CODEC2_COMMON_H264NALPARSER_H

#include <stdint.h>

#include <v4l2_codec2/common/NalParser.h>

namespace android {

class H264NalParser : public NalParser {
public:

    static constexpr uint8_t kIDRType = 5;

    static constexpr uint8_t kSPSType = 7;

    static constexpr uint8_t kPPSType = 8;

    H264NalParser(const uint8_t* data, size_t length);
    ~H264NalParser() = default;

    bool locateSPS() override;
    bool locateIDR() override;

    uint8_t type() const;

    bool findCodedColorAspects(ColorAspects* colorAspects) override;
};

}

#endif
