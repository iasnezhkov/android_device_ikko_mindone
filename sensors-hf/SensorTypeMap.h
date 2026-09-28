/*
 * SPDX-FileCopyrightText: The LineageOS Project
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

// SensorInfo.flags bit layout (android.hardware.sensors SensorInfo.aidl /
// historical hardware/interfaces sensors HIDL 1.0 types.hal - unchanged
// across every sensors HAL generation to date):
//   bits 0-1: reporting mode (0=continuous 2=on-change 4=one-shot
//             6=special-reporting; note this is NOT contiguous 0..3, it's
//             a 2-bit field with values {0,2,4,6}? -- actually AOSP encodes
//             it as bits [1:0] of the CONSTANT, i.e. the raw enum values
//             ARE the flag bits already; OR the constant straight in.)
//   bit 0 (0x1): WAKE_UP_SENSOR
//   bits [2:1] (0x6): REPORTING_MODE_MASK, values CONTINUOUS=0,
//                     ON_CHANGE=2, ONE_SHOT=4, SPECIAL_REPORTING=6
//   bit 4 (0x10): DATA_INJECTION supported
//   bits [7:5] (0xE0): dynamic-sensor / additional-info mask (unused here)
//   bits [10:8] (0x700): mask direct report (unused, direct channel = v2)
constexpr int32_t kFlagWakeUp = 0x1;
constexpr int32_t kReportingModeContinuous = 0x0;
constexpr int32_t kReportingModeOnChange = 0x2;
constexpr int32_t kReportingModeOneShot = 0x4;
constexpr int32_t kReportingModeSpecial = 0x6;

struct SensorTypeInfo {
    SensorType aidlType;
    const char* typeAsString;  // SensorType.aidl string constants (android.sensor.*)
    float maxRange;
    float resolution;
    float power;           // mA
    int32_t minDelayUs;
    int32_t maxDelayUs;
    int32_t fifoReservedEventCount;
    int32_t fifoMaxEventCount;
    int32_t flags;
    const char* requiredPermission;  // "" if none
    bool verified;  // false = placeholder numbers, see file header
};

std::optional<SensorTypeInfo> lookupSensorTypeInfo(uint8_t kernelSensorType);

}  // namespace hf
}  // namespace mindone

#endif  // MINDONE_SENSOR_TYPE_MAP_H_
