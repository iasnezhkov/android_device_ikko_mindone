// Copyright 2021 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMMON_HEVCNALPARSER_H
#define ANDROID_V4L2_CODEC2_COMMON_HEVCNALPARSER_H

#include <stdint.h>

#include <v4l2_codec2/common/NalParser.h>

namespace android {

class HEVCNalParser : public NalParser {
public:

    static constexpr uint8_t kIDRType = 19;
    static constexpr uint8_t kVPSType = 32;
    static constexpr uint8_t kSPSType = 33;
    static constexpr uint8_t kPPSType = 34;

    HEVCNalParser(const uint8_t* data, size_t length);
    ~HEVCNalParser() = default;

    bool locateSPS() override;
    bool locateIDR() override;

    uint8_t type() const override;

    bool findCodedColorAspects(ColorAspects* colorAspects) override;
};

}

#endif
