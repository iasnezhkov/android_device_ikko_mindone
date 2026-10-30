/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/camera/device/ICameraDevice.h>
#include <android/hardware/camera/device/3.2/ICameraDevice.h>

#include <memory>
#include <string>

namespace mindone::camera {

class NameMap {
  public:
    virtual ~NameMap() = default;
    virtual std::string remember(const std::string& backend) = 0;
    virtual std::string lookup(const std::string& name) = 0;
};

ndk::ScopedAStatus createCameraDevice(
        const ::android::sp<::android::hardware::camera::device::V3_2::ICameraDevice>& backend,
        const std::shared_ptr<NameMap>& names,
        std::shared_ptr<::aidl::android::hardware::camera::device::ICameraDevice>* device);

}
