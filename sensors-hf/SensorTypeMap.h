/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Kernel hf_manager sensor_type <-> AIDL android.hardware.sensors.SensorType,
 * plus the static per-type property table that fills in every SensorInfo
 * field the kernel ABI does not carry (maxRange, resolution, power,
 * min/maxDelay, fifo, flags - see hf_manager_uapi.h::sensor_info, which
 * only has {sensor_type, gain, name, vendor}).
 *
 * IMPORTANT: every numeric value below tagged NEEDS-VERIFICATION is a
 * placeholder, not a measured constant. hf_manager gives us no path to the
 * real chip datasheet numbers; the only way to get them right is to read
 * back sensor_info.name/vendor on-device (HF_MANAGER_REQUEST_SENSOR_INFO)
 * and either (a) look up that exact chip's datasheet, or (b) recover the
 * numbers MTK's stock blob already has compiled in by disassembling
 * /vendor/lib64/hw/sensors.mt6789.so (objdump -d / -s -j .rodata; the blob
 * cannot be patched, but it CAN be read for its constant tables) and cross
 * checking against SENSORS-HAL-PLAN-1309. Getting reportingMode
 * and flags wrong breaks correctness (e.g. auto-rotate would think a
 * continuous sensor is one-shot); getting maxRange/resolution/power wrong
 * only misinforms apps that read SensorInfo cosmetically. Ship v1 with the
 * placeholders, fix from real data before relying on power-management
 * decisions (HANDOFF: this device's small ~1960 mAh battery makes
 * sensor power accounting matter more than usual).
 *
 * What IS already confirmed live (not placeholder): the fact log F4294
 * dumped /proc/hf_manager on-device and got the real 19-sensor list with
 * real gain divisors and real chip names (icm4n607 accel+gyro, mmc5603
 * mag, stk6a2x_als light - proximity is NOT among them, see
 * SensorTypeMap.cpp). SensorTypeMap.cpp cites the exact gain value next to
 * each of those entries.
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

// Returns false for: SENSOR_TYPE_INVALID, anything >= SENSOR_TYPE_PEDOMETER
// (55) [MTK-only, no AOSP SensorType], and DYNAMIC_SENSOR_META /
// ADDITIONAL_INFO (32/33) [framework-synthesized event tags, not real
// enumerable sensors - see SENSORS-HAL-PLAN-1309.md "what is UNSUPPORTED"].
std::optional<SensorTypeInfo> lookupSensorTypeInfo(uint8_t kernelSensorType);

}  // namespace hf
}  // namespace mindone

#endif  // MINDONE_SENSOR_TYPE_MAP_H_
