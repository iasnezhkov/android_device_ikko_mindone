/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

typedef uint32_t audio_devices_t;
typedef int32_t audio_mode_t;
typedef int32_t audio_io_handle_t;
typedef int32_t audio_patch_handle_t;
typedef uint32_t audio_output_flags_t;
typedef uint32_t audio_input_flags_t;
typedef uint32_t audio_format_t;
typedef uint32_t audio_channel_mask_t;

enum {
    AUDIO_MODE_NORMAL = 0,
    AUDIO_MODE_RINGTONE = 1,
    AUDIO_MODE_IN_CALL = 2,
    AUDIO_MODE_IN_COMMUNICATION = 3,
};

enum {
    AUDIO_DEVICE_NONE = 0,
    AUDIO_DEVICE_OUT_EARPIECE = 0x1,
    AUDIO_DEVICE_OUT_SPEAKER = 0x2,
    AUDIO_DEVICE_OUT_WIRED_HEADSET = 0x4,
    AUDIO_DEVICE_OUT_WIRED_HEADPHONE = 0x8,
    AUDIO_DEVICE_OUT_BLUETOOTH_SCO = 0x10,
    AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET = 0x20,
    AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT = 0x40,
    AUDIO_DEVICE_OUT_TELEPHONY_TX = 0x10000,
    AUDIO_DEVICE_BIT_IN = 0x80000000u,
    AUDIO_DEVICE_IN_BUILTIN_MIC = AUDIO_DEVICE_BIT_IN | 0x4u,
    AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET = AUDIO_DEVICE_BIT_IN | 0x8u,
    AUDIO_DEVICE_IN_WIRED_HEADSET = AUDIO_DEVICE_BIT_IN | 0x10u,
    AUDIO_DEVICE_IN_VOICE_CALL = AUDIO_DEVICE_BIT_IN | 0x40u,
};

enum {
    AUDIO_PATCH_HANDLE_NONE = 0,
    AUDIO_PORT_TYPE_DEVICE = 1,
    AUDIO_PORT_TYPE_MIX = 2,
};

struct audio_config {
    uint32_t sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    size_t frame_count;
};

struct audio_port_config {
    int type;
    union {
        struct {
            audio_devices_t type;
        } device;
        struct {
            audio_io_handle_t handle;
        } mix;
    } ext;
};

static inline int audio_is_input_device(audio_devices_t device) {
    return (device & AUDIO_DEVICE_BIT_IN) != 0;
}
