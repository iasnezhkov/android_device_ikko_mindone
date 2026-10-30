/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CameraProvider.h"

#include "CameraDevice.h"
#include "StreamConvert.h"

#include <aidl/android/hardware/camera/common/Status.h>
#include <android/hardware/camera/provider/2.6/ICameraProviderCallback.h>
#include <log/log.h>

#include <map>
#include <mutex>
#include <regex>
#include <utility>

namespace mindone::camera {

namespace hc = ::android::hardware::camera::common::V1_0;
using ::android::hardware::hidl_string;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ndk::ScopedAStatus;

static ScopedAStatus status(hc::Status value) {
    ac::Status mapped = ac::Status::INTERNAL_ERROR;
    switch (value) {
        case hc::Status::OK:
            return ScopedAStatus::ok();
        case hc::Status::ILLEGAL_ARGUMENT:
            mapped = ac::Status::ILLEGAL_ARGUMENT;
            break;
        case hc::Status::CAMERA_IN_USE:
            mapped = ac::Status::CAMERA_IN_USE;
            break;
        case hc::Status::MAX_CAMERAS_IN_USE:
            mapped = ac::Status::MAX_CAMERAS_IN_USE;
            break;
        case hc::Status::METHOD_NOT_SUPPORTED:
        case hc::Status::OPERATION_NOT_SUPPORTED:
            mapped = ac::Status::OPERATION_NOT_SUPPORTED;
            break;
        case hc::Status::CAMERA_DISCONNECTED:
            mapped = ac::Status::CAMERA_DISCONNECTED;
            break;
        case hc::Status::INTERNAL_ERROR:
            break;
    }
    return ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(mapped));
}

struct DeviceNames final : NameMap {
    std::mutex mutex;
    std::map<std::string, std::string> backendNames;

    std::string remember(const std::string& backend) override {
        static const std::regex pattern("device@3\\.[0-9]+/internal/(.+)");
        std::smatch match;
        if (!std::regex_match(backend, match, pattern)) return backend;
        std::string name = "device@1.1/internal/" + match[1].str();
        std::lock_guard<std::mutex> lock(mutex);
        backendNames[name] = backend;
        return name;
    }

    std::string lookup(const std::string& name) override {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = backendNames.find(name);
        return it == backendNames.end() ? std::string() : it->second;
    }
};


class ProviderCallback : public hp::V2_6::ICameraProviderCallback {
  public:
    ProviderCallback(std::shared_ptr<ap::ICameraProviderCallback> callback,
                     std::shared_ptr<NameMap> names)
        : mCallback(std::move(callback)), mNames(std::move(names)) {}

    Return<void> cameraDeviceStatusChange(const hidl_string& name,
                                         hc::CameraDeviceStatus value) override {
        check(mCallback->cameraDeviceStatusChange(mNames->remember(name.c_str()),
                                                 static_cast<ac::CameraDeviceStatus>(value)));
        return Void();
    }

    Return<void> torchModeStatusChange(const hidl_string& name,
                                      hc::TorchModeStatus value) override {
        check(mCallback->torchModeStatusChange(mNames->remember(name.c_str()),
                                              static_cast<ac::TorchModeStatus>(value)));
        return Void();
    }

    Return<void> physicalCameraDeviceStatusChange(const hidl_string& name,
            const hidl_string& physicalName, hc::CameraDeviceStatus value) override {
        check(mCallback->physicalCameraDeviceStatusChange(mNames->remember(name.c_str()),
                mNames->remember(physicalName.c_str()), static_cast<ac::CameraDeviceStatus>(value)));
        return Void();
    }

  private:
    static void check(const ScopedAStatus& result) {
        if (!result.isOk()) ALOGW("Camera provider callback failed: %s",
                                 result.getDescription().c_str());
    }
    const std::shared_ptr<ap::ICameraProviderCallback> mCallback;
    const std::shared_ptr<NameMap> mNames;
};

CameraProvider::CameraProvider(const ::android::sp<hp::V2_6::ICameraProvider>& backend)
    : mBackend(backend), mNames(std::make_shared<DeviceNames>()) {}

ScopedAStatus CameraProvider::setCallback(
        const std::shared_ptr<ap::ICameraProviderCallback>& callback) {
    if (!callback) return status(hc::Status::ILLEGAL_ARGUMENT);
    auto result = mBackend->setCallback(new ProviderCallback(callback, mNames));
    return result.isOk() ? status(result) : status(hc::Status::INTERNAL_ERROR);
}

ScopedAStatus CameraProvider::getVendorTags(std::vector<ac::VendorTagSection>* tags) {
    tags->clear();
    hc::Status value = hc::Status::INTERNAL_ERROR;
    auto result = mBackend->getVendorTags([&](auto backendStatus, const auto& sections) {
        value = backendStatus;
        if (value != hc::Status::OK) return;
        for (const auto& section : sections) {
            ac::VendorTagSection converted;
            converted.sectionName = section.sectionName.c_str();
            for (const auto& tag : section.tags) {
                ac::VendorTag entry;
                entry.tagId = static_cast<int32_t>(tag.tagId);
                entry.tagName = tag.tagName.c_str();
                entry.tagType = static_cast<ac::CameraMetadataType>(tag.tagType);
                converted.tags.push_back(std::move(entry));
            }
            tags->push_back(std::move(converted));
        }
    });
    return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
}

ScopedAStatus CameraProvider::getCameraIdList(std::vector<std::string>* names) {
    names->clear();
    hc::Status value = hc::Status::INTERNAL_ERROR;
    auto result = mBackend->getCameraIdList([&](auto backendStatus, const auto& backendNames) {
        value = backendStatus;
        if (value != hc::Status::OK) return;
        for (const auto& name : backendNames) names->push_back(mNames->remember(name.c_str()));
    });
    return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
}

ScopedAStatus CameraProvider::getCameraDeviceInterface(
        const std::string& name, std::shared_ptr<ad::ICameraDevice>* device) {
    device->reset();
    if (mNames->lookup(name).empty()) {
        std::vector<std::string> names;
        auto result = getCameraIdList(&names);
        if (!result.isOk()) return result;
    }
    std::string backendName = mNames->lookup(name);
    if (backendName.empty()) return status(hc::Status::ILLEGAL_ARGUMENT);
    hc::Status value = hc::Status::INTERNAL_ERROR;
    ::android::sp<::android::hardware::camera::device::V3_2::ICameraDevice> backend;
    auto result = mBackend->getCameraDeviceInterface_V3_x(backendName,
            [&](auto backendStatus, const auto& interface) {
                value = backendStatus;
                backend = interface;
            });
    if (!result.isOk()) return status(hc::Status::INTERNAL_ERROR);
    if (value != hc::Status::OK) return status(value);
    if (!backend) return status(hc::Status::INTERNAL_ERROR);
    return createCameraDevice(backend, mNames, device);
}

ScopedAStatus CameraProvider::notifyDeviceStateChange(int64_t state) {
    auto result = mBackend->notifyDeviceStateChange(static_cast<uint64_t>(state));
    return status(result.isOk() ? hc::Status::OK : hc::Status::INTERNAL_ERROR);
}

ScopedAStatus CameraProvider::getConcurrentCameraIds(
        std::vector<ap::ConcurrentCameraIdCombination>* combinations) {
    combinations->clear();
    hc::Status value = hc::Status::INTERNAL_ERROR;
    auto result = mBackend->getConcurrentStreamingCameraIds(
            [&](auto backendStatus, const auto& ids) {
                value = backendStatus;
                if (value != hc::Status::OK) return;
                for (const auto& combination : ids) {
                    ap::ConcurrentCameraIdCombination converted;
                    for (const auto& id : combination) converted.combination.emplace_back(id.c_str());
                    combinations->push_back(std::move(converted));
                }
            });
    return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
}

ScopedAStatus CameraProvider::isConcurrentStreamCombinationSupported(
        const std::vector<ap::CameraIdAndStreamCombination>& configs, bool* supported) {
    *supported = false;
    ::android::hardware::hidl_vec<hp::V2_6::CameraIdAndStreamCombination> converted;
    converted.resize(configs.size());
    for (size_t i = 0; i < configs.size(); ++i) {
        converted[i].cameraId = configs[i].cameraId;
        auto result = convertStreams(configs[i].streamConfiguration,
                                     &converted[i].streamConfiguration);
        if (!result.isOk()) return result;
    }
    hc::Status value = hc::Status::INTERNAL_ERROR;
    auto result = mBackend->isConcurrentStreamCombinationSupported(converted,
            [&](auto backendStatus, bool query) { value = backendStatus; *supported = query; });
    return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
}

}
