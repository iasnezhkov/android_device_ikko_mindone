// Copyright 2015 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMMON_VIDEO_PIXEL_FORMAT_H
#define ANDROID_V4L2_CODEC2_COMMON_VIDEO_PIXEL_FORMAT_H

#include <string>

#include "ui/Size.h"

namespace android {

enum class VideoPixelFormat {
    I420,
    YV12,
    I422,
    I420A,
    I444,
    NV12,
    NV21,
    YUY2,
    ARGB,
    XRGB,
    RGB24,
    MJPEG,
    Y16,
    ABGR,
    XBGR,
    P016LE,
    XR30,
    XB30,
    BGRA,
    RGBA,
    P010,

    YUV420P9,
    YUV420P10,
    YUV422P9,
    YUV422P10,
    YUV444P9,
    YUV444P10,
    YUV420P12,
    YUV422P12,
    YUV444P12,
    UNKNOWN,
};

std::string videoPixelFormatToString(VideoPixelFormat format);

std::string fourccToString(uint32_t fourcc);

size_t bitDepth(VideoPixelFormat format);

size_t numPlanes(VideoPixelFormat format);

size_t allocationSize(VideoPixelFormat format, const android::ui::Size& coded_size);

android::ui::Size planeSize(VideoPixelFormat format, size_t plane,
                            const android::ui::Size& coded_size);

int planeHorizontalBitsPerPixel(VideoPixelFormat format, size_t plane);

int planeBitsPerPixel(VideoPixelFormat format, size_t plane);

int bytesPerElement(VideoPixelFormat format, size_t plane);

bool isValidPlane(VideoPixelFormat format, size_t plane);

android::ui::Size SampleSize(VideoPixelFormat format, size_t plane);

}

#endif
