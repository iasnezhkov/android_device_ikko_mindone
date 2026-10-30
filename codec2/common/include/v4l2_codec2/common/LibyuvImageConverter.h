// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMMON_LIBYUV_IMAGE_CONVERTER_H
#define ANDROID_V4L2_CODEC2_COMMON_LIBYUV_IMAGE_CONVERTER_H

#include <v4l2_codec2/common/ImageConverter.h>

namespace android {

class LibyuvImageConverter : public ImageConverter {
public:
    LibyuvImageConverter() = default;
    ~LibyuvImageConverter() override = default;

    static bool convertMM21ToI420(const TiledMM21Image& src, uint8_t* y, size_t strideY,
                                  uint8_t* u, size_t strideU, uint8_t* v, size_t strideV,
                                  int width, int height);

    bool convertMM21ToNV12(const TiledMM21Image& src, const NV12WritableImage& dst, int width,
                           int height) override;

    bool convertMT10ToP010(const TiledMT10Image& src, const P010WritableImage& dst, int width,
                           int height) override;

    bool convertMT10ToNV12(const TiledMT10Image& src, const NV12WritableImage& dst, int width,
                           int height) override;
};

}

#endif
