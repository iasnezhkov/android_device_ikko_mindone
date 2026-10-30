/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CameraDevice.h"
#include "CameraDeviceCallback.h"
#include "CameraDeviceSession.h"
#include <aidl/android/hardware/camera/device/BnCameraDevice.h>
#include <android/hardware/camera/device/3.5/ICameraDevice.h>
#include <android/hardware/camera/device/3.7/ICameraDevice.h>
#include <cutils/native_handle.h>

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
    CameraDevice(const ::android::sp<hd::V3_2::ICameraDevice>& backend,
                 const std::shared_ptr<NameMap>& names) : mBackend(backend), mNames(names) {}
    ScopedAStatus getCameraCharacteristics(ad::CameraMetadata* metadata) override {
        hc::Status value = hc::Status::INTERNAL_ERROR;
        auto result = mBackend->getCameraCharacteristics([&](auto code, const auto& bytes) {
            value = code;
            if (code == hc::Status::OK) metadata->metadata.assign(bytes.begin(), bytes.end());
        });
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
        return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
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
        auto conversion = convertStreams(streams, &converted);
        if (!conversion.isOk()) return conversion;
        auto result = backend35->isStreamCombinationSupported(converted, receive);
        return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
    }
    ScopedAStatus open(const std::shared_ptr<ad::ICameraDeviceCallback>& callback,
                      std::shared_ptr<ad::ICameraDeviceSession>* session) override {
        session->reset();
        if (!callback) return status(hc::Status::ILLEGAL_ARGUMENT);
        auto adapter = createCameraDeviceCallback(callback);
        hc::Status value = hc::Status::INTERNAL_ERROR;
        auto result = mBackend->open(adapter, [&](auto code, const auto& backend) {
            value = code;
            if (code == hc::Status::OK && backend) {
                *session = createCameraDeviceSession(backend, adapter);
            }
            if (code == hc::Status::OK && !*session) value = hc::Status::INTERNAL_ERROR;
        });
        return status(result.isOk() ? value : hc::Status::INTERNAL_ERROR);
    }
    ScopedAStatus setTorchMode(bool on) override {
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
    const std::shared_ptr<NameMap> mNames;
};
ScopedAStatus createCameraDevice(const ::android::sp<hd::V3_2::ICameraDevice>& backend,
        const std::shared_ptr<NameMap>& names, std::shared_ptr<ad::ICameraDevice>* device) {
    *device = ndk::SharedRefBase::make<CameraDevice>(backend, names);
    return ScopedAStatus::ok();
}
}
