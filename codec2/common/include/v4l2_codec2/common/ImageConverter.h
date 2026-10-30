// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMMON_IMAGE_CONVERTER_H
#define ANDROID_V4L2_CODEC2_COMMON_IMAGE_CONVERTER_H

#include <cstddef>
#include <cstdint>

namespace android {

struct TiledMM21Image {
    const uint8_t* y = nullptr;
    const uint8_t* uv = nullptr;
    size_t strideY = 0;
    size_t strideUV = 0;
    int dmaBufFd = -1;
    size_t dmaBufSize = 0;
};

struct NV12WritableImage {
    uint8_t* y = nullptr;
    uint8_t* uv = nullptr;
    size_t strideY = 0;
    size_t strideUV = 0;
    int dmaBufFd = -1;
    size_t offsetY = 0;
    size_t offsetUV = 0;
    size_t sizeY = 0;
    size_t sizeUV = 0;
    size_t strideV = 0;
    size_t offsetV = 0;
    size_t sizeV = 0;
    size_t dmaBufSize = 0;
    uint32_t chromaStep = 2;
};

struct TiledMT10Image {
    const uint8_t* data = nullptr;
    size_t strideY = 0;
    bool raster = false;
    int dmaBufFd = -1;
    size_t dmaBufSize = 0;
};

struct P010WritableImage {
    uint8_t* y = nullptr;
    uint8_t* uv = nullptr;
    size_t strideY = 0;
    size_t strideUV = 0;
};

class ImageConverter {
public:
    virtual ~ImageConverter() = default;

    virtual bool convertMM21ToNV12(const TiledMM21Image& src, const NV12WritableImage& dst,
                                   int width, int height) = 0;

    virtual bool convertMT10ToP010(const TiledMT10Image& src, const P010WritableImage& dst,
                                   int width, int height) {
        return false;
    }

    virtual bool convertMT10ToNV12(const TiledMT10Image& src, const NV12WritableImage& dst,
                                   int width, int height) {
        return false;
    }
};

}

#endif
