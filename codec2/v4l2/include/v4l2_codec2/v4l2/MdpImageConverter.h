// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_V4L2_MDP_IMAGE_CONVERTER_H
#define ANDROID_V4L2_CODEC2_V4L2_MDP_IMAGE_CONVERTER_H

#include <memory>

#include <v4l2_codec2/common/ImageConverter.h>
#include <v4l2_codec2/v4l2/MdpReplay.h>

namespace android {

class MdpImageConverter : public ImageConverter {
public:
    static std::unique_ptr<MdpImageConverter> Create();

    ~MdpImageConverter() override;

    bool convertMM21ToNV12(const TiledMM21Image& src, const NV12WritableImage& dst, int width,
                           int height) override;

private:
    MdpImageConverter();

    bool init();

    struct Impl;
    std::unique_ptr<Impl> mImpl;
    mdpreplay::MdpReplayEngine mEngine;
};

}

#endif
