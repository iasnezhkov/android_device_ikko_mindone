/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "StreamConvert.h"
#include <aidl/android/hardware/camera/common/Status.h>
#include <algorithm>
namespace mindone::camera {
ndk::ScopedAStatus convertStreams(const ad::StreamConfiguration& source,
                                 hd::V3_7::StreamConfiguration* target) {
    *target = {};
    target->operationMode = static_cast<hd::V3_2::StreamConfigurationMode>(source.operationMode);
    target->sessionParams = source.sessionParams.metadata;
    target->streamConfigCounter = static_cast<uint32_t>(source.streamConfigCounter);
    target->multiResolutionInputImage = source.multiResolutionInputImage;
    target->streams.resize(source.streams.size());
    for (size_t i = 0; i < source.streams.size(); ++i) {
        const auto& s = source.streams[i];
        if (s.width <= 0 || s.height <= 0 || s.bufferSize < 0 || s.groupId < -1 ||
            static_cast<int64_t>(s.dynamicRangeProfile) != 1 ||
            static_cast<int64_t>(s.useCase) != 0 ||
            (static_cast<uint64_t>(s.usage) >> 32) != 0) {
            return ndk::ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(
                    ::aidl::android::hardware::camera::common::Status::ILLEGAL_ARGUMENT));
        }
        auto& t = target->streams[i];
        t.v3_4 = {{s.id, static_cast<hd::V3_2::StreamType>(s.streamType),
                   static_cast<uint32_t>(s.width), static_cast<uint32_t>(s.height),
                   static_cast<::android::hardware::graphics::common::V1_0::PixelFormat>(s.format),
                   static_cast<uint32_t>(s.usage), static_cast<int32_t>(s.dataSpace),
                   static_cast<hd::V3_2::StreamRotation>(s.rotation)},
                  s.physicalCameraId, static_cast<uint32_t>(s.bufferSize)};
        t.groupId = s.groupId;
        t.sensorPixelModesUsed.resize(s.sensorPixelModesUsed.size());
        for (size_t j = 0; j < s.sensorPixelModesUsed.size(); ++j) {
            t.sensorPixelModesUsed[j] = static_cast<::android::hardware::camera::metadata::V3_6::
                    CameraMetadataEnumAndroidSensorPixelMode>(
                    s.sensorPixelModesUsed[j]);
        }
    }
    return ndk::ScopedAStatus::ok();
}
ndk::ScopedAStatus convertStreams(const ad::StreamConfiguration& source,
                                 hd::V3_4::StreamConfiguration* target) {
    hd::V3_7::StreamConfiguration full;
    auto result = convertStreams(source, &full);
    if (!result.isOk()) return result;
    if (full.multiResolutionInputImage) {
        return ndk::ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(
                ::aidl::android::hardware::camera::common::Status::ILLEGAL_ARGUMENT));
    }
    target->operationMode = full.operationMode;
    target->sessionParams = full.sessionParams;
    target->streams.resize(full.streams.size());
    for (size_t i = 0; i < full.streams.size(); ++i) {
        const auto& s = full.streams[i];
        const bool defaultMode = std::all_of(s.sensorPixelModesUsed.begin(),
                s.sensorPixelModesUsed.end(), [](auto mode) {
                    return mode == ::android::hardware::camera::metadata::V3_6::
                            CameraMetadataEnumAndroidSensorPixelMode::
                            ANDROID_SENSOR_PIXEL_MODE_DEFAULT;
                });
        if (s.groupId != -1 || !defaultMode) {
            return ndk::ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(
                    ::aidl::android::hardware::camera::common::Status::ILLEGAL_ARGUMENT));
        }
        target->streams[i] = s.v3_4;
    }
    return result;
}
}
