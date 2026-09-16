/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace copy of the mind_one kernel's hf_manager ioctl/event ABI.
 *
 * Verbatim field-for-field mirror of:
 *   the kernel tree's mindone/modules/hf_manager/hf_sensor_io.h
 *   the kernel tree's mindone/modules/hf_manager/hf_sensor_type.h
 * (mind_one out-of-tree kernel module, MT6789/Helio G99, ACK 6.12 base.)
 *
 * DO NOT hand-edit struct layouts here without re-checking the kernel
 * headers above line-for-line: hf_manager_ioctl()/hf_manager_write() size
 * every copy_from_user()/copy_to_user() with sizeof(these structs) on the
 * kernel side (see hf_manager.c:1565-1582, 1486-1490), so any packing or
 * field-order drift between this file and the kernel silently corrupts
 * the ioctl payload instead of failing loudly.
 *
 * The kernel headers use the kernel's __packed / __aligned() macros
 * (linux/compiler_types.h), which are not visible to a plain userspace
 * NDK/bionic translation unit. They are re-defined below with the GCC/Clang
 * attribute spelling so the layouts stay byte-identical.
 */

#ifndef MINDONE_HF_MANAGER_UAPI_H_
#define MINDONE_HF_MANAGER_UAPI_H_

#include <cstdint>
#include <linux/ioctl.h>

#ifndef __packed
#define __packed __attribute__((packed))
#endif
#ifndef __aligned
#define __aligned(x) __attribute__((aligned(x)))
#endif

namespace mindone {
namespace hf {

// ---------------------------------------------------------------------
// hf_sensor_type.h — kernel sensor_type numeric space.
//
// IDs 1..34 are declared by the kernel as "follow google default sensor
// type" and, verified by inspection, match the AOSP SensorType.aidl
// integer values 1:1 (both ultimately trace back to the same historical
// Android sensors.h numbering). IDs 55+ are MTK/vendor additions
// (pedometer, in-pocket, gesture, SAR, OIS, flicker, PPG/EKG, ...) with
// no 1:1 AOSP SensorType — see SensorTypeMap.h for which of those, if
// any, this HAL surfaces.
// ---------------------------------------------------------------------
enum : uint8_t {
    SENSOR_TYPE_INVALID = 0,
    SENSOR_TYPE_ACCELEROMETER = 1,
    SENSOR_TYPE_MAGNETIC_FIELD,
    SENSOR_TYPE_ORIENTATION,
    SENSOR_TYPE_GYROSCOPE,
    SENSOR_TYPE_LIGHT,
    SENSOR_TYPE_PRESSURE,
    SENSOR_TYPE_TEMPERATURE,
    SENSOR_TYPE_PROXIMITY,
    SENSOR_TYPE_GRAVITY,
    SENSOR_TYPE_LINEAR_ACCELERATION,
    SENSOR_TYPE_ROTATION_VECTOR,
    SENSOR_TYPE_RELATIVE_HUMIDITY,
    SENSOR_TYPE_AMBIENT_TEMPERATURE,
    SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED,
    SENSOR_TYPE_GAME_ROTATION_VECTOR,
    SENSOR_TYPE_GYROSCOPE_UNCALIBRATED,
    SENSOR_TYPE_SIGNIFICANT_MOTION,
    SENSOR_TYPE_STEP_DETECTOR,
    SENSOR_TYPE_STEP_COUNTER,
    SENSOR_TYPE_GEOMAGNETIC_ROTATION_VECTOR,
    SENSOR_TYPE_HEART_RATE,
    SENSOR_TYPE_TILT_DETECTOR,
    SENSOR_TYPE_WAKE_GESTURE,
    SENSOR_TYPE_GLANCE_GESTURE,
    SENSOR_TYPE_PICK_UP_GESTURE,
    SENSOR_TYPE_WRIST_TILT_GESTURE,
    SENSOR_TYPE_DEVICE_ORIENTATION,
    SENSOR_TYPE_POSE_6DOF,
    SENSOR_TYPE_STATIONARY_DETECT,
    SENSOR_TYPE_MOTION_DETECT,
    SENSOR_TYPE_HEART_BEAT,
    SENSOR_TYPE_DYNAMIC_SENSOR_META,
    SENSOR_TYPE_ADDITIONAL_INFO,
    SENSOR_TYPE_LOW_LATENCY_OFFBODY_DETECT,
    SENSOR_TYPE_ACCELEROMETER_UNCALIBRATED,

    /* MTK vendor additions - no AOSP SensorType equivalent */
    SENSOR_TYPE_PEDOMETER = 55,
    SENSOR_TYPE_IN_POCKET,
    SENSOR_TYPE_ACTIVITY,
    SENSOR_TYPE_PDR,
    SENSOR_TYPE_FREEFALL,
    SENSOR_TYPE_FLAT,
    SENSOR_TYPE_FACE_DOWN,
    SENSOR_TYPE_SHAKE,
    SENSOR_TYPE_BRINGTOSEE,
    SENSOR_TYPE_ANSWER_CALL,
    SENSOR_TYPE_GEOFENCE,
    SENSOR_TYPE_FLOOR_COUNTER,
    SENSOR_TYPE_EKG,
    SENSOR_TYPE_PPG1,
    SENSOR_TYPE_PPG2,
    SENSOR_TYPE_RGBW,
    SENSOR_TYPE_GYRO_TEMPERATURE,
    SENSOR_TYPE_SAR,
    SENSOR_TYPE_OIS,
    SENSOR_TYPE_FLICKER,
    SENSOR_TYPE_GYRO_SECONDARY,
    SENSOR_TYPE_FLICKER_REAR,
    SENSOR_TYPE_RGBW_REAR,
    SENSOR_TYPE_SENSOR_MAX,
};

enum : uint8_t {
    SENSOR_ACCURANCY_UNRELIALE = 0,
    SENSOR_ACCURANCY_LOW,
    SENSOR_ACCURANCY_MEDIUM,
    SENSOR_ACCURANCY_HIGH,
};

// ---------------------------------------------------------------------
// hf_sensor_io.h — ioctl/event/list structs and command codes.
// ---------------------------------------------------------------------

// hf_manager_cmd.action nibble (write()-only path; NOT an ioctl number).
enum : uint8_t {
    HF_MANAGER_SENSOR_DISABLE = 0,
    HF_MANAGER_SENSOR_ENABLE,
    HF_MANAGER_SENSOR_FLUSH,
    HF_MANAGER_SENSOR_ENABLE_CALI,
    HF_MANAGER_SENSOR_CONFIG_CALI,
    HF_MANAGER_SENSOR_SELFTEST,
    HF_MANAGER_SENSOR_RAWDATA,
    HF_MANAGER_SENSOR_MAX_ACTION,
};

// hf_manager_event.action (read() event stream).
enum : uint8_t {
    DATA_ACTION = 0,
    FLUSH_ACTION,
    BIAS_ACTION,
    CALI_ACTION,
    TEMP_ACTION,
    TEST_ACTION,
    RAW_ACTION,
    MAX_ACTION,
};

// Payload of hf_manager_cmd.data for action == ENABLE (hf_manager.c:656,
// hf_manager_save_update_enable()). Units: nanoseconds.
struct hf_manager_batch {
    int64_t delay;
    int64_t latency;
} __packed __aligned(4);

// write(fd, &cmd, sizeof(cmd)) payload (hf_manager.c:1478 hf_manager_write()).
struct hf_manager_cmd {
    uint8_t sensor_type;
    uint8_t action : 4;
    uint8_t down_sample : 1;
    uint8_t length;
    uint8_t padding[1];
    int8_t data[48] __aligned(4);
} __packed __aligned(4);

// read(fd, buf, N*sizeof(event)) record (hf_manager.c:1450 hf_manager_read()).
struct hf_manager_event {
    int64_t timestamp;
    uint8_t sensor_type;
    uint8_t accurancy;
    uint8_t action;
    uint8_t padding[1];
    union {
        int32_t word[16];
        int8_t byte[0];
    };
} __packed __aligned(4);

// HF_MANAGER_REQUEST_SENSOR_INFO payload. NOTE: this is the *entire*
// per-sensor description the kernel exposes - there is no maxRange/
// resolution/power/minDelay/maxDelay/fifo field anywhere in the ABI.
// See SensorTypeMap.h and SENSORS-HAL-PLAN-1309.md ("static property
// table") for how the HAL fills in the AIDL SensorInfo fields this
// struct does not carry.
struct sensor_info {
    uint8_t sensor_type;
    uint8_t padding[3];
    uint32_t gain;
    char name[16];
    char vendor[16];
} __packed __aligned(4);

struct custom_cmd {
    uint8_t command;
    uint8_t tx_len;
    uint8_t rx_len;
    uint8_t padding[1];
    union {
        int32_t data[15];
        int32_t word[15];
        int8_t byte[1];  // kernel: int8_t byte[]; flexible array not legal in a union in C++, sized here for safety.
    };
} __packed __aligned(4);

// Generic ioctl envelope: {sensor_type, payload-in-byte[64]}.
struct ioctl_packet {
    uint8_t sensor_type;
    uint8_t padding[3];
    union {
        bool status;
        int8_t byte[64];
    };
} __packed __aligned(4);

#define HF_MANAGER_REQUEST_REGISTER_STATUS  _IOWR('a', 1, struct ioctl_packet)
#define HF_MANAGER_REQUEST_BIAS_DATA        _IOW('a', 2, struct ioctl_packet)
#define HF_MANAGER_REQUEST_CALI_DATA        _IOW('a', 3, struct ioctl_packet)
#define HF_MANAGER_REQUEST_TEMP_DATA        _IOW('a', 4, struct ioctl_packet)
#define HF_MANAGER_REQUEST_TEST_DATA        _IOW('a', 5, struct ioctl_packet)
#define HF_MANAGER_REQUEST_SENSOR_INFO      _IOWR('a', 6, struct ioctl_packet)
#define HF_MANAGER_REQUEST_CUST_DATA        _IOWR('a', 7, struct ioctl_packet)
#define HF_MANAGER_REQUEST_READY_STATUS     _IOWR('a', 8, struct ioctl_packet)

}  // namespace hf
}  // namespace mindone

#endif  // MINDONE_HF_MANAGER_UAPI_H_
