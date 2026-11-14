/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "mindone.camera.aidl"

#include "HighFps.h"

#include <android-base/properties.h>
#include <log/log.h>
#include <system/camera_metadata.h>
#include <system/graphics.h>

#include <cstring>
#include <memory>

namespace mindone::camera {
namespace {

namespace ad = ::aidl::android::hardware::camera::device;
using Metadata = std::unique_ptr<camera_metadata_t, decltype(&free_camera_metadata)>;

constexpr char kSwitch[] = "persist.vendor.camera.hfps60";
constexpr int64_t kMaxWidth = 2048;
constexpr int64_t kMaxHeight = 1152;
constexpr uint32_t kHfpsModeTag = 0x80080002;
constexpr int32_t kHfpsMode60 = 1;

std::vector<uint64_t> aligned(const std::vector<uint8_t>& bytes) {
    std::vector<uint64_t> words((bytes.size() + 7) / 8);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
}

}

bool highFpsEnabled() {
    static const bool enabled = ::android::base::GetBoolProperty(kSwitch, false);
    return enabled;
}

bool highFpsOutput(int64_t format, int64_t width, int64_t height) {
    return (format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED ||
            format == HAL_PIXEL_FORMAT_YCBCR_420_888) &&
           width > 0 && height > 0 && width <= kMaxWidth && height <= kMaxHeight;
}

bool highFpsStreams(const ad::StreamConfiguration& config) {
    if (config.operationMode != ad::StreamConfigurationMode::NORMAL_MODE ||
        config.streams.empty()) {
        return false;
    }
    for (const auto& stream : config.streams) {
        if (stream.streamType != ad::StreamType::OUTPUT ||
            !highFpsOutput(static_cast<int64_t>(stream.format), stream.width, stream.height)) {
            return false;
        }
    }
    return true;
}

bool highFpsRequested(const std::vector<uint8_t>& sessionParams) {
    if (sessionParams.empty()) return false;
    auto words = aligned(sessionParams);
    const auto* metadata = reinterpret_cast<const camera_metadata_t*>(words.data());
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(metadata, ANDROID_CONTROL_AE_TARGET_FPS_RANGE, &entry) != 0 ||
        entry.count != 2) {
        return false;
    }
    return entry.data.i32[1] > kNormalMaxFps;
}

bool requestHighFps(std::vector<uint8_t>* sessionParams) {
    auto words = aligned(*sessionParams);
    const auto* source = reinterpret_cast<const camera_metadata_t*>(words.data());
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(source, kHfpsModeTag, &entry) == 0) {
        ALOGI("hfpsMode is set by the client, kept");
        return false;
    }
    Metadata metadata(allocate_camera_metadata(get_camera_metadata_entry_count(source) + 1,
                                               get_camera_metadata_data_count(source) + 8),
                      free_camera_metadata);
    if (!metadata || append_camera_metadata(metadata.get(), source) != 0 ||
        add_camera_metadata_entry(metadata.get(), kHfpsModeTag, &kHfpsMode60, 1) != 0) {
        ALOGE("Cannot add hfpsMode to the session parameters, the session stays at 30 fps");
        return false;
    }
    Metadata compact(clone_camera_metadata(metadata.get()), free_camera_metadata);
    if (!compact) return false;
    const auto* raw = reinterpret_cast<const uint8_t*>(compact.get());
    sessionParams->assign(raw, raw + get_camera_metadata_size(compact.get()));
    ALOGI("Session asks for more than %d fps on outputs within %lldx%lld: hfpsMode=%d",
          kNormalMaxFps, static_cast<long long>(kMaxWidth), static_cast<long long>(kMaxHeight),
          kHfpsMode60);
    return true;
}

}
