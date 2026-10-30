/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MINDONE_SENSOR_TYPE_MAP_H_
#define MINDONE_SENSOR_TYPE_MAP_H_

#include <cstdint>
#include <optional>
#include <string>

#include <aidl/android/hardware/sensors/SensorInfo.h>
#include <aidl/android/hardware/sensors/SensorType.h>

#include "hf_manager_uapi.h"

namespace mindone {
namespace hf {

using ::aidl::android::hardware::sensors::SensorInfo;
using ::aidl::android::hardware::sensors::SensorType;

constexpr int32_t kFlagWakeUp = 0x1;
constexpr int32_t kReportingModeContinuous = 0x0;
constexpr int32_t kReportingModeOnChange = 0x2;
constexpr int32_t kReportingModeOneShot = 0x4;
constexpr int32_t kReportingModeSpecial = 0x6;

struct SensorTypeInfo {
    SensorType aidlType;
    const char* typeAsString;
    float maxRange;
    float resolution;
    float power;
    int32_t minDelayUs;
    int32_t maxDelayUs;
    int32_t fifoReservedEventCount;
    int32_t fifoMaxEventCount;
    int32_t flags;
    const char* requiredPermission;
    bool verified;
};

std::optional<SensorTypeInfo> lookupSensorTypeInfo(uint8_t kernelSensorType);

}
}

#endif
