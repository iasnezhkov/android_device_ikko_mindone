// Copyright 2021 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMMON_NALPARSER_H
#define ANDROID_V4L2_CODEC2_COMMON_NALPARSER_H

#include <stdint.h>

#include <media/stagefright/foundation/ABitReader.h>

namespace android {

class NalParser {
public:

    struct ColorAspects {
        uint32_t primaries;
        uint32_t transfer;
        uint32_t coeffs;
        bool fullRange;
    };

    NalParser(const uint8_t* data, size_t length);
    virtual ~NalParser() = default;

    bool locateNextNal();

    virtual bool locateSPS() = 0;
    virtual bool locateIDR() = 0;

    const uint8_t* data() const;

    size_t length() const;

    virtual uint8_t type() const = 0;

    virtual bool findCodedColorAspects(ColorAspects* colorAspects) = 0;

    static bool parseUE(ABitReader* br, uint32_t* val);

    static bool parseSE(ABitReader* br, int32_t* val);

protected:
    const uint8_t* findNextStartCodePos() const;

    const uint8_t kNalStartCode[3] = {0x00, 0x00, 0x01};

    const size_t kNalStartCodeLength = 3;

    const uint8_t* mCurrNalDataPos;
    const uint8_t* mDataEnd;
    const uint8_t* mNextNalStartCodePos;
};

}

#endif
