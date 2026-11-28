/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "AudioBackend.h"

PRODUCTION_ENUMS

struct mindone_stream_out {
    enum output_device routed_device;
    bool reopen_pending;
    struct mindone_stream_out *next;
};

struct mindone_stream_in {
    enum input_device routed_device;
    bool reopen_pending;
};

struct mindone_audio_device {
    pthread_mutex_t lock;
    bool bt_wb;
    bool in_call;
    enum output_device voice_out_device;
    struct mindone_stream_out *outs;
    struct mindone_stream_in *active_in;
};

static struct mindone_audio_device g_dev;
static struct mindone_stream_out g_sco_out;
static struct mindone_stream_out g_spk_out;
static struct mindone_stream_in g_sco_in;
static unsigned int g_voice_changes;
static int g_change_error;
static unsigned int g_opens;
static unsigned int g_closes;

static int voice_route_change_sco(struct mindone_audio_device *adev, enum output_device out) {
    if (!adev->in_call || out != OUT_DEVICE_BT_SCO) return -EINVAL;
    ++g_voice_changes;
    return g_change_error;
}

PRODUCTION_SET_SCO

void sco_test_reset(void) {
    memset(&g_dev, 0, sizeof(g_dev));
    pthread_mutex_init(&g_dev.lock, NULL);
    g_sco_out = (struct mindone_stream_out){
        .routed_device = OUT_DEVICE_BT_SCO,
        .reopen_pending = false,
        .next = &g_spk_out,
    };
    g_spk_out = (struct mindone_stream_out){
        .routed_device = OUT_DEVICE_SPEAKER,
        .reopen_pending = false,
        .next = NULL,
    };
    g_sco_in = (struct mindone_stream_in){
        .routed_device = IN_DEVICE_BT_SCO,
        .reopen_pending = false,
    };
    g_dev.outs = &g_sco_out;
    g_dev.active_in = &g_sco_in;
    g_voice_changes = 0;
    g_change_error = 0;
    g_opens = 0;
    g_closes = 0;
}

void sco_test_set_in_call(bool in_call, bool bt_sco_voice) {
    g_dev.in_call = in_call;
    g_dev.voice_out_device = bt_sco_voice ? OUT_DEVICE_BT_SCO : OUT_DEVICE_EARPIECE;
}

void sco_test_set_change_error(int err) { g_change_error = err; }
bool sco_test_bt_wb(void) { return g_dev.bt_wb; }
bool sco_test_out_reopen_pending(void) { return g_sco_out.reopen_pending; }
bool sco_test_other_out_reopen_pending(void) { return g_spk_out.reopen_pending; }
bool sco_test_in_reopen_pending(void) { return g_sco_in.reopen_pending; }
void sco_test_clear_reopen_flags(void) {
    g_sco_out.reopen_pending = false;
    g_spk_out.reopen_pending = false;
    g_sco_in.reopen_pending = false;
}
unsigned int sco_test_voice_change_count(void) { return g_voice_changes; }
unsigned int sco_test_open_count(void) { return g_opens; }
unsigned int sco_test_close_count(void) { return g_closes; }

int mindone_audio_device_open(struct mindone_audio_device **device) {
    if (!device) return -EINVAL;
    ++g_opens;
    *device = &g_dev;
    return 0;
}

int mindone_audio_device_close(struct mindone_audio_device *device) {
    if (device != &g_dev) return -EINVAL;
    ++g_closes;
    pthread_mutex_destroy(&g_dev.lock);
    return 0;
}

int mindone_audio_set_mode(struct mindone_audio_device *device, audio_mode_t mode) {
    (void)device;
    (void)mode;
    return 0;
}

int mindone_audio_set_voice_volume(struct mindone_audio_device *device, float volume) {
    (void)device;
    (void)volume;
    return 0;
}

int mindone_audio_set_mic_mute(struct mindone_audio_device *device, bool muted) {
    (void)device;
    (void)muted;
    return 0;
}

int mindone_audio_get_voice(struct mindone_audio_device *device, struct mindone_audio_voice *voice) {
    if (!device || !voice) return -EINVAL;
    memset(voice, 0, sizeof(*voice));
    return 0;
}

int mindone_audio_create_patch(struct mindone_audio_device *device,
                               unsigned int source_count, const struct audio_port_config *sources,
                               unsigned int sink_count, const struct audio_port_config *sinks,
                               audio_patch_handle_t *handle) {
    (void)device;
    (void)source_count;
    (void)sources;
    (void)sink_count;
    (void)sinks;
    if (handle) *handle = 1;
    return 0;
}

int mindone_audio_release_patch(struct mindone_audio_device *device, audio_patch_handle_t handle) {
    (void)device;
    (void)handle;
    return 0;
}
