/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/camera/device/StreamConfiguration.h>

#include <cstdint>
#include <vector>

namespace mindone::camera {

constexpr int32_t kNormalMaxFps = 30;
constexpr int32_t kHighFps = 60;
constexpr int64_t kHighFpsFrameDuration = 16666666;

bool highFpsEnabled();
bool highFpsOutput(int64_t format, int64_t width, int64_t height);
bool highFpsStreams(const ::aidl::android::hardware::camera::device::StreamConfiguration& config);
bool highFpsRequested(const std::vector<uint8_t>& sessionParams);
bool requestHighFps(std::vector<uint8_t>* sessionParams);

}
