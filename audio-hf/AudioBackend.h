/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>
#include <system/audio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mindone_audio_device;
struct mindone_stream_out;
struct mindone_stream_in;

enum mindone_audio_command {
    MINDONE_AUDIO_START,
    MINDONE_AUDIO_PAUSE,
    MINDONE_AUDIO_FLUSH,
    MINDONE_AUDIO_DRAIN,
    MINDONE_AUDIO_STANDBY,
    MINDONE_AUDIO_TRANSFER,
};

struct mindone_audio_route {
    audio_devices_t devices;
};

struct mindone_audio_voice {
    audio_mode_t mode;
    float volume;
    bool mic_mute;
};

enum mindone_audio_sco_band {
    MINDONE_AUDIO_SCO_NB,
    MINDONE_AUDIO_SCO_WB,
};

struct mindone_audio_sco {
    enum mindone_audio_sco_band band;
};

struct mindone_audio_gain {
    float left;
    float right;
};

int mindone_audio_device_open(struct mindone_audio_device **device);
int mindone_audio_device_close(struct mindone_audio_device *device);
int mindone_audio_set_mode(struct mindone_audio_device *device, audio_mode_t mode);
int mindone_audio_set_voice_volume(struct mindone_audio_device *device, float volume);
int mindone_audio_set_mic_mute(struct mindone_audio_device *device, bool muted);
int mindone_audio_get_voice(struct mindone_audio_device *device, struct mindone_audio_voice *voice);
int mindone_audio_speech_gain(struct mindone_audio_device *device, int gain);
int mindone_audio_speech_pump(struct mindone_audio_device *device);
int mindone_audio_set_sco(struct mindone_audio_device *device, struct mindone_audio_sco sco);
int mindone_audio_create_patch(struct mindone_audio_device *device,
                              unsigned int source_count, const struct audio_port_config *sources,
                              unsigned int sink_count, const struct audio_port_config *sinks,
                              audio_patch_handle_t *handle);
int mindone_audio_release_patch(struct mindone_audio_device *device, audio_patch_handle_t handle);

int mindone_audio_output_get_config(struct mindone_stream_out *stream, struct audio_config *config);
int mindone_audio_output_open(struct mindone_audio_device *device, audio_io_handle_t handle,
                             audio_devices_t devices, audio_output_flags_t flags,
                             struct audio_config *config, struct mindone_stream_out **stream);
void mindone_audio_output_close(struct mindone_stream_out *stream);
int mindone_audio_output_command(struct mindone_stream_out *stream, enum mindone_audio_command command);
ssize_t mindone_audio_output_write(struct mindone_stream_out *stream, const void *buffer, size_t bytes);
int mindone_audio_output_latency(struct mindone_stream_out *stream, uint32_t *latency_ms);
int mindone_audio_output_position(struct mindone_stream_out *stream, uint64_t *frames,
                                 struct timespec *timestamp);
int mindone_audio_output_route(struct mindone_stream_out *stream, struct mindone_audio_route route);
int mindone_audio_output_gain(struct mindone_stream_out *stream, struct mindone_audio_gain gain);

int mindone_audio_input_open(struct mindone_audio_device *device, audio_io_handle_t handle,
                            audio_devices_t devices, audio_input_flags_t flags,
                            struct audio_config *config, struct mindone_stream_in **stream);
int mindone_audio_input_get_config(struct mindone_stream_in *stream, struct audio_config *config);
void mindone_audio_input_close(struct mindone_stream_in *stream);
int mindone_audio_input_command(struct mindone_stream_in *stream, enum mindone_audio_command command);
ssize_t mindone_audio_input_read(struct mindone_stream_in *stream, void *buffer, size_t bytes);
int mindone_audio_input_buffer_size(struct mindone_stream_in *stream, size_t *bytes);
int mindone_audio_input_position(struct mindone_stream_in *stream, int64_t *frames, int64_t *time_ns);
int mindone_audio_input_route(struct mindone_stream_in *stream, struct mindone_audio_route route);
int mindone_audio_input_gain(struct mindone_stream_in *stream, float gain);

#ifdef __cplusplus
}
#endif
