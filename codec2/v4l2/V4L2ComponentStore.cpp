// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#define LOG_TAG "V4L2ComponentStore"

#include <v4l2_codec2/v4l2/V4L2ComponentStore.h>

#include <stdint.h>

#include <memory>
#include <mutex>

#include <cutils/properties.h>

#include <C2.h>
#include <C2Config.h>
#include <log/log.h>
#include <media/stagefright/foundation/MediaDefs.h>

#include <v4l2_codec2/components/ComponentStore.h>
#include <v4l2_codec2/v4l2/V4L2ComponentCommon.h>
#include <v4l2_codec2/v4l2/V4L2ComponentFactory.h>

namespace android {

std::shared_ptr<C2ComponentStore> V4L2ComponentStore::Create() {
    ALOGV("%s()", __func__);

    static std::mutex mutex;
    static std::weak_ptr<C2ComponentStore> platformStore;

    std::lock_guard<std::mutex> lock(mutex);
    std::shared_ptr<C2ComponentStore> store = platformStore.lock();
    if (store != nullptr) return store;

    auto builder = ComponentStore::Builder("android.componentStore.v4l2");

    if (property_get_bool("ro.vendor.v4l2_codec2.encoder.supported.h264", false))
        builder.encoder(V4L2ComponentName::kH264Encoder, VideoCodec::H264,
                        &V4L2ComponentFactory::create);
    if (property_get_bool("ro.vendor.v4l2_codec2.encoder.supported.hevc", false))
        builder.encoder(V4L2ComponentName::kHEVCEncoder, VideoCodec::HEVC,
                        &V4L2ComponentFactory::create);

    if (property_get_bool("ro.vendor.v4l2_codec2.decoder.supported.h264", false))
        builder.decoder(V4L2ComponentName::kH264Decoder, VideoCodec::H264,
                        &V4L2ComponentFactory::create);
    if (property_get_bool("ro.vendor.v4l2_codec2.decoder.supported.vp8", false))
        builder.decoder(V4L2ComponentName::kVP8Decoder, VideoCodec::VP8,
                        &V4L2ComponentFactory::create);
    if (property_get_bool("ro.vendor.v4l2_codec2.decoder.supported.vp9", false))
        builder.decoder(V4L2ComponentName::kVP9Decoder, VideoCodec::VP9,
                        &V4L2ComponentFactory::create);
    if (property_get_bool("ro.vendor.v4l2_codec2.decoder.supported.hevc", false))
        builder.decoder(V4L2ComponentName::kHEVCDecoder, VideoCodec::HEVC,
                        &V4L2ComponentFactory::create);
    if (property_get_bool("ro.vendor.v4l2_codec2.decoder.supported.av1", false))
        builder.decoder(V4L2ComponentName::kAV1Decoder, VideoCodec::AV1,
                        &V4L2ComponentFactory::create);
    if (property_get_bool("ro.vendor.v4l2_codec2.decoder.supported.mpeg4", false))
        builder.decoder(V4L2ComponentName::kMPEG4Decoder, VideoCodec::MPEG4,
                        &V4L2ComponentFactory::create);
    if (property_get_bool("ro.vendor.v4l2_codec2.decoder.supported.h263", false))
        builder.decoder(V4L2ComponentName::kH263Decoder, VideoCodec::H263,
                        &V4L2ComponentFactory::create);

    if (property_get_bool("ro.vendor.v4l2_codec2.decoder.supported.h264.secure", false))
        builder.decoder(V4L2ComponentName::kH264SecureDecoder, VideoCodec::H264,
                        &V4L2ComponentFactory::create);
    if (property_get_bool("ro.vendor.v4l2_codec2.decoder.supported.vp8.secure", false))
        builder.decoder(V4L2ComponentName::kVP8SecureDecoder, VideoCodec::VP8,
                        &V4L2ComponentFactory::create);
    if (property_get_bool("ro.vendor.v4l2_codec2.decoder.supported.vp9.secure", false))
        builder.decoder(V4L2ComponentName::kVP9SecureDecoder, VideoCodec::VP9,
                        &V4L2ComponentFactory::create);
    if (property_get_bool("ro.vendor.v4l2_codec2.decoder.supported.hevc.secure", false))
        builder.decoder(V4L2ComponentName::kHEVCSecureDecoder, VideoCodec::HEVC,
                        &V4L2ComponentFactory::create);

    store = std::shared_ptr<C2ComponentStore>(std::move(builder).build());
    platformStore = store;
    return store;
}

}
