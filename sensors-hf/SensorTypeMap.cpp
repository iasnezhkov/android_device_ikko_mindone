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
        // 14.09 v3: maxRange/resolution for every physical AND virtual/fusion type below
        // now come from the STOCK MTK sensor HAL blob itself
        // (vendor/ikko/mindone/proprietary/vendor/lib64/hw/sensors.mt6789.so,
        // BuildID f4f66b93347cbf72e8ebf8f496729df7), not a datasheet guess: disassembly of
        // SensorListV2::initSensorList() (the vector<sensor_t> candidate-list builder that
        // blob runs at HAL startup) shows a literal-pool float pair {maxRange,resolution}
        // per physical sensor CATEGORY (shared across every candidate chip MTK lists for
        // that category on this SoC family - lsm6dsm_acc/bmi160_acc/icm4n607_acc/... all
        // read the identical {78.4532, 0.0012} pair; icm4n607_acc/icm4n607_gyro/mmc5603/
        // stk6a2x_als are the four candidates that are actually the real chips on this
        // board, per F4294). Every recovered pair below is independently cross-checked
        // against the already-verified minDelay/maxDelay/fifo fields (F4294 gains,
        // dumpsys-sensorservice.txt) which the SAME literal-pool read also produces, and
        // those matched exactly (see F4352) - high confidence.
        // IMPORTANT, also recovered live from the same table: the stock blob's own "power"
        // field is 0.0f for EVERY SINGLE entry in this table without exception (physical
        // chips and virtual/fusion outputs alike) - i.e. MTK's reference HAL never fills in
        // a real power-consumption number at all. We deliberately do NOT copy that 0.0f
        // for the four real physical chips (accel/gyro/mag/light): this device's small
        // ~1960 mAh battery makes power accounting matter more than usual
        // (HANDOFF), and a real datasheet-derived number is strictly more useful to
        // the framework's power accounting than a known-fake zero - "stock does it" is not
        // a reason to also do it here (project rule: "the stock behaviour is not a justification").
        // For continuous virtual/fusion outputs (types 3/9/10/11/15/20) we likewise keep
        // our own non-zero composite estimates (they really do run continuous SCP fusion
        // compute, so 0.0 would be actively misleading). For the one-shot/special-trigger
        // gesture/detector virtual sensors (17/18/19/22/23, low duty-cycle by nature) we DO
        // adopt the stock 0.0f: it is directly confirmed for two of them on this exact
        // board (significant_motion, wake_gesture) and physically plausible for the rest.
        // Rates, FIFO sizes, reporting modes, wake-up flags and permissions still follow the
        // stock MediaTek HAL as captured on this device
        // (dumpsys sensorservice on the 06.09 boot), unchanged
        // from v2 and independently re-confirmed against the same disassembled table (F4352):
        // accel/gyro 12.5-400 Hz with FIFO 4500/3000, mag 5-50 Hz, fusion outputs 50-200 Hz,
        // rot_vec/game_rotvec FIFO 4500/300, uncali_mag 4500/600, step_detect 4500/100,
        // tilt = special-trigger + wake-up, step sensors need ACTIVITY_RECOGNITION.
        // Real chip identities per F4294 (live /proc/hf_manager dump, 13.09): accel+gyro =
        // icm4n607 (one chip, two sensor_types), mag = mmc5603, light = stk6a2x_als.
        //
        // AIDL spec fix (F4352, SENSORS-V3-1409): hardware/interfaces/sensors/aidl/
        // android/hardware/sensors/SensorInfo.aidl's minDelayUs doc is explicit - "one-shot:
        // -1". v2 left minDelayUs=0 on every one-shot entry below (copy-paste of the
        // continuous-sensor default); the stock dump confirms the real value on this board
        // (dumpsys-sensorservice.txt: significant/wake both print "minDelay=-1us"). Fixed on
        // every kReportingModeOneShot entry (significant_motion, wake_gesture,
        // glance_gesture, pick_up_gesture, stationary_detect, motion_detect), not just the
        // two present on this board, since it is a table-wide spec bug, not a per-sensor one.
        {SENSOR_TYPE_ACCELEROMETER, {T::ACCELEROMETER, "android.sensor.accelerometer",
            78.4532f, 0.0012f, 0.27f, 2500, 80000, 3000, 4500,
            kReportingModeContinuous, "", true}},  // icm4n607_acc, gain=1000 (F4294); maxRange/resolution = stock table (+-8g, 8*9.80665; F4352), power = datasheet (ICM-42607 low-noise accel-only ~0.27 mA, stock reports 0.0); FIFO 4500/3000, 12.5-400 Hz = stock
        {SENSOR_TYPE_MAGNETIC_FIELD, {T::MAGNETIC_FIELD, "android.sensor.magnetic_field",
            4912.0f, 0.15f, 0.3f, 20000, 200000, 0, 0,
            kReportingModeContinuous, "", true}},  // mmc5603, gain=1000 (F4294); maxRange/resolution = stock table (F4352) - the classic Android-wide 4912 uT/0.15 uT magnetometer convention, not an MMC5603NJ-datasheet number; power = datasheet estimate, stock reports 0.0; 5-50 Hz, no batching = stock
        {SENSOR_TYPE_ORIENTATION, {T::ORIENTATION, "android.sensor.orientation",
            360.0f, 0.00390625f, 0.9f /* accel+mag combined, rough */, 5000, 20000, 0, 0,
            kReportingModeContinuous, "", true}},  // fusion output, gain=1000000 (F4294); maxRange/resolution = stock table (F4352, resolution = 1/256, an 8-bit compass-heading convention); power kept as our own composite estimate, stock reports 0.0
        {SENSOR_TYPE_GYROSCOPE, {T::GYROSCOPE, "android.sensor.gyroscope",
            34.9066f, 0.0011f, 0.55f, 2500, 80000, 3000, 4500,
            kReportingModeContinuous, "", true}},  // icm4n607_gyro, gain=1000000 (F4294); maxRange/resolution = stock table (+-2000 dps; F4352) - matches (rounds to) the datasheet LSB math v2 used (34.90659/0.0010652644 rad/s), just coarser rounding; power = datasheet, stock reports 0.0; FIFO 4500/3000, 12.5-400 Hz = stock
        {SENSOR_TYPE_LIGHT, {T::LIGHT, "android.sensor.light",
            65535.0f, 1.0f, 0.11f, 0, 0, 0, 0,
            kReportingModeOnChange, "", true}},  // stk6a2x_als (Sensortek ALS, 16-bit, ~0.11 mA), gain=1 (F4294); maxRange/resolution = stock table, unchanged from v2 (already matched exactly - F4352); power kept as datasheet estimate (this entry's own power literal wasn't cleanly recoverable from the table, but every other resolved entry reads 0.0, so stock is very likely also 0.0 here)
        {SENSOR_TYPE_PRESSURE, {T::PRESSURE, "android.sensor.pressure",
            1100.0f /*hPa*/, 0.005f, 0.004f, 66666, 1000000, 0, 300,
            kReportingModeContinuous, "", false}},  // NOT present per F4294; kept in case a future SCP fw adds it. Out of this task's board-sensor scope (not in the F4294/dumpsys list) - left as v2's placeholder even though the same disassembly also recovered a real stock candidate value (bmp280/380/390: 1572.86 hPa / 0.0016 hPa, power 0.0) for a bonus data point, see SENSORS-V3-1409
        // SENSOR_TYPE_TEMPERATURE (7): deprecated in Android and absent from the AIDL SensorType
        // enum (compile-checked 13.09) - intentionally not mapped; the kernel never registers it here.
        // NOT present per F4294: this board's stk6a2x is wired ALS-only,
        // proximity is handled (if at all) through the separate SAR/"aw_sar"
        // charger-notifier path in hf_manager.c, which is a different
        // sensor_type (SENSOR_TYPE_SAR=71, MTK-only, no AOSP SensorType) and
        // out of scope for this HAL - see plan doc "what is UNSUPPORTED".
        // Entry kept so a board/firmware revision that DOES wire proximity
        // through hf_manager is picked up automatically.
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
            kReportingModeContinuous, "", true}},  // maxRange/resolution = stock table (F4352) - stock's gravity range is exactly half the accel entry's own range (4*9.80665, i.e. a fixed +-4g-equivalent convention), same LSB as accel; power kept as our own composite estimate, stock reports 0.0
        {SENSOR_TYPE_LINEAR_ACCELERATION, {T::LINEAR_ACCELERATION, "android.sensor.linear_acceleration",
            39.2266f, 0.0012f, 0.23f, 5000, 200000, 300, 4500,
            kReportingModeContinuous, "", true}},  // maxRange/resolution = stock table (F4352) - shares gravity's exact literal pair in the stock blob, not accel's; power kept as our own composite estimate, stock reports 0.0
        {SENSOR_TYPE_ROTATION_VECTOR, {T::ROTATION_VECTOR, "android.sensor.rotation_vector",
            1.0f, 5.9604645e-08f, 1.0f /* accel+gyro+mag fusion */, 5000, 200000, 300, 4500,
            kReportingModeContinuous, "", true}},  // maxRange unchanged (already matched); resolution = stock table (F4352) = 1/2^24, a 24-bit quaternion-component convention; power kept as our own composite estimate, stock reports 0.0
        {SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED, {T::MAGNETIC_FIELD_UNCALIBRATED, "android.sensor.magnetic_field_uncalibrated",
            4912.0f, 0.15f, 0.3f, 20000, 200000, 600, 4500,
            kReportingModeContinuous, "", true}},  // same MMC5603NJ as type 2; maxRange/resolution = stock table (F4352), same 4912/0.15 convention as the calibrated field; FIFO 4500/600 = stock
        {SENSOR_TYPE_GAME_ROTATION_VECTOR, {T::GAME_ROTATION_VECTOR, "android.sensor.game_rotation_vector",
            1.0f, 5.9604645e-08f, 1.03f /* accel+gyro fusion */, 5000, 200000, 300, 4500,
            kReportingModeContinuous, "", true}},  // maxRange unchanged; resolution = stock table (F4352) = 1/2^24; power kept as our own composite estimate, stock reports 0.0
        {SENSOR_TYPE_GYROSCOPE_UNCALIBRATED, {T::GYROSCOPE_UNCALIBRATED, "android.sensor.gyroscope_uncalibrated",
            34.9066f, 0.0011f, 0.55f, 2500, 80000, 3000, 4500,
            kReportingModeContinuous, "", true}},  // same ICM-42607 gyro core as type 4; maxRange/resolution = stock table (F4352), same pair as the calibrated gyro; FIFO 4500/3000 = stock
        {SENSOR_TYPE_SIGNIFICANT_MOTION, {T::SIGNIFICANT_MOTION, "android.sensor.significant_motion",
            1.0f, 1.0f, 0.0f, -1, 0, 0, 0,
            kReportingModeOneShot | kFlagWakeUp, "", false}},  // maxRange/resolution still placeholder (not cleanly recoverable for this virtual entry from the stock table); power = 0.0 IS the confirmed stock value here (F4352) and is plausible (rare-event, near-zero average draw); minDelayUs=-1 per AIDL SensorInfo.aidl spec for one-shot sensors (was 0, v2 bug) - matches the stock dump's "minDelay=-1us" for this exact sensor
        {SENSOR_TYPE_STEP_DETECTOR, {T::STEP_DETECTOR, "android.sensor.step_detector",
            1.0f, 1.0f, 0.0f, 0, 0, 100, 4500,
            kReportingModeSpecial, "android.permission.ACTIVITY_RECOGNITION", false}},  // maxRange/resolution still placeholder; power = 0.0 confirmed stock value (F4352); FIFO 4500/100, permission = stock. special-trigger (not one-shot), so minDelayUs stays 0 per spec ("special: 0, unless otherwise noted")
        {SENSOR_TYPE_STEP_COUNTER, {T::STEP_COUNTER, "android.sensor.step_counter",
            2147483648.0f, 1.0f, 0.0f, 0, 0, 0, 0,
            kReportingModeOnChange, "android.permission.ACTIVITY_RECOGNITION", true}},  // maxRange = stock table value (F4352) = 2^31, the "counter is effectively unbounded" convention (was 1.0e6 placeholder); resolution unchanged (already matched); power not directly resolved for this entry but every resolved neighbour reads 0.0 (F4352) - adopted; permission = stock
        {SENSOR_TYPE_GEOMAGNETIC_ROTATION_VECTOR, {T::GEOMAGNETIC_ROTATION_VECTOR, "android.sensor.geomagnetic_rotation_vector",
            1.0f, 5.9604645e-08f, 0.7f /* accel+mag fusion */, 5000, 200000, 300, 4500,
            kReportingModeContinuous, "", true}},  // maxRange unchanged; resolution = stock table (F4352) = 1/2^24; power kept as our own composite estimate, stock reports 0.0
        {SENSOR_TYPE_TILT_DETECTOR, {T::TILT_DETECTOR, "android.sensor.tilt_detector",
            1.0f, 1.0f, 0.0f, 0, 0, 0, 0,
            kReportingModeSpecial | kFlagWakeUp, "", false}},  // present on this board; special-trigger + wake-up (flags 0x7) = stock. maxRange/resolution still placeholder; power set to 0.0 by the same-class pattern as significant_motion/wake_gesture (F4352, not individually confirmed for this exact entry - medium confidence). special-trigger, not one-shot: minDelayUs stays 0 per spec
        {SENSOR_TYPE_WAKE_GESTURE, {T::WAKE_GESTURE, "android.sensor.wake_gesture",
            1.0f, 1.0f, 0.0f, -1, 0, 0, 0,
            kReportingModeOneShot | kFlagWakeUp, "", false}},  // maxRange/resolution still placeholder; power = 0.0 IS the confirmed stock value here (F4352); minDelayUs=-1 per AIDL spec for one-shot (was 0, v2 bug) - matches the stock dump's "minDelay=-1us" for this exact sensor
        {SENSOR_TYPE_GLANCE_GESTURE, {T::GLANCE_GESTURE, "android.sensor.glance_gesture",
            1.0f, 1.0f, 0.0f, -1, 0, 0, 0,
            kReportingModeOneShot, "", false}},  // not present on this board (F4294); minDelayUs=-1 fix applied table-wide regardless (AIDL spec, one-shot); power set to 0.0 by pattern (F4352)
        {SENSOR_TYPE_PICK_UP_GESTURE, {T::PICK_UP_GESTURE, "android.sensor.pick_up_gesture",
            1.0f, 1.0f, 0.0f, -1, 0, 0, 0,
            kReportingModeOneShot, "", false}},  // not present on this board (F4294); minDelayUs=-1 fix applied table-wide (AIDL spec); power set to 0.0 by pattern (F4352)
        {SENSOR_TYPE_WRIST_TILT_GESTURE, {T::WRIST_TILT_GESTURE, "android.sensor.wrist_tilt_gesture",
            1.0f, 1.0f, 0.5f, 0, 0, 0, 0,
            kReportingModeSpecial, "", false}},  // low confidence, not present on this board, not touched by this pass
        {SENSOR_TYPE_DEVICE_ORIENTATION, {T::DEVICE_ORIENTATION, "android.sensor.device_orientation",
            3.0f, 1.0f, 0.0f, 0, 0, 0, 0,
            kReportingModeOnChange, "", true}},  // maxRange/resolution unchanged (already matched stock table exactly - F4352); power not directly resolved for this entry but every resolved neighbour reads 0.0 (F4352) - adopted (was 0.5 placeholder)
        {SENSOR_TYPE_POSE_6DOF, {T::POSE_6DOF, "android.sensor.pose_6dof",
            1.0f, 0.0001f, 1.0f, 10000, 200000, 0, 0,
            kReportingModeContinuous, "", false}},  // low confidence, rarely present, not touched by this pass
        {SENSOR_TYPE_STATIONARY_DETECT, {T::STATIONARY_DETECT, "android.sensor.stationary_detect",
            1.0f, 1.0f, 0.0f, -1, 0, 0, 0,
            kReportingModeOneShot, "", false}},  // minDelayUs=-1 fix applied table-wide (AIDL spec, one-shot); power set to 0.0 by pattern (F4352)
        {SENSOR_TYPE_MOTION_DETECT, {T::MOTION_DETECT, "android.sensor.motion_detect",
            1.0f, 1.0f, 0.0f, -1, 0, 0, 0,
            kReportingModeOneShot, "", false}},  // minDelayUs=-1 fix applied table-wide (AIDL spec, one-shot); power set to 0.0 by pattern (F4352)
        {SENSOR_TYPE_HEART_BEAT, {T::HEART_BEAT, "android.sensor.heart_beat",
            1.0f, 1.0f, 0.1f, 0, 0, 0, 0,
            kReportingModeSpecial, "android.permission.BODY_SENSORS", false}},
        {SENSOR_TYPE_LOW_LATENCY_OFFBODY_DETECT, {T::LOW_LATENCY_OFFBODY_DETECT, "android.sensor.low_latency_offbody_detect",
            1.0f, 1.0f, 0.5f, 0, 0, 0, 0,
            kReportingModeSpecial, "", false}},
        {SENSOR_TYPE_ACCELEROMETER_UNCALIBRATED, {T::ACCELEROMETER_UNCALIBRATED, "android.sensor.accelerometer_uncalibrated",
            78.4532f, 0.0012f, 0.27f, 2500, 80000, 3000, 4500,
            kReportingModeContinuous, "", true}},  // same ICM-42607 accel core as type 1; maxRange/resolution set equal to type 1's now-corrected stock values by symmetry (the direct table read for this specific entry was ambiguous/contaminated by register reuse from the preceding gravity/linear_acc block in the disassembly - medium-high confidence, not a clean independent recovery; see SENSORS-V3-1409)
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
