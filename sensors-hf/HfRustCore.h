/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef MINDONE_HF_RUST_CORE_H
#define MINDONE_HF_RUST_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mindone_hf_decoded_event {
    int64_t timestamp_ns;
    int32_t words[16];
    uint32_t sensor_type;
    uint32_t accuracy;
    uint32_t action;
};

struct mindone_hf_decoded_info {
    uint32_t sensor_type;
    uint32_t gain;
    uint8_t name[16];
    uint8_t vendor[16];
};

struct mindone_hf_state {
    int64_t period_ns;
    int64_t latency_ns;
    uint32_t active;
    uint32_t reserved;
};

struct mindone_hf_state_transition {
    struct mindone_hf_state next;
    uint32_t command;
    uint32_t reserved;
};

struct mindone_hf_calibration_layout {
    uint32_t length;
    uint32_t bias_offset;
    uint32_t bias_length;
    uint32_t cali_offset;
    uint32_t cali_length;
    uint32_t temp_offset;
    uint32_t temp_length;
};

struct mindone_hf_calibration_payload {
    uint8_t bytes[48];
    struct mindone_hf_calibration_layout layout;
    uint32_t combined;
};

struct mindone_hf_calibration_event {
    int32_t words[6];
    uint32_t count;
    uint32_t region;
};

int32_t mindone_hf_calibration_payload(uint8_t sensor_type,
                                      const int32_t* bias, size_t bias_count,
                                      const int32_t* cali, size_t cali_count,
                                      const int32_t* temp, size_t temp_count,
                                      struct mindone_hf_calibration_payload* output);
int32_t mindone_hf_calibration_event(uint8_t sensor_type, uint8_t action,
                                    const int32_t* words, size_t count,
                                    struct mindone_hf_calibration_event* output);

enum mindone_hf_state_command {
    MINDONE_HF_STATE_NONE = 0,
    MINDONE_HF_STATE_ENABLE = 1,
    MINDONE_HF_STATE_FLUSH = 2,
};

int32_t mindone_hf_state_activate(const struct mindone_hf_state* current, int32_t sensor_type,
                                 uint32_t enabled, struct mindone_hf_state_transition* output);
int32_t mindone_hf_state_batch(const struct mindone_hf_state* current, int32_t sensor_type,
                              int64_t period_ns, int64_t latency_ns,
                              struct mindone_hf_state_transition* output);
int32_t mindone_hf_state_flush(const struct mindone_hf_state* current, int32_t sensor_type,
                              uint32_t one_shot, struct mindone_hf_state_transition* output);

int32_t mindone_hf_decode_event(const uint8_t* input, size_t length,
                              struct mindone_hf_decoded_event* output);
int32_t mindone_hf_decode_info(const uint8_t* input, size_t length,
                             struct mindone_hf_decoded_info* output);
int32_t mindone_hf_enable_command(uint8_t sensor_type, uint8_t enabled,
                                int64_t delay_ns, int64_t latency_ns,
                                uint8_t* output, size_t capacity);
int32_t mindone_hf_flush_command(uint8_t sensor_type, uint8_t* output, size_t capacity);
int32_t mindone_hf_calibration_command(uint8_t sensor_type, const uint8_t* input,
                                     size_t length, uint8_t* output, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
