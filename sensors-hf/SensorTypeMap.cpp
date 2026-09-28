/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#include "SensorTypeMap.h"

#include <unordered_map>

namespace mindone {
namespace hf {

namespace {

using T = SensorType;

// clang-format off
const std::unordered_map<uint8_t, SensorTypeInfo>& table() {
    static const std::unordered_map<uint8_t, SensorTypeInfo>* kTable =
        new std::unordered_map<uint8_t, SensorTypeInfo>{
        {SENSOR_TYPE_ACCELEROMETER, {T::ACCELEROMETER, "android.sensor.accelerometer",
            78.4532f, 0.0012f, 0.27f, 2500, 80000, 3000, 4500,
            kReportingModeContinuous, "", true}},
        {SENSOR_TYPE_MAGNETIC_FIELD, {T::MAGNETIC_FIELD, "android.sensor.magnetic_field",
            4912.0f, 0.15f, 0.3f, 20000, 200000, 0, 0,
            kReportingModeContinuous, "", true}},
        {SENSOR_TYPE_ORIENTATION, {T::ORIENTATION, "android.sensor.orientation",
            360.0f, 0.00390625f, 0.9f /* accel+mag combined, rough */, 5000, 20000, 0, 0,
            kReportingModeContinuous, "", true}},
        {SENSOR_TYPE_GYROSCOPE, {T::GYROSCOPE, "android.sensor.gyroscope",
            34.9066f, 0.0011f, 0.55f, 2500, 80000, 3000, 4500,
            kReportingModeContinuous, "", true}},
        {SENSOR_TYPE_LIGHT, {T::LIGHT, "android.sensor.light",
            65535.0f, 1.0f, 0.11f, 0, 0, 0, 0,
            kReportingModeOnChange, "", true}},
        {SENSOR_TYPE_PRESSURE, {T::PRESSURE, "android.sensor.pressure",
            1100.0f /*hPa*/, 0.005f, 0.004f, 66666, 1000000, 0, 300,
            kReportingModeContinuous, "", false}},
        {SENSOR_TYPE_PROXIMITY, {T::PROXIMITY, "android.sensor.proximity",
            5.0f /*cm*/, 5.0f /*binary near/far*/, 0.75f, 0, 0, 0, 0,
            kReportingModeOnChange, "", false}},
        {SENSOR_TYPE_RELATIVE_HUMIDITY, {T::RELATIVE_HUMIDITY, "android.sensor.relative_humidity",
            100.0f, 0.1f, 0.02f, 100000, 0, 0, 0,
            kReportingModeOnChange, "", false}},
        {SENSOR_TYPE_AMBIENT_TEMPERATURE, {T::AMBIENT_TEMPERATURE, "android.sensor.ambient_temperature",
            80.0f, 0.1f, 0.02f, 100000, 0, 0, 0,
            kReportingModeOnChange, "", false}},
        {SENSOR_TYPE_HEART_RATE, {T::HEART_RATE, "android.sensor.heart_rate",
            250.0f, 1.0f, 0.1f, 200000, 0, 0, 0,
            kReportingModeOnChange, "android.permission.BODY_SENSORS", false}},

        // --- virtual/composite sensors: fusion outputs, no chip of their own ---
        // MINDONE-SENSOR-RATES: the stock tables cap every fusion output at maxDelay 20000 us,
        // which means 50 Hz is the SLOWEST rate they can be asked for -- an app that wants ten
        // samples a second is clamped up to fifty. Measured consequence on this device: a
        // background service asked for linear acceleration and got 20 ms, the floor, and held it
        // with the screen off for hours. The stock HAL advertises the same cap, but "stock does
        // it too" is not a reason: nothing in a software fusion running on the SCP needs a 50 Hz
        // floor. The cap becomes 200000 us, which is not an invented number -- it is what this
        // same SCP already advertises for uncali_mag.
        // Gravity, linear acceleration and the geomagnetic rotation vector also declared no FIFO,
        // while rotation_vector and game_rotation_vector -- the same fusion pipeline, the same
        // chip -- declare 4500/300. Without a FIFO the framework never asks for batching, so every
        // sample must be delivered as it is produced. They now declare the same FIFO as their
        // siblings; Sensors::batch() already passes maxReportLatencyNs to hf_manager and
        // Sensors::flush() already drives hf_manager's flush, so the claim is backed by code that
        // works for the sensors that always had it.
        {SENSOR_TYPE_GRAVITY, {T::GRAVITY, "android.sensor.gravity",
            39.2266f, 0.0012f, 0.23f /* rides on accel */, 5000, 200000, 300, 4500,
            kReportingModeContinuous, "", true}},
        {SENSOR_TYPE_LINEAR_ACCELERATION, {T::LINEAR_ACCELERATION, "android.sensor.linear_acceleration",
            39.2266f, 0.0012f, 0.23f, 5000, 200000, 300, 4500,
            kReportingModeContinuous, "", true}},
        {SENSOR_TYPE_ROTATION_VECTOR, {T::ROTATION_VECTOR, "android.sensor.rotation_vector",
            1.0f, 5.9604645e-08f, 1.0f /* accel+gyro+mag fusion */, 5000, 200000, 300, 4500,
            kReportingModeContinuous, "", true}},
        {SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED, {T::MAGNETIC_FIELD_UNCALIBRATED, "android.sensor.magnetic_field_uncalibrated",
            4912.0f, 0.15f, 0.3f, 20000, 200000, 600, 4500,
            kReportingModeContinuous, "", true}},
        {SENSOR_TYPE_GAME_ROTATION_VECTOR, {T::GAME_ROTATION_VECTOR, "android.sensor.game_rotation_vector",
            1.0f, 5.9604645e-08f, 1.03f /* accel+gyro fusion */, 5000, 200000, 300, 4500,
            kReportingModeContinuous, "", true}},
        {SENSOR_TYPE_GYROSCOPE_UNCALIBRATED, {T::GYROSCOPE_UNCALIBRATED, "android.sensor.gyroscope_uncalibrated",
            34.9066f, 0.0011f, 0.55f, 2500, 80000, 3000, 4500,
            kReportingModeContinuous, "", true}},
        {SENSOR_TYPE_SIGNIFICANT_MOTION, {T::SIGNIFICANT_MOTION, "android.sensor.significant_motion",
            1.0f, 1.0f, 0.0f, -1, 0, 0, 0,
            kReportingModeOneShot | kFlagWakeUp, "", false}},
        {SENSOR_TYPE_STEP_DETECTOR, {T::STEP_DETECTOR, "android.sensor.step_detector",
            1.0f, 1.0f, 0.0f, 0, 0, 100, 4500,
            kReportingModeSpecial, "android.permission.ACTIVITY_RECOGNITION", false}},
        {SENSOR_TYPE_STEP_COUNTER, {T::STEP_COUNTER, "android.sensor.step_counter",
            2147483648.0f, 1.0f, 0.0f, 0, 0, 0, 0,
            kReportingModeOnChange, "android.permission.ACTIVITY_RECOGNITION", true}},
        {SENSOR_TYPE_GEOMAGNETIC_ROTATION_VECTOR, {T::GEOMAGNETIC_ROTATION_VECTOR, "android.sensor.geomagnetic_rotation_vector",
            1.0f, 5.9604645e-08f, 0.7f /* accel+mag fusion */, 5000, 200000, 300, 4500,
            kReportingModeContinuous, "", true}},
        {SENSOR_TYPE_TILT_DETECTOR, {T::TILT_DETECTOR, "android.sensor.tilt_detector",
            1.0f, 1.0f, 0.0f, 0, 0, 0, 0,
            kReportingModeSpecial | kFlagWakeUp, "", false}},
        {SENSOR_TYPE_WAKE_GESTURE, {T::WAKE_GESTURE, "android.sensor.wake_gesture",
            1.0f, 1.0f, 0.0f, -1, 0, 0, 0,
            kReportingModeOneShot | kFlagWakeUp, "", false}},
        {SENSOR_TYPE_GLANCE_GESTURE, {T::GLANCE_GESTURE, "android.sensor.glance_gesture",
            1.0f, 1.0f, 0.0f, -1, 0, 0, 0,
            kReportingModeOneShot, "", false}},
        {SENSOR_TYPE_PICK_UP_GESTURE, {T::PICK_UP_GESTURE, "android.sensor.pick_up_gesture",
            1.0f, 1.0f, 0.0f, -1, 0, 0, 0,
            kReportingModeOneShot, "", false}},
        {SENSOR_TYPE_WRIST_TILT_GESTURE, {T::WRIST_TILT_GESTURE, "android.sensor.wrist_tilt_gesture",
            1.0f, 1.0f, 0.5f, 0, 0, 0, 0,
            kReportingModeSpecial, "", false}},  // low confidence, not present on this board, not touched by this pass
        {SENSOR_TYPE_DEVICE_ORIENTATION, {T::DEVICE_ORIENTATION, "android.sensor.device_orientation",
            3.0f, 1.0f, 0.0f, 0, 0, 0, 0,
            kReportingModeOnChange, "", true}},
        {SENSOR_TYPE_POSE_6DOF, {T::POSE_6DOF, "android.sensor.pose_6dof",
            1.0f, 0.0001f, 1.0f, 10000, 200000, 0, 0,
            kReportingModeContinuous, "", false}},  // low confidence, rarely present, not touched by this pass
        {SENSOR_TYPE_STATIONARY_DETECT, {T::STATIONARY_DETECT, "android.sensor.stationary_detect",
            1.0f, 1.0f, 0.0f, -1, 0, 0, 0,
            kReportingModeOneShot, "", false}},
        {SENSOR_TYPE_MOTION_DETECT, {T::MOTION_DETECT, "android.sensor.motion_detect",
            1.0f, 1.0f, 0.0f, -1, 0, 0, 0,
            kReportingModeOneShot, "", false}},
        {SENSOR_TYPE_HEART_BEAT, {T::HEART_BEAT, "android.sensor.heart_beat",
            1.0f, 1.0f, 0.1f, 0, 0, 0, 0,
            kReportingModeSpecial, "android.permission.BODY_SENSORS", false}},
        {SENSOR_TYPE_LOW_LATENCY_OFFBODY_DETECT, {T::LOW_LATENCY_OFFBODY_DETECT, "android.sensor.low_latency_offbody_detect",
            1.0f, 1.0f, 0.5f, 0, 0, 0, 0,
            kReportingModeSpecial, "", false}},
        {SENSOR_TYPE_ACCELEROMETER_UNCALIBRATED, {T::ACCELEROMETER_UNCALIBRATED, "android.sensor.accelerometer_uncalibrated",
            78.4532f, 0.0012f, 0.27f, 2500, 80000, 3000, 4500,
            kReportingModeContinuous, "", true}},
    };
    return *kTable;
}
// clang-format on

}  // namespace

std::optional<SensorTypeInfo> lookupSensorTypeInfo(uint8_t kernelSensorType) {
    // Not real enumerable sensors: framework-synthesized event tags.
    if (kernelSensorType == SENSOR_TYPE_INVALID ||
        kernelSensorType == SENSOR_TYPE_DYNAMIC_SENSOR_META ||
        kernelSensorType == SENSOR_TYPE_ADDITIONAL_INFO ||
        kernelSensorType >= SENSOR_TYPE_PEDOMETER /* MTK-only range, 55+ */) {
        return std::nullopt;
    }
    const auto& t = table();
    auto it = t.find(kernelSensorType);
    if (it == t.end()) return std::nullopt;
    return it->second;
}

}  // namespace hf
}  // namespace mindone
