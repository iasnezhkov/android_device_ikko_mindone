/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "mindone.camera.aidl"

#include "CameraDevice.h"
#include "CameraDeviceCallback.h"
#include "CameraDeviceSession.h"
#include "CharacteristicsFixup.h"
#include "MetadataEdit.h"
#include <aidl/android/hardware/camera/device/BnCameraDevice.h>
#include <android/hardware/camera/device/3.5/ICameraDevice.h>
#include <android/hardware/camera/device/3.7/ICameraDevice.h>
#include <cutils/native_handle.h>
#include <log/log.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace mindone::camera {
namespace hc = ::android::hardware::camera::common::V1_0;
using ndk::ScopedAStatus;
static ScopedAStatus status(hc::Status value) {
    if (value == hc::Status::OK) return ScopedAStatus::ok();
    int32_t mapped = static_cast<int32_t>(value);
    if (mapped >= static_cast<int32_t>(hc::Status::OPERATION_NOT_SUPPORTED)) --mapped;
    return ScopedAStatus::fromServiceSpecificError(mapped);
}
class CameraDevice : public ad::BnCameraDevice {
  public:
    CameraDevice(const ::android::sp<hd::V3_2::ICameraDevice>& backend, const std::string& name,
                 const std::shared_ptr<NameMap>& names, const std::shared_ptr<SensorOwner>& sensor,
                 const MaximumResolution& maximumResolution)
        : mBackend(backend), mName(name), mNames(names), mSensor(sensor),
          mMaximumResolution(maximumResolution) {}
    ScopedAStatus getCameraCharacteristics(ad::CameraMetadata* metadata) override {
        hc::Status value = hc::Status::INTERNAL_ERROR;
        auto result = mBackend->getCameraCharacteristics([&](auto code, const auto& bytes) {
            value = code;
            if (code == hc::Status::OK) metadata->metadata.assign(bytes.begin(), bytes.end());
        });
        if (result.isOk() && value == hc::Status::OK &&
            !fixCharacteristics(&metadata->metadata, mMaximumResolution.enabled())) {
            ALOGE("%s: characteristics fixup failed, passing the HAL metadata through",
                  mName.c_str());
        }
        return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
    }
    ScopedAStatus getPhysicalCameraCharacteristics(const std::string& id,
                                                 ad::CameraMetadata* metadata) override {
        auto cast = hd::V3_5::ICameraDevice::castFrom(mBackend);
        if (!cast.isOk()) return status(hc::Status::INTERNAL_ERROR);
        ::android::sp<hd::V3_5::ICameraDevice> backend = cast;
        if (!backend) return status(hc::Status::OPERATION_NOT_SUPPORTED);
        hc::Status value = hc::Status::INTERNAL_ERROR;
        auto result = backend->getPhysicalCameraCharacteristics(id,
                [&](auto code, const auto& bytes) {
            value = code;
            if (code == hc::Status::OK) metadata->metadata.assign(bytes.begin(), bytes.end());
        });
        return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
    }
    ScopedAStatus getResourceCost(
            ::aidl::android::hardware::camera::common::CameraResourceCost* cost) override {
        hc::Status value = hc::Status::INTERNAL_ERROR;
        auto result = mBackend->getResourceCost([&](auto code, const auto& source) {
            value = code;
            if (code != hc::Status::OK) return;
            cost->resourceCost = static_cast<int32_t>(source.resourceCost);
            cost->conflictingDevices.clear();
            for (const auto& name : source.conflictingDevices) {
                cost->conflictingDevices.push_back(mNames->remember(name.c_str()));
            }
        });
        if (!result.isOk() || value != hc::Status::OK) {
            return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
        }
        auto& conflicts = cost->conflictingDevices;
        for (const auto& other : mNames->names()) {
            if (other != mName && std::find(conflicts.begin(), conflicts.end(), other) ==
                                          conflicts.end()) {
                conflicts.push_back(other);
            }
        }
        return ScopedAStatus::ok();
    }
    ScopedAStatus isStreamCombinationSupported(const ad::StreamConfiguration& streams,
                                             bool* supported) override {
        *supported = false;
        hc::Status value = hc::Status::INTERNAL_ERROR;
        auto receive = [&](auto code, bool query) { value = code; *supported = query; };
        auto cast37 = hd::V3_7::ICameraDevice::castFrom(mBackend);
        if (!cast37.isOk()) return status(hc::Status::INTERNAL_ERROR);
        ::android::sp<hd::V3_7::ICameraDevice> backend37 = cast37;
        if (backend37) {
            hd::V3_7::StreamConfiguration converted;
            auto conversion = convertStreams(streams, &converted);
            if (!conversion.isOk()) return conversion;
            auto result = backend37->isStreamCombinationSupported_3_7(converted, receive);
            return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
        }
        auto cast35 = hd::V3_5::ICameraDevice::castFrom(mBackend);
        if (!cast35.isOk()) return status(hc::Status::INTERNAL_ERROR);
        ::android::sp<hd::V3_5::ICameraDevice> backend35 = cast35;
        if (!backend35) return status(hc::Status::OPERATION_NOT_SUPPORTED);
        hd::V3_4::StreamConfiguration converted;
        auto conversion = convertStreams(streams, &converted, mMaximumResolution.enabled());
        if (!conversion.isOk()) return conversion;
        auto result = backend35->isStreamCombinationSupported(converted, receive);
        return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
    }
    ScopedAStatus open(const std::shared_ptr<ad::ICameraDeviceCallback>& callback,
                      std::shared_ptr<ad::ICameraDeviceSession>* session) override {
        session->reset();
        if (!callback) return status(hc::Status::ILLEGAL_ARGUMENT);
        bool absent = false;
        auto hold = mSensor->acquire(mName, &absent);
        if (absent) {
            ALOGW("%s: the flip module faces the other way", mName.c_str());
            return status(hc::Status::CAMERA_DISCONNECTED);
        }
        if (!hold) {
            ALOGW("%s: the shared sensor is held by another camera ID", mName.c_str());
            return status(hc::Status::CAMERA_IN_USE);
        }
        if (hold->handover()) {
            auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(
                    hold->settled() - SensorOwner::Clock::now());
            if (wait.count() > 0) {
                ALOGI("%s: %s released the shared sensor, waiting %lld ms for the HAL to power "
                      "it down", mName.c_str(), hold->previous().c_str(),
                      static_cast<long long>(wait.count()));
                std::this_thread::sleep_for(wait);
            }
        }
        auto adapter = createCameraDeviceCallback(callback);
        if (mMaximumResolution.enabled()) {
            ad::CameraMetadata characteristics;
            if (getCameraCharacteristics(&characteristics).isOk()) {
                auto binning = peek<int32_t>(characteristics.metadata,
                                             ANDROID_SENSOR_INFO_BINNING_FACTOR);
                auto partial = peek<int32_t>(characteristics.metadata,
                                             ANDROID_REQUEST_PARTIAL_RESULT_COUNT);
                if (binning.size() == 2 && binning[0] == binning[1] && partial.size() == 1) {
                    getMetadataQueues(adapter)->pixelModes.configure(
                            mMaximumResolution.remosaicTag, binning[0], partial[0]);
                }
            }
        }
        Reopen reopen = [device = mBackend,
                         adapter]() -> ::android::sp<hd::V3_2::ICameraDeviceSession> {
            ::android::sp<hd::V3_2::ICameraDeviceSession> fresh;
            auto result = device->open(adapter, [&](auto code, const auto& backend) {
                if (code == hc::Status::OK) fresh = backend;
            });
            if (!result.isOk()) return {};
            return fresh;
        };
        hc::Status value = hc::Status::INTERNAL_ERROR;
        auto result = mBackend->open(adapter, [&](auto code, const auto& backend) {
            value = code;
            if (code == hc::Status::OK && backend) {
                *session = createCameraDeviceSession(backend, adapter, std::move(hold),
                                                     std::move(reopen));
            }
            if (code == hc::Status::OK && !*session) value = hc::Status::INTERNAL_ERROR;
        });
        return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
    }
    ScopedAStatus setTorchMode(bool on) override {
        if (mSensor->absent(mName)) return status(hc::Status::CAMERA_DISCONNECTED);
        auto result = mBackend->setTorchMode(on ? hc::TorchMode::ON : hc::TorchMode::OFF);
        return status(result.isOk() ? static_cast<hc::Status>(result) : hc::Status::INTERNAL_ERROR);
    }
    ScopedAStatus turnOnTorchWithStrengthLevel(int32_t) override { return unsupported(); }
    ScopedAStatus getTorchStrengthLevel(int32_t*) override { return unsupported(); }
    ScopedAStatus openInjectionSession(const std::shared_ptr<ad::ICameraDeviceCallback>&,
            std::shared_ptr<ad::ICameraInjectionSession>*) override { return unsupported(); }
    binder_status_t dump(int fd, const char**, uint32_t) override {
        native_handle_t* handle = native_handle_create(1, 0);
        if (!handle) return STATUS_NO_MEMORY;
        handle->data[0] = fd;
        auto result = mBackend->dumpState(::android::hardware::hidl_handle(handle));
        native_handle_delete(handle);
        return result.isOk() ? STATUS_OK : STATUS_FAILED_TRANSACTION;
    }
  private:
    static ScopedAStatus unsupported() { return status(hc::Status::OPERATION_NOT_SUPPORTED); }
    const ::android::sp<hd::V3_2::ICameraDevice> mBackend;
    const std::string mName;
    const std::shared_ptr<NameMap> mNames;
    const std::shared_ptr<SensorOwner> mSensor;
    const MaximumResolution mMaximumResolution;
};
ScopedAStatus createCameraDevice(const ::android::sp<hd::V3_2::ICameraDevice>& backend,
        const std::string& name, const std::shared_ptr<NameMap>& names,
        const std::shared_ptr<SensorOwner>& sensor, const MaximumResolution& maximumResolution,
        std::shared_ptr<ad::ICameraDevice>* device) {
    *device = ndk::SharedRefBase::make<CameraDevice>(backend, name, names, sensor,
                                                     maximumResolution);
    return ScopedAStatus::ok();
}
}
