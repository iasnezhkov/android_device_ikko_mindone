/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "mindone.camera.aidl"

#include "MaximumResolution.h"
#include "MetadataEdit.h"

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <log/log.h>
#include <system/camera_metadata.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>

namespace mindone::camera {
namespace {

namespace hc = ::android::hardware::camera::common::V1_0;

constexpr char kProperty[] = "persist.vendor.camera.maxres";
constexpr char kKernelSwitch[] = "/sys/module/imgsensor_isp6s/parameters/imx766_4cell";
constexpr char kRemosaicSection[] = "com.mediatek.control.capture";
constexpr char kRemosaicName[] = "remosaicenable";
constexpr size_t kTrackedFrames = 64;

struct Coordinates {
    uint32_t tag;
    size_t stride;
    size_t count;
};

constexpr Coordinates kRequestCoordinates[] = {
        {ANDROID_SCALER_CROP_REGION, 4, 4},
        {ANDROID_CONTROL_AE_REGIONS, 5, 4},
        {ANDROID_CONTROL_AF_REGIONS, 5, 4},
        {ANDROID_CONTROL_AWB_REGIONS, 5, 4},
};

constexpr Coordinates kResultCoordinates[] = {
        {ANDROID_SCALER_CROP_REGION, 4, 4},
        {ANDROID_CONTROL_AE_REGIONS, 5, 4},
        {ANDROID_CONTROL_AF_REGIONS, 5, 4},
        {ANDROID_CONTROL_AWB_REGIONS, 5, 4},
        {ANDROID_STATISTICS_FACE_RECTANGLES, 4, 4},
        {ANDROID_STATISTICS_FACE_LANDMARKS, 6, 6},
};

bool scale(camera_metadata_t* metadata, const Coordinates& coordinates, int32_t factor, bool up) {
    auto list = values<int32_t>(metadata, coordinates.tag);
    if (list.empty()) return true;
    if (list.size() % coordinates.stride) return false;
    for (size_t i = 0; i < list.size(); i += coordinates.stride) {
        for (size_t j = 0; j < coordinates.count; ++j) {
            list[i + j] = up ? list[i + j] * factor : list[i + j] / factor;
        }
    }
    return store(metadata, coordinates.tag, list.data(), list.size());
}

std::string readSwitch() {
    std::string value;
    if (!::android::base::ReadFileToString(kKernelSwitch, &value)) return {};
    return ::android::base::Trim(value);
}

}

int32_t applyMaximumResolutionSwitch() {
    const int32_t mode = std::max(0, ::android::base::GetIntProperty(kProperty, 0));
    const std::string current = readSwitch();
    if (current.empty()) {
        if (mode) ALOGW("%s=%d, but the kernel has no %s", kProperty, mode, kKernelSwitch);
        return 0;
    }
    const std::string wanted = std::to_string(mode);
    if (current != wanted && !::android::base::WriteStringToFile(wanted, kKernelSwitch)) {
        ALOGE("Cannot set %s to %s: %s", kKernelSwitch, wanted.c_str(), strerror(errno));
        return 0;
    }
    if (readSwitch() != wanted) {
        ALOGE("%s does not read back %s", kKernelSwitch, wanted.c_str());
        return 0;
    }
    if (mode) ALOGI("Sensor format mode %d set through %s", mode, kKernelSwitch);
    return mode;
}

MaximumResolution loadMaximumResolution(
        int32_t mode,
        const ::android::sp<::android::hardware::camera::provider::V2_6::ICameraProvider>& backend) {
    MaximumResolution result;
    if (mode <= 0) return result;
    uint32_t tag = 0;
    auto status = backend->getVendorTags([&](auto code, const auto& sections) {
        if (code != hc::Status::OK) return;
        for (const auto& section : sections) {
            if (section.sectionName != kRemosaicSection) continue;
            for (const auto& entry : section.tags) {
                if (entry.tagName == kRemosaicName &&
                    entry.tagType == hc::CameraMetadataType::INT32) {
                    tag = entry.tagId;
                }
            }
        }
    });
    if (!status.isOk() || !tag || get_camera_metadata_tag_type(tag) != TYPE_INT32) {
        ALOGE("No int32 %s.%s vendor tag, maximum resolution stays off", kRemosaicSection,
              kRemosaicName);
        return result;
    }
    ALOGI("Maximum resolution on, remosaic through vendor tag 0x%x", tag);
    result.remosaicTag = tag;
    return result;
}

void PixelModes::configure(uint32_t remosaicTag, int32_t binning, int32_t partialCount) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!remosaicTag || binning < 2 || partialCount < 1) return;
    mRemosaicTag = remosaicTag;
    mBinning = binning;
    mPartialCount = static_cast<uint32_t>(partialCount);
}

bool PixelModes::enabled() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mRemosaicTag != 0;
}

bool PixelModes::mapRequests(const std::vector<uint32_t>& frames,
                             std::vector<std::vector<uint8_t>>* settings) {
    std::lock_guard<std::mutex> lock(mMutex);
    mBatch.clear();
    if (!mRemosaicTag) return true;
    uint8_t mode = mLast;
    for (size_t i = 0; i < frames.size(); ++i) {
        auto& bytes = (*settings)[i];
        if (!bytes.empty()) {
            const auto requested = peek<uint8_t>(bytes, ANDROID_SENSOR_PIXEL_MODE);
            mode = requested.size() == 1 &&
                                   requested[0] == ANDROID_SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION
                           ? ANDROID_SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION
                           : ANDROID_SENSOR_PIXEL_MODE_DEFAULT;
        }
        const int32_t enable = 1;
        if (!bytes.empty() && mode == ANDROID_SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION &&
            !editMetadata(&bytes, 1, 64, [&](camera_metadata_t* metadata) {
                bool ok = store(metadata, mRemosaicTag, &enable, 1);
                for (const auto& coordinates : kRequestCoordinates) {
                    ok = ok && scale(metadata, coordinates, mBinning, false);
                }
                return ok;
            })) {
            for (size_t j = 0; j < i; ++j) mFrames.erase(frames[j]);
            mBatch.clear();
            return false;
        }
        mBatch.push_back(mode);
        mFrames[frames[i]] = mode;
    }
    while (mFrames.size() > kTrackedFrames) mFrames.erase(mFrames.begin());
    return true;
}

void PixelModes::keep(const std::vector<uint32_t>& frames, size_t processed) {
    std::lock_guard<std::mutex> lock(mMutex);
    for (size_t i = processed; i < frames.size(); ++i) mFrames.erase(frames[i]);
    if (processed > 0 && processed <= mBatch.size()) mLast = mBatch[processed - 1];
    mBatch.clear();
}

bool PixelModes::mapResult(uint32_t frame, uint32_t partial, std::vector<uint8_t>* result) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mRemosaicTag || partial != mPartialCount || result->empty()) return true;
    uint8_t mode = ANDROID_SENSOR_PIXEL_MODE_DEFAULT;
    auto it = mFrames.find(frame);
    if (it != mFrames.end()) {
        mode = it->second;
        mFrames.erase(it);
    }
    return editMetadata(result, 1, 64, [&](camera_metadata_t* metadata) {
        bool ok = store(metadata, ANDROID_SENSOR_PIXEL_MODE, &mode, 1);
        if (mode != ANDROID_SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION) return ok;
        for (const auto& coordinates : kResultCoordinates) {
            ok = ok && scale(metadata, coordinates, mBinning, true);
        }
        return ok;
    });
}

void PixelModes::forget(uint32_t frame) {
    std::lock_guard<std::mutex> lock(mMutex);
    mFrames.erase(frame);
}

void PixelModes::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    mFrames.clear();
    mBatch.clear();
}

}
