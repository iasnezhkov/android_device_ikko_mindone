// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: BSD-3-Clause

#define LOG_TAG "LibyuvImageConverter"

#include <v4l2_codec2/common/LibyuvImageConverter.h>

#include <limits>

#include <libyuv/convert.h>
#include <log/log.h>

namespace android {

namespace {

constexpr int kMt10TileW = 16;
constexpr int kMt10LumaTileRows = 32;
constexpr int kMt10ChromaTileRows = 16;
constexpr int kMt10PartRows = 4;
constexpr int kMt10PartBytes = 80;
constexpr int kMt10PartLsbBytes = 16;
constexpr int kMt10MsbRowBytes = 16;
constexpr int kMt10LumaTileBytes = 640;
constexpr int kMt10ChromaTileBytes = 320;

int mt10RoundUp(int v, int mult) {
    return (v + mult - 1) / mult * mult;
}

uint16_t mt10DecodeSample(const uint8_t* plane, int tileRow, int tileCol, int rowInTile,
                          int colInTile, int tileBytes, int tilesPerRow, bool raster) {
    const int partition = rowInTile / kMt10PartRows;
    const int rowInPart = rowInTile % kMt10PartRows;
    const long tileIndex = static_cast<long>(tileRow) * tilesPerRow + tileCol;
    const uint8_t* part =
            plane + tileIndex * tileBytes + static_cast<long>(partition) * kMt10PartBytes;
    const uint8_t* lsb = part;
    const uint8_t* msb = part + kMt10PartLsbBytes;
    const uint8_t msbByte = msb[rowInPart * kMt10MsbRowBytes + colInTile];
    uint8_t lsb2;

    if (!raster) {
        lsb2 = (lsb[colInTile] >> (rowInPart * 2)) & 3;
    } else {
        const int idx = rowInPart * 4 + colInTile / 4;
        const int shift = (colInTile % 4) * 2;
        lsb2 = (lsb[idx] >> shift) & 3;
    }
    return static_cast<uint16_t>(((static_cast<uint16_t>(msbByte) << 2) | lsb2) << 6);
}

void mt10DecodePlane(const uint8_t* src, int tileBytes, int tileRows, int tilesPerRow, int widthOut,
                     int heightRows, bool raster, uint8_t* dst, size_t dstStrideBytes,
                     int bytesPerSample) {
    for (int y = 0; y < heightRows; y++) {
        const int tileRow = y / tileRows;
        const int rowInTile = y % tileRows;
        uint8_t* drow = dst + static_cast<size_t>(y) * dstStrideBytes;

        for (int x = 0; x < widthOut; x++) {
            const int tileCol = x / kMt10TileW;
            const int colInTile = x % kMt10TileW;
            const uint16_t sample = mt10DecodeSample(src, tileRow, tileCol, rowInTile, colInTile,
                                                      tileBytes, tilesPerRow, raster);
            if (bytesPerSample == 2) {
                drow[x * 2] = static_cast<uint8_t>(sample & 0xff);
                drow[x * 2 + 1] = static_cast<uint8_t>(sample >> 8);
            } else {
                drow[x] = static_cast<uint8_t>(sample >> 8);
            }
        }
    }
}

}

bool LibyuvImageConverter::convertMT10ToP010(const TiledMT10Image& src, const P010WritableImage& dst,
                                             int width, int height) {
    if (!src.data || !dst.y || !dst.uv || width <= 0 || height <= 0 ||
        src.strideY < static_cast<size_t>(width)) {
        ALOGE("convertMT10ToP010: invalid arguments, width=%d height=%d strideY=%zu", width, height,
              src.strideY);
        return false;
    }

    const int tilesPerRow = static_cast<int>(src.strideY) / kMt10TileW;
    const int lumaTileRowCount = mt10RoundUp(height, kMt10LumaTileRows) / kMt10LumaTileRows;
    const size_t yBytes = static_cast<size_t>(tilesPerRow) * lumaTileRowCount * kMt10LumaTileBytes;

    mt10DecodePlane(src.data, kMt10LumaTileBytes, kMt10LumaTileRows, tilesPerRow, width, height,
                    src.raster, dst.y, dst.strideY, 2);
    mt10DecodePlane(src.data + yBytes, kMt10ChromaTileBytes, kMt10ChromaTileRows, tilesPerRow, width,
                    (height + 1) / 2, src.raster, dst.uv, dst.strideUV, 2);
    return true;
}

bool LibyuvImageConverter::convertMT10ToNV12(const TiledMT10Image& src, const NV12WritableImage& dst,
                                             int width, int height) {
    if (!src.data || !dst.y || !dst.uv || width <= 0 || height <= 0 ||
        src.strideY < static_cast<size_t>(width)) {
        ALOGE("convertMT10ToNV12: invalid arguments, width=%d height=%d strideY=%zu", width, height,
              src.strideY);
        return false;
    }

    const int tilesPerRow = static_cast<int>(src.strideY) / kMt10TileW;
    const int lumaTileRowCount = mt10RoundUp(height, kMt10LumaTileRows) / kMt10LumaTileRows;
    const size_t yBytes = static_cast<size_t>(tilesPerRow) * lumaTileRowCount * kMt10LumaTileBytes;

    mt10DecodePlane(src.data, kMt10LumaTileBytes, kMt10LumaTileRows, tilesPerRow, width, height,
                    src.raster, dst.y, dst.strideY, 1);
    mt10DecodePlane(src.data + yBytes, kMt10ChromaTileBytes, kMt10ChromaTileRows, tilesPerRow, width,
                    (height + 1) / 2, src.raster, dst.uv, dst.strideUV, 1);
    return true;
}

bool LibyuvImageConverter::convertMM21ToI420(const TiledMM21Image& src, uint8_t* y, size_t strideY,
                                           uint8_t* u, size_t strideU, uint8_t* v, size_t strideV,
                                           int width, int height) {
    const size_t maxStride = std::numeric_limits<int>::max();
    if (!src.y || !src.uv || !y || !u || !v || width <= 0 || height <= 0 ||
        width == std::numeric_limits<int>::max() ||
        height == std::numeric_limits<int>::max()) {
        return false;
    }
    const size_t chromaWidth = (static_cast<size_t>(width) + 1) / 2;
    if (src.strideY < static_cast<size_t>(width) || src.strideUV < chromaWidth * 2 ||
        src.strideY > maxStride / 32 || src.strideUV > maxStride / 16 ||
        src.strideY % 16 || src.strideUV % 16 ||
        strideY < static_cast<size_t>(width) || strideU < chromaWidth ||
        strideV < chromaWidth || strideY > maxStride || strideU > maxStride ||
        strideV > maxStride) {
        return false;
    }
    const uint64_t yRows = (static_cast<uint64_t>(height) + 31) / 32 * 32;
    const uint64_t uvRows = ((static_cast<uint64_t>(height) + 1) / 2 + 15) / 16 * 16;
    const uintptr_t sourceY = reinterpret_cast<uintptr_t>(src.y);
    const uintptr_t sourceUV = reinterpret_cast<uintptr_t>(src.uv);
    if (sourceUV < sourceY || sourceUV - sourceY > src.dmaBufSize ||
        src.dmaBufSize > std::numeric_limits<uintptr_t>::max() - sourceY ||
        src.strideY * yRows > sourceUV - sourceY ||
        src.strideUV * uvRows > src.dmaBufSize - (sourceUV - sourceY)) {
        return false;
    }
    return libyuv::MM21ToI420(src.y, static_cast<int>(src.strideY), src.uv,
                             static_cast<int>(src.strideUV), y, static_cast<int>(strideY),
                             u, static_cast<int>(strideU), v, static_cast<int>(strideV),
                             width, height) == 0;
}

bool LibyuvImageConverter::convertMM21ToNV12(const TiledMM21Image& src,
                                             const NV12WritableImage& dst, int width,
                                             int height) {
    if (!src.y || !src.uv || !dst.y || !dst.uv || width <= 0 || height <= 0) {
        ALOGE("convertMM21ToNV12: invalid arguments, width=%d height=%d", width, height);
        return false;
    }

    const int ret = libyuv::MM21ToNV12(src.y, static_cast<int>(src.strideY), src.uv,
                                       static_cast<int>(src.strideUV), dst.y,
                                       static_cast<int>(dst.strideY), dst.uv,
                                       static_cast<int>(dst.strideUV), width, height);
    if (ret != 0) {
        ALOGE("libyuv::MM21ToNV12 failed, ret=%d", ret);
        return false;
    }
    return true;
}

}
