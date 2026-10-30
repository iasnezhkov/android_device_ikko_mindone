/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "StreamConvert.h"
#include <aidl/android/hardware/camera/device/ICameraDeviceCallback.h>
#include <android/hardware/camera/device/3.5/ICameraDeviceCallback.h>
#include <fmq/AidlMessageQueue.h>
#include <fmq/MessageQueue.h>
#include <mutex>
#include <cstring>
#include <system/camera_metadata.h>
namespace mindone::camera {
using AidlQueue = ::android::AidlMessageQueue<int8_t,
        ::aidl::android::hardware::common::fmq::SynchronizedReadWrite>;
using HidlQueue = ::android::hardware::MessageQueue<uint8_t,
        ::android::hardware::kSynchronizedReadWrite>;
template <class T> bool validMetadata(const T& bytes) {
    if (bytes.size() == 0) return true;
    std::vector<uint64_t> aligned((bytes.size() + 7) / 8);
    std::memcpy(aligned.data(), bytes.data(), bytes.size());
    const auto* metadata = reinterpret_cast<const camera_metadata_t*>(aligned.data());
    size_t size = bytes.size();
    return validate_camera_metadata_structure(metadata, &size) == 0 &&
            get_camera_metadata_size(metadata) == bytes.size();
}
struct MetadataQueues {
    AidlQueue request{1 << 20, false};
    AidlQueue result{1 << 20, false};
    std::unique_ptr<HidlQueue> backend;
    std::mutex requestMutex, resultMutex;
};
std::shared_ptr<MetadataQueues> getMetadataQueues(
        const ::android::sp<hd::V3_5::ICameraDeviceCallback>& callback);
::android::sp<hd::V3_5::ICameraDeviceCallback> createCameraDeviceCallback(
        const std::shared_ptr<ad::ICameraDeviceCallback>& callback);
}
