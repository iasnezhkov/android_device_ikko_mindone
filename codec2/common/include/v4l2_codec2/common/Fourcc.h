// Copyright 2019 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMMON_FOURCC_H
#define ANDROID_V4L2_CODEC2_COMMON_FOURCC_H

#include <stdint.h>
#include <optional>
#include <string>

#include <v4l2_codec2/common/VideoPixelFormat.h>

namespace android {

constexpr uint32_t composeFourcc(char a, char b, char c, char d) {
    return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) |
           (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(d) << 24);
}

class Fourcc {
public:
    enum Value : uint32_t {

        AR24 = composeFourcc('A', 'R', '2', '4'),

        AB24 = composeFourcc('A', 'B', '2', '4'),

        XR24 = composeFourcc('X', 'R', '2', '4'),

        XB24 = composeFourcc('X', 'B', '2', '4'),

        RGB4 = composeFourcc('R', 'G', 'B', '4'),

        BGR4 = composeFourcc('B', 'G', 'R', '4'),

        YU12 = composeFourcc('Y', 'U', '1', '2'),

        YV12 = composeFourcc('Y', 'V', '1', '2'),

        YM12 = composeFourcc('Y', 'M', '1', '2'),

        YM21 = composeFourcc('Y', 'M', '2', '1'),

        YUYV = composeFourcc('Y', 'U', 'Y', 'V'),

        NV12 = composeFourcc('N', 'V', '1', '2'),

        NV21 = composeFourcc('N', 'V', '2', '1'),

        NM12 = composeFourcc('N', 'M', '1', '2'),

        NM21 = composeFourcc('N', 'M', '2', '1'),

        YM16 = composeFourcc('Y', 'M', '1', '6'),

        MT21 = composeFourcc('M', 'T', '2', '1'),

        MM21 = composeFourcc('M', 'M', '2', '1'),

        P010 = composeFourcc('P', '0', '1', '0'),
    };

    explicit Fourcc(Fourcc::Value fourcc);
    Fourcc& operator=(const Fourcc& fourcc);
    ~Fourcc();

    bool operator==(const Fourcc& rhs) const { return mValue == rhs.mValue; }

    static std::optional<Fourcc> fromUint32(uint32_t fourcc);

    static std::optional<Fourcc> fromVideoPixelFormat(VideoPixelFormat pixelFormat,
                                                      bool singlePlanar = true);

    static std::optional<Fourcc> fromV4L2PixFmt(uint32_t v4l2PixFmt);

    VideoPixelFormat toVideoPixelFormat() const;

    uint32_t toV4L2PixFmt() const;

    std::optional<Fourcc> toSinglePlanar() const;

    bool isMultiPlanar() const;

    std::string toString() const;

private:
    Value mValue;
};

bool operator!=(const Fourcc& lhs, const Fourcc& rhs);

}

#endif
