/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <aidl/android/hardware/camera/device/StreamConfiguration.h>
#include <android/hardware/camera/device/3.7/types.h>
#include <android/binder_auto_utils.h>

namespace mindone::camera {
namespace ad = ::aidl::android::hardware::camera::device;
namespace hd = ::android::hardware::camera::device;
ndk::ScopedAStatus convertStreams(const ad::StreamConfiguration& source,
                                 hd::V3_4::StreamConfiguration* target);
ndk::ScopedAStatus convertStreams(const ad::StreamConfiguration& source,
                                 hd::V3_7::StreamConfiguration* target);
}
