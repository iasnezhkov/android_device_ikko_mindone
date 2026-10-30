// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_COMMON_HELPERS_H
#define ANDROID_V4L2_CODEC2_COMMON_HELPERS_H

#include <C2Config.h>
#include <system/graphics.h>
#include <ui/Size.h>

#include <v4l2_codec2/common/VideoPixelFormat.h>

namespace android {

struct VideoEncoderAcceleratorConfig {
    enum VideoFrameStorageType {
        SHMEM = 0,
        DMABUF = 1,
    };

    VideoPixelFormat mInputFormat;
    ui::Size mInputVisibleSize;
    C2Config::profile_t mOutputProfile;
    uint32_t mInitialBitrate;
    uint32_t mInitialFramerate;
    uint8_t mH264OutputLevel;
    VideoFrameStorageType mStorageType;
};

uint8_t c2LevelToV4L2Level(C2Config::level_t level);

android_ycbcr getGraphicBlockInfo(const C2ConstGraphicBlock& block);

bool extractSPSPPS(const uint8_t* data, size_t length, std::vector<uint8_t>* sps,
                   std::vector<uint8_t>* pps);

bool extractVPSSPSPPS(const uint8_t* data, size_t length, std::vector<uint8_t>* vps,
                      std::vector<uint8_t>* sps, std::vector<uint8_t>* pps);

bool extractCSDInfo(std::unique_ptr<C2StreamInitDataInfo::output>* const csd, const uint8_t* data,
                    size_t length);

bool extractCSDInfoHevc(std::unique_ptr<C2StreamInitDataInfo::output>* const csd,
                        const uint8_t* data, size_t length);

size_t prependSPSPPSToIDR(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize,
                          std::vector<uint8_t>* sps, std::vector<uint8_t>* pps);

}

#endif
