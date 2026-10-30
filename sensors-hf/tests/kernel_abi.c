/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define __packed __attribute__((packed))
#define __aligned(value) __attribute__((aligned(value)))
#define __user

#include "hf_sensor_io.h"
#include "hf_sensor_type.h"

_Static_assert(sizeof(void*) == 8, "64-bit target");
_Static_assert(sizeof(struct hf_manager_event) == 76, "event wire length");
_Static_assert(_Alignof(struct hf_manager_event) == 4, "event wire alignment");
_Static_assert(offsetof(struct hf_manager_event, sensor_type) == 8, "event type offset");
_Static_assert(offsetof(struct hf_manager_event, action) == 10, "event action offset");
_Static_assert(offsetof(struct hf_manager_event, word) == 12, "event payload offset");
_Static_assert(sizeof(struct hf_manager_cmd) == 52, "command wire length");
_Static_assert(_Alignof(struct hf_manager_cmd) == 4, "command wire alignment");
_Static_assert(offsetof(struct hf_manager_cmd, data) == 4, "command payload offset");
_Static_assert(sizeof(struct sensor_info) == 40, "sensor info wire length");
_Static_assert(sizeof(struct ioctl_packet) == 68, "ioctl envelope wire length");
_Static_assert(SENSOR_TYPE_SENSOR_MAX == 78, "sensor type upper bound");
_Static_assert(HF_MANAGER_SENSOR_ENABLE == 1, "enable command value");
_Static_assert(HF_MANAGER_SENSOR_FLUSH == 2, "flush command value");
_Static_assert(HF_MANAGER_SENSOR_CONFIG_CALI == 4, "calibration command value");
_Static_assert(RAW_ACTION == 6 && MAX_ACTION == 7, "event action bounds");
_Static_assert(HF_MANAGER_REQUEST_SENSOR_INFO == 0xc0446106U, "sensor info ioctl encoding");
_Static_assert(HF_MANAGER_REQUEST_READY_STATUS == 0xc0446108U, "ready ioctl encoding");

int main(void) {
    puts("PASS exact kernel UAPI sizes, alignments, offsets, action/type bounds and ioctl encoding");
}
