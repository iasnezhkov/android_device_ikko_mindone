/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "CameraDevice.h"
#include "StreamConvert.h"
#include <aidl/android/hardware/camera/device/ICameraDeviceSession.h>
#include <android/hardware/camera/device/3.2/ICameraDeviceSession.h>
#include <android/hardware/camera/device/3.5/ICameraDeviceCallback.h>
#include <functional>
namespace mindone::camera {
using Reopen = std::function<::android::sp<hd::V3_2::ICameraDeviceSession>()>;
std::shared_ptr<ad::ICameraDeviceSession> createCameraDeviceSession(
        const ::android::sp<hd::V3_2::ICameraDeviceSession>& backend,
        const ::android::sp<hd::V3_5::ICameraDeviceCallback>& callback,
        std::unique_ptr<SensorOwner::Hold> hold, Reopen reopen);
}
