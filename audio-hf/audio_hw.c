/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "audio_hw_mindone"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <sound/asound.h>
#include <malloc.h>
#include <math.h>
#include <pthread.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>

#include <log/log.h>
#include <cutils/properties.h>

#include "AudioBackend.h"

#include <tinyalsa/pcm.h>
#include <tinyalsa/mixer.h>

_Static_assert(sizeof(struct pcm_config) == 64, "tinyalsa v2 LP64 configuration ABI");
#include <audio_route/audio_route.h>

#if defined(MINDONE_AUDIO_RUST_ROUTES)
#include "AudioRustRoutes.h"
#endif

#define PCM_CARD                0

#define PCM_DEVICE_PRIMARY      0
#define PCM_DEVICE_FAST         2
#define PCM_DEVICE_DEEP_BUFFER  3
#define PCM_DEVICE_CAPTURE_MAIN 9
#define PCM_DEVICE_BTCVSD       46
#define SCO_RATE_NB             8000
#define SCO_RATE_WB             16000
#define PCM_DEVICE_SPEECH_HOSTLESS  22
#define SPEECH_HOSTLESS_RATE        48000
#define SPEECH_HOSTLESS_CHANNELS    2
#define SPEECH_HOSTLESS_PERIOD_SIZE (SPEECH_HOSTLESS_RATE / 50)
#define SPEECH_HOSTLESS_PERIOD_COUNT 4

#define OUT_SAMPLING_RATE   48000
#define OUT_PERIOD_SIZE     960
#define OUT_PERIOD_COUNT    4

#define OUT_DEEP_PERIOD_SIZE    1920
#define OUT_DEEP_PERIOD_COUNT   8
#define OUT_FAST_PERIOD_SIZE    240
#define OUT_FAST_PERIOD_COUNT   4
#define OUT_CHANNEL_COUNT   2

#define IN_SAMPLING_RATE    48000
#define IN_PERIOD_SIZE      960
#define IN_PERIOD_COUNT     4
#define IN_CHANNEL_COUNT    2

#define IN_DIGITAL_GAIN_Q8  3072

#define MIXER_PATHS_XML     "/vendor/etc/mixer_paths.xml"

enum output_device {
    OUT_DEVICE_NONE = 0,
    OUT_DEVICE_SPEAKER,
    OUT_DEVICE_HEADPHONE,
    OUT_DEVICE_EARPIECE,
    OUT_DEVICE_BT_SCO,
};
#define OUT_DEVICE_COUNT (OUT_DEVICE_BT_SCO + 1)

enum input_device {
    IN_DEVICE_NONE = 0,
    IN_DEVICE_MAIN_MIC,
    IN_DEVICE_HEADSET_MIC,
    IN_DEVICE_BT_SCO,
};
#define IN_DEVICE_COUNT (IN_DEVICE_BT_SCO + 1)

struct mindone_audio_device {
    pthread_mutex_t lock;
    struct audio_route *route;
    struct mixer *mixer;

    audio_mode_t mode;
    bool mic_mute;
    float voice_volume;
    struct mixer_ctl *voice_gain_ctl;
    long voice_gain_values[2];
    unsigned int voice_gain_count;

    bool bt_wb;

    bool in_call;
    bool speech_on;
    int speech_fd;
    enum output_device voice_out_device;
    enum input_device voice_in_device;
    struct pcm *speech_hostless_out;
    struct pcm *speech_hostless_in;

    struct mindone_stream_out *active_out;
    struct mindone_stream_in *active_in;
    struct mindone_stream_in *ins[8];
    int n_ins;

    int out_ep_users[OUT_DEVICE_COUNT];
    int in_ep_users[IN_DEVICE_COUNT];
    enum input_device input_routed_device;

    struct mindone_stream_out *outs;
};

struct mindone_stream_out {
    size_t frame_size;
    struct mindone_stream_out *next;

    pthread_mutex_t lock;
    struct mindone_audio_device *dev;

    struct pcm *pcm;
    struct pcm_config config;
    int pcm_device;
    audio_output_flags_t flags;
    audio_devices_t device;
    enum output_device routed_device;
    bool crossbar_on;
    audio_io_handle_t io_handle;

    struct pcm_config hw_config;
    bool reopen_pending;

    bool standby;
    bool aidl_paused;
    bool aidl_hw_paused;
    uint64_t aidl_discarded;
    uint64_t aidl_presented;
    struct timespec aidl_timestamp;
    uint64_t written_frames;

    int16_t *sco_buf;
    size_t sco_buf_frames;
    size_t sco_staged;
    int32_t sco_acc;
    unsigned int sco_acc_n;

    int16_t *dsp_buf;
    size_t dsp_frames;
    float hp_b0, hp_b1, hp_b2, hp_a1, hp_a2;
    uint32_t hp_rate;
    float hp_x1[OUT_CHANNEL_COUNT], hp_x2[OUT_CHANNEL_COUNT];
    float hp_y1[OUT_CHANNEL_COUNT], hp_y2[OUT_CHANNEL_COUNT];
    float lim_gain;
    float volume_left;
    float volume_right;
};

struct mindone_stream_in {
    size_t frame_size;

    pthread_mutex_t lock;
    struct mindone_audio_device *dev;

    struct pcm *pcm;
    struct pcm_config config;
    struct pcm_config hw_config;
    audio_devices_t device;
    enum input_device routed_device;
    audio_io_handle_t io_handle;
    bool reopen_pending;

    bool standby;
    uint64_t read_frames;

    int16_t *sco_in_buf;
    size_t sco_in_frames;
    int16_t sco_last;

    int16_t *downmix_buf;
    size_t downmix_frames;
};


static enum output_device output_device_from_mask(audio_devices_t device)
{
    if (device & (AUDIO_DEVICE_OUT_WIRED_HEADSET | AUDIO_DEVICE_OUT_WIRED_HEADPHONE))
        return OUT_DEVICE_HEADPHONE;
    if (device & AUDIO_DEVICE_OUT_SPEAKER)
        return OUT_DEVICE_SPEAKER;
    if (device & AUDIO_DEVICE_OUT_EARPIECE)
        return OUT_DEVICE_EARPIECE;
    if (device & (AUDIO_DEVICE_OUT_BLUETOOTH_SCO |
                  AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET |
                  AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT))
        return OUT_DEVICE_BT_SCO;
    return OUT_DEVICE_SPEAKER;
}

static const char *output_path_name(enum output_device d)
{
    switch (d) {
    case OUT_DEVICE_HEADPHONE: return "headphone";
    case OUT_DEVICE_EARPIECE:  return "earpiece";
    case OUT_DEVICE_BT_SCO:    return NULL;
    case OUT_DEVICE_SPEAKER:
    default:                   return "speaker";
    }
}

static const char *output_path_off_name(enum output_device d)
{
    switch (d) {
    case OUT_DEVICE_HEADPHONE: return "headphone-off";
    case OUT_DEVICE_EARPIECE:  return "earpiece-off";
    case OUT_DEVICE_BT_SCO:    return NULL;
    case OUT_DEVICE_SPEAKER:
    default:                   return "speaker-off";
    }
}

#if defined(MINDONE_AUDIO_RUST_ROUTES)
static int plan_endpoint_transition(const struct mindone_audio_route_state *current,
                                    enum output_device old_device,
                                    enum output_device new_device,
                                    struct mindone_audio_route_plan *plan)
{
    _Static_assert(OUT_DEVICE_NONE == 0 && OUT_DEVICE_SPEAKER == 1 &&
                   OUT_DEVICE_HEADPHONE == 2 && OUT_DEVICE_EARPIECE == 3 &&
                   OUT_DEVICE_BT_SCO == 4 && OUT_DEVICE_COUNT == 5, "endpoint ABI");
    int32_t status = mindone_audio_route_plan(current, (uint32_t)old_device,
                                             (uint32_t)new_device, plan);
    return status == 0 ? 0 : status == -5 ? -EOVERFLOW : -EINVAL;
}

static struct mindone_audio_route_state endpoint_snapshot(const struct mindone_audio_device *adev)
{
    _Static_assert(sizeof(adev->out_ep_users[0]) == sizeof(int32_t), "owner count ABI");
    struct mindone_audio_route_state state;
    for (unsigned int i = 0; i < OUT_DEVICE_COUNT; i++)
        state.users[i] = adev->out_ep_users[i];
    return state;
}

static int check_endpoint_transition(const struct mindone_audio_device *adev,
                                     enum output_device old_device,
                                     enum output_device new_device)
{
    struct mindone_audio_route_state current = endpoint_snapshot(adev);
    struct mindone_audio_route_plan plan;
    return plan_endpoint_transition(&current, old_device, new_device, &plan);
}

static int apply_endpoint_transition(struct mindone_audio_device *adev,
                                     enum output_device old_device,
                                     enum output_device new_device)
{
    struct mindone_audio_route_state current = endpoint_snapshot(adev);
    struct mindone_audio_route_plan plan;
    int status = plan_endpoint_transition(&current, old_device, new_device, &plan);
    if (status != 0)
        return status;
    for (unsigned int i = 0; i < OUT_DEVICE_COUNT; i++)
        adev->out_ep_users[i] = plan.next.users[i];
    for (uint32_t i = 0; i < plan.action_count; i++) {
        const struct mindone_audio_route_action *action = &plan.actions[i];
        enum output_device endpoint = (enum output_device)action->endpoint;
        if (action->kind == 1)
            audio_route_apply_path(adev->route, output_path_name(endpoint));
        else if (action->kind == 2)
            audio_route_apply_path(adev->route, output_path_off_name(endpoint));
        else if (action->kind == 3)
            audio_route_update_mixer(adev->route);
    }
    return 0;
}
#endif

static int endpoint_acquire(struct mindone_audio_device *adev, enum output_device d)
{
#if defined(MINDONE_AUDIO_RUST_ROUTES)
    return apply_endpoint_transition(adev, OUT_DEVICE_NONE, d);
#else
    const char *on_path;

    if (d == OUT_DEVICE_NONE)
        return 0;
    if (adev->out_ep_users[d]++ > 0)
        return 0;
    on_path = output_path_name(d);
    if (on_path) {
        audio_route_apply_path(adev->route, on_path);
        audio_route_update_mixer(adev->route);
    }
    return 0;
#endif
}

static int endpoint_release(struct mindone_audio_device *adev, enum output_device d)
{
#if defined(MINDONE_AUDIO_RUST_ROUTES)
    return apply_endpoint_transition(adev, d, OUT_DEVICE_NONE);
#else
    const char *off_path;

    if (d == OUT_DEVICE_NONE)
        return 0;
    if (adev->out_ep_users[d] <= 0) {
        ALOGW("%s: endpoint %d released more often than acquired", __func__, d);
        return 0;
    }
    if (--adev->out_ep_users[d] > 0)
        return 0;
    off_path = output_path_off_name(d);
    if (off_path) {
        audio_route_apply_path(adev->route, off_path);
        if (d == OUT_DEVICE_SPEAKER && adev->out_ep_users[OUT_DEVICE_EARPIECE] > 0)
            audio_route_apply_path(adev->route, output_path_name(OUT_DEVICE_EARPIECE));
        else if (d == OUT_DEVICE_EARPIECE && adev->out_ep_users[OUT_DEVICE_SPEAKER] > 0)
            audio_route_apply_path(adev->route, output_path_name(OUT_DEVICE_SPEAKER));
        audio_route_update_mixer(adev->route);
    }
    return 0;
#endif
}

static int apply_output_route(struct mindone_audio_device *adev,
                                enum output_device old_device,
                                enum output_device new_device)
{
#if defined(MINDONE_AUDIO_RUST_ROUTES)
    if (old_device == OUT_DEVICE_NONE) {
        return endpoint_acquire(adev, new_device);
    }
    return apply_endpoint_transition(adev, old_device, new_device);
#else
    if (old_device == new_device)
        return 0;
    endpoint_release(adev, old_device);
    endpoint_acquire(adev, new_device);
    return 0;
#endif
}

static int teardown_output_route(struct mindone_audio_device *adev,
                                   enum output_device device)
{
    return endpoint_release(adev, device);
}

static const char *dl_crossbar_path_name(int pcm_device)
{
    switch (pcm_device) {
    case PCM_DEVICE_FAST:        return "dl2-adda";
    case PCM_DEVICE_DEEP_BUFFER: return "dl3-adda";
    case PCM_DEVICE_PRIMARY:
    default:                     return "dl1-adda";
    }
}

static const char *dl_crossbar_off_path_name(int pcm_device)
{
    switch (pcm_device) {
    case PCM_DEVICE_FAST:        return "dl2-adda-off";
    case PCM_DEVICE_DEEP_BUFFER: return "dl3-adda-off";
    case PCM_DEVICE_PRIMARY:
    default:                     return "dl1-adda-off";
    }
}

static void apply_dl_crossbar(struct mindone_audio_device *adev,
                               struct mindone_stream_out *out)
{
    if (out->crossbar_on)
        return;
    audio_route_apply_path(adev->route, dl_crossbar_path_name(out->pcm_device));
    audio_route_update_mixer(adev->route);
    out->crossbar_on = true;
}

static void teardown_dl_crossbar(struct mindone_audio_device *adev,
                                  struct mindone_stream_out *out)
{
    if (!out->crossbar_on)
        return;
    audio_route_apply_path(adev->route, dl_crossbar_off_path_name(out->pcm_device));
    audio_route_update_mixer(adev->route);
    out->crossbar_on = false;
}


static enum input_device input_device_from_mask(audio_devices_t device)
{
    const audio_devices_t type = device & ~AUDIO_DEVICE_BIT_IN;

    if (type & (AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET & ~AUDIO_DEVICE_BIT_IN))
        return IN_DEVICE_BT_SCO;
    if (type & (AUDIO_DEVICE_IN_WIRED_HEADSET & ~AUDIO_DEVICE_BIT_IN))
        return IN_DEVICE_HEADSET_MIC;
    return IN_DEVICE_MAIN_MIC;
}

static const char *input_path_name(enum input_device d)
{
    switch (d) {
    case IN_DEVICE_HEADSET_MIC: return "headset-mic";
    case IN_DEVICE_BT_SCO:      return NULL;
    case IN_DEVICE_MAIN_MIC:
    default:                    return "mic";
    }
}

static const char *input_path_off_name(enum input_device d)
{
    switch (d) {
    case IN_DEVICE_HEADSET_MIC: return "headset-mic-off";
    case IN_DEVICE_BT_SCO:      return NULL;
    case IN_DEVICE_MAIN_MIC:
    default:                    return "mic-off";
    }
}

static void apply_input_route(struct mindone_audio_device *adev,
                              enum input_device old_device,
                              enum input_device new_device)
{
    enum input_device selected = IN_DEVICE_NONE;
    const char *off, *on;

    if (old_device == new_device)
        return;
    if (old_device != IN_DEVICE_NONE) {
        if (adev->in_ep_users[old_device] <= 0) {
            ALOGW("%s: input %d released more often than acquired", __func__, old_device);
            return;
        }
        adev->in_ep_users[old_device]--;
    }
    if (new_device != IN_DEVICE_NONE)
        adev->in_ep_users[new_device]++;
    if (adev->voice_in_device != IN_DEVICE_NONE &&
        adev->in_ep_users[adev->voice_in_device] > 0)
        selected = adev->voice_in_device;
    else if (adev->in_ep_users[IN_DEVICE_MAIN_MIC] > 0)
        selected = IN_DEVICE_MAIN_MIC;
    else if (adev->in_ep_users[IN_DEVICE_HEADSET_MIC] > 0)
        selected = IN_DEVICE_HEADSET_MIC;
    if (adev->input_routed_device == selected)
        return;
    off = adev->input_routed_device != IN_DEVICE_NONE ?
        input_path_off_name(adev->input_routed_device) : NULL;
    on = selected != IN_DEVICE_NONE ? input_path_name(selected) : NULL;
    if (off)
        audio_route_apply_path(adev->route, off);
    if (on)
        audio_route_apply_path(adev->route, on);
    audio_route_update_mixer(adev->route);
    adev->input_routed_device = selected;
}

static void teardown_input_route(struct mindone_audio_device *adev,
                                 enum input_device device)
{
    apply_input_route(adev, device, IN_DEVICE_NONE);
}

static const char *output_volume_control_name(enum output_device device)
{
    switch (device) {
    case OUT_DEVICE_HEADPHONE: return "Headset Volume";
    case OUT_DEVICE_EARPIECE: return "Handset Volume";
    case OUT_DEVICE_SPEAKER: return "Lineout Volume";
    default: return NULL;
    }
}

static int voice_gain_acquire(struct mindone_audio_device *adev, enum output_device device)
{
    const char *name = output_volume_control_name(device);
    struct mixer_ctl *ctl;
    unsigned int count;

    if (!adev->mixer || !name || adev->voice_gain_ctl)
        return -EINVAL;
    ctl = mixer_get_ctl_by_name(adev->mixer, name);
    if (!ctl)
        return -ENODEV;
    count = mixer_ctl_get_num_values(ctl);
    if (!count || count > sizeof(adev->voice_gain_values) / sizeof(adev->voice_gain_values[0]))
        return -EINVAL;
    for (unsigned int i = 0; i < count; i++) {
        int value = mixer_ctl_get_value(ctl, i);

        if (value < 0)
            return value;
        if (value < mixer_ctl_get_range_min(ctl) || value > mixer_ctl_get_range_max(ctl))
            return -ERANGE;
        adev->voice_gain_values[i] = value;
    }
    adev->voice_gain_ctl = ctl;
    adev->voice_gain_count = count;
    return 0;
}

static void voice_gain_release(struct mindone_audio_device *adev)
{
    int rc;

    if (!adev->voice_gain_ctl)
        return;
    rc = mixer_ctl_set_array(adev->voice_gain_ctl, adev->voice_gain_values,
                             adev->voice_gain_count);
    if (rc)
        ALOGE("%s: failed to restore endpoint gain: %d", __func__, rc);
    adev->voice_gain_ctl = NULL;
    adev->voice_gain_count = 0;
}

static void set_hw_output_volume(struct mindone_audio_device *adev,
                                  enum output_device device, float vol)
{
    const char *ctl_name = output_volume_control_name(device);
    struct mixer_ctl *ctl;
    int min, max, value;
    unsigned int i, num_values;

    if (!adev->mixer || !ctl_name)
        return;

    ctl = mixer_get_ctl_by_name(adev->mixer, ctl_name);
    if (!ctl) {
        ALOGW("%s: mixer control '%s' not found", __func__, ctl_name);
        return;
    }

    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;

    min = mixer_ctl_get_range_min(ctl);
    max = mixer_ctl_get_range_max(ctl);
    value = min + (int)lroundf(vol * (float)(max - min));

    num_values = mixer_ctl_get_num_values(ctl);
    for (i = 0; i < num_values; i++)
        mixer_ctl_set_value(ctl, (int)i, value);
}

#define SCO_PCM_PACKET_BYTES    60
#define SCO_PCM_PACKET_FRAMES   (SCO_PCM_PACKET_BYTES / (int)sizeof(int16_t))
#define SCO_PERIOD_FRAMES       240
#define SCO_PERIOD_COUNT        4

static uint32_t sco_rate(const struct mindone_audio_device *adev)
{
    return adev->bt_wb ? SCO_RATE_WB : SCO_RATE_NB;
}

static void sco_pcm_config(const struct mindone_audio_device *adev, struct pcm_config *c)
{
    memset(c, 0, sizeof(*c));
    c->channels = 1;
    c->rate = sco_rate(adev);
    c->format = PCM_FORMAT_S16_LE;
    c->period_size = SCO_PERIOD_FRAMES;
    c->period_count = SCO_PERIOD_COUNT;
}

static void bt_sco_configure(struct mindone_audio_device *adev)
{
    struct mixer_ctl *ctl;

    if (!adev->mixer)
        return;

    ctl = mixer_get_ctl_by_name(adev->mixer, "BTCVSD Band");
    if (ctl)
        mixer_ctl_set_enum_by_string(ctl, adev->bt_wb ? "WB" : "NB");
    else
        ALOGW("%s: 'BTCVSD Band' not found", __func__);

    ctl = mixer_get_ctl_by_name(adev->mixer, "BTCVSD Tx Mute Switch");
    if (ctl)
        mixer_ctl_set_value(ctl, 0, adev->mic_mute ? 1 : 0);
}

static int output_pcm_device(const struct mindone_stream_out *out, enum output_device dev)
{
    return (dev == OUT_DEVICE_BT_SCO) ? PCM_DEVICE_BTCVSD : out->pcm_device;
}

#define CCCI_AUD_NODE "/dev/ccci_aud"

#define SPH_MSG_ID_SPEECH_ON 12064
#define SPH_MSG_ID_SPEECH_OFF 12065
#define SPH_MSG_ID_SET_SPEECH_MODE 12075

#define SPH_APP_NORMAL_CALL 0

#define SPH_CCCI_CHANNEL 5
#define SPH_CCCI_PAYLOAD_MAGIC 0xA2A2
#define SPH_TASK_SPEECH_ON 25

#define SPH_INFO_PAYLOAD_SIZE 128
#define SPH_INFO_OFF_APP 0
#define SPH_INFO_OFF_BT 1
#define SPH_INFO_OFF_RATE_ENUM 2
#define SPH_INFO_OFF_OPENDSP 3
#define SPH_INFO_OFF_PATH 4
#define SPH_INFO_OFF_PARAM_EMI_VALID 5
#define SPH_INFO_OFF_EMI_PARAM_SIZE 20
#define SPH_INFO_OFF_EMI_PARAM_OFFSET 24
#define SPH_INFO_OFF_SMARTPA_CONFIG 84
#define SPH_INFO_PATH_SHM_CCCI 0
#define SPH_INFO_PATH_SHM_USIP 2
#define SPH_INFO_EMI_EMPTY_VALID 2
#define SPH_INFO_EMI_REAL_VALID 1
#define SPH_INFO_SMARTPA_SINGLE 1
#define SPH_INFO_RATE_48K 3

#define PROP_SPH_PARAM_ENABLE "vendor.audio.mindone.sph_param"

#define CCCI_RAW_AUDIO_NODE "/dev/ccci_raw_audio"
#define SPEECHPARSER_LIB "/vendor/lib64/libspeechparser_vendor.so"

#define CCCI_IOC_MAGIC 'C'
#define CCCI_IOC_SMEM_LEN _IOR(CCCI_IOC_MAGIC, 49, unsigned int)
#define SPH_SHM_SIZE 53248u
#define SPH_SHM_OFF_MD_VERSION 120
#define USIP_NODE "/dev/usip"
#define USIP_IOC_SIZE _IOWR('D', 0xf0, uint64_t)
#define USIP_MAP_SIZE 0x30000u
#define USIP_PARAM_OFFSET 0x4000u
#define USIP_PARAM_SIZE 0x2bfe0u
#define USIP_INIT_CTL "Speech_SHM_USIP"
#define USIP_WIDX_CTL "Speech_SHM_Widx"

#define SP_PARAM_BUF_CAPACITY 49152u
#define SP_IDX_VOLUME_DEFAULT 3u

struct sp_input_attr {
    uint32_t inputDevice;
    uint32_t outputDevice;
    uint32_t idxVolume;
    uint32_t scenario;
    uint32_t featureOn;
    uint16_t ttyMode;
    uint8_t custType;
    uint8_t ipcPath;
    uint8_t extraMode;
    uint8_t memoryIdx;
    uint8_t pad[2];
} __attribute__((packed));

struct sp_out_buf {
    uint32_t memorySize;
    uint32_t dataSize;
    void *bufferAddr;
} __attribute__((packed));

struct sp_kv {
    uint64_t reserved0;
    char *stringAddr;
};

typedef void *(*sp_handle_get_instance_fn)(void);
typedef int (*sp_handle_init_fn)(void *);
typedef int (*sp_get_param_buffer_fn)(void *, const struct sp_input_attr *, struct sp_out_buf *);
typedef int (*sp_key_value_fn)(void *, struct sp_kv *);

struct ccci_sph_mailbox {
    int32_t reserved0;
    uint16_t param16;
    uint16_t msg_id;
    uint32_t channel;
    uint32_t param32;
} __attribute__((packed));

struct ccci_sph_payload_hdr {
    int32_t reserved0;
    uint32_t len_a;
    uint32_t channel;
    uint16_t len_b;
    uint16_t msg_id;
    uint16_t magic;
    uint16_t task;
    uint16_t payload_size;
} __attribute__((packed));

static void build_sph_info(unsigned char *out)
{
    memset(out, 0, SPH_INFO_PAYLOAD_SIZE);
    out[SPH_INFO_OFF_APP] = SPH_APP_NORMAL_CALL;
    out[SPH_INFO_OFF_RATE_ENUM] = SPH_INFO_RATE_48K;
    out[SPH_INFO_OFF_PATH] = SPH_INFO_PATH_SHM_CCCI;
    out[SPH_INFO_OFF_PARAM_EMI_VALID] = SPH_INFO_EMI_EMPTY_VALID;
    out[SPH_INFO_OFF_SMARTPA_CONFIG] = SPH_INFO_SMARTPA_SINGLE;
}

static void speech_bt_info(unsigned char *info, bool wb)
{
    info[SPH_INFO_OFF_BT] = wb ? 4 : 3;
    info[SPH_INFO_OFF_RATE_ENUM] = wb ? 1 : 0;
}

static void sph_put_u32_le(unsigned char *out, int off, uint32_t v)
{
    out[off] = (unsigned char)(v & 0xff);
    out[off + 1] = (unsigned char)((v >> 8) & 0xff);
    out[off + 2] = (unsigned char)((v >> 16) & 0xff);
    out[off + 3] = (unsigned char)((v >> 24) & 0xff);
}

static uint32_t sph_get_u32_le(const volatile unsigned char *in, int off)
{
    return (uint32_t)in[off] | ((uint32_t)in[off + 1] << 8) | ((uint32_t)in[off + 2] << 16) |
           ((uint32_t)in[off + 3] << 24);
}

static void sph_info_set_real_params(unsigned char *info, uint32_t size, uint32_t offset)
{
    info[SPH_INFO_OFF_PATH] = SPH_INFO_PATH_SHM_USIP;
    info[SPH_INFO_OFF_PARAM_EMI_VALID] = SPH_INFO_EMI_REAL_VALID;
    sph_put_u32_le(info, SPH_INFO_OFF_EMI_PARAM_SIZE, size);
    sph_put_u32_le(info, SPH_INFO_OFF_EMI_PARAM_OFFSET, offset);
}

static void sph_input_attr_for_route(enum output_device out, struct sp_input_attr *attr)
{
    memset(attr, 0, sizeof(*attr));
    attr->idxVolume = SP_IDX_VOLUME_DEFAULT;
    attr->scenario = SPH_APP_NORMAL_CALL;

    switch (out) {
    case OUT_DEVICE_SPEAKER:
        attr->outputDevice = AUDIO_DEVICE_OUT_SPEAKER;
        attr->inputDevice = AUDIO_DEVICE_IN_BUILTIN_MIC;
        break;
    case OUT_DEVICE_HEADPHONE:
        attr->outputDevice = AUDIO_DEVICE_OUT_WIRED_HEADSET;
        attr->inputDevice = AUDIO_DEVICE_IN_WIRED_HEADSET;
        break;
    case OUT_DEVICE_BT_SCO:
        attr->outputDevice = AUDIO_DEVICE_OUT_BLUETOOTH_SCO;
        attr->inputDevice = AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
        break;
    case OUT_DEVICE_EARPIECE:
    default:
        attr->outputDevice = AUDIO_DEVICE_OUT_EARPIECE;
        attr->inputDevice = AUDIO_DEVICE_IN_BUILTIN_MIC;
        break;
    }
}

static void sph_memory_barrier(void)
{
#if defined(__aarch64__)
    __asm__ __volatile__("dsb ish" ::: "memory");
#else
    __sync_synchronize();
#endif
}

static void sph_memory_copy(volatile unsigned char *dst, const unsigned char *src, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        dst[i] = src[i];
    sph_memory_barrier();
}

static int sph_md_version(uint32_t *version)
{
    unsigned int len = 0;
    int fd = open(CCCI_RAW_AUDIO_NODE, O_RDONLY | O_CLOEXEC);
    const volatile unsigned char *map;
    int rc = -EIO;

    if (fd < 0)
        return -errno;
    if (ioctl(fd, CCCI_IOC_SMEM_LEN, &len) < 0 || len < SPH_SHM_SIZE)
        goto done;
    map = mmap(NULL, SPH_SHM_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED)
        goto done;
    *version = sph_get_u32_le(map, SPH_SHM_OFF_MD_VERSION) & 0xffffu;
    munmap((void *)map, SPH_SHM_SIZE);
    if (*version && *version != 0xffffu)
        rc = 0;
done:
    close(fd);
    return rc;
}

static int sph_usip_write(struct mindone_audio_device *adev, const unsigned char *data,
                          uint32_t size, uint32_t *offset)
{
    struct mixer_ctl *init_ctl = mixer_get_ctl_by_name(adev->mixer, USIP_INIT_CTL);
    struct mixer_ctl *widx_ctl = mixer_get_ctl_by_name(adev->mixer, USIP_WIDX_CTL);
    volatile unsigned char *map = MAP_FAILED;
    uint64_t len = 0;
    uint32_t widx;
    int fd = -1, rc = -EIO, initialized, value;

    if (!data || !offset || !size || size > USIP_PARAM_SIZE - 16u)
        return -EINVAL;
    if (!init_ctl || !widx_ctl || mixer_ctl_get_num_values(init_ctl) != 1 ||
        mixer_ctl_get_num_values(widx_ctl) != 1 ||
        mixer_ctl_get_type(init_ctl) != MIXER_CTL_TYPE_BOOL ||
        mixer_ctl_get_type(widx_ctl) != MIXER_CTL_TYPE_INT)
        return -ENODEV;
    initialized = mixer_ctl_get_value(init_ctl, 0);
    value = mixer_ctl_get_value(widx_ctl, 0);
    if ((initialized != 0 && initialized != 1) || value < 0 ||
        (uint32_t)value >= USIP_PARAM_SIZE)
        return -EINVAL;
    widx = initialized ? (uint32_t)value : 0;
    fd = open(USIP_NODE, O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -errno;
    if (ioctl(fd, USIP_IOC_SIZE, &len) < 0 || len < USIP_MAP_SIZE)
        goto done;
    map = mmap(NULL, USIP_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED)
        goto done;
    if (!initialized) {
        for (size_t i = 0; i < 32; ++i) {
            map[0xc00u + i] = 0x0a;
            map[0x2ffe0u + i] = 0x0a;
        }
        *(volatile uint32_t *)(map + 0xc20u) = 0xc28u;
        *(volatile uint32_t *)(map + 0xc24u) = USIP_PARAM_OFFSET;
        sph_memory_barrier();
        if (mixer_ctl_set_value(widx_ctl, 0, 0) ||
            mixer_ctl_get_value(widx_ctl, 0) != 0 ||
            mixer_ctl_set_value(init_ctl, 0, 1) ||
            mixer_ctl_get_value(init_ctl, 0) != 1)
            goto done;
    } else if (sph_get_u32_le(map, 0xc20) != 0xc28u ||
               sph_get_u32_le(map, 0xc24) != USIP_PARAM_OFFSET) {
        goto done;
    }
    if (USIP_PARAM_SIZE - widx < size) {
        widx = 0;
        if (mixer_ctl_set_value(widx_ctl, 0, 0) ||
            mixer_ctl_get_value(widx_ctl, 0) != 0)
            goto done;
    }
    sph_memory_copy(map + USIP_PARAM_OFFSET + widx, data, size);
    for (uint32_t i = 0; i < size; ++i) {
        if (map[USIP_PARAM_OFFSET + widx + i] != data[i])
            goto done;
    }
    value = (int)(widx + size);
    if ((uint32_t)value == USIP_PARAM_SIZE)
        value = 0;
    if (mixer_ctl_set_value(widx_ctl, 0, value) ||
        mixer_ctl_get_value(widx_ctl, 0) != value)
        goto done;
    *offset = widx;
    rc = 0;
done:
    if (map != MAP_FAILED)
        munmap((void *)map, USIP_MAP_SIZE);
    close(fd);
    if (rc)
        ALOGE("%s: USIP parameter write failed (%d)", __func__, rc);
    return rc;
}

static int sph_param_fetch_real(struct mindone_audio_device *adev, enum output_device out,
                                unsigned char *info)
{
    void *lib = NULL, *handle;
    struct sp_input_attr attr;
    struct sp_out_buf out_buf;
    unsigned char *param_buf = NULL;
    uint32_t md_version, offset;
    int rc = sph_md_version(&md_version);

    if (rc)
        return rc;
    lib = dlopen(SPEECHPARSER_LIB, RTLD_NOW);
    if (!lib)
        return -ENOENT;
    sp_handle_get_instance_fn get_instance =
        (sp_handle_get_instance_fn)dlsym(lib, "spHandleGetInstance");
    sp_handle_init_fn init_fn = (sp_handle_init_fn)dlsym(lib, "spHandleInit");
    sp_get_param_buffer_fn get_param = (sp_get_param_buffer_fn)dlsym(lib, "getParamBuffer");
    sp_key_value_fn set_kv = (sp_key_value_fn)dlsym(lib, "setKeyValuePair");
    rc = -ENOSYS;
    if (!get_instance || !init_fn || !get_param || !set_kv)
        goto done;
    handle = get_instance();
    rc = -EIO;
    if (!handle || init_fn(handle) <= 0)
        goto done;
    char query[64];
    struct sp_kv kv = { .reserved0 = 0, .stringAddr = query };
    snprintf(query, sizeof(query), "SPEECH_PARSER_SET_PARAM,MDVERSION=%u", md_version);
    if (set_kv(handle, &kv))
        goto done;
    param_buf = calloc(1, SP_PARAM_BUF_CAPACITY);
    if (!param_buf) {
        rc = -ENOMEM;
        goto done;
    }
    sph_input_attr_for_route(out, &attr);
    memset(&out_buf, 0, sizeof(out_buf));
    out_buf.memorySize = SP_PARAM_BUF_CAPACITY;
    out_buf.bufferAddr = param_buf;
    if (get_param(handle, &attr, &out_buf) || out_buf.bufferAddr != param_buf ||
        !out_buf.dataSize || out_buf.dataSize > out_buf.memorySize ||
        out_buf.memorySize > SP_PARAM_BUF_CAPACITY)
        goto done;
    rc = sph_usip_write(adev, param_buf, out_buf.dataSize, &offset);
    if (!rc) {
        sph_info_set_real_params(info, out_buf.dataSize, offset);
        ALOGI("%s: USIP params route=%d size=%u offset=%u", __func__, out,
              out_buf.dataSize, offset);
    }
done:
    free(param_buf);
    dlclose(lib);
    if (rc)
        ALOGE("%s: parameters unavailable (%d)", __func__, rc);
    return rc;
}

static int ccci_send_mailbox(int fd, uint16_t msg_id, uint16_t param16, uint32_t param32)
{
    struct ccci_sph_mailbox m;

    memset(&m, 0, sizeof(m));
    m.reserved0 = -1;
    m.param16 = param16;
    m.msg_id = msg_id;
    m.channel = SPH_CCCI_CHANNEL;
    m.param32 = param32;
    return write(fd, &m, sizeof(m)) == (ssize_t)sizeof(m) ? 0 : -1;
}

static int ccci_send_payload(int fd, uint16_t msg_id, const void *payload, uint16_t payload_size)
{
    unsigned char buf[sizeof(struct ccci_sph_payload_hdr) + SPH_INFO_PAYLOAD_SIZE];
    struct ccci_sph_payload_hdr hdr;
    size_t total = sizeof(hdr) + payload_size;

    if (total > sizeof(buf))
        return -1;
    memset(&hdr, 0, sizeof(hdr));
    hdr.len_a = (uint32_t)payload_size + 6;
    hdr.channel = SPH_CCCI_CHANNEL;
    hdr.len_b = (uint16_t)(payload_size + 6);
    hdr.msg_id = msg_id;
    hdr.magic = SPH_CCCI_PAYLOAD_MAGIC;
    hdr.task = SPH_TASK_SPEECH_ON;
    hdr.payload_size = payload_size;
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), payload, payload_size);
    return write(fd, buf, total) == (ssize_t)total ? 0 : -1;
}

static int64_t sph_monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts))
        return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int ccci_receive(int fd, uint16_t expected, bool stale)
{
    unsigned char buf[4096];
    int64_t now = sph_monotonic_ms();
    if (now < 0)
        return -EIO;
    int64_t deadline = now + (stale ? 200 : 1000);
    for (;;) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        now = sph_monotonic_ms();
        if (now < 0 || now >= deadline)
            return -ETIMEDOUT;
        int timeout = (int)(deadline - now);
        if (stale && timeout > 50)
            timeout = 50;
        int rc = poll(&pfd, 1, timeout);
        if (rc < 0 && errno == EINTR)
            continue;
        if (!rc)
            return stale ? 0 : -ETIMEDOUT;
        if (rc < 0 || !(pfd.revents & POLLIN) ||
            (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
            return -EIO;
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (n < 16)
            return -EIO;
        uint32_t first = sph_get_u32_le(buf, 0);
        uint32_t channel = sph_get_u32_le(buf, 8) & 0xffffu;
        uint32_t args = sph_get_u32_le(buf, first == UINT32_MAX ? 4 : 12);
        uint16_t id = (uint16_t)(args >> 16);
        ALOGI("CCCI RX phase=%s id=0x%04x channel=%u raw16=0x%04x raw32=0x%08x",
              stale ? "stale" : "ack", id, channel, (unsigned int)(args & 0xffffu),
              sph_get_u32_le(buf, 12));
        if (channel != 4 || (first == UINT32_MAX && n != 16) ||
            (first != UINT32_MAX &&
             (n < 26 || sph_get_u32_le(buf, 4) != (uint32_t)n ||
              (sph_get_u32_le(buf, 16) & 0xffffu) != 0x1234u ||
              (sph_get_u32_le(buf, 20) & 0xffffu) + 26u != (uint32_t)n)))
            return -EPROTO;
        if (id == 0xafa0 && first != UINT32_MAX && n == 58 &&
            (sph_get_u32_le(buf, 16) >> 16) == 26) {
            if (ccci_send_mailbox(fd, 0x2fa0, 0, 0))
                return -EIO;
        }
        if (!stale && id == expected)
            return 0;
    }
}

static int speech_rx_pump(struct mindone_audio_device *adev, const char *value)
{
    unsigned char buf[4096];
    unsigned int frames = 0, replies = 0;
    int rc = -EAGAIN;
    int64_t now, deadline;

    if (strcmp(value, "1"))
        return -EINVAL;
    if (!adev->in_call || !adev->speech_on || adev->speech_fd < 0)
        return -ENOTCONN;
    now = sph_monotonic_ms();
    if (now < 0)
        return -EIO;
    deadline = now + 100;
    for (unsigned int attempt = 0; attempt < 8; ++attempt) {
        struct pollfd pfd = { .fd = adev->speech_fd, .events = POLLIN };
        now = sph_monotonic_ms();
        if (now < 0) {
            rc = -EIO;
            break;
        }
        if (now >= deadline)
            break;
        int ready = poll(&pfd, 1, 0);
        if (ready < 0 && errno == EINTR)
            continue;
        if (!ready) {
            rc = 0;
            break;
        }
        if (ready < 0 || !(pfd.revents & POLLIN) ||
            (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            rc = -EIO;
            break;
        }
        ssize_t n = read(adev->speech_fd, buf, sizeof(buf));
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (n < 16) {
            rc = -EIO;
            break;
        }
        ++frames;
        uint32_t first = sph_get_u32_le(buf, 0);
        uint32_t channel = sph_get_u32_le(buf, 8) & 0xffffu;
        uint32_t args = sph_get_u32_le(buf, first == UINT32_MAX ? 4 : 12);
        uint16_t id = (uint16_t)(args >> 16);
        if (channel != 4 || (first == UINT32_MAX && n != 16) ||
            (first != UINT32_MAX &&
             (n < 26 || sph_get_u32_le(buf, 4) != (uint32_t)n ||
              (sph_get_u32_le(buf, 16) & 0xffffu) != 0x1234u ||
              (sph_get_u32_le(buf, 20) & 0xffffu) + 26u != (uint32_t)n))) {
            rc = -EPROTO;
            break;
        }
        ALOGI("CCCI RX phase=pump id=0x%04x bytes=%zd raw16=0x%04x raw32=0x%08x",
              id, n, (unsigned int)(args & 0xffffu), sph_get_u32_le(buf, 12));
        if (id != 0xaf90)
            continue;
        if (first == UINT32_MAX || n != 210 ||
            (sph_get_u32_le(buf, 16) >> 16) != 0x14 ||
            (sph_get_u32_le(buf, 20) >> 16) != 1 ||
            (sph_get_u32_le(buf, 22) >> 16) != 1) {
            ALOGE("CCCI RX phase=pump rejected AF90 layout bytes=%zd", n);
            rc = -EPROTO;
            break;
        }
        now = sph_monotonic_ms();
        if (now < 0 || now >= deadline) {
            rc = now < 0 ? -EIO : -EAGAIN;
            break;
        }
        int write_rc = ccci_send_mailbox(adev->speech_fd, 0x2f90, 0, 0);
        ALOGI("CCCI RX phase=pump AF90 task=20 payload=184 reply=0x2f90 write_rc=%d",
              write_rc);
        if (write_rc) {
            rc = -EIO;
            break;
        }
        ++replies;
    }
    ALOGI("%s: frames=%u replies=%u rc=%d", __func__, frames, replies, rc);
    return rc;
}

static int speech_modem_mute(struct mindone_audio_device *adev, uint16_t message, bool mute)
{
    if (adev->speech_fd < 0)
        return -ENODEV;
    int rc = ccci_receive(adev->speech_fd, 0, true);
    if (rc)
        return rc;
    if (ccci_send_mailbox(adev->speech_fd, message, mute, 0))
        return -EIO;
    return ccci_receive(adev->speech_fd, message | 0x8000, false);
}

static int speech_ccci_off(struct mindone_audio_device *adev)
{
    int rc = 0;
    if (adev->speech_fd < 0)
        return 0;
    int drain_rc = ccci_receive(adev->speech_fd, 0, true);
    if (ccci_send_mailbox(adev->speech_fd, SPH_MSG_ID_SPEECH_OFF, 0, 0))
        rc = -EIO;
    else if (!drain_rc)
        rc = ccci_receive(adev->speech_fd, SPH_MSG_ID_SPEECH_OFF | 0x8000, false);
    else
        rc = drain_rc;
    close(adev->speech_fd);
    adev->speech_fd = -1;
    if (rc)
        ALOGE("%s: SpeechOff not acknowledged (%d)", __func__, rc);
    return rc;
}

static int speech_ccci_set_mode(struct mindone_audio_device *adev, enum output_device out)
{
    unsigned char info[SPH_INFO_PAYLOAD_SIZE];
    int rc;

    if (adev->speech_fd < 0)
        return -ENODEV;
    build_sph_info(info);
    if (out == OUT_DEVICE_BT_SCO)
        speech_bt_info(info, adev->bt_wb);
    if (property_get_bool(PROP_SPH_PARAM_ENABLE, true)) {
        rc = sph_param_fetch_real(adev, out, info);
        if (rc)
            return rc;
    }
    rc = ccci_receive(adev->speech_fd, 0, true);
    if (rc)
        return rc;
    rc = ccci_send_payload(adev->speech_fd, SPH_MSG_ID_SET_SPEECH_MODE,
                           info, sizeof(info));
    if (rc)
        return rc;
    return ccci_receive(adev->speech_fd, SPH_MSG_ID_SET_SPEECH_MODE | 0x8000, false);
}

static int speech_ccci_on(struct mindone_audio_device *adev, enum output_device out)
{
    unsigned char info[SPH_INFO_PAYLOAD_SIZE];
    const uint16_t messages[] = { SPH_MSG_ID_SET_SPEECH_MODE, SPH_MSG_ID_SPEECH_ON };
    bool on_sent = false;
    int rc = 0;

    if (adev->speech_fd >= 0)
        return -EBUSY;
    adev->speech_fd = open(CCCI_AUD_NODE, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (adev->speech_fd < 0)
        return -errno;
    for (size_t i = 0; i < sizeof(messages) / sizeof(messages[0]); ++i) {
        build_sph_info(info);
        if (out == OUT_DEVICE_BT_SCO)
            speech_bt_info(info, adev->bt_wb);
        if (property_get_bool(PROP_SPH_PARAM_ENABLE, true)) {
            rc = sph_param_fetch_real(adev, out, info);
            if (rc)
                break;
        }
        rc = ccci_receive(adev->speech_fd, 0, true);
        if (rc)
            break;
        if (messages[i] == SPH_MSG_ID_SPEECH_ON)
            on_sent = true;
        if (ccci_send_payload(adev->speech_fd, messages[i], info, sizeof(info))) {
            rc = -EIO;
            break;
        }
        rc = ccci_receive(adev->speech_fd, messages[i] | 0x8000, false);
        if (rc)
            break;
    }
    if (!rc && out == OUT_DEVICE_BT_SCO)
        rc = speech_modem_mute(adev, 0x2f02, adev->mic_mute);
    if (rc) {
        ALOGE("%s: speech start failed (%d)", __func__, rc);
        if (on_sent)
            speech_ccci_off(adev);
        else {
            close(adev->speech_fd);
            adev->speech_fd = -1;
        }
    }
    return rc;
}

static struct pcm *speech_hostless_pcm_open(bool capture)
{
    struct pcm_config c;
    struct pcm *pcm;

    memset(&c, 0, sizeof(c));
    c.channels = SPEECH_HOSTLESS_CHANNELS;
    c.rate = SPEECH_HOSTLESS_RATE;
    c.format = PCM_FORMAT_S16_LE;
    c.period_size = SPEECH_HOSTLESS_PERIOD_SIZE;
    c.period_count = SPEECH_HOSTLESS_PERIOD_COUNT;
    c.stop_threshold = ULONG_MAX;
    pcm = pcm_open(PCM_CARD, PCM_DEVICE_SPEECH_HOSTLESS, capture ? PCM_IN : PCM_OUT, &c);
    if (!pcm || !pcm_is_ready(pcm)) {
        ALOGE("%s: pcm_open(device=%d, capture=%d) failed: %s", __func__,
              PCM_DEVICE_SPEECH_HOSTLESS, capture, pcm ? pcm_get_error(pcm) : "unknown");
        if (pcm) {
            pcm_close(pcm);
            pcm = NULL;
        }
        return NULL;
    }
    return pcm;
}

static void speech_hostless_close(struct mindone_audio_device *adev)
{
    if (adev->speech_hostless_out) {
        pcm_close(adev->speech_hostless_out);
        adev->speech_hostless_out = NULL;
    }
    if (adev->speech_hostless_in) {
        pcm_close(adev->speech_hostless_in);
        adev->speech_hostless_in = NULL;
    }
}

static int speech_hostless_open(struct mindone_audio_device *adev)
{
    if (adev->speech_hostless_out && adev->speech_hostless_in)
        return 0;
    if (adev->speech_hostless_out || adev->speech_hostless_in)
        goto fail;
    adev->speech_hostless_in = speech_hostless_pcm_open(true);
    if (!adev->speech_hostless_in)
        goto fail;
    adev->speech_hostless_out = speech_hostless_pcm_open(false);
    if (!adev->speech_hostless_out)
        goto fail;
    if (pcm_start(adev->speech_hostless_out)) {
        ALOGE("%s: playback start failed: %s", __func__,
              pcm_get_error(adev->speech_hostless_out));
        goto fail;
    }
    if (pcm_start(adev->speech_hostless_in)) {
        ALOGE("%s: capture start failed: %s", __func__,
              pcm_get_error(adev->speech_hostless_in));
        goto fail;
    }
    return 0;

fail:
    speech_hostless_close(adev);
    return -EIO;
}

static int voice_route_disable(struct mindone_audio_device *adev, bool speech_off)
{
#if defined(MINDONE_AUDIO_RUST_ROUTES)
    if (adev->in_call) {
        int rc = check_endpoint_transition(adev, adev->voice_out_device, OUT_DEVICE_NONE);
        if (rc)
            return rc;
    }
#endif
    if (adev->in_call && adev->voice_out_device != OUT_DEVICE_BT_SCO) {
        audio_route_apply_path(adev->route, "voice-echo-ref-off");
        audio_route_apply_path(adev->route, "voice-uplink-off");
        audio_route_apply_path(adev->route, "voice-downlink-off");
        enum input_device in = adev->voice_in_device;
        adev->voice_in_device = IN_DEVICE_NONE;
        teardown_input_route(adev, in);
        audio_route_update_mixer(adev->route);
    }

    if (speech_off) {
        if (adev->speech_on)
            speech_ccci_off(adev);
        speech_hostless_close(adev);
        adev->speech_on = false;
    }

    voice_gain_release(adev);
    if (!adev->in_call)
        return 0;
    endpoint_release(adev, adev->voice_out_device);
    adev->voice_out_device = OUT_DEVICE_NONE;
    adev->in_call = false;
    ALOGI("%s: voice route down", __func__);
    return 0;
}

static int voice_route_enable(struct mindone_audio_device *adev, enum output_device out)
{
    int rc = 0;

    if (adev->in_call)
        return 0;
    rc = apply_output_route(adev, adev->voice_out_device, out);
    if (rc)
        return rc;
    adev->voice_out_device = out;
    adev->voice_in_device = out == OUT_DEVICE_BT_SCO ? IN_DEVICE_NONE :
        out == OUT_DEVICE_HEADPHONE ? IN_DEVICE_HEADSET_MIC : IN_DEVICE_MAIN_MIC;
    if (out != OUT_DEVICE_BT_SCO) {
        apply_input_route(adev, IN_DEVICE_NONE, adev->voice_in_device);
        audio_route_apply_path(adev->route, "voice-downlink");
        if (!adev->mic_mute)
            audio_route_apply_path(adev->route, "voice-uplink");
        audio_route_update_mixer(adev->route);
    }
    adev->in_call = true;

    if (out != OUT_DEVICE_BT_SCO)
        rc = speech_hostless_open(adev);
    if (!rc && out != OUT_DEVICE_BT_SCO)
        rc = voice_gain_acquire(adev, out);
    if (!rc && !adev->speech_on)
        rc = speech_ccci_on(adev, out);
    if (rc) {
        voice_route_disable(adev, true);
        return rc;
    }
    adev->speech_on = true;
    set_hw_output_volume(adev, out, adev->voice_volume);
    ALOGI("%s: voice route up on %s", __func__, output_path_name(out) ? output_path_name(out) : "bt-sco");
    return 0;
}

static int voice_route_change_sco(struct mindone_audio_device *adev, enum output_device out)
{
#if defined(MINDONE_AUDIO_RUST_ROUTES)
    int planning_rc = check_endpoint_transition(adev, adev->voice_out_device, out);
    if (planning_rc)
        return planning_rc;
#endif
    int rc = speech_modem_mute(adev, 0x2f03, true);
    if (!rc)
        rc = speech_modem_mute(adev, 0x2f02, true);
    if (!rc)
        rc = voice_route_disable(adev, false);
    if (!rc) {
        speech_hostless_close(adev);
        rc = voice_route_enable(adev, out);
    }
    if (!rc)
        rc = speech_ccci_set_mode(adev, out);
    if (!rc)
        rc = speech_modem_mute(adev, 0x2f02,
                               out == OUT_DEVICE_BT_SCO && adev->mic_mute);
    if (!rc)
        rc = speech_modem_mute(adev, 0x2f03, false);
    if (rc)
        voice_route_disable(adev, true);
    return rc;
}

static enum output_device voice_output_device(struct mindone_audio_device *adev)
{
    if (adev->active_out && adev->active_out->device)
        return output_device_from_mask(adev->active_out->device);
    return OUT_DEVICE_EARPIECE;
}


static uint32_t out_get_sample_rate(const struct mindone_stream_out *out)
{
    return out->config.rate;
}

static size_t out_get_buffer_size(const struct mindone_stream_out *out)
{
    return out->config.period_size * out->frame_size;
}

static int do_out_standby(struct mindone_stream_out *out)
{
    struct mindone_audio_device *adev = out->dev;

    if (out->standby)
        return 0;

    pthread_mutex_lock(&adev->lock);
#if defined(MINDONE_AUDIO_RUST_ROUTES)
    if (out->routed_device != OUT_DEVICE_BT_SCO) {
        int rc = check_endpoint_transition(adev, out->routed_device, OUT_DEVICE_NONE);
        if (rc) {
            pthread_mutex_unlock(&adev->lock);
            return rc;
        }
    }
#endif
    if (out->pcm) {
        pcm_close(out->pcm);
        out->pcm = NULL;
    }

    teardown_dl_crossbar(adev, out);
    if (out->routed_device != OUT_DEVICE_BT_SCO)
        teardown_output_route(adev, out->routed_device);
    if (adev->active_out == out)
        adev->active_out = NULL;
    out->routed_device = OUT_DEVICE_NONE;
    out->standby = true;
    pthread_mutex_unlock(&adev->lock);
    memset(out->hp_x1, 0, sizeof(out->hp_x1));
    memset(out->hp_x2, 0, sizeof(out->hp_x2));
    memset(out->hp_y1, 0, sizeof(out->hp_y1));
    memset(out->hp_y2, 0, sizeof(out->hp_y2));
    out->lim_gain = 1.0f;
    out->sco_staged = 0;
    out->sco_acc = 0;
    out->sco_acc_n = 0;
    return 0;
}

static int out_standby(struct mindone_stream_out *out)
{
    int ret;

    pthread_mutex_lock(&out->lock);
    ret = do_out_standby(out);
    pthread_mutex_unlock(&out->lock);
    return ret;
}

static int out_aidl_state(struct mindone_stream_out *out, enum mindone_audio_command command);

static int out_set_route(struct mindone_stream_out *out, audio_devices_t new_mask)
{
    struct mindone_audio_device *adev = out->dev;
    int rc = 0;
    bool restart = false;

    pthread_mutex_lock(&out->lock);
    pthread_mutex_lock(&adev->lock);
    if (!out->standby) {
        enum output_device new_dev = output_device_from_mask(new_mask);

        restart = (new_dev == OUT_DEVICE_BT_SCO) !=
                  (out->routed_device == OUT_DEVICE_BT_SCO);
        if (!restart) {
            rc = apply_output_route(adev, out->routed_device, new_dev);
            if (!rc)
                out->routed_device = new_dev;
        }
    }
    if (!rc && !restart)
        out->device = new_mask;
    pthread_mutex_unlock(&adev->lock);
    if (restart) {
        rc = do_out_standby(out);
        if (!rc) {
            pthread_mutex_lock(&adev->lock);
            out->device = new_mask;
            pthread_mutex_unlock(&adev->lock);
        }
    }
    pthread_mutex_unlock(&out->lock);
    return rc;
}

static uint32_t out_get_latency(const struct mindone_stream_out *out)
{
    return (out->config.period_size * out->config.period_count * 1000) /
           out->config.rate;
}

static int out_set_volume(struct mindone_stream_out *out, float left, float right)
{

    if (!isfinite(left) || !isfinite(right) || left < 0.0f || left > 1.0f ||
        right < 0.0f || right > 1.0f)
        return -EINVAL;
    pthread_mutex_lock(&out->lock);
    out->volume_left = left;
    out->volume_right = right;
    pthread_mutex_unlock(&out->lock);
    return 0;
}

static int start_output_stream(struct mindone_stream_out *out)
{
    struct mindone_audio_device *adev = out->dev;
    enum output_device new_dev;
    struct pcm_config hw_config;
    int pcm_dev;

    pthread_mutex_lock(&adev->lock);
    new_dev = output_device_from_mask(out->device);
    pcm_dev = output_pcm_device(out, new_dev);
    if (new_dev == OUT_DEVICE_BT_SCO)
        sco_pcm_config(adev, &hw_config);
    else
        hw_config = out->config;
    pthread_mutex_unlock(&adev->lock);

    struct pcm *pcm = pcm_open(PCM_CARD, pcm_dev, PCM_OUT | PCM_MONOTONIC, &hw_config);
    if (!pcm || !pcm_is_ready(pcm)) {
        ALOGE("%s: pcm_open(device=%d) failed: %s", __func__,
              pcm_dev, pcm ? pcm_get_error(pcm) : "unknown");
        if (pcm)
            pcm_close(pcm);
        return -ENODEV;
    }

    hw_config = *pcm_get_config(pcm);
    pthread_mutex_lock(&adev->lock);
    if (new_dev != output_device_from_mask(out->device) ||
        (new_dev == OUT_DEVICE_BT_SCO && hw_config.rate != sco_rate(adev))) {
        pthread_mutex_unlock(&adev->lock);
        pcm_close(pcm);
        return -EAGAIN;
    }
#if defined(MINDONE_AUDIO_RUST_ROUTES)
    if (new_dev != OUT_DEVICE_BT_SCO) {
        int rc = check_endpoint_transition(adev, OUT_DEVICE_NONE, new_dev);
        if (rc) {
            pthread_mutex_unlock(&adev->lock);
            pcm_close(pcm);
            return rc;
        }
    }
#endif
    if (new_dev == OUT_DEVICE_BT_SCO) {
        bt_sco_configure(adev);
    } else {
        apply_dl_crossbar(adev, out);
        apply_output_route(adev, OUT_DEVICE_NONE, new_dev);
    }
    out->pcm = pcm;
    out->hw_config = hw_config;
    out->reopen_pending = false;
    adev->active_out = out;
    out->routed_device = new_dev;
    out->standby = false;
    pthread_mutex_unlock(&adev->lock);

    return 0;
}

#define SPK_HPF_HZ          180.0f
#define SPK_LIMIT_CEILING   0.89f
#define SPK_LIM_ATTACK      0.002f
#define SPK_LIM_RELEASE     0.00002f

static void speaker_dsp_process(struct mindone_stream_out *out, int16_t *data, size_t frames)
{
    const int ch = (int)out->config.channels;
    float b0, b1, b2, a1, a2;
    size_t i;
    int c;

    if (out->hp_rate != out->config.rate) {
        const float w = 2.0f * (float)M_PI * SPK_HPF_HZ / (float)out->config.rate;
        const float cosw = cosf(w), sinw = sinf(w);
        const float alpha = sinw / 1.41421356f;
        const float a0 = 1.0f + alpha;

        out->hp_b0 = ((1.0f + cosw) * 0.5f) / a0;
        out->hp_b1 = (-(1.0f + cosw)) / a0;
        out->hp_b2 = out->hp_b0;
        out->hp_a1 = (-2.0f * cosw) / a0;
        out->hp_a2 = (1.0f - alpha) / a0;
        out->hp_rate = out->config.rate;
    }
    b0 = out->hp_b0; b1 = out->hp_b1; b2 = out->hp_b2;
    a1 = out->hp_a1; a2 = out->hp_a2;

    for (i = 0; i < frames; i++) {
        float peak = 0.0f;
        float v[OUT_CHANNEL_COUNT];

        for (c = 0; c < ch && c < OUT_CHANNEL_COUNT; c++) {
            const float x = (float)data[i * ch + c] / 32768.0f;
            float y = b0 * x + b1 * out->hp_x1[c] + b2 * out->hp_x2[c]
                      - a1 * out->hp_y1[c] - a2 * out->hp_y2[c];

            out->hp_x2[c] = out->hp_x1[c];
            out->hp_x1[c] = x;
            out->hp_y2[c] = out->hp_y1[c];
            out->hp_y1[c] = y;

            v[c] = y;
            if (fabsf(y) > peak)
                peak = fabsf(y);
        }

        {
            const float want = (peak * out->lim_gain > SPK_LIMIT_CEILING && peak > 0.0f)
                               ? SPK_LIMIT_CEILING / peak : 1.0f;
            if (want < out->lim_gain)
                out->lim_gain -= SPK_LIM_ATTACK;
            else
                out->lim_gain += SPK_LIM_RELEASE;
            if (out->lim_gain > 1.0f)
                out->lim_gain = 1.0f;
            if (out->lim_gain < 0.05f)
                out->lim_gain = 0.05f;
        }

        for (c = 0; c < ch && c < OUT_CHANNEL_COUNT; c++) {
            float y = v[c] * out->lim_gain * 32768.0f;
            if (y > 32767.0f)
                y = 32767.0f;
            else if (y < -32768.0f)
                y = -32768.0f;
            data[i * ch + c] = (int16_t)y;
        }
    }
}

static int sco_out_convert(struct mindone_stream_out *out, const int16_t *in, size_t frames)
{
    const unsigned int ratio = out->config.rate / out->hw_config.rate;
    const unsigned int ch = out->config.channels;
    const size_t need = out->sco_staged + frames / ratio + 1;
    size_t i;

    if (out->sco_buf_frames < need) {
        int16_t *nb = (int16_t *)realloc(out->sco_buf, need * sizeof(int16_t));
        if (!nb)
            return -ENOMEM;
        out->sco_buf = nb;
        out->sco_buf_frames = need;
    }

    for (i = 0; i < frames; i++) {
        int32_t mono = in[i * ch];
        if (ch > 1)
            mono = (mono + in[i * ch + 1]) / 2;
        out->sco_acc += mono;
        if (++out->sco_acc_n == ratio) {
            out->sco_buf[out->sco_staged++] = (int16_t)(out->sco_acc / (int32_t)ratio);
            out->sco_acc = 0;
            out->sco_acc_n = 0;
        }
    }
    return (int)out->sco_staged;
}

static int pcm_transfer_frames(struct pcm *pcm, const void *buffer, size_t frames,
                               size_t frame_size, bool input)
{
    if (!frame_size || frames > SIZE_MAX / frame_size || (!buffer && frames))
        return -EINVAL;
    unsigned char *data = (unsigned char *)buffer;
    bool recovered = false;
    while (frames) {
        unsigned int count = frames > INT_MAX ? INT_MAX : (unsigned int)frames;
        int rc = input ? pcm_readi(pcm, data, count) : pcm_writei(pcm, data, count);
        if (rc < 0) {
            if (!input && !recovered) {
                recovered = true;
                if (pcm_prepare(pcm) == 0)
                    continue;
            }
            return rc;
        }
        if (!rc || (unsigned int)rc > count)
            return -EIO;
        data += (size_t)rc * frame_size;
        frames -= (unsigned int)rc;
    }
    return 0;
}

static int sco_out_flush(struct mindone_stream_out *out)
{
    const size_t whole = (out->sco_staged / SCO_PCM_PACKET_FRAMES) * SCO_PCM_PACKET_FRAMES;
    int ret;

    if (whole == 0)
        return 0;
    ret = pcm_transfer_frames(out->pcm, out->sco_buf, whole, sizeof(int16_t), false);
    if (ret)
        return ret;
    out->sco_staged -= whole;
    if (out->sco_staged)
        memmove(out->sco_buf, out->sco_buf + whole, out->sco_staged * sizeof(int16_t));
    return 0;
}

static int pcm_write_recovering(struct mindone_stream_out *out, const void *buffer, size_t bytes)
{
    return pcm_transfer_frames(out->pcm, buffer, bytes / out->frame_size,
                               out->frame_size, false);
}

static void output_volume_process(struct mindone_stream_out *out, int16_t *data,
                                   size_t frames)
{
    const unsigned int channels = out->config.channels;

    for (size_t i = 0; i < frames; i++) {
        data[i * channels] = (int16_t)((float)data[i * channels] * out->volume_left);
        if (channels > 1)
            data[i * channels + 1] =
                (int16_t)((float)data[i * channels + 1] * out->volume_right);
    }
}

static int aidl_pcm_ioctl(struct pcm *pcm, int request, void *arg)
{
    errno = 0;
    int rc = ioctl(pcm_get_file_descriptor(pcm), request, arg);
    return rc < 0 && errno ? -errno : rc;
}

static int aidl_pcm_state(struct pcm *pcm)
{
    struct snd_pcm_status status = {0};
    int rc = aidl_pcm_ioctl(pcm, SNDRV_PCM_IOCTL_STATUS, &status);
    return rc ? rc : status.state;
}

static int out_aidl_position(struct mindone_stream_out *out, uint64_t *frames,
                             struct timespec *timestamp)
{
    if (out->aidl_paused || out->standby) {
        if (!out->aidl_timestamp.tv_sec && !out->aidl_timestamp.tv_nsec)
            return -ENODATA;
        *frames = out->aidl_presented;
        *timestamp = out->aidl_timestamp;
        return 0;
    }
    snd_pcm_sframes_t delay = 0;
    int rc = aidl_pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_DELAY, &delay);
    if (rc)
        return rc;
    if (clock_gettime(CLOCK_MONOTONIC, timestamp))
        return -errno;
    uint64_t queued = delay > 0 ? (uint64_t)delay : 0;
    queued += out->sco_staged;
    queued = queued * out->config.rate / out->hw_config.rate + out->sco_acc_n;
    uint64_t submitted = out->written_frames - out->aidl_discarded;
    uint64_t presented = submitted - (queued < submitted ? queued : submitted);
    if (presented > out->aidl_presented)
        out->aidl_presented = presented;
    out->aidl_timestamp = *timestamp;
    *frames = out->aidl_presented;
    return 0;
}

static int out_aidl_run(struct mindone_stream_out *out)
{
    int rc = pcm_prepare(out->pcm);
    if (rc)
        return rc;
    struct snd_pcm_sw_params sw = {0};
    sw.tstamp_mode = SNDRV_PCM_TSTAMP_ENABLE;
    sw.period_step = 1;
    sw.avail_min = out->hw_config.period_size;
    sw.start_threshold = 1;
    sw.stop_threshold = ULONG_MAX;
    sw.silence_size = ULONG_MAX;
    sw.proto = SNDRV_PROTOCOL_VERSION(2, 0, 12);
    sw.tstamp_type = SNDRV_PCM_TSTAMP_TYPE_MONOTONIC;
    rc = aidl_pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_SW_PARAMS, &sw);
    if (rc)
        return rc;
    return pcm_start(out->pcm);
}

static int out_aidl_align(struct mindone_stream_out *out)
{
    snd_pcm_sframes_t delay = 0;
    int rc = aidl_pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_DELAY, &delay);
    if (rc || delay >= 0)
        return rc;
    snd_pcm_uframes_t frames = (snd_pcm_uframes_t)(-(delay + 1)) + 1;
    return aidl_pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_FORWARD, &frames);
}

static int out_aidl_tail(struct mindone_stream_out *out)
{
    if (out->routed_device != OUT_DEVICE_BT_SCO ||
        (!out->sco_staged && !out->sco_acc_n))
        return 0;
    size_t count = out->sco_staged + (out->sco_acc_n != 0);
    size_t padded = (count + SCO_PCM_PACKET_FRAMES - 1) /
                    SCO_PCM_PACKET_FRAMES * SCO_PCM_PACKET_FRAMES;
    if (out->sco_buf_frames < padded) {
        int16_t *next = realloc(out->sco_buf, padded * sizeof(*next));
        if (!next)
            return -ENOMEM;
        out->sco_buf = next;
        out->sco_buf_frames = padded;
    }
    if (out->sco_acc_n)
        out->sco_buf[out->sco_staged++] = out->sco_acc / (int32_t)out->sco_acc_n;
    memset(out->sco_buf + out->sco_staged, 0,
           (padded - out->sco_staged) * sizeof(*out->sco_buf));
    out->sco_staged = padded;
    out->sco_acc = 0;
    out->sco_acc_n = 0;
    return sco_out_flush(out);
}

static int out_aidl_get_position(struct mindone_stream_out *out, uint64_t *frames,
                                 struct timespec *timestamp)
{
    if (!frames || !timestamp)
        return -EINVAL;
    pthread_mutex_lock(&out->lock);
    int rc = out_aidl_position(out, frames, timestamp);
    pthread_mutex_unlock(&out->lock);
    return rc;
}

static int out_aidl_state(struct mindone_stream_out *out, enum mindone_audio_command command)
{
    if ((unsigned int)command > MINDONE_AUDIO_TRANSFER)
        return -EINVAL;
    pthread_mutex_lock(&out->lock);
    int rc = 0;
    if (command == MINDONE_AUDIO_STANDBY) {
        if (!out->standby)
            out_aidl_position(out, &out->aidl_presented, &out->aidl_timestamp);
        rc = do_out_standby(out);
        if (!rc) {
            out->aidl_discarded = out->written_frames - out->aidl_presented;
            out->aidl_paused = false;
            out->aidl_hw_paused = false;
        }
    } else if (command == MINDONE_AUDIO_START || command == MINDONE_AUDIO_TRANSFER) {
        pthread_mutex_lock(&out->dev->lock);
        bool reopen = out->reopen_pending;
        pthread_mutex_unlock(&out->dev->lock);
        if (reopen && !out->standby) {
            out_aidl_position(out, &out->aidl_presented, &out->aidl_timestamp);
            rc = do_out_standby(out);
        }
        bool opening = out->standby;
        if (!rc && opening) {
            out->aidl_discarded = out->written_frames - out->aidl_presented;
            out->aidl_paused = false;
            out->aidl_hw_paused = false;
            rc = start_output_stream(out);
        }
        if (!rc && out->aidl_hw_paused)
            rc = aidl_pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_PAUSE, (void *)(uintptr_t)0);
        else if (!rc && (opening || out->aidl_paused ||
                         aidl_pcm_state(out->pcm) != PCM_STATE_RUNNING))
            rc = out_aidl_run(out);
        if (!rc && command == MINDONE_AUDIO_TRANSFER)
            rc = out_aidl_align(out);
        if (!rc) {
            out->aidl_paused = false;
            out->aidl_hw_paused = false;
        } else {
            do_out_standby(out);
        }
    } else if (out->standby || !out->pcm) {
        rc = -ENODEV;
    } else if (command == MINDONE_AUDIO_PAUSE || command == MINDONE_AUDIO_FLUSH) {
        uint64_t frames;
        struct timespec timestamp;
        rc = out_aidl_position(out, &frames, &timestamp);
        if (!rc && command == MINDONE_AUDIO_PAUSE) {
            rc = aidl_pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_PAUSE, (void *)(uintptr_t)1);
            if (!rc) {
                out->aidl_hw_paused = true;
            } else if (rc == -EINVAL || rc == -ENOTTY || rc == -ENOSYS ||
                       rc == -EOPNOTSUPP) {
                rc = pcm_stop(out->pcm);
                if (!rc) {
                    out->aidl_discarded = out->written_frames - out->aidl_presented;
                    out->sco_staged = 0;
                    out->sco_acc = 0;
                    out->sco_acc_n = 0;
                }
            }
            if (!rc)
                out->aidl_paused = true;
        } else if (!rc) {
            rc = pcm_stop(out->pcm);
            if (!rc) {
                out->aidl_discarded = out->written_frames - out->aidl_presented;
                out->sco_staged = 0;
                out->sco_acc = 0;
                out->sco_acc_n = 0;
                out->aidl_paused = false;
                out->aidl_hw_paused = false;
                rc = out_aidl_run(out);
            }
        }
    } else {
        rc = out_aidl_align(out);
        if (!rc)
            rc = out_aidl_tail(out);
        struct timespec begin;
        if (!rc && clock_gettime(CLOCK_MONOTONIC, &begin))
            rc = -errno;
        while (!rc) {
            uint64_t frames;
            struct timespec timestamp;
            rc = out_aidl_position(out, &frames, &timestamp);
            if (rc || frames >= out->written_frames - out->aidl_discarded)
                break;
            if (timestamp.tv_sec - begin.tv_sec >= 2) {
                rc = -ETIMEDOUT;
                break;
            }
            usleep(5000);
        }
    }
    pthread_mutex_unlock(&out->lock);
    return rc;
}

static ssize_t out_write(struct mindone_stream_out *out, const void *buffer,
                          size_t bytes)
{
    size_t frames, frame_size;
    int ret;
    enum output_device routed_device;

    pthread_mutex_lock(&out->lock);
    frame_size = out->frame_size;
    if (!frame_size || bytes % frame_size || (!buffer && bytes) ||
        !out->config.channels || out->config.channels > OUT_CHANNEL_COUNT) {
        ret = -EINVAL;
        goto written;
    }
    if (!bytes) {
        ret = 0;
        goto written;
    }
    frames = bytes / frame_size;
    pthread_mutex_lock(&out->dev->lock);
    ret = out->reopen_pending && !out->standby;
    pthread_mutex_unlock(&out->dev->lock);
    if (ret) {
        ret = do_out_standby(out);
        if (ret)
            goto written;
    }

    if (out->standby) {
        ret = start_output_stream(out);
        if (ret != 0)
            goto written;
    }

    pthread_mutex_lock(&out->dev->lock);
    routed_device = out->routed_device;
    pthread_mutex_unlock(&out->dev->lock);

    if (routed_device == OUT_DEVICE_SPEAKER ||
        out->volume_left != 1.0f || out->volume_right != 1.0f) {
        if (out->dsp_frames < frames) {
            int16_t *next = realloc(out->dsp_buf, bytes);

            if (!next) {
                ret = -ENOMEM;
                goto written;
            }
            out->dsp_buf = next;
            out->dsp_frames = frames;
        }
        memcpy(out->dsp_buf, buffer, bytes);
        if (routed_device == OUT_DEVICE_SPEAKER)
            speaker_dsp_process(out, out->dsp_buf, frames);
        output_volume_process(out, out->dsp_buf, frames);
        buffer = out->dsp_buf;
    }

    if (routed_device == OUT_DEVICE_BT_SCO) {
        ret = sco_out_convert(out, buffer, frames);
        if (ret >= 0)
            ret = sco_out_flush(out);
    } else {
        ret = pcm_write_recovering(out, buffer, bytes);
    }
    if (ret == 0)
        out->written_frames += frames;
written:
    pthread_mutex_unlock(&out->lock);
    return (ret == 0) ? (ssize_t)bytes : ret;
}


static uint32_t in_get_sample_rate(const struct mindone_stream_in *in)
{
    return in->config.rate;
}

static size_t in_get_buffer_size(const struct mindone_stream_in *in)
{
    return in->config.period_size * in->config.period_count *
           in->frame_size;
}

static int do_in_standby(struct mindone_stream_in *in)
{
    struct mindone_audio_device *adev = in->dev;

    if (in->standby)
        return 0;

    if (in->pcm) {
        pcm_close(in->pcm);
        in->pcm = NULL;
    }

    pthread_mutex_lock(&adev->lock);
    teardown_input_route(adev, in->routed_device);
    if (adev->active_in == in)
        adev->active_in = NULL;
    in->routed_device = IN_DEVICE_NONE;
    in->standby = true;
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int in_standby(struct mindone_stream_in *in)
{
    int ret;

    pthread_mutex_lock(&in->lock);
    ret = do_in_standby(in);
    pthread_mutex_unlock(&in->lock);
    return ret;
}

static int in_aidl_start(struct mindone_stream_in *in);

static int in_set_route(struct mindone_stream_in *in, audio_devices_t new_mask)
{
    struct mindone_audio_device *adev = in->dev;
    bool restart = false;

    pthread_mutex_lock(&in->lock);
    pthread_mutex_lock(&adev->lock);
    in->device = new_mask;
    if (!in->standby) {
        enum input_device new_dev = input_device_from_mask(new_mask);

        restart = (new_dev == IN_DEVICE_BT_SCO) !=
                  (in->routed_device == IN_DEVICE_BT_SCO);
        if (!restart) {
            apply_input_route(adev, in->routed_device, new_dev);
            in->routed_device = new_dev;
        }
    }
    pthread_mutex_unlock(&adev->lock);
    if (restart)
        do_in_standby(in);
    pthread_mutex_unlock(&in->lock);
    return 0;
}

static int in_set_gain(struct mindone_stream_in *in, float gain)
{
    struct mindone_audio_device *adev = in->dev;
    struct mixer_ctl *ctl_l, *ctl_r;
    int min, max, value;

    if (!adev->mixer)
        return 0;

    ctl_l = mixer_get_ctl_by_name(adev->mixer, "PGA1 Volume");
    ctl_r = mixer_get_ctl_by_name(adev->mixer, "PGA2 Volume");
    if (!ctl_l || !ctl_r)
        return 0;

    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;

    min = mixer_ctl_get_range_min(ctl_l);
    max = mixer_ctl_get_range_max(ctl_l);
    value = min + (int)lroundf(gain * (float)(max - min));

    mixer_ctl_set_value(ctl_l, 0, value);
    mixer_ctl_set_value(ctl_r, 0, value);
    return 0;
}

static int start_input_stream(struct mindone_stream_in *in)
{
    struct mindone_audio_device *adev = in->dev;
    enum input_device new_dev;
    int pcm_dev;

    pthread_mutex_lock(&adev->lock);
    new_dev = input_device_from_mask(in->device);
    if (new_dev == IN_DEVICE_BT_SCO) {
        pcm_dev = PCM_DEVICE_BTCVSD;
        sco_pcm_config(adev, &in->hw_config);
    } else {
        pcm_dev = PCM_DEVICE_CAPTURE_MAIN;
        in->hw_config = in->config;
    }
    in->reopen_pending = false;
    pthread_mutex_unlock(&adev->lock);

    in->pcm = pcm_open(PCM_CARD, pcm_dev, PCM_IN, &in->hw_config);
    if (!in->pcm || !pcm_is_ready(in->pcm)) {
        ALOGE("%s: pcm_open(device=%d) failed: %s", __func__,
              pcm_dev, in->pcm ? pcm_get_error(in->pcm) : "unknown");
        if (in->pcm) {
            pcm_close(in->pcm);
            in->pcm = NULL;
        }
        return -ENODEV;
    }

    in->hw_config = *pcm_get_config(in->pcm);
    pthread_mutex_lock(&adev->lock);
    if (new_dev != input_device_from_mask(in->device) ||
        (new_dev == IN_DEVICE_BT_SCO && in->hw_config.rate != sco_rate(adev))) {
        pthread_mutex_unlock(&adev->lock);
        pcm_close(in->pcm);
        in->pcm = NULL;
        return -EAGAIN;
    }
    if (new_dev == IN_DEVICE_BT_SCO)
        bt_sco_configure(adev);
    apply_input_route(adev, IN_DEVICE_NONE, new_dev);
    adev->active_in = in;
    in->routed_device = new_dev;
    in->standby = false;
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int in_aidl_start(struct mindone_stream_in *in)
{
    pthread_mutex_lock(&in->lock);
    int rc = 0;
    if (in->standby)
        rc = start_input_stream(in);
    if (!rc && aidl_pcm_state(in->pcm) != PCM_STATE_RUNNING)
        rc = pcm_prepare(in->pcm);
    if (!rc && aidl_pcm_state(in->pcm) != PCM_STATE_RUNNING)
        rc = pcm_start(in->pcm);
    if (rc)
        do_in_standby(in);
    pthread_mutex_unlock(&in->lock);
    return rc;
}

static ssize_t in_read(struct mindone_stream_in *in, void *buffer, size_t bytes)
{
    ssize_t result = (ssize_t)bytes;
    int ret;
    enum input_device routed_device;

    pthread_mutex_lock(&in->lock);
    const size_t frame_size = in->frame_size;
    if (!frame_size || bytes % frame_size || (!buffer && bytes)) {
        result = -EINVAL;
        goto done;
    }
    if (!bytes)
        goto done;
    pthread_mutex_lock(&in->dev->lock);
    ret = in->reopen_pending && !in->standby;
    pthread_mutex_unlock(&in->dev->lock);
    if (ret) {
        ret = do_in_standby(in);
        if (ret) {
            result = ret < 0 ? ret : -EIO;
            goto done;
        }
    }

    if (in->standby) {
        ret = start_input_stream(in);
        if (ret != 0) {
            result = ret < 0 ? ret : -EIO;
            goto done;
        }
    }

    pthread_mutex_lock(&in->dev->lock);
    routed_device = in->routed_device;
    pthread_mutex_unlock(&in->dev->lock);

    if (routed_device == IN_DEVICE_BT_SCO) {
        if (!in->hw_config.rate || in->config.rate < in->hw_config.rate ||
            in->config.rate % in->hw_config.rate) {
            result = -EINVAL;
            goto done;
        }
        const unsigned int ratio = in->config.rate / in->hw_config.rate;
        const size_t frames = bytes / sizeof(int16_t);
        const size_t src_frames = frames / ratio;
        int16_t *dst = (int16_t *)buffer;
        size_t i;

        if (frames % ratio) {
            result = -EINVAL;
            goto done;
        }
        if (in->sco_in_frames < src_frames) {
            int16_t *nb = (int16_t *)realloc(in->sco_in_buf, src_frames * sizeof(int16_t));
            if (!nb) {
                result = -ENOMEM;
                goto done;
            }
            in->sco_in_buf = nb;
            in->sco_in_frames = src_frames;
        }
        ret = pcm_transfer_frames(in->pcm, in->sco_in_buf, src_frames, sizeof(int16_t), true);
        if (ret != 0) {
            result = ret < 0 ? ret : -EIO;
            goto done;
        }
        for (i = 0; i < src_frames * ratio; i++) {
            const size_t k = i / ratio;
            const int32_t prev = (k == 0) ? in->sco_last : in->sco_in_buf[k - 1];
            const int32_t cur = in->sco_in_buf[k];
            const int32_t step = (int32_t)(i % ratio) + 1;
            dst[i] = (int16_t)(prev + (cur - prev) * step / (int32_t)ratio);
        }
        if (src_frames)
            in->sco_last = in->sco_in_buf[src_frames - 1];
        in->read_frames += frames;
        goto done;
    }


    if (in->config.channels == 1) {
        ret = pcm_transfer_frames(in->pcm, buffer, bytes / in->frame_size,
                                  in->frame_size, true);
        if (ret == 0)
            in->read_frames += bytes / in->frame_size;
        else
            result = ret < 0 ? ret : -EIO;
        goto done;
    }

    {
        const size_t frames = bytes / sizeof(int16_t);
        const size_t need = frames * in->config.channels;
        int16_t *dst = (int16_t *)buffer;
        size_t i;

        if (in->downmix_frames < frames) {
            int16_t *nb = (int16_t *)realloc(in->downmix_buf, need * sizeof(int16_t));
            if (!nb) {
                result = -ENOMEM;
                goto done;
            }
            in->downmix_buf = nb;
            in->downmix_frames = frames;
        }

        ret = pcm_transfer_frames(in->pcm, in->downmix_buf, frames,
                                  in->config.channels * sizeof(int16_t), true);
        if (ret != 0) {
            result = ret < 0 ? ret : -EIO;
            goto done;
        }

        for (i = 0; i < frames; i++) {
            const int32_t l = in->downmix_buf[i * in->config.channels];
            const int32_t r = in->downmix_buf[i * in->config.channels + 1];
            int32_t v = ((l + r) / 2) * IN_DIGITAL_GAIN_Q8 >> 8;

            if (v > 32767)
                v = 32767;
            else if (v < -32768)
                v = -32768;
            dst[i] = (int16_t)v;
        }
        in->read_frames += frames;
    }

done:
    pthread_mutex_unlock(&in->lock);
    return result;
}

static int in_get_capture_position(const struct mindone_stream_in *stream,
                                    int64_t *frames, int64_t *time)
{
    if (!stream || !frames || !time)
        return -EINVAL;
    return -ENOSYS;
}

static int open_output_stream(struct mindone_audio_device *adev,
                                    audio_io_handle_t handle,
                                    audio_devices_t devices,
                                    audio_output_flags_t flags,
                                    struct audio_config *config,
                                    struct mindone_stream_out **stream_out)
{
    struct mindone_stream_out *out;
    int rc;

    if (!stream_out)
        return -EINVAL;
    *stream_out = NULL;
    if (!adev || !config)
        return -EINVAL;
    if (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)
        return -ENOSYS;

    out = (struct mindone_stream_out *)calloc(1, sizeof(*out));
    if (!out)
        return -ENOMEM;
    out->io_handle = handle;

    if (flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) {
        out->pcm_device = PCM_DEVICE_DEEP_BUFFER;
    } else if (flags & AUDIO_OUTPUT_FLAG_FAST) {
        out->pcm_device = PCM_DEVICE_FAST;
    } else {
        out->pcm_device = PCM_DEVICE_PRIMARY;
    }

    out->config.channels = OUT_CHANNEL_COUNT;
    out->config.rate = OUT_SAMPLING_RATE;
    out->config.format = PCM_FORMAT_S16_LE;
    switch (out->pcm_device) {
    case PCM_DEVICE_DEEP_BUFFER:
        out->config.period_size = OUT_DEEP_PERIOD_SIZE;
        out->config.period_count = OUT_DEEP_PERIOD_COUNT;
        break;
    case PCM_DEVICE_FAST:
        out->config.period_size = OUT_FAST_PERIOD_SIZE;
        out->config.period_count = OUT_FAST_PERIOD_COUNT;
        break;
    default:
        out->config.period_size = OUT_PERIOD_SIZE;
        out->config.period_count = OUT_PERIOD_COUNT;
        break;
    }

    out->frame_size = OUT_CHANNEL_COUNT * sizeof(int16_t);

    out->dev = adev;
    out->flags = flags;
    out->device = devices;
    out->standby = true;
    out->routed_device = OUT_DEVICE_NONE;
    out->crossbar_on = false;
    out->hw_config = out->config;
    out->lim_gain = 1.0f;
    out->volume_left = 1.0f;
    out->volume_right = 1.0f;
    rc = pthread_mutex_init(&out->lock, NULL);
    if (rc) {
        free(out);
        return -rc;
    }

    config->format = AUDIO_FORMAT_PCM_16_BIT;
    config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    config->sample_rate = OUT_SAMPLING_RATE;

    pthread_mutex_lock(&adev->lock);
    struct mindone_stream_out **link = &adev->outs;
    while (*link)
        link = &(*link)->next;
    *link = out;
    pthread_mutex_unlock(&adev->lock);

    *stream_out = out;
    return 0;
}

static void close_output_stream(struct mindone_stream_out *out)
{
    struct mindone_audio_device *adev = out->dev;

    int rc = out_standby(out);
    if (rc) {
        ALOGE("%s: standby failed: %d", __func__, rc);
        pthread_mutex_lock(&out->lock);
        if (out->pcm) {
            pcm_close(out->pcm);
            out->pcm = NULL;
        }
        pthread_mutex_lock(&adev->lock);
        bool shared_crossbar = false;
        for (const struct mindone_stream_out *other = adev->outs; other; other = other->next) {
            if (other != out && other->crossbar_on && other->pcm_device == out->pcm_device)
                shared_crossbar = true;
        }
        if (shared_crossbar)
            out->crossbar_on = false;
        else
            teardown_dl_crossbar(adev, out);
        if (adev->active_out == out)
            adev->active_out = NULL;
        out->standby = true;
        out->routed_device = OUT_DEVICE_NONE;
        out->reopen_pending = false;
        pthread_mutex_unlock(&adev->lock);
        pthread_mutex_unlock(&out->lock);
    }

    pthread_mutex_lock(&adev->lock);
    struct mindone_stream_out **link = &adev->outs;
    while (*link && *link != out)
        link = &(*link)->next;
    if (*link == out) {
        *link = out->next;
        out->next = NULL;
    }
    pthread_mutex_unlock(&adev->lock);

    pthread_mutex_destroy(&out->lock);
    free(out->dsp_buf);
    free(out->sco_buf);
    free(out);
}

static int open_input_stream(struct mindone_audio_device *adev, audio_io_handle_t handle,
                             audio_devices_t devices, audio_input_flags_t flags,
                             struct audio_config *config, struct mindone_stream_in **stream_in)
{
    struct mindone_stream_in *in;

    if (!stream_in)
        return -EINVAL;
    *stream_in = NULL;
    if (!adev || !config)
        return -EINVAL;
    if (flags & AUDIO_INPUT_FLAG_MMAP_NOIRQ)
        return -ENOSYS;

    in = (struct mindone_stream_in *)calloc(1, sizeof(*in));
    if (!in)
        return -ENOMEM;
    in->io_handle = handle;

    in->config.channels = IN_CHANNEL_COUNT;
    in->config.rate = IN_SAMPLING_RATE;
    in->config.period_size = IN_PERIOD_SIZE;
    in->config.period_count = IN_PERIOD_COUNT;
    in->config.format = PCM_FORMAT_S16_LE;
    in->hw_config = in->config;

    in->frame_size = sizeof(int16_t);

    in->dev = adev;
    in->device = devices;
    in->standby = true;
    in->routed_device = IN_DEVICE_NONE;
    pthread_mutex_init(&in->lock, NULL);

    pthread_mutex_lock(&adev->lock);
    if (adev->n_ins == (int)(sizeof(adev->ins) / sizeof(adev->ins[0]))) {
        pthread_mutex_unlock(&adev->lock);
        pthread_mutex_destroy(&in->lock);
        free(in);
        return -ENOMEM;
    }
    adev->ins[adev->n_ins++] = in;
    pthread_mutex_unlock(&adev->lock);

    config->format = AUDIO_FORMAT_PCM_16_BIT;
    config->channel_mask = AUDIO_CHANNEL_IN_MONO;
    config->sample_rate = IN_SAMPLING_RATE;

    *stream_in = in;
    return 0;
}

static void close_input_stream(struct mindone_stream_in *in)
{
    struct mindone_audio_device *adev = in->dev;

    in_standby(in);
    pthread_mutex_lock(&adev->lock);
    for (int i = 0; i < adev->n_ins; ++i) {
        if (adev->ins[i] == in) {
            adev->ins[i] = adev->ins[--adev->n_ins];
            break;
        }
    }
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_destroy(&in->lock);
    free(in->downmix_buf);
    free(in->sco_in_buf);
    free(in);
}

static int set_voice_volume(struct mindone_audio_device *adev, float volume)
{
    if (!isfinite(volume) || volume < 0.0f || volume > 1.0f)
        return -EINVAL;
    pthread_mutex_lock(&adev->lock);
    adev->voice_volume = volume;
    if (adev->in_call)
        set_hw_output_volume(adev, adev->voice_out_device, volume);
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int set_mode(struct mindone_audio_device *adev, audio_mode_t mode)
{
    bool want_call;

    pthread_mutex_lock(&adev->lock);
    if (mode == adev->mode) {
        pthread_mutex_unlock(&adev->lock);
        return 0;
    }
    int rc = 0;

    want_call = (mode == AUDIO_MODE_IN_CALL);

    if (want_call)
        rc = voice_route_enable(adev, voice_output_device(adev));
    else
        rc = voice_route_disable(adev, true);
    if (!rc)
        adev->mode = mode;

    pthread_mutex_unlock(&adev->lock);
    return rc;
}

static int set_mic_mute(struct mindone_audio_device *adev, bool state)
{
    struct mixer_ctl *controls[3];
    int previous[3];
    size_t count = 0;
    int ret = 0;

    pthread_mutex_lock(&adev->lock);
    if (adev->mic_mute == state)
        goto done;
    if (adev->in_call && adev->voice_out_device == OUT_DEVICE_BT_SCO) {
        ret = speech_modem_mute(adev, 0x2f02, state);
        if (ret)
            voice_route_disable(adev, true);
        else
            adev->mic_mute = state;
        goto done;
    }
    if (!adev->mixer) {
        ret = -ENODEV;
        goto done;
    }
    if (adev->in_call) {
        controls[count++] = mixer_get_ctl_by_name(adev->mixer, "PCM_2_PB_CH1 ADDA_UL_CH1");
        controls[count++] = mixer_get_ctl_by_name(adev->mixer, "PCM_2_PB_CH2 ADDA_UL_CH2");
    }
    struct mixer_ctl *sco_mute = mixer_get_ctl_by_name(adev->mixer, "BTCVSD Tx Mute Switch");
    if (sco_mute)
        controls[count++] = sco_mute;
    for (size_t i = 0; i < count; ++i) {
        if (!controls[i]) {
            ret = -ENOSYS;
            goto done;
        }
        previous[i] = mixer_ctl_get_value(controls[i], 0);
        if (previous[i] < 0) {
            ret = previous[i];
            goto done;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        ret = mixer_ctl_set_value(controls[i], 0,
                                 controls[i] == sco_mute ? state : !state);
        if (ret) {
            for (size_t j = 0; j <= i; ++j)
                mixer_ctl_set_value(controls[j], 0, previous[j]);
            goto done;
        }
    }
    if (adev->in_call)
        audio_route_apply_path(adev->route, state ? "voice-uplink-off" : "voice-uplink");
    adev->mic_mute = state;
done:
    pthread_mutex_unlock(&adev->lock);
    return ret < 0 ? ret : (ret ? -EIO : 0);
}

#if defined(MINDONE_AUDIO_RUST_ROUTES)
static int check_output_patch(const struct mindone_audio_device *adev,
                               unsigned int num_sources,
                               const struct audio_port_config *sources,
                               enum output_device next)
{
    struct mindone_audio_route_state shadow = endpoint_snapshot(adev);
    for (const struct mindone_stream_out *out = adev->outs; out; out = out->next) {
        bool selected = false;
        for (unsigned int j = 0; j < num_sources; j++)
            if (sources[j].type == AUDIO_PORT_TYPE_MIX &&
                sources[j].ext.mix.handle == out->io_handle)
                selected = true;
        if (!selected || out->standby || out->routed_device == OUT_DEVICE_NONE ||
            out->routed_device == next ||
            ((next == OUT_DEVICE_BT_SCO) != (out->routed_device == OUT_DEVICE_BT_SCO)))
            continue;
        struct mindone_audio_route_plan plan;
        int rc = plan_endpoint_transition(&shadow, out->routed_device, next, &plan);
        if (rc)
            return rc;
        shadow = plan.next;
    }
    return 0;
}
#endif

static int create_audio_patch(struct mindone_audio_device *adev,
                                    unsigned int num_sources,
                                    const struct audio_port_config *sources,
                                    unsigned int num_sinks,
                                    const struct audio_port_config *sinks,
                                    audio_patch_handle_t *handle)
{
    if (!sources || !sinks || num_sources < 1 || num_sinks < 1 || !handle)
        return -EINVAL;

    if (sinks[0].type == AUDIO_PORT_TYPE_DEVICE &&
        sinks[0].ext.device.type == AUDIO_DEVICE_OUT_TELEPHONY_TX) {
        *handle = AUDIO_PATCH_HANDLE_NONE + 1;
        return 0;
    }

    pthread_mutex_lock(&adev->lock);

    if (sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {
        if (num_sinks != 1) {
            pthread_mutex_unlock(&adev->lock);
            return -EINVAL;
        }
        enum output_device d = output_device_from_mask(sinks[0].ext.device.type);

        if ((adev->in_call || adev->mode == AUDIO_MODE_IN_CALL) &&
            sources[0].type == AUDIO_PORT_TYPE_DEVICE &&
            sources[0].ext.device.type == AUDIO_DEVICE_IN_VOICE_CALL) {
            if (num_sources != 1) {
                pthread_mutex_unlock(&adev->lock);
                return -EINVAL;
            }
            if (adev->in_call &&
                (d == OUT_DEVICE_BT_SCO || adev->voice_out_device == OUT_DEVICE_BT_SCO) &&
                adev->voice_out_device != d) {
                int rc = voice_route_change_sco(adev, d);
                if (rc) {
                    pthread_mutex_unlock(&adev->lock);
                    return rc;
                }
            } else if (!adev->in_call || adev->voice_out_device != d) {
#if defined(MINDONE_AUDIO_RUST_ROUTES)
                int planning_rc = check_endpoint_transition(adev, adev->voice_out_device, d);
                if (planning_rc) {
                    pthread_mutex_unlock(&adev->lock);
                    return planning_rc;
                }
#endif
                if (adev->in_call) {
                    int rc = speech_ccci_set_mode(adev, d);
                    if (rc) {
                        voice_route_disable(adev, true);
                        pthread_mutex_unlock(&adev->lock);
                        return rc;
                    }
                }
                int rc = voice_route_disable(adev, false);
                if (!rc)
                    rc = voice_route_enable(adev, d);
                if (rc) {
                    pthread_mutex_unlock(&adev->lock);
                    return rc;
                }
            }
        } else {
            unsigned int si;

            for (si = 0; si < num_sources; si++) {
                bool found = false;
                if (sources[si].type == AUDIO_PORT_TYPE_MIX)
                    for (const struct mindone_stream_out *o = adev->outs; o; o = o->next)
                        if (sources[si].ext.mix.handle == o->io_handle) {
                            found = true;
                            break;
                        }
                if (!found) {
                    pthread_mutex_unlock(&adev->lock);
                    return -EINVAL;
                }
            }

#if defined(MINDONE_AUDIO_RUST_ROUTES)
            int planning_rc = check_output_patch(adev, num_sources, sources, d);
            if (planning_rc) {
                pthread_mutex_unlock(&adev->lock);
                return planning_rc;
            }
#endif
            for (struct mindone_stream_out *o = adev->outs; o; o = o->next) {
                bool mine = false;

                for (si = 0; si < num_sources; si++)
                    if (sources[si].type == AUDIO_PORT_TYPE_MIX &&
                        sources[si].ext.mix.handle == o->io_handle)
                        mine = true;
                if (!mine)
                    continue;
                if (!o->standby && o->routed_device != OUT_DEVICE_NONE) {
                    if ((d == OUT_DEVICE_BT_SCO) != (o->routed_device == OUT_DEVICE_BT_SCO)) {
                        o->reopen_pending = true;
                    } else if (o->routed_device != d) {
                        int rc = apply_output_route(adev, o->routed_device, d);
                        if (rc) {
                            pthread_mutex_unlock(&adev->lock);
                            return rc;
                        }
                        o->routed_device = d;
                    }
                }
                o->device = sinks[0].ext.device.type;
            }
        }
    } else if (sources[0].type == AUDIO_PORT_TYPE_DEVICE) {
        enum input_device d = input_device_from_mask(sources[0].ext.device.type);
        bool matched = false;

        for (int i = 0; i < adev->n_ins; ++i) {
            struct mindone_stream_in *in = adev->ins[i];
            bool mine = false;

            for (unsigned int si = 0; si < num_sinks; ++si)
                if (sinks[si].type == AUDIO_PORT_TYPE_MIX &&
                    sinks[si].ext.mix.handle == in->io_handle)
                    mine = true;
            if (!mine)
                continue;
            matched = true;
            in->device = sources[0].ext.device.type;
            if (!in->standby) {
                if ((d == IN_DEVICE_BT_SCO) != (in->routed_device == IN_DEVICE_BT_SCO)) {
                    in->reopen_pending = true;
                } else if (in->routed_device != d) {
                    apply_input_route(adev, in->routed_device, d);
                    in->routed_device = d;
                }
            }
        }
        if (!matched) {
            pthread_mutex_unlock(&adev->lock);
            return -EINVAL;
        }
    }

    pthread_mutex_unlock(&adev->lock);

    *handle = AUDIO_PATCH_HANDLE_NONE + 1;
    return 0;
}

static int release_audio_patch(struct mindone_audio_device *adev,
                                     audio_patch_handle_t handle)
{
    (void)adev; (void)handle;
    return 0;
}

int mindone_audio_device_close(struct mindone_audio_device *adev)
{
    if (!adev)
        return -EINVAL;
    pthread_mutex_lock(&adev->lock);
    if (adev->outs || adev->n_ins) {
        pthread_mutex_unlock(&adev->lock);
        return -EBUSY;
    }
    int rc = voice_route_disable(adev, true);
    if (rc) {
        pthread_mutex_unlock(&adev->lock);
        return rc;
    }
    speech_ccci_off(adev);
    speech_hostless_close(adev);
    pthread_mutex_unlock(&adev->lock);
    if (adev->route)
        audio_route_free(adev->route);
    if (adev->mixer)
        mixer_close(adev->mixer);
    pthread_mutex_destroy(&adev->lock);
    free(adev);
    return 0;
}

int mindone_audio_device_open(struct mindone_audio_device **device)
{
    struct mindone_audio_device *adev;

    if (!device)
        return -EINVAL;
    *device = NULL;

    adev = (struct mindone_audio_device *)calloc(1, sizeof(*adev));
    if (!adev)
        return -ENOMEM;

    adev->speech_fd = -1;
    adev->voice_volume = 1.0f;

    adev->mixer = mixer_open(PCM_CARD);
    if (!adev->mixer) {
        ALOGE("%s: mixer_open(card=%d) failed", __func__, PCM_CARD);
        free(adev);
        return -ENODEV;
    }

    adev->route = audio_route_init(PCM_CARD, MIXER_PATHS_XML);
    if (!adev->route) {
        ALOGE("%s: audio_route_init('%s') failed", __func__, MIXER_PATHS_XML);
        mixer_close(adev->mixer);
        free(adev);
        return -ENODEV;
    }

    pthread_mutex_init(&adev->lock, NULL);
    adev->mode = AUDIO_MODE_NORMAL;

    *device = adev;
    return 0;
}

int mindone_audio_set_mode(struct mindone_audio_device *device, audio_mode_t mode)
{
    if (!device || (mode != AUDIO_MODE_NORMAL && mode != AUDIO_MODE_RINGTONE &&
                    mode != AUDIO_MODE_IN_CALL && mode != AUDIO_MODE_IN_COMMUNICATION))
        return -EINVAL;
    return set_mode(device, mode);
}

int mindone_audio_set_voice_volume(struct mindone_audio_device *device, float volume)
{
    return device ? set_voice_volume(device, volume) : -EINVAL;
}

int mindone_audio_set_mic_mute(struct mindone_audio_device *device, bool muted)
{
    return device ? set_mic_mute(device, muted) : -EINVAL;
}

int mindone_audio_get_voice(struct mindone_audio_device *device, struct mindone_audio_voice *voice)
{
    if (!device || !voice)
        return -EINVAL;
    pthread_mutex_lock(&device->lock);
    voice->mode = device->mode;
    voice->volume = device->voice_volume;
    voice->mic_mute = device->mic_mute;
    pthread_mutex_unlock(&device->lock);
    return 0;
}

int mindone_audio_speech_gain(struct mindone_audio_device *device, int gain)
{
    if (!device || gain < -255 || gain > 0)
        return -EINVAL;
    pthread_mutex_lock(&device->lock);
    int rc = -ENOTCONN;
    if (device->in_call && device->speech_on && device->speech_fd >= 0)
        rc = ccci_send_mailbox(device->speech_fd, 0x2f00, (uint16_t)(int16_t)gain, 0) ? -EIO : 0;
    pthread_mutex_unlock(&device->lock);
    return rc;
}

int mindone_audio_speech_pump(struct mindone_audio_device *device)
{
    if (!device)
        return -EINVAL;
    pthread_mutex_lock(&device->lock);
    int rc = speech_rx_pump(device, "1");
    pthread_mutex_unlock(&device->lock);
    return rc;
}

int mindone_audio_set_sco(struct mindone_audio_device *device, struct mindone_audio_sco sco)
{
    if (!device || (sco.band != MINDONE_AUDIO_SCO_NB && sco.band != MINDONE_AUDIO_SCO_WB))
        return -EINVAL;
    int rc = 0;
    pthread_mutex_lock(&device->lock);
    bool wb = sco.band == MINDONE_AUDIO_SCO_WB;
    if (wb != device->bt_wb) {
        device->bt_wb = wb;
        for (struct mindone_stream_out *out = device->outs; out; out = out->next)
            if (out->routed_device == OUT_DEVICE_BT_SCO)
                out->reopen_pending = true;
        if (device->active_in && device->active_in->routed_device == IN_DEVICE_BT_SCO)
            device->active_in->reopen_pending = true;
        if (device->in_call && device->voice_out_device == OUT_DEVICE_BT_SCO)
            rc = voice_route_change_sco(device, OUT_DEVICE_BT_SCO);
    }
    pthread_mutex_unlock(&device->lock);
    return rc;
}

int mindone_audio_create_patch(struct mindone_audio_device *device,
                              unsigned int source_count, const struct audio_port_config *sources,
                              unsigned int sink_count, const struct audio_port_config *sinks,
                              audio_patch_handle_t *handle)
{
    if (!device || !sources || !sinks || !handle || !source_count || !sink_count)
        return -EINVAL;
    return create_audio_patch(device, source_count, sources, sink_count, sinks, handle);
}

int mindone_audio_release_patch(struct mindone_audio_device *device, audio_patch_handle_t handle)
{
    return device ? release_audio_patch(device, handle) : -EINVAL;
}

int mindone_audio_output_open(struct mindone_audio_device *device, audio_io_handle_t handle,
                             audio_devices_t devices, audio_output_flags_t flags,
                             struct audio_config *config, struct mindone_stream_out **stream)
{
    return open_output_stream(device, handle, devices, flags, config, stream);
}

void mindone_audio_output_close(struct mindone_stream_out *stream)
{
    if (stream)
        close_output_stream(stream);
}

int mindone_audio_output_command(struct mindone_stream_out *stream, enum mindone_audio_command command)
{
    return stream ? out_aidl_state(stream, command) : -EINVAL;
}

ssize_t mindone_audio_output_write(struct mindone_stream_out *stream, const void *buffer, size_t bytes)
{
    if (!stream || (!buffer && bytes) || bytes > SSIZE_MAX ||
        !stream->frame_size || bytes % stream->frame_size)
        return -EINVAL;
    if (bytes) {
        int rc = out_aidl_state(stream, MINDONE_AUDIO_TRANSFER);
        if (rc)
            return rc;
    }
    return out_write(stream, buffer, bytes);
}

int mindone_audio_output_latency(struct mindone_stream_out *stream, uint32_t *latency_ms)
{
    if (!stream || !latency_ms)
        return -EINVAL;
    *latency_ms = out_get_latency(stream);
    return 0;
}

int mindone_audio_output_position(struct mindone_stream_out *stream, uint64_t *frames,
                                 struct timespec *timestamp)
{
    return stream && frames && timestamp
            ? out_aidl_get_position(stream, frames, timestamp) : -EINVAL;
}

int mindone_audio_output_route(struct mindone_stream_out *stream, struct mindone_audio_route route)
{
    return stream ? out_set_route(stream, route.devices) : -EINVAL;
}

int mindone_audio_output_gain(struct mindone_stream_out *stream, struct mindone_audio_gain gain)
{
    return stream ? out_set_volume(stream, gain.left, gain.right) : -EINVAL;
}

int mindone_audio_input_open(struct mindone_audio_device *device, audio_io_handle_t handle,
                            audio_devices_t devices, audio_input_flags_t flags,
                            struct audio_config *config, struct mindone_stream_in **stream)
{
    return open_input_stream(device, handle, devices, flags, config, stream);
}

void mindone_audio_input_close(struct mindone_stream_in *stream)
{
    if (stream)
        close_input_stream(stream);
}

int mindone_audio_input_command(struct mindone_stream_in *stream, enum mindone_audio_command command)
{
    if (!stream)
        return -EINVAL;
    switch (command) {
    case MINDONE_AUDIO_START:
        return in_aidl_start(stream);
    case MINDONE_AUDIO_FLUSH:
    case MINDONE_AUDIO_STANDBY:
        return in_standby(stream);
    case MINDONE_AUDIO_PAUSE:
    case MINDONE_AUDIO_DRAIN:
        return 0;
    default:
        return -EINVAL;
    }
}

ssize_t mindone_audio_input_read(struct mindone_stream_in *stream, void *buffer, size_t bytes)
{
    if (!stream || (!buffer && bytes) || bytes > SSIZE_MAX ||
        !stream->frame_size || bytes % stream->frame_size)
        return -EINVAL;
    return in_read(stream, buffer, bytes);
}

int mindone_audio_input_buffer_size(struct mindone_stream_in *stream, size_t *bytes)
{
    if (!stream || !bytes)
        return -EINVAL;
    *bytes = in_get_buffer_size(stream);
    return 0;
}

int mindone_audio_input_position(struct mindone_stream_in *stream, int64_t *frames, int64_t *time_ns)
{
    return stream && frames && time_ns
            ? in_get_capture_position(stream, frames, time_ns) : -EINVAL;
}

int mindone_audio_input_route(struct mindone_stream_in *stream, struct mindone_audio_route route)
{
    return stream ? in_set_route(stream, route.devices) : -EINVAL;
}

int mindone_audio_input_gain(struct mindone_stream_in *stream, float gain)
{
    if (!stream || !isfinite(gain) || gain < 0.0f || gain > 1.0f)
        return -EINVAL;
    return in_set_gain(stream, gain);
}

int mindone_audio_output_get_config(struct mindone_stream_out *stream, struct audio_config *config)
{
    if (!stream || !config || !stream->frame_size)
        return -EINVAL;
    *config = (struct audio_config){
        .sample_rate = out_get_sample_rate(stream),
        .channel_mask = AUDIO_CHANNEL_OUT_STEREO,
        .format = AUDIO_FORMAT_PCM_16_BIT,
        .frame_count = out_get_buffer_size(stream) / stream->frame_size,
    };
    return 0;
}

int mindone_audio_input_get_config(struct mindone_stream_in *stream, struct audio_config *config)
{
    if (!stream || !config || !stream->frame_size)
        return -EINVAL;
    *config = (struct audio_config){
        .sample_rate = in_get_sample_rate(stream),
        .channel_mask = AUDIO_CHANNEL_IN_MONO,
        .format = AUDIO_FORMAT_PCM_16_BIT,
        .frame_count = in_get_buffer_size(stream) / stream->frame_size,
    };
    return 0;
}
