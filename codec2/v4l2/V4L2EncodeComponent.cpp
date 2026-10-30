// Copyright 2023 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#define LOG_TAG "V4L2EncodeComponent"

#include <v4l2_codec2/v4l2/V4L2EncodeComponent.h>

#include <base/bind_helpers.h>

#include <cutils/properties.h>

#include <v4l2_codec2/common/Common.h>
#include <v4l2_codec2/common/H264.h>
#include <v4l2_codec2/components/BitstreamBuffer.h>
#include <v4l2_codec2/components/EncodeInterface.h>
#include <v4l2_codec2/v4l2/V4L2Encoder.h>

namespace android {

std::atomic<int32_t> V4L2EncodeComponent::sConcurrentInstances = 0;

std::shared_ptr<C2Component> V4L2EncodeComponent::create(
        C2String name, c2_node_id_t id, std::shared_ptr<EncodeInterface> intfImpl,
        C2ComponentFactory::ComponentDeleter deleter) {
    ALOGV("%s(%s)", __func__, name.c_str());

    static const int32_t kMaxConcurrentInstances =
            property_get_int32("ro.vendor.v4l2_codec2.encode_concurrent_instances", -1);

    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    if (kMaxConcurrentInstances >= 0 && sConcurrentInstances.load() >= kMaxConcurrentInstances) {
        ALOGW("Cannot create additional encoder, maximum number of instances reached: %d",
              kMaxConcurrentInstances);
        return nullptr;
    }

    return std::shared_ptr<C2Component>(new V4L2EncodeComponent(name, id, std::move(intfImpl)),
                                        deleter);
}

V4L2EncodeComponent::V4L2EncodeComponent(C2String name, c2_node_id_t id,
                                         std::shared_ptr<EncodeInterface> interface)
      : EncodeComponent(name, id, interface) {
    ALOGV("%s():", __func__);
    sConcurrentInstances.fetch_add(1, std::memory_order_relaxed);
}

V4L2EncodeComponent::~V4L2EncodeComponent() {
    ALOGV("%s():", __func__);
    sConcurrentInstances.fetch_sub(1, std::memory_order_relaxed);
}

bool V4L2EncodeComponent::initializeEncoder() {
    ALOGV("%s()", __func__);
    ALOG_ASSERT(mEncoderTaskRunner->RunsTasksInCurrentSequence());
    ALOG_ASSERT(!mInputFormatConverter);
    ALOG_ASSERT(!mEncoder);

    mRecentFrameTimes.clear();

    C2Config::profile_t outputProfile = mInterface->getOutputProfile();

    mExtractCSD = isH264Profile(outputProfile) ||
                  isValidProfileForCodec(VideoCodec::HEVC, outputProfile);

    std::optional<uint8_t> h264Level;
    if (isH264Profile(outputProfile)) {
        h264Level = c2LevelToV4L2Level(mInterface->getOutputLevel());
    }

    std::optional<uint32_t> stride =
            getVideoFrameStride(VideoEncoder::kInputPixelFormat, mInterface->getInputVisibleSize());
    if (!stride) {
        ALOGE("Failed to get video frame stride");
        reportError(C2_CORRUPTED);
        return false;
    }

    mBitrateMode = mInterface->getBitrateMode();
    if (property_get_bool("persist.vendor.v4l2_codec2.disable_vbr", false)) {

        ALOGW("VBR is disabled on this device");
        mBitrateMode = C2Config::BITRATE_CONST;
    }

    mBitrate = mInterface->getBitrate();

    mEncoder = V4L2Encoder::create(
            outputProfile, h264Level, mInterface->getInputVisibleSize(), *stride,
            mInterface->getKeyFramePeriod(), mBitrateMode, mBitrate,
            mBitrate * VideoEncoder::kPeakBitrateMultiplier,
            ::base::BindRepeating(&V4L2EncodeComponent::fetchOutputBlock, mWeakThis),
            ::base::BindRepeating(&V4L2EncodeComponent::onInputBufferDone, mWeakThis),
            ::base::BindRepeating(&V4L2EncodeComponent::onOutputBufferDone, mWeakThis),
            ::base::BindRepeating(&V4L2EncodeComponent::onDrainDone, mWeakThis),
            ::base::BindRepeating(&V4L2EncodeComponent::reportError, mWeakThis, C2_CORRUPTED),
            mEncoderTaskRunner);
    if (!mEncoder) {
        ALOGE("Failed to create V4L2Encoder (profile: %s)", profileToString(outputProfile));
        return false;
    }

    return true;
}

}
