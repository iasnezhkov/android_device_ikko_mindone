/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "MaximumResolution.h"
#include "SensorOwner.h"

#include <aidl/android/hardware/camera/device/ICameraDevice.h>
#include <android/hardware/camera/device/3.2/ICameraDevice.h>

#include <memory>
#include <string>
#include <vector>

namespace mindone::camera {

class NameMap {
  public:
    virtual ~NameMap() = default;
    virtual std::string remember(const std::string& backend) = 0;
    virtual std::string lookup(const std::string& name) = 0;
    virtual std::vector<std::string> names() = 0;
};

ndk::ScopedAStatus createCameraDevice(
        const ::android::sp<::android::hardware::camera::device::V3_2::ICameraDevice>& backend,
        const std::string& name, const std::shared_ptr<NameMap>& names,
        const std::shared_ptr<SensorOwner>& sensor, const MaximumResolution& maximumResolution,
        std::shared_ptr<::aidl::android::hardware::camera::device::ICameraDevice>* device);

}
