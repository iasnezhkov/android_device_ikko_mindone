/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "mindone.camera.aidl"

#include "OisControl.h"
#include "MetadataEdit.h"

#include <android-base/file.h>
#include <android-base/properties.h>
#include <log/log.h>
#include <system/camera_metadata.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace mindone::camera {
namespace {

constexpr char kOisProperty[] = "persist.vendor.camera.ois";
constexpr char kOisModePath[] =
        "/sys/devices/platform/soc/11eb1000.i2c/i2c-4/4-0069/mode";
constexpr size_t kTrackedFrames = 64;

}

bool oisAvailable() {
    if (!::android::base::GetBoolProperty(kOisProperty, true)) return false;
    return access(kOisModePath, R_OK | W_OK) == 0;
}

void OisModes::configure(int32_t partialCount) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (partialCount < 1) return;
    mEnabled = true;
    mPartialCount = static_cast<uint32_t>(partialCount);
    mLast = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    mApplied.clear();
    mBatch.clear();
    mFrames.clear();
}

bool OisModes::enabled() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mEnabled;
}

bool OisModes::setDefaultRequest(::aidl::android::hardware::camera::device::RequestTemplate type,
                                 std::vector<uint8_t>* bytes) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mEnabled || bytes->empty()) return true;
    const uint8_t mode = type == ::aidl::android::hardware::camera::device::RequestTemplate::MANUAL
                                 ? ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF
                                 : ANDROID_LENS_OPTICAL_STABILIZATION_MODE_ON;
    return editMetadata(bytes, 1, 8, [&](camera_metadata_t* metadata) {
        return store(metadata, ANDROID_LENS_OPTICAL_STABILIZATION_MODE, &mode, 1);
    });
}

bool OisModes::mapRequests(const std::vector<uint32_t>& frames,
                           std::vector<std::vector<uint8_t>>* settings) {
    std::lock_guard<std::mutex> lock(mMutex);
    mBatch.clear();
    if (!mEnabled) return true;
    uint8_t mode = mLast;
    const uint8_t off = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    for (size_t i = 0; i < frames.size(); ++i) {
        auto& bytes = (*settings)[i];
        bool rewrite = false;
        if (!bytes.empty()) {
            const auto requested = peek<uint8_t>(bytes, ANDROID_LENS_OPTICAL_STABILIZATION_MODE);
            if (requested.size() > 1) {
                for (size_t j = 0; j < i; ++j) mFrames.erase(frames[j]);
                mBatch.clear();
                return false;
            }
            if (requested.size() == 1) {
                mode = requested[0] == ANDROID_LENS_OPTICAL_STABILIZATION_MODE_ON
                               ? ANDROID_LENS_OPTICAL_STABILIZATION_MODE_ON
                               : ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
                rewrite = requested[0] != off;
            }
        }
        if (rewrite &&
            !editMetadata(&bytes, 1, 8, [&](camera_metadata_t* metadata) {
                return store(metadata, ANDROID_LENS_OPTICAL_STABILIZATION_MODE, &off, 1);
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

void OisModes::keep(const std::vector<uint32_t>& frames, size_t processed) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mEnabled) return;
    for (size_t i = processed; i < frames.size(); ++i) mFrames.erase(frames[i]);
    if (processed > 0 && processed <= mBatch.size()) {
        mLast = mBatch[processed - 1];
        applyLocked(mLast == ANDROID_LENS_OPTICAL_STABILIZATION_MODE_ON ? "ois" : "center");
    }
    mBatch.clear();
}

bool OisModes::mapResult(uint32_t frame, uint32_t partial, std::vector<uint8_t>* result) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mEnabled || result->empty()) return true;
    auto it = mFrames.find(frame);
    if (it == mFrames.end()) return true;
    const auto existing = peek<uint8_t>(*result, ANDROID_LENS_OPTICAL_STABILIZATION_MODE);
    if (existing.empty() && partial != mPartialCount) return true;
    const uint8_t mode = it->second;
    mFrames.erase(it);
    if (existing.size() == 1 && existing[0] == mode) return true;
    return editMetadata(result, 1, 8, [&](camera_metadata_t* metadata) {
        return store(metadata, ANDROID_LENS_OPTICAL_STABILIZATION_MODE, &mode, 1);
    });
}

void OisModes::forget(uint32_t frame) {
    std::lock_guard<std::mutex> lock(mMutex);
    mFrames.erase(frame);
}

void OisModes::park() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mEnabled) applyLocked("off");
    mLast = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    mBatch.clear();
    mFrames.clear();
}

void OisModes::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    mBatch.clear();
    mFrames.clear();
}

void OisModes::applyLocked(const std::string& mode) {
    if (mApplied == mode) return;
    if (!::android::base::WriteStringToFile(mode, kOisModePath)) {
        ALOGW("Cannot set %s to %s: %s", kOisModePath, mode.c_str(), strerror(errno));
        return;
    }
    mApplied = mode;
}

}
