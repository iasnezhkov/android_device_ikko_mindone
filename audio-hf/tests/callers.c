/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "AudioRustRoutes.h"

#define ALOGW(...) fake_log(__VA_ARGS__)
#define ALOGI(...) fake_log(__VA_ARGS__)
#define ALOGE(...) fake_log(__VA_ARGS__)
#define AUDIO_DEVICE_OUT_SPEAKER 2u
#define AUDIO_DEVICE_OUT_EARPIECE 1u
#define AUDIO_DEVICE_OUT_WIRED_HEADSET 4u
#define AUDIO_DEVICE_OUT_WIRED_HEADPHONE 8u
#define AUDIO_DEVICE_OUT_BLUETOOTH_SCO 16u
#define AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET 32u
#define AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT 64u
#define AUDIO_DEVICE_OUT_TELEPHONY_TX 128u
#define AUDIO_DEVICE_BIT_IN 0x80000000u
#define AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET (AUDIO_DEVICE_BIT_IN | 8u)
#define AUDIO_DEVICE_IN_WIRED_HEADSET (AUDIO_DEVICE_BIT_IN | 16u)
#define AUDIO_DEVICE_IN_VOICE_CALL (AUDIO_DEVICE_BIT_IN | 64u)
#define AUDIO_PORT_TYPE_DEVICE 1
#define AUDIO_PORT_TYPE_MIX 2
#define AUDIO_MODE_NORMAL 0
#define AUDIO_MODE_RINGTONE 1
#define AUDIO_MODE_IN_COMMUNICATION 3
#define AUDIO_MODE_IN_CALL 2
#define AUDIO_PATCH_HANDLE_NONE 0
#define AUDIO_PARAMETER_STREAM_ROUTING "routing"
#define PCM_CARD 0
#define PCM_OUT 0
#define PCM_MONOTONIC 8

typedef uint32_t audio_devices_t;
typedef int audio_mode_t;
typedef int audio_output_flags_t;
typedef int audio_io_handle_t;
typedef int audio_patch_handle_t;
struct audio_stream { int unused; };
struct audio_stream_out { struct audio_stream common; };
struct audio_stream_in { struct audio_stream common; };
struct pcm_config {
    unsigned int channels, rate, period_size, period_count;
    int format;
    unsigned long start_threshold, stop_threshold, silence_threshold, silence_size, avail_min;
};
struct pcm { int alive; struct pcm_config config; };
struct audio_route { unsigned int effects; };
struct audio_port_config {
    int type;
    union {
        struct { audio_devices_t type; } device;
        struct { int handle; } mix;
    } ext;
};

PRODUCTION_ENUMS
PRODUCTION_STRUCTS

static unsigned int checks, pcm_opens, pcm_closes, modem_calls, freed;
static struct pcm opened_pcm;
static struct mindone_audio_device *change_on_open;
static struct mindone_stream_out *closing_stream;
static void fake_log(const char *format, ...) { (void)format; }

static void check(int condition, const char *message)
{
    checks++;
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void audio_route_apply_path(struct audio_route *route, const char *path)
{
    check(path != NULL, "valid mixer path");
    route->effects++;
}
static void audio_route_update_mixer(struct audio_route *route) { route->effects++; }
static void pcm_close(struct pcm *pcm) { check(pcm->alive, "close live PCM"); pcm->alive = 0; pcm_closes++; }
static const struct pcm_config *pcm_get_config(const struct pcm *pcm) { return &pcm->config; }
static int pcm_is_ready(const struct pcm *pcm) { return pcm->alive; }
static const char *pcm_get_error(const struct pcm *pcm) { (void)pcm; return "fake"; }
static struct pcm *pcm_open(unsigned int card, unsigned int device, unsigned int flags, const struct pcm_config *config)
{
    (void)card; (void)device; (void)flags; (void)config;
    pcm_opens++;
    opened_pcm.alive = 1;
    opened_pcm.config = *config;
    if (change_on_open)
        change_on_open->out_ep_users[OUT_DEVICE_SPEAKER] = INT_MAX;
    return &opened_pcm;
}
static void apply_dl_crossbar(struct mindone_audio_device *adev, struct mindone_stream_out *out)
{
    adev->route->effects++;
    out->crossbar_on = true;
}
static void teardown_dl_crossbar(struct mindone_audio_device *adev, struct mindone_stream_out *out)
{
    if (out->crossbar_on) {
        adev->route->effects++;
        out->crossbar_on = false;
    }
}
static int output_pcm_device(struct mindone_stream_out *out, enum output_device next)
{
    return next == OUT_DEVICE_BT_SCO ? 46 : out->pcm_device;
}
static unsigned int sco_rate(struct mindone_audio_device *adev) { return adev->bt_wb ? 16000 : 8000; }
static void sco_pcm_config(struct mindone_audio_device *adev, struct pcm_config *config)
{
    *config = (struct pcm_config){.rate = sco_rate(adev), .channels = 1};
}
static void bt_sco_configure(struct mindone_audio_device *adev) { adev->route->effects++; }
static void apply_input_route(struct mindone_audio_device *adev, enum input_device old, enum input_device next)
{
    (void)old; (void)next; adev->route->effects++;
}
static void teardown_input_route(struct mindone_audio_device *adev, enum input_device endpoint)
{
    (void)endpoint; adev->route->effects++;
}
static int voice_gain_acquire(struct mindone_audio_device *adev, enum output_device endpoint)
{
    (void)endpoint; adev->route->effects++; return 0;
}
static void voice_gain_release(struct mindone_audio_device *adev) { adev->route->effects++; }
static int speech_hostless_open(struct mindone_audio_device *adev)
{
    adev->speech_hostless_out = &opened_pcm;
    adev->speech_hostless_in = &opened_pcm;
    adev->route->effects++;
    return 0;
}
static void speech_hostless_close(struct mindone_audio_device *adev)
{
    adev->speech_hostless_out = NULL;
    adev->speech_hostless_in = NULL;
    adev->route->effects++;
}
static int speech_ccci_on(struct mindone_audio_device *adev, enum output_device endpoint)
{
    (void)adev; (void)endpoint; modem_calls++; return 0;
}
static int speech_ccci_off(struct mindone_audio_device *adev) { (void)adev; modem_calls++; return 0; }
static int speech_ccci_set_mode(struct mindone_audio_device *adev, enum output_device endpoint)
{
    (void)adev; (void)endpoint; modem_calls++; return 0;
}
static void set_hw_output_volume(struct mindone_audio_device *adev, enum output_device endpoint, float volume)
{
    (void)endpoint; (void)volume; adev->route->effects++;
}
static enum output_device voice_output_device(struct mindone_audio_device *adev)
{
    (void)adev; return OUT_DEVICE_EARPIECE;
}
struct str_parms { const char *value; };
static __attribute__((unused)) struct str_parms *str_parms_create_str(const char *value)
{
    struct str_parms *parms = malloc(sizeof(*parms));
    if (parms) parms->value = value;
    return parms;
}
static __attribute__((unused)) int str_parms_get_str(struct str_parms *parms, const char *key, char *value, int capacity)
{
    (void)key;
    return snprintf(value, (size_t)capacity, "%s", parms->value);
}
static __attribute__((unused)) void str_parms_destroy(struct str_parms *parms) { free(parms); }
static void checked_free(void *pointer)
{
    if (pointer == closing_stream)
        check(closing_stream->standby && closing_stream->routed_device == OUT_DEVICE_NONE &&
              !closing_stream->pcm && !closing_stream->crossbar_on && !closing_stream->reopen_pending,
              "failed standby close frees owned resources");
    if (pointer) freed++;
    free(pointer);
}
static size_t __attribute__((unused)) audio_stream_out_frame_size(struct audio_stream_out *stream) { (void)stream; return 4; }
static void speaker_dsp_process(struct mindone_stream_out *out, int16_t *data, size_t frames)
{
    (void)data; (void)frames; out->dev->route->effects++;
}
static void output_volume_process(struct mindone_stream_out *out, int16_t *data, size_t frames)
{
    (void)data; (void)frames; out->dev->route->effects++;
}
static int sco_out_convert(struct mindone_stream_out *out, const void *data, size_t frames)
{
    (void)data; (void)frames; out->dev->route->effects++; return 0;
}
static int sco_out_flush(struct mindone_stream_out *out) { out->dev->route->effects++; return 0; }
static int pcm_write_recovering(struct mindone_stream_out *out, const void *data, size_t bytes)
{
    (void)data; (void)bytes; out->dev->route->effects++; return 0;
}

static int voice_route_change_sco(struct mindone_audio_device *adev, enum output_device out)
{
    (void)adev;
    (void)out;
    return -ENOSYS;
}

PRODUCTION_FUNCTIONS

struct saved {
    struct mindone_audio_device device;
    struct mindone_stream_out stream;
    unsigned int effects, closes, modem;
};

static struct saved save(struct mindone_audio_device *device, struct mindone_stream_out *out)
{
    struct saved saved = {.device = *device, .stream = *out, .effects = device->route->effects,
                          .closes = pcm_closes, .modem = modem_calls};
    memset(&saved.device.lock, 0, sizeof(saved.device.lock));
    memset(&saved.stream.lock, 0, sizeof(saved.stream.lock));
    return saved;
}

static void unchanged(const struct saved *expected, struct mindone_audio_device *device,
                       struct mindone_stream_out *out, const char *message)
{
    struct saved current = save(device, out);
    check(memcmp(&expected->device, &current.device, sizeof(current.device)) == 0 &&
          memcmp(&expected->stream, &current.stream, sizeof(current.stream)) == 0 &&
          expected->effects == current.effects && expected->modem == current.modem,
          message);
}

static void init(struct mindone_audio_device *device, struct mindone_stream_out *out,
                  struct audio_route *route)
{
    *route = (struct audio_route){0};
    *device = (struct mindone_audio_device){.route = route};
    *out = (struct mindone_stream_out){.dev = device, .frame_size = 4, .device = AUDIO_DEVICE_OUT_SPEAKER,
        .routed_device = OUT_DEVICE_SPEAKER, .config = {.rate = 48000, .channels = 2},
        .hw_config = {.rate = 12345, .channels = 1}, .io_handle = 17};
    pthread_mutex_init(&device->lock, NULL);
    pthread_mutex_init(&out->lock, NULL);
    device->outs = out;
    device->out_ep_users[OUT_DEVICE_SPEAKER] = 1;
    pcm_opens = pcm_closes = modem_calls = freed = 0;
    change_on_open = NULL;
}

static void finish(struct mindone_audio_device *device, struct mindone_stream_out *out)
{
    pthread_mutex_destroy(&out->lock);
    pthread_mutex_destroy(&device->lock);
}

static void parameters_and_standby(void)
{
    struct mindone_audio_device device;
    struct mindone_stream_out out;
    struct audio_route route;
    struct pcm pcm = {.alive = 1};
    init(&device, &out, &route);
    out.pcm = &pcm;
    out.crossbar_on = true;
    device.active_out = &out;
    device.out_ep_users[OUT_DEVICE_HEADPHONE] = INT_MAX;
    struct saved before = save(&device, &out);
    check(out_set_route(&out, 8) == -EOVERFLOW, "parameter planning error propagates");
    unchanged(&before, &device, &out, "parameter rejection preserves entire caller state");
    device.out_ep_users[OUT_DEVICE_SPEAKER] = 0;
    before = save(&device, &out);
    check(out_set_route(&out, 16) == -EINVAL, "restart standby error propagates");
    unchanged(&before, &device, &out, "restart rejection preserves mask and stream");
    check(do_out_standby(&out) == -EINVAL, "standby underflow rejected");
    unchanged(&before, &device, &out, "standby rejection precedes PCM and crossbar teardown");
    check(pcm_closes == 0 && pcm.alive, "rejected standby keeps PCM alive");
    out.reopen_pending = true;
    before = save(&device, &out);
    int16_t samples[2] = {1, 2};
    check(out_write(&out, samples, sizeof(samples)) == -EINVAL, "write propagates standby rejection");
    unchanged(&before, &device, &out, "write rejection cannot restart or process data");
    out.routed_device = OUT_DEVICE_BT_SCO;
    check(do_out_standby(&out) == 0, "uncounted SCO standby succeeds");
    check(!out.pcm && out.standby && out.routed_device == OUT_DEVICE_NONE && !device.active_out,
          "SCO standby clears its own stream");
    finish(&device, &out);
}

static void staged_start(void)
{
    struct mindone_audio_device device;
    struct mindone_stream_out out;
    struct audio_route route;
    init(&device, &out, &route);
    out.standby = true;
    out.reopen_pending = true;
    out.routed_device = OUT_DEVICE_NONE;
    device.out_ep_users[OUT_DEVICE_SPEAKER] = INT_MAX;
    struct saved before = save(&device, &out);
    check(start_output_stream(&out) == -EOVERFLOW, "start planning error propagates");
    unchanged(&before, &device, &out, "start rejection preserves config and accepted state");
    check(pcm_opens == 1 && pcm_closes == 1 && !opened_pcm.alive, "rejected staged PCM is closed");
    device.out_ep_users[OUT_DEVICE_SPEAKER] = 0;
    change_on_open = &device;
    check(start_output_stream(&out) == -EOVERFLOW, "post-open snapshot is revalidated");
    unchanged(&before, &device, &out, "post-open rejection preserves stream state");
    check(pcm_opens == 2 && pcm_closes == 2, "concurrent count change does not leak PCM");
    change_on_open = NULL;
    device.out_ep_users[OUT_DEVICE_SPEAKER] = 0;
    check(start_output_stream(&out) == 0, "valid start accepted");
    check(out.pcm == &opened_pcm && out.hw_config.rate == 48000 && !out.reopen_pending &&
          !out.standby && out.routed_device == OUT_DEVICE_SPEAKER && device.active_out == &out &&
          device.out_ep_users[OUT_DEVICE_SPEAKER] == 1, "valid start publishes complete state");
    check(do_out_standby(&out) == 0, "valid stream cleans up");
    finish(&device, &out);
}

static void voice_errors(void)
{
    struct mindone_audio_device device;
    struct mindone_stream_out out;
    struct audio_route route;
    init(&device, &out, &route);
    device.out_ep_users[OUT_DEVICE_EARPIECE] = INT_MAX;
    struct saved before = save(&device, &out);
    check(voice_route_enable(&device, OUT_DEVICE_EARPIECE) == -EOVERFLOW, "voice enable error propagates");
    unchanged(&before, &device, &out, "voice enable rejection precedes transport and route state");
    device.in_call = device.speech_on = true;
    device.voice_out_device = OUT_DEVICE_SPEAKER;
    device.out_ep_users[OUT_DEVICE_SPEAKER] = 0;
    device.mode = AUDIO_MODE_IN_CALL;
    before = save(&device, &out);
    check(voice_route_disable(&device, true) == -EINVAL, "voice disable error propagates");
    unchanged(&before, &device, &out, "voice disable rejection precedes all side effects");
    check(mindone_audio_set_mode(&device, AUDIO_MODE_NORMAL) == -EINVAL, "mode error propagates");
    unchanged(&before, &device, &out, "mode rejection preserves software call state");
    device.out_ep_users[OUT_DEVICE_SPEAKER] = 1;
    before = save(&device, &out);
    struct audio_port_config source = {.type = AUDIO_PORT_TYPE_DEVICE,
                                       .ext.device.type = AUDIO_DEVICE_IN_VOICE_CALL};
    struct audio_port_config sink = {.type = AUDIO_PORT_TYPE_DEVICE,
                                     .ext.device.type = AUDIO_DEVICE_OUT_EARPIECE};
    audio_patch_handle_t handle = 99;
    check(mindone_audio_create_patch(&device, 1, &source, 1, &sink, &handle) == -EOVERFLOW,
          "voice patch planning error propagates");
    unchanged(&before, &device, &out, "voice patch rejection precedes CCCI and teardown");
    check(handle == 99 && modem_calls == 0, "rejected voice patch keeps handle and modem");
    finish(&device, &out);
}

static void cumulative_patch(void)
{
    struct mindone_audio_device device;
    struct mindone_stream_out out, second;
    struct audio_route route;
    init(&device, &out, &route);
    second = out;
    memset(&second.lock, 0, sizeof(second.lock));
    pthread_mutex_init(&second.lock, NULL);
    second.io_handle = 18;
    out.next = &second;
    device.out_ep_users[OUT_DEVICE_SPEAKER] = 2;
    device.out_ep_users[OUT_DEVICE_HEADPHONE] = INT_MAX - 1;
    struct audio_port_config sources[2] = {{.type = AUDIO_PORT_TYPE_MIX, .ext.mix.handle = 17},
                                           {.type = AUDIO_PORT_TYPE_MIX, .ext.mix.handle = 18}};
    struct audio_port_config sink = {.type = AUDIO_PORT_TYPE_DEVICE,
                                     .ext.device.type = AUDIO_DEVICE_OUT_WIRED_HEADPHONE};
    audio_patch_handle_t handle = 77;
    struct saved before = save(&device, &out);
    struct saved second_before = save(&device, &second);
    check(mindone_audio_create_patch(&device, 2, sources, 1, &sink, &handle) == -EOVERFLOW,
          "cumulative multi-owner overflow rejected");
    unchanged(&before, &device, &out, "cumulative patch rejection preserves first stream and counts");
    unchanged(&second_before, &device, &second, "cumulative patch rejection preserves second stream");
    check(handle == 77, "rejected cumulative patch keeps handle");
    device.out_ep_users[OUT_DEVICE_HEADPHONE] = 0;
    device.out_ep_users[OUT_DEVICE_SPEAKER] = 1;
    before = save(&device, &out);
    check(mindone_audio_create_patch(&device, 2, sources, 1, &sink, &handle) == -EINVAL,
          "cumulative multi-owner underflow rejected");
    unchanged(&before, &device, &out, "cumulative underflow has no partial route");
    device.out_ep_users[OUT_DEVICE_SPEAKER] = 2;
    check(mindone_audio_create_patch(&device, 2, sources, 1, &sink, &handle) == 0,
          "valid multi-owner patch accepted");
    check(out.routed_device == OUT_DEVICE_HEADPHONE && second.routed_device == OUT_DEVICE_HEADPHONE &&
          out.device == AUDIO_DEVICE_OUT_WIRED_HEADPHONE && second.device == out.device &&
          device.out_ep_users[OUT_DEVICE_HEADPHONE] == 2 && device.out_ep_users[OUT_DEVICE_SPEAKER] == 0 &&
          handle != 77, "valid multi-owner patch publishes all state");
    pthread_mutex_destroy(&second.lock);
    finish(&device, &out);
}

static void close_after_rejection(bool shared)
{
    struct mindone_audio_device device;
    struct audio_route route;
    struct mindone_stream_out *out = malloc(sizeof(*out));
    struct pcm pcm = {.alive = 1};
    init(&device, out, &route);
    struct mindone_stream_out sibling = {.dev = &device, .crossbar_on = true};
    if (shared) {
        out->next = &sibling;
    }
    out->pcm = &pcm;
    out->crossbar_on = true;
    out->reopen_pending = true;
    out->dsp_buf = malloc(8);
    out->sco_buf = malloc(8);
    device.active_out = out;
    device.out_ep_users[OUT_DEVICE_SPEAKER] = 0;
    device.out_ep_users[OUT_DEVICE_EARPIECE] = 1;
    int counts[OUT_DEVICE_COUNT];
    memcpy(counts, device.out_ep_users, sizeof(counts));
    closing_stream = out;
    close_output_stream(out);
    closing_stream = NULL;
    check(pcm_closes == 1 && !pcm.alive && freed == 3, "failed standby close frees owned resources");
    check(!device.active_out && device.outs == (shared ? &sibling : NULL) && !sibling.next,
          "failed standby close detaches stream pointers");
    check(memcmp(counts, device.out_ep_users, sizeof(counts)) == 0 &&
          route.effects == (shared ? 0u : 1u) && sibling.crossbar_on,
          "failed standby close preserves shared endpoint owners and paths");
    pthread_mutex_destroy(&device.lock);
}

int main(void)
{
    parameters_and_standby();
    staged_start();
    voice_errors();
    cumulative_patch();
    close_after_rejection(false);
    close_after_rejection(true);
    printf("%u checks: exact outer caller error propagation and cleanup PASS\n", checks);
    return 0;
}
