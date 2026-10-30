/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef MINDONE_HF_MANAGER_UAPI_H_
#define MINDONE_HF_MANAGER_UAPI_H_

#include <cstddef>
#include <cstdint>
#include <linux/ioctl.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "hf_manager requires a little-endian target"
#endif

namespace mindone {
namespace hf {

enum : uint8_t {
    SENSOR_TYPE_INVALID = 0,
    SENSOR_TYPE_ACCELEROMETER = 1,
    SENSOR_TYPE_MAGNETIC_FIELD = 2,
    SENSOR_TYPE_ORIENTATION = 3,
    SENSOR_TYPE_GYROSCOPE = 4,
    SENSOR_TYPE_LIGHT = 5,
    SENSOR_TYPE_PRESSURE = 6,
    SENSOR_TYPE_PROXIMITY = 8,
    SENSOR_TYPE_GRAVITY = 9,
    SENSOR_TYPE_LINEAR_ACCELERATION = 10,
    SENSOR_TYPE_ROTATION_VECTOR = 11,
    SENSOR_TYPE_RELATIVE_HUMIDITY = 12,
    SENSOR_TYPE_AMBIENT_TEMPERATURE = 13,
    SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED = 14,
    SENSOR_TYPE_GAME_ROTATION_VECTOR = 15,
    SENSOR_TYPE_GYROSCOPE_UNCALIBRATED = 16,
    SENSOR_TYPE_SIGNIFICANT_MOTION = 17,
    SENSOR_TYPE_STEP_DETECTOR = 18,
    SENSOR_TYPE_STEP_COUNTER = 19,
    SENSOR_TYPE_GEOMAGNETIC_ROTATION_VECTOR = 20,
    SENSOR_TYPE_HEART_RATE = 21,
    SENSOR_TYPE_TILT_DETECTOR = 22,
    SENSOR_TYPE_WAKE_GESTURE = 23,
    SENSOR_TYPE_GLANCE_GESTURE = 24,
    SENSOR_TYPE_PICK_UP_GESTURE = 25,
    SENSOR_TYPE_WRIST_TILT_GESTURE = 26,
    SENSOR_TYPE_DEVICE_ORIENTATION = 27,
    SENSOR_TYPE_POSE_6DOF = 28,
    SENSOR_TYPE_STATIONARY_DETECT = 29,
    SENSOR_TYPE_MOTION_DETECT = 30,
    SENSOR_TYPE_HEART_BEAT = 31,
    SENSOR_TYPE_DYNAMIC_SENSOR_META = 32,
    SENSOR_TYPE_ADDITIONAL_INFO = 33,
    SENSOR_TYPE_LOW_LATENCY_OFFBODY_DETECT = 34,
    SENSOR_TYPE_ACCELEROMETER_UNCALIBRATED = 35,
};
constexpr uint8_t SENSOR_TYPE_PEDOMETER = 55;
constexpr uint8_t SENSOR_TYPE_SENSOR_MAX = 78;

constexpr uint8_t HF_MANAGER_SENSOR_DISABLE = 0;
constexpr uint8_t HF_MANAGER_SENSOR_ENABLE = 1;
constexpr uint8_t HF_MANAGER_SENSOR_FLUSH = 2;
constexpr uint8_t HF_MANAGER_SENSOR_CONFIG_CALI = 4;

constexpr uint8_t DATA_ACTION = 0;
constexpr uint8_t FLUSH_ACTION = 1;
constexpr uint8_t BIAS_ACTION = 2;
constexpr uint8_t CALI_ACTION = 3;
constexpr uint8_t TEMP_ACTION = 4;
constexpr uint8_t MAX_ACTION = 7;

struct __attribute__((packed, aligned(4))) hf_manager_batch {
    int64_t delay, latency;
};

struct alignas(4) hf_manager_cmd {
    uint8_t sensor_type;
    uint8_t action : 4, down_sample : 1;
    uint8_t length;
    uint8_t reserved;
    int8_t data[48];
};

struct __attribute__((packed, aligned(4))) hf_manager_event {
    int64_t timestamp;
    uint8_t sensor_type, accurancy, action, padding[1];
    int32_t word[16];
};

struct alignas(4) sensor_info {
    uint8_t sensor_type;
    uint8_t padding[3];
    uint32_t gain;
    char name[16], vendor[16];
};

struct alignas(4) ioctl_packet {
    uint8_t sensor_type;
    uint8_t reserved[3];
    union {
        uint8_t status;
        int8_t byte[64];
    };
};

constexpr unsigned int HF_MANAGER_REQUEST_REGISTER_STATUS =
        _IOC(_IOC_READ | _IOC_WRITE, 'a', 1, 68);
constexpr unsigned int HF_MANAGER_REQUEST_BIAS_DATA = _IOC(_IOC_WRITE, 'a', 2, 68);
constexpr unsigned int HF_MANAGER_REQUEST_CALI_DATA = _IOC(_IOC_WRITE, 'a', 3, 68);
constexpr unsigned int HF_MANAGER_REQUEST_TEMP_DATA = _IOC(_IOC_WRITE, 'a', 4, 68);
constexpr unsigned int HF_MANAGER_REQUEST_SENSOR_INFO =
        _IOC(_IOC_READ | _IOC_WRITE, 'a', 6, 68);
constexpr unsigned int HF_MANAGER_REQUEST_READY_STATUS =
        _IOC(_IOC_READ | _IOC_WRITE, 'a', 8, 68);

static_assert(sizeof(hf_manager_batch) == 16 && alignof(hf_manager_batch) == 4);
static_assert(sizeof(hf_manager_cmd) == 52 && offsetof(hf_manager_cmd, data) == 4);
static_assert(sizeof(hf_manager_event) == 76 && alignof(hf_manager_event) == 4);
static_assert(offsetof(hf_manager_event, word) == 12);
static_assert(sizeof(sensor_info) == 40 && offsetof(sensor_info, gain) == 4);
static_assert(sizeof(ioctl_packet) == 68 && offsetof(ioctl_packet, byte) == 4);

}
}
#endif
