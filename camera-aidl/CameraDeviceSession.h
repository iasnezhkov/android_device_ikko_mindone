/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "StreamConvert.h"
#include <aidl/android/hardware/camera/device/ICameraDeviceSession.h>
#include <android/hardware/camera/device/3.2/ICameraDeviceSession.h>
#include <android/hardware/camera/device/3.5/ICameraDeviceCallback.h>
namespace mindone::camera {
std::shared_ptr<ad::ICameraDeviceSession> createCameraDeviceSession(
        const ::android::sp<hd::V3_2::ICameraDeviceSession>& backend,
        const ::android::sp<hd::V3_5::ICameraDeviceCallback>& callback);
}
