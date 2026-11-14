/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CameraProvider.h"

#include "CameraDevice.h"
#include "FlipWatcher.h"
#include "StreamConvert.h"

#include <aidl/android/hardware/camera/common/Status.h>
#include <android-base/properties.h>
#include <android/hardware/camera/provider/2.6/ICameraProviderCallback.h>
#include <log/log.h>

#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <utility>

namespace mindone::camera {

namespace hc = ::android::hardware::camera::common::V1_0;
using ::android::hardware::hidl_string;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ndk::ScopedAStatus;

constexpr char kFlipSwitch[] = "persist.vendor.camera.flip_exclusive";
constexpr char kHallPosition[] = "/sys/devices/platform/soc/10010000.kp/hall_position";
constexpr char kInputDir[] = "/dev/input";

struct FlipLinks {
    std::mutex mutex;
    std::shared_ptr<ap::ICameraProviderCallback> callback;
    std::map<std::string, hc::TorchModeStatus> torch;
};

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

    std::vector<std::string> names() override {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::string> result;
        for (const auto& entry : backendNames) result.push_back(entry.first);
        return result;
    }
};


class ProviderCallback : public hp::V2_6::ICameraProviderCallback {
  public:
    ProviderCallback(std::shared_ptr<ap::ICameraProviderCallback> callback,
                     std::shared_ptr<NameMap> names, std::shared_ptr<SensorOwner> sensor,
                     std::shared_ptr<FlipLinks> links)
        : mCallback(std::move(callback)), mNames(std::move(names)), mSensor(std::move(sensor)),
          mLinks(std::move(links)) {}

    Return<void> cameraDeviceStatusChange(const hidl_string& name,
                                         hc::CameraDeviceStatus value) override {
        const std::string converted = mNames->remember(name.c_str());
        if (mSensor->absent(converted)) {
            ALOGI("%s: HAL status %d kept back, the flip module faces the other way",
                  converted.c_str(), static_cast<int>(value));
            return Void();
        }
        check(mCallback->cameraDeviceStatusChange(converted,
                                                 static_cast<ac::CameraDeviceStatus>(value)));
        return Void();
    }

    Return<void> torchModeStatusChange(const hidl_string& name,
                                      hc::TorchModeStatus value) override {
        const std::string converted = mNames->remember(name.c_str());
        if (mLinks) {
            std::lock_guard<std::mutex> lock(mLinks->mutex);
            mLinks->torch[converted] = value;
        }
        if (mSensor->absent(converted)) return Void();
        check(mCallback->torchModeStatusChange(converted,
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
    const std::shared_ptr<SensorOwner> mSensor;
    const std::shared_ptr<FlipLinks> mLinks;
};

static void torchOff(const ::android::sp<hp::V2_6::ICameraProvider>& backend,
                     const std::shared_ptr<NameMap>& names,
                     const std::shared_ptr<FlipLinks>& links, const std::string& name) {
    {
        std::lock_guard<std::mutex> lock(links->mutex);
        auto it = links->torch.find(name);
        if (it == links->torch.end() || it->second != hc::TorchModeStatus::AVAILABLE_ON) return;
    }
    ::android::sp<hd::V3_2::ICameraDevice> device;
    auto result = backend->getCameraDeviceInterface_V3_x(names->lookup(name),
            [&](auto code, const auto& interface) {
                if (code == hc::Status::OK) device = interface;
            });
    if (!result.isOk() || !device) {
        ALOGW("%s: no device to turn the torch off before the flip", name.c_str());
        return;
    }
    auto off = device->setTorchMode(hc::TorchMode::OFF);
    ALOGI("%s: torch off before the flip: %d", name.c_str(),
          off.isOk() ? static_cast<int>(static_cast<hc::Status>(off)) : -1);
}

static std::shared_ptr<FlipWatcher> createFlip(
        const ::android::sp<hp::V2_6::ICameraProvider>& backend,
        const std::shared_ptr<NameMap>& names, const std::shared_ptr<SensorOwner>& sensor,
        const std::shared_ptr<FlipLinks>& links) {
    if (!::android::base::GetBoolProperty(kFlipSwitch, false)) return nullptr;
    FlipWatcher::Hooks hooks;
    hooks.setAbsent = [sensor](const std::set<std::string>& absent) {
        sensor->setAbsent(absent);
    };
    hooks.leave = [backend, names, links](const std::string& name) {
        torchOff(backend, names, links, name);
    };
    hooks.report = [links](const std::string& name, bool present) {
        std::shared_ptr<ap::ICameraProviderCallback> callback;
        {
            std::lock_guard<std::mutex> lock(links->mutex);
            callback = links->callback;
        }
        if (!callback) return false;
        auto result = callback->cameraDeviceStatusChange(name, present
                ? ac::CameraDeviceStatus::PRESENT : ac::CameraDeviceStatus::NOT_PRESENT);
        if (!result.isOk()) ALOGW("%s: presence report failed: %s", name.c_str(),
                                 result.getDescription().c_str());
        return result.isOk();
    };
    return FlipWatcher::create(kHallPosition, kInputDir, std::move(hooks));
}

CameraProvider::CameraProvider(const ::android::sp<hp::V2_6::ICameraProvider>& backend,
                               const MaximumResolution& maximumResolution)
    : mBackend(backend),
      mNames(std::make_shared<DeviceNames>()),
      mSensor(std::make_shared<SensorOwner>()),
      mLinks(std::make_shared<FlipLinks>()),
      mFlip(createFlip(mBackend, mNames, mSensor, mLinks)),
      mMaximumResolution(maximumResolution) {}

ScopedAStatus CameraProvider::setCallback(
        const std::shared_ptr<ap::ICameraProviderCallback>& callback) {
    if (!callback) return status(hc::Status::ILLEGAL_ARGUMENT);
    if (mFlip) {
        std::lock_guard<std::mutex> lock(mLinks->mutex);
        mLinks->callback = callback;
    }
    auto result = mBackend->setCallback(
            new ProviderCallback(callback, mNames, mSensor, mFlip ? mLinks : nullptr));
    if (!result.isOk()) return status(hc::Status::INTERNAL_ERROR);
    if (mFlip && static_cast<hc::Status>(result) == hc::Status::OK) mFlip->attach();
    return status(result);
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
    return createCameraDevice(backend, name, mNames, mSensor, mMaximumResolution, device);
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
