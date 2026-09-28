/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "audio_hw_mindone"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <malloc.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <log/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <hardware/audio.h>
#include <hardware/hardware.h>

#include <tinyalsa/asoundlib.h>
#include <audio_route/audio_route.h>

#define PCM_CARD                0

#define PCM_DEVICE_PRIMARY      0   /* Playback_1 / DL1  -- MT6789_PRIMARY_MEMIF */
#define PCM_DEVICE_FAST         2   /* Playback_2 / DL2  -- MT6789_FAST_MEMIF    */
#define PCM_DEVICE_DEEP_BUFFER  3   /* Playback_3 / DL3  -- MT6789_DEEP_MEMIF    */
#define PCM_DEVICE_CAPTURE_MAIN 9   /* Capture_1  / UL1  -- MT6789_RECORD_MEMIF  */
#define PCM_DEVICE_BTCVSD       46
#define SCO_RATE_NB             8000
#define SCO_RATE_WB             16000
#define PCM_DEVICE_SPEECH_HOSTLESS  22
#define SPEECH_HOSTLESS_RATE        8000
#define SPEECH_HOSTLESS_CHANNELS    2
#define SPEECH_HOSTLESS_PERIOD_SIZE (SPEECH_HOSTLESS_RATE / 50)
#define SPEECH_HOSTLESS_PERIOD_COUNT 4

#define OUT_SAMPLING_RATE   48000
#define OUT_PERIOD_SIZE     960
#define OUT_PERIOD_COUNT    4

/* Per-path buffering. Until 15.09 all three output paths used OUT_PERIOD_SIZE/COUNT, which made
 * the "deep buffer" path - the one the framework picks for music and for playback with the screen
 * off - no deeper than the primary one, and left the fast path no quicker.
 *
 * The AFE raises one interrupt per period, and AudioFlinger's thread for that output wakes once
 * per period to refill it, so the period size is directly the wake-up rate. Measured on the
 * device via /proc/interrupts (Afe_ISR_Handle) while capturing: 960 frames at 48 kHz = 50
 * interrupts/s, 1920 frames = 25/s, exactly the 1:1 relation.
 *
 * Deep buffer therefore uses 40 ms periods (half the wake-ups) and eight of them, so a music
 * stream holds 320 ms in flight and the CPU can stay in a deeper idle state between refills -
 * which is what matters on a 1960 mAh battery. The fast path goes the other way: 5 ms periods for
 * UI feedback and games, where latency is the whole point and the stream is short-lived. The
 * primary path is unchanged, because it is what everything else falls back to and 80 ms of
 * buffering is a safe middle. */
#define OUT_DEEP_PERIOD_SIZE    1920   /* 40 ms at 48 kHz */
#define OUT_DEEP_PERIOD_COUNT   8      /* 320 ms in flight */
#define OUT_FAST_PERIOD_SIZE    240    /* 5 ms at 48 kHz */
#define OUT_FAST_PERIOD_COUNT   4      /* 20 ms in flight */
#define OUT_CHANNEL_COUNT   2

#define IN_SAMPLING_RATE    48000
#define IN_PERIOD_SIZE      960
#define IN_PERIOD_COUNT     4
#define IN_CHANNEL_COUNT    2

/* Digital capture gain, in Q8 (256 = unity).
 *
 * The built-in microphone is digital (device tree mediatek,mic-type = DMIC), so the codec's
 * analog preamp is not in this path and there is no hardware gain stage between the PDM
 * decimator and the capture memif - AFE_GAIN1/GAIN2 feed other memifs, not UL1. Measured on the
 * device 15.09 with speech at ~50 cm: RMS 93 of 32767, i.e. -51 dBFS, with peaks at -32 dBFS.
 * That is roughly 26 dB below a normal recording level, which is why recordings sounded empty
 * even once the routing was correct.
 *
 * x12 (+21.6 dB) puts that speech at about -29 dBFS with peaks near -11 dBFS, leaving real
 * headroom for loud sources rather than pushing them into the limiter. The noise floor rises
 * with the signal (it is the microphone's own, not something this gain adds), so this is a level
 * correction, not a quality one - the 8 dB available from the DMIC clock rate is the part worth
 * having in the kernel instead. */
#define IN_DIGITAL_GAIN_Q8  3072

#define MIXER_PATHS_XML     "/vendor/etc/mixer_paths.xml"

enum output_device {
    OUT_DEVICE_NONE = 0,
    OUT_DEVICE_SPEAKER,
    OUT_DEVICE_HEADPHONE,
    OUT_DEVICE_EARPIECE,        /* voice calls (15.09): RCV Mux -> receiver, docs section 2.3 */
    OUT_DEVICE_BT_SCO,          /* 15.09: kernel btcvsd codec, bypasses the AFE crossbar */
};
#define OUT_DEVICE_COUNT (OUT_DEVICE_BT_SCO + 1)

enum input_device {
    IN_DEVICE_NONE = 0,
    IN_DEVICE_MAIN_MIC,
    IN_DEVICE_HEADSET_MIC,
    IN_DEVICE_BT_SCO,           /* 16.09: btcvsd capture on PCM 46, no codec path at all */
};

struct mindone_audio_device {
    struct audio_hw_device hw_device;

    pthread_mutex_t lock;               /* protects device + stream state below */
    struct audio_route *route;
    struct mixer *mixer;

    audio_mode_t mode;
    bool mic_mute;
    float voice_volume;

    /* SCO band as told by the Bluetooth stack ("bt_wbs=on" once mSBC is negotiated, "bt_swb=on"
     * for LC3 super-wideband, which btcvsd can only serve as WB). NB until told otherwise: that
     * is what HFP starts every link on. Changing it while a SCO stream is open restarts that
     * stream, because the PCM rate is fixed at open. */
    bool bt_wb;

    /* Voice call state (15.09). in_call is what actually gates the modem crossbar: the framework
     * can call set_voice_volume()/set_mic_mute() outside a call, and those must not touch it. */
    bool in_call;
    bool speech_on;
    enum output_device voice_out_device;
    struct pcm *speech_hostless_out;
    struct pcm *speech_hostless_in;

    struct mindone_stream_out *active_out;
    struct mindone_stream_in *active_in;

    int out_ep_users[OUT_DEVICE_COUNT];

    /* Every open output stream, so a device change (audio patch) can move all of them rather
     * than only the last one started. */
    struct mindone_stream_out *outs[8];
    int n_outs;
};

struct mindone_stream_out {
    struct audio_stream_out stream;

    pthread_mutex_t lock;
    struct mindone_audio_device *dev;

    struct pcm *pcm;
    struct pcm_config config;
    int pcm_device;                     /* PCM_DEVICE_PRIMARY / _FAST / _DEEP_BUFFER */
    audio_output_flags_t flags;
    audio_devices_t device;             /* AUDIO_DEVICE_OUT_* bitmask, last set_parameters() */
    enum output_device routed_device;
    bool crossbar_on;                   /* whether this stream's dlN-adda path is applied */
    audio_io_handle_t io_handle;        /* the framework's name for this output; patches use it */

    /* What pcm_open() was actually given: config for the AFE memifs, the SCO config for btcvsd.
     * Everything the framework sees (buffer size, latency, rate) still comes from config. */
    struct pcm_config hw_config;
    /* The device the stream is routed to needs a different PCM device or config (SCO <-> anything
     * else). Set under adev->lock by whoever changed the device; out_write() restarts the stream. */
    bool reopen_pending;

    bool standby;
    uint64_t written_frames;            /* in stream (48 kHz) frames, whatever the PCM runs at */

    /* SCO conversion state, see sco_out_convert(). sco_buf holds decimated mono frames not yet
     * written (btcvsd takes whole 60-byte packets only, so a write's remainder waits for the next
     * one); sco_acc/sco_acc_n carry a partial decimation window across writes. */
    int16_t *sco_buf;
    size_t sco_buf_frames;
    size_t sco_staged;
    int32_t sco_acc;
    unsigned int sco_acc_n;

    /* Speaker conditioning state (see speaker_dsp_process()). Only used while this stream is
     * routed to OUT_DEVICE_SPEAKER; a headphone or SCO stream leaves it untouched. */
    int16_t *dsp_buf;
    size_t dsp_frames;
    /* High-pass coefficients, computed once per stream rather than per buffer: they depend only
     * on the stream rate, which does not change while a stream is open, and recomputing them
     * meant two transcendental calls and six divisions on every write - up to 200 times a second
     * on the fast path. */
    float hp_b0, hp_b1, hp_b2, hp_a1, hp_a2;
    uint32_t hp_rate;
    float hp_x1[OUT_CHANNEL_COUNT], hp_x2[OUT_CHANNEL_COUNT];
    float hp_y1[OUT_CHANNEL_COUNT], hp_y2[OUT_CHANNEL_COUNT];
    float lim_gain;
};

struct mindone_stream_in {
    struct audio_stream_in stream;

    pthread_mutex_t lock;
    struct mindone_audio_device *dev;

    struct pcm *pcm;
    struct pcm_config config;
    struct pcm_config hw_config;        /* see the output side: the SCO config differs */
    audio_devices_t device;
    enum input_device routed_device;
    audio_io_handle_t io_handle;
    bool reopen_pending;

    bool standby;
    uint64_t read_frames;               /* in stream (48 kHz) frames */

    /* SCO capture: source samples at the band rate before interpolation, and the last sample of
     * the previous read so the interpolation is continuous across reads. */
    int16_t *sco_in_buf;
    size_t sco_in_frames;
    int16_t sco_last;

    /* Scratch for the stereo->mono downmix in in_read(). The capture hardware always runs two
     * channels (left = main mic pad, right = the second pad), while the stream this HAL offers
     * the framework is mono, so the two cannot be the same buffer. Grown on demand and freed in
     * adev_close_input_stream(). */
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
        return OUT_DEVICE_EARPIECE;     /* 15.09: reachable now that voice calls are served */
    if (device & (AUDIO_DEVICE_OUT_BLUETOOTH_SCO |
                  AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET |
                  AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT))
        return OUT_DEVICE_BT_SCO;       /* 15.09 */
    /* A2DP never reaches primary (audio.bluetooth.default serves it) and USB has its own HAL, so
     * anything left really is the speaker rather than an unroutable stream. */
    return OUT_DEVICE_SPEAKER;
}

static const char *output_path_name(enum output_device d)
{
    switch (d) {
    case OUT_DEVICE_HEADPHONE: return "headphone";
    case OUT_DEVICE_EARPIECE:  return "earpiece";
    /* BT SCO has no audio_route path on purpose: btcvsd exposes its own kcontrols
     * ("BTCVSD Band"/"Tx Mute"), not AFE crossbar switches - see bt_sco_configure(). */
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

/* ---- Output endpoints are shared and reference-counted ----------------------
 * The codec-side device path is one physical thing (LOL Mux, the aw87xxx amp, RCV Mux) used by
 * every output stream routed to that device. It goes up when the first stream arrives and down
 * when the last one leaves; a stream that stops while another is still playing must not touch
 * it. Caller must hold adev->lock for all four functions below. */
static void endpoint_acquire(struct mindone_audio_device *adev, enum output_device d)
{
    const char *on_path;

    if (d == OUT_DEVICE_NONE)
        return;
    if (adev->out_ep_users[d]++ > 0)
        return;                         /* already up for another stream */
    /* NULL means "this endpoint is not an audio_route path" (BT SCO) - the count is still kept
     * so acquire/release stay symmetric, but there is nothing to apply. */
    on_path = output_path_name(d);
    if (on_path) {
        audio_route_apply_path(adev->route, on_path);
        audio_route_update_mixer(adev->route);
    }
}

static void endpoint_release(struct mindone_audio_device *adev, enum output_device d)
{
    const char *off_path;

    if (d == OUT_DEVICE_NONE)
        return;
    if (adev->out_ep_users[d] <= 0) {
        ALOGW("%s: endpoint %d released more often than acquired", __func__, d);
        return;
    }
    if (--adev->out_ep_users[d] > 0)
        return;                         /* someone else is still playing here */
    off_path = output_path_off_name(d);
    if (off_path) {
        audio_route_apply_path(adev->route, off_path);
        audio_route_update_mixer(adev->route);
    }
}

/* Moves ONE stream's endpoint from old_device to new_device. Releasing before acquiring keeps
 * the old behaviour for the single-stream case: switching speaker -> headphone drops
 * "Ext_Speaker_Amp Switch" so the aw87xxx is not left powered for nothing (battery). With
 * several streams, the speaker stays up as long as any of them is still routed there. */
static void apply_output_route(struct mindone_audio_device *adev,
                                enum output_device old_device,
                                enum output_device new_device)
{
    if (old_device == new_device)
        return;
    endpoint_release(adev, old_device);
    endpoint_acquire(adev, new_device);
}

static void teardown_output_route(struct mindone_audio_device *adev,
                                   enum output_device device)
{
    endpoint_release(adev, device);
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
    /* 🔴 15.09: this test used to be a plain `device & AUDIO_DEVICE_IN_WIRED_HEADSET`, and it was
     * ALWAYS true. Every AUDIO_DEVICE_IN_* constant carries the shared AUDIO_DEVICE_BIT_IN flag
     * (0x80000000), so a bitwise AND between any two of them is non-zero on that bit alone:
     *     AUDIO_DEVICE_IN_BUILTIN_MIC   0x80000004
     *     AUDIO_DEVICE_IN_WIRED_HEADSET 0x80000010
     *     AND                           0x80000000  -> "true"
     * The result was that the built-in mic selected the headset path, so PGA L/R Mux sat on AIN1
     * (the headset pad) instead of AIN0 and every recording came out silent. Caught on the device
     * with tinymix during an actual recording, not by reading the code.
     * Masking the flag off first makes the comparison mean what it reads as. */
    const audio_devices_t type = device & ~AUDIO_DEVICE_BIT_IN;

    if (type & (AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET & ~AUDIO_DEVICE_BIT_IN))
        return IN_DEVICE_BT_SCO;
    if (type & (AUDIO_DEVICE_IN_WIRED_HEADSET & ~AUDIO_DEVICE_BIT_IN))
        return IN_DEVICE_HEADSET_MIC;
    return IN_DEVICE_MAIN_MIC;
}

/* NULL for BT SCO: btcvsd is not on the codec, there is no mixer path to apply. */
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
    const char *off = (old_device != IN_DEVICE_NONE) ? input_path_off_name(old_device) : NULL;
    const char *on = input_path_name(new_device);

    if (old_device == new_device)
        return;
    if (off)
        audio_route_apply_path(adev->route, off);
    if (on)
        audio_route_apply_path(adev->route, on);
    if (off || on)
        audio_route_update_mixer(adev->route);
}

static void teardown_input_route(struct mindone_audio_device *adev,
                                 enum input_device device)
{
    const char *off = (device != IN_DEVICE_NONE) ? input_path_off_name(device) : NULL;

    if (!off)
        return;
    audio_route_apply_path(adev->route, off);
    audio_route_update_mixer(adev->route);
}

static void set_hw_output_volume(struct mindone_audio_device *adev,
                                  enum output_device device, float vol)
{
    /* One gain control per endpoint. Handset Volume is MONO (1 value, range 0..18 on this
     * codec) while Headset/Lineout are stereo - so the number of values is read from the control
     * instead of assuming two, which would fail silently on the earpiece. */
    const char *ctl_name = (device == OUT_DEVICE_HEADPHONE) ? "Headset Volume"
                         : (device == OUT_DEVICE_EARPIECE)  ? "Handset Volume"
                                                            : "Lineout Volume";
    struct mixer_ctl *ctl;
    int min, max, value;
    unsigned int i, num_values;

    /* Nothing of ours is in a SCO link's gain chain: the headset sets its own level. */
    if (!adev->mixer || device == OUT_DEVICE_BT_SCO)
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

/* ---- BT SCO -------------------------------------------------------------------
 * 15.09, corrected 16.09. btcvsd is a transport to the Bluetooth chip sitting beside the AFE,
 * not on its crossbar, so a SCO stream needs none of the routing above - just PCM 46 opened
 * with the band's own config, and the band told to the driver. The chip does the CVSD/mSBC
 * coding; what crosses PCM 46 is plain S16 mono at 8 kHz (NB) or 16 kHz (WB), in 60-byte
 * packets. The band is what the Bluetooth stack negotiated and announced through
 * set_parameters(bt_wbs=...), NOT anything this HAL can see on its streams: those are always
 * 48 kHz stereo, whatever the link.
 *
 * Caller must hold adev->lock.
 */
#define SCO_PCM_PACKET_BYTES    60      /* BTCVSD_TX_PACKET_SIZE in mtk_btcvsd.c */
#define SCO_PCM_PACKET_FRAMES   (SCO_PCM_PACKET_BYTES / (int)sizeof(int16_t))
#define SCO_PERIOD_FRAMES       240     /* 8 packets: 15 ms at 16 kHz, 30 ms at 8 kHz */
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

    /* Mute follows the device-wide flag: a SCO call muted before the route came up must stay
     * muted, and this control is the only mute btcvsd offers. */
    ctl = mixer_get_ctl_by_name(adev->mixer, "BTCVSD Tx Mute Switch");
    if (ctl)
        mixer_ctl_set_value(ctl, 0, adev->mic_mute ? 1 : 0);
}

/* Which PCM device a stream must open for the endpoint it is routed to. */
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
#define SPH_CCCI_PAYLOAD_MAGIC 0xA522
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
#define SPH_INFO_EMI_EMPTY_VALID 2
#define SPH_INFO_EMI_REAL_VALID 1
#define SPH_INFO_SMARTPA_SINGLE 1

#define PROP_SPH_PARAM_ENABLE "vendor.audio.mindone.sph_param"

#define CCCI_RAW_AUDIO_NODE "/dev/ccci_raw_audio"
#define SPEECHPARSER_LIB "/vendor/lib64/libspeechparser_vendor.so"

#define CCCI_IOC_MAGIC 'C'
#define CCCI_IOC_SMEM_BASE _IOR(CCCI_IOC_MAGIC, 48, unsigned int)
#define CCCI_IOC_SMEM_LEN _IOR(CCCI_IOC_MAGIC, 49, unsigned int)

#define SPH_SHM_SIZE 53248
#define SPH_SHM_OFF_AP_FLAG 32
#define SPH_SHM_OFF_SPH_PARAM_OFFSET 40
#define SPH_SHM_OFF_SPH_PARAM_SIZE 44
#define SPH_SHM_OFF_SPH_PARAM_WIDX 52
#define SPH_SHM_OFF_MD_VERSION 120

#define SPH_SHM_SPH_PARAM_OFFSET 128
#define SPH_SHM_SPH_PARAM_SIZE 12288

#define SP_MDVERSION_PROVEN_FALLBACK 0x3000u
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

static void build_sph_info(unsigned char *out, bool wb)
{
    memset(out, 0, SPH_INFO_PAYLOAD_SIZE);
    out[SPH_INFO_OFF_APP] = SPH_APP_NORMAL_CALL;
    out[SPH_INFO_OFF_RATE_ENUM] = wb ? 1 : 0;
    out[SPH_INFO_OFF_PATH] = SPH_INFO_PATH_SHM_CCCI;
    out[SPH_INFO_OFF_PARAM_EMI_VALID] = SPH_INFO_EMI_EMPTY_VALID;
    out[SPH_INFO_OFF_SMARTPA_CONFIG] = SPH_INFO_SMARTPA_SINGLE;
}

static void sph_put_u32_le(unsigned char *out, int off, uint32_t v)
{
    out[off] = (unsigned char)(v & 0xff);
    out[off + 1] = (unsigned char)((v >> 8) & 0xff);
    out[off + 2] = (unsigned char)((v >> 16) & 0xff);
    out[off + 3] = (unsigned char)((v >> 24) & 0xff);
}

static uint32_t sph_get_u32_le(const unsigned char *in, int off)
{
    return (uint32_t)in[off] | ((uint32_t)in[off + 1] << 8) | ((uint32_t)in[off + 2] << 16) |
           ((uint32_t)in[off + 3] << 24);
}

static void sph_info_set_real_params(unsigned char *info, uint32_t size, uint32_t offset)
{
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

static unsigned char *sph_smem_open(int *out_fd, unsigned int *out_len)
{
    int fd;
    unsigned int base = 0;
    unsigned int len = 0;
    void *map;

    fd = open(CCCI_RAW_AUDIO_NODE, O_RDWR);
    if (fd < 0) {
        ALOGE("%s: open(%s) failed: %s", __func__, CCCI_RAW_AUDIO_NODE, strerror(errno));
        return NULL;
    }
    if (ioctl(fd, CCCI_IOC_SMEM_BASE, &base) < 0) {
        ALOGE("%s: CCCI_IOC_SMEM_BASE failed: %s", __func__, strerror(errno));
        close(fd);
        return NULL;
    }
    if (ioctl(fd, CCCI_IOC_SMEM_LEN, &len) < 0 || len < SPH_SHM_SIZE) {
        ALOGE("%s: CCCI_IOC_SMEM_LEN failed or too small (%u): %s", __func__, len,
              strerror(errno));
        close(fd);
        return NULL;
    }
    map = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        ALOGE("%s: mmap failed: %s", __func__, strerror(errno));
        close(fd);
        return NULL;
    }
    *out_fd = fd;
    *out_len = len;
    return (unsigned char *)map;
}

static bool sph_param_fetch_real(enum output_device out, unsigned char *info)
{
    void *lib, *handle = NULL;
    sp_handle_get_instance_fn get_instance = NULL;
    sp_handle_init_fn init_fn = NULL;
    sp_get_param_buffer_fn get_param = NULL;
    sp_key_value_fn set_kv = NULL;
    struct sp_input_attr attr;
    struct sp_out_buf out_buf;
    static unsigned char param_buf[SP_PARAM_BUF_CAPACITY];
    unsigned char *shm;
    int smem_fd = -1;
    unsigned int smem_len = 0;
    unsigned int md_version;
    bool ok = false;

    shm = sph_smem_open(&smem_fd, &smem_len);
    if (!shm)
        return false;

    md_version = sph_get_u32_le(shm, SPH_SHM_OFF_MD_VERSION) & 0xffffu;
    if (!md_version)
        md_version = SP_MDVERSION_PROVEN_FALLBACK;

    lib = dlopen(SPEECHPARSER_LIB, RTLD_NOW);
    if (!lib) {
        ALOGE("%s: dlopen(%s) failed: %s", __func__, SPEECHPARSER_LIB, dlerror());
        munmap(shm, smem_len);
        close(smem_fd);
        return false;
    }

    get_instance = (sp_handle_get_instance_fn)dlsym(lib, "spHandleGetInstance");
    init_fn = (sp_handle_init_fn)dlsym(lib, "spHandleInit");
    get_param = (sp_get_param_buffer_fn)dlsym(lib, "getParamBuffer");
    set_kv = (sp_key_value_fn)dlsym(lib, "setKeyValuePair");
    if (!get_instance || !get_param || !set_kv) {
        ALOGE("%s: dlsym failed on %s", __func__, SPEECHPARSER_LIB);
        goto out_dlclose;
    }

    handle = get_instance();
    if (!handle) {
        ALOGE("%s: spHandleGetInstance() returned NULL", __func__);
        goto out_dlclose;
    }
    if (init_fn)
        init_fn(handle);

    {
        char query[64];
        struct sp_kv kv;

        snprintf(query, sizeof(query), "SPEECH_PARSER_SET_PARAM,MDVERSION=%u", md_version);
        kv.reserved0 = 0;
        kv.stringAddr = query;
        set_kv(handle, &kv);
    }

    sph_input_attr_for_route(out, &attr);

    memset(param_buf, 0, sizeof(param_buf));
    memset(&out_buf, 0, sizeof(out_buf));
    out_buf.memorySize = sizeof(param_buf);
    out_buf.bufferAddr = param_buf;

    if (get_param(handle, &attr, &out_buf) != 0 || out_buf.dataSize == 0 ||
        out_buf.dataSize > sizeof(param_buf) || out_buf.dataSize > SPH_SHM_SPH_PARAM_SIZE - 16) {
        ALOGE("%s: getParamBuffer failed or blob does not fit the %u-byte ring (dataSize=%u)",
              __func__, (unsigned int)(SPH_SHM_SPH_PARAM_SIZE - 16), out_buf.dataSize);
        goto out_dlclose;
    }

    sph_put_u32_le(shm, SPH_SHM_OFF_AP_FLAG, 1);
    sph_put_u32_le(shm, SPH_SHM_OFF_SPH_PARAM_OFFSET, SPH_SHM_SPH_PARAM_OFFSET);
    sph_put_u32_le(shm, SPH_SHM_OFF_SPH_PARAM_SIZE, SPH_SHM_SPH_PARAM_SIZE);
    memcpy(shm + SPH_SHM_SPH_PARAM_OFFSET, param_buf, out_buf.dataSize);
    sph_put_u32_le(shm, SPH_SHM_OFF_SPH_PARAM_WIDX, out_buf.dataSize);

    sph_info_set_real_params(info, out_buf.dataSize, 0);
    ok = true;

out_dlclose:
    dlclose(lib);
    munmap(shm, smem_len);
    close(smem_fd);
    return ok;
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

static void speech_ccci_on(enum output_device out, bool wb)
{
    unsigned char info[SPH_INFO_PAYLOAD_SIZE];
    int fd;

    build_sph_info(info, wb);
    if (property_get_bool(PROP_SPH_PARAM_ENABLE, false)) {
        if (sph_param_fetch_real(out, info))
            ALOGI("%s: real speech params loaded for %s", __func__,
                  output_path_name(out) ? output_path_name(out) : "bt-sco");
        else
            ALOGW("%s: real speech params unavailable, sending param_emi_valid=%d", __func__,
                  SPH_INFO_EMI_EMPTY_VALID);
    }

    fd = open(CCCI_AUD_NODE, O_RDWR);
    if (fd < 0) {
        ALOGE("%s: open(%s) failed: %s", __func__, CCCI_AUD_NODE, strerror(errno));
        return;
    }
    if (ccci_send_payload(fd, SPH_MSG_ID_SET_SPEECH_MODE, info, SPH_INFO_PAYLOAD_SIZE))
        ALOGE("%s: SetSpeechMode write failed: %s", __func__, strerror(errno));
    if (ccci_send_payload(fd, SPH_MSG_ID_SPEECH_ON, info, SPH_INFO_PAYLOAD_SIZE))
        ALOGE("%s: SpeechOn write failed: %s", __func__, strerror(errno));
    close(fd);
}

static void speech_ccci_off(void)
{
    int fd = open(CCCI_AUD_NODE, O_RDWR);

    if (fd < 0) {
        ALOGE("%s: open(%s) failed: %s", __func__, CCCI_AUD_NODE, strerror(errno));
        return;
    }
    if (ccci_send_mailbox(fd, SPH_MSG_ID_SPEECH_OFF, 0, 0))
        ALOGE("%s: SpeechOff write failed: %s", __func__, strerror(errno));
    close(fd);
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
    if (pcm_start(pcm)) {
        ALOGE("%s: pcm_start(device=%d, capture=%d) failed: %s", __func__,
              PCM_DEVICE_SPEECH_HOSTLESS, capture, pcm_get_error(pcm));
        pcm_close(pcm);
        return NULL;
    }
    return pcm;
}

static void speech_hostless_open(struct mindone_audio_device *adev)
{
    if (adev->speech_hostless_out || adev->speech_hostless_in)
        return;
    adev->speech_hostless_out = speech_hostless_pcm_open(false);
    adev->speech_hostless_in = speech_hostless_pcm_open(true);
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

static void voice_route_enable(struct mindone_audio_device *adev, enum output_device out)
{
    if (adev->in_call)
        return;

    if (out == OUT_DEVICE_BT_SCO) {
        /* See the header: the modem <-> btcvsd copy loop does not exist yet. The earpiece is the
         * one endpoint that always works for a call, and a call you can hear beats one routed
         * correctly into nothing. */
        ALOGW("%s: voice call over BT SCO is not implemented, keeping the call on the earpiece",
              __func__);
        out = OUT_DEVICE_EARPIECE;
    }

    /* Endpoint first, so the far end is never briefly routed at whatever the last media stream
     * left behind (e.g. the speaker amp still on while the call belongs on the earpiece). */
    apply_output_route(adev, adev->voice_out_device, out);
    adev->voice_out_device = out;

    /* The uplink carries whatever the ADC/PGA muxes select, so the mic path has to be up too -
     * the crossbar alone would send silence. */
    audio_route_apply_path(adev->route, "mic");
    audio_route_apply_path(adev->route, "voice-downlink");
    if (!adev->mic_mute)
        audio_route_apply_path(adev->route, "voice-uplink");

    /* Echo reference only matters when the downlink is played out loud; on the earpiece the
     * acoustic coupling is small and the extra memif is wasted power. */
    if (out == OUT_DEVICE_SPEAKER)
        audio_route_apply_path(adev->route, "voice-echo-ref");

    audio_route_update_mixer(adev->route);

    if (!adev->speech_on) {
        speech_hostless_open(adev);
        speech_ccci_on(out, false);
        adev->speech_on = true;
    }
    adev->in_call = true;

    set_hw_output_volume(adev, out, adev->voice_volume);
    ALOGI("%s: voice route up on %s", __func__, output_path_name(out) ? output_path_name(out) : "bt-sco");
}

static void voice_route_disable(struct mindone_audio_device *adev, bool speech_off)
{
    if (!adev->in_call)
        return;

    audio_route_apply_path(adev->route, "voice-echo-ref-off");
    audio_route_apply_path(adev->route, "voice-uplink-off");
    audio_route_apply_path(adev->route, "voice-downlink-off");
    audio_route_apply_path(adev->route, "mic-off");
    audio_route_update_mixer(adev->route);

    if (speech_off && adev->speech_on) {
        speech_ccci_off();
        speech_hostless_close(adev);
        adev->speech_on = false;
    }

    /* The call held the endpoint like any other user; a media stream still routed there keeps
     * it up. Done after the modem paths are down so the far end never hears the transition. */
    endpoint_release(adev, adev->voice_out_device);

    adev->voice_out_device = OUT_DEVICE_NONE;
    adev->in_call = false;
    ALOGI("%s: voice route down", __func__);
}

/* Which endpoint a call should use. The framework tells us through set_parameters()/the output
 * stream device; with no media stream open (the usual case for a plain call) it has not told us
 * anything, and the sane default for a phone call is the earpiece, not the speaker. */
static enum output_device voice_output_device(struct mindone_audio_device *adev)
{
    if (adev->active_out && adev->active_out->device)
        return output_device_from_mask(adev->active_out->device);
    return OUT_DEVICE_EARPIECE;
}

/* ---- audio_stream_out ------------------------------------------------------ */

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    struct mindone_stream_out *out = (struct mindone_stream_out *)stream;
    return out->config.rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    (void)stream; (void)rate;
    return -ENOSYS; /* fixed-rate output; AudioFlinger resamples */
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct mindone_stream_out *out = (struct mindone_stream_out *)stream;
    return out->config.period_size * out->config.period_count *
           audio_stream_out_frame_size(&out->stream);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    (void)stream;
    return AUDIO_CHANNEL_OUT_STEREO;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    (void)stream;
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    (void)stream;
    return (format == AUDIO_FORMAT_PCM_16_BIT) ? 0 : -ENOSYS;
}

static int do_out_standby(struct mindone_stream_out *out)
{
    struct mindone_audio_device *adev = out->dev;

    if (out->standby)
        return 0;

    if (out->pcm) {
        pcm_close(out->pcm);
        out->pcm = NULL;
    }

    pthread_mutex_lock(&adev->lock);
    teardown_dl_crossbar(adev, out);
    teardown_output_route(adev, out->routed_device);
    if (adev->active_out == out)
        adev->active_out = NULL;
    /* routed_device and standby are read by adev_create_audio_patch() under adev->lock alone,
     * so they change under it too -- otherwise a device change could catch this stream half
     * way out. */
    out->routed_device = OUT_DEVICE_NONE;
    out->standby = true;
    pthread_mutex_unlock(&adev->lock);
    /* Clear the conditioning state with the stream: stale filter history and a ducked limiter
     * gain would otherwise show up as a click and a fade-in on the next start. */
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

static int out_standby(struct audio_stream *stream)
{
    struct mindone_stream_out *out = (struct mindone_stream_out *)stream;
    int ret;

    pthread_mutex_lock(&out->lock);
    ret = do_out_standby(out);
    pthread_mutex_unlock(&out->lock);
    return ret;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    (void)stream; (void)fd;
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct mindone_stream_out *out = (struct mindone_stream_out *)stream;
    struct mindone_audio_device *adev = out->dev;
    struct str_parms *parms;
    char value[32];
    int ret;

    parms = str_parms_create_str(kvpairs);
    if (!parms)
        return -ENOMEM;

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING,
                             value, sizeof(value));
    if (ret >= 0) {
        audio_devices_t new_mask = (audio_devices_t)atoi(value);

        pthread_mutex_lock(&out->lock);
        out->device = new_mask;

        if (!out->standby) {
            enum output_device new_dev = output_device_from_mask(new_mask);

            if ((new_dev == OUT_DEVICE_BT_SCO) != (out->routed_device == OUT_DEVICE_BT_SCO)) {
                /* Different PCM device and config: the stream restarts on the next write. */
                do_out_standby(out);
            } else {
                pthread_mutex_lock(&adev->lock);
                apply_output_route(adev, out->routed_device, new_dev);
                out->routed_device = new_dev;
                pthread_mutex_unlock(&adev->lock);
            }
        }
        pthread_mutex_unlock(&out->lock);
    }

    str_parms_destroy(parms);
    return 0;
}

static char *out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    (void)stream; (void)keys;
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct mindone_stream_out *out = (struct mindone_stream_out *)stream;
    return (out->config.period_size * out->config.period_count * 1000) /
           out->config.rate;
}

static int out_set_volume(struct audio_stream_out *stream, float left, float right)
{
    struct mindone_stream_out *out = (struct mindone_stream_out *)stream;

    set_hw_output_volume(out->dev, out->routed_device, (left + right) / 2.0f);
    return 0;
}

static int start_output_stream(struct mindone_stream_out *out)
{
    struct mindone_audio_device *adev = out->dev;
    enum output_device new_dev;
    int pcm_dev;

    /* out->device and the SCO band are written under adev->lock by the patch and parameter
     * paths; read them the same way so a change is either fully seen or not at all. */
    pthread_mutex_lock(&adev->lock);
    new_dev = output_device_from_mask(out->device);
    /* 15.09: BT SCO lives on its own PCM device (btcvsd), not on the DL memif this stream was
     * opened against, and it must NOT get the dlN-adda crossbar - see bt_sco_configure(). */
    pcm_dev = output_pcm_device(out, new_dev);
    if (new_dev == OUT_DEVICE_BT_SCO)
        sco_pcm_config(adev, &out->hw_config);
    else
        out->hw_config = out->config;
    out->reopen_pending = false;
    pthread_mutex_unlock(&adev->lock);

    /* MINDONE-AVSYNC: PCM_MONOTONIC makes pcm_get_htimestamp answer in
     * CLOCK_MONOTONIC, which is the clock the framework compares against. Without it the
     * driver timestamps in another base and out_get_presentation_position below would be
     * precisely as useless as the written_frames guess it replaces. */
    out->pcm = pcm_open(PCM_CARD, pcm_dev, PCM_OUT | PCM_MONOTONIC, &out->hw_config);
    if (!out->pcm || !pcm_is_ready(out->pcm)) {
        ALOGE("%s: pcm_open(device=%d) failed: %s", __func__,
              pcm_dev, out->pcm ? pcm_get_error(out->pcm) : "unknown");
        if (out->pcm) {
            pcm_close(out->pcm);
            out->pcm = NULL;
        }
        return -ENODEV;
    }

    pthread_mutex_lock(&adev->lock);
    if (new_dev == OUT_DEVICE_BT_SCO) {
        bt_sco_configure(adev);
    } else {
        apply_dl_crossbar(adev, out);
        apply_output_route(adev, OUT_DEVICE_NONE, new_dev);
    }
    adev->active_out = out;
    out->routed_device = new_dev;
    out->standby = false;               /* see do_out_standby() for why this is under adev->lock */
    pthread_mutex_unlock(&adev->lock);

    return 0;
}

#define SPK_HPF_HZ          180.0f
#define SPK_LIMIT_CEILING   0.89f    /* ~-1 dBFS, leaves room for the HPF's transient overshoot */
#define SPK_LIM_ATTACK      0.002f   /* per-sample gain step down: full duck in ~10 ms at 48 kHz */
#define SPK_LIM_RELEASE     0.00002f /* ~1 s back to unity: slow enough not to pump */

static void speaker_dsp_process(struct mindone_stream_out *out, int16_t *data, size_t frames)
{
    const int ch = (int)out->config.channels;
    float b0, b1, b2, a1, a2;
    size_t i;
    int c;

    /* Second-order Butterworth high-pass. Recomputed only when the rate changes, so a 44.1 kHz
     * stream still gets the same corner as a 48 kHz one without paying for it every buffer. */
    if (out->hp_rate != out->config.rate) {
        const float w = 2.0f * (float)M_PI * SPK_HPF_HZ / (float)out->config.rate;
        const float cosw = cosf(w), sinw = sinf(w);
        const float alpha = sinw / 1.41421356f;          /* Q = 1/sqrt(2) */
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

        /* One gain for both channels: attenuating them independently would move the stereo image
         * on every peak, which is far more audible than the level change itself. */
        {
            const float want = (peak * out->lim_gain > SPK_LIMIT_CEILING && peak > 0.0f)
                               ? SPK_LIMIT_CEILING / peak : 1.0f;
            if (want < out->lim_gain)
                out->lim_gain -= SPK_LIM_ATTACK;         /* duck fast */
            else
                out->lim_gain += SPK_LIM_RELEASE;        /* recover slowly */
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

/* ---- SCO conversion ---------------------------------------------------------
 * 48 kHz stereo in, mono at the band rate out. Both ratios are whole numbers (48/16 = 3,
 * 48/8 = 6), so the decimation is a box average over the ratio: the two channels are summed,
 * `ratio` consecutive frames are averaged into one output sample. For speech that is an
 * adequate anti-alias filter (the first null of the box sits exactly at the output rate) and it
 * costs one add per input sample; anything better would be more code than the link can hear.
 * The partial window at the end of a buffer carries over to the next call in sco_acc/sco_acc_n,
 * so frame counts need not divide evenly.
 *
 * btcvsd refuses a write that is not a whole number of packets, so the converted samples are
 * staged in sco_buf and only whole packets are written; the remainder waits for the next write.
 * Returns the number of staged frames now ready, or a negative errno. Caller holds out->lock. */
static int sco_out_convert(struct mindone_stream_out *out, const int16_t *in, size_t frames)
{
    const unsigned int ratio = out->config.rate / out->hw_config.rate;   /* 3 or 6 */
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

/* Writes the whole packets staged so far and keeps the rest. Caller holds out->lock. */
static int sco_out_flush(struct mindone_stream_out *out)
{
    const size_t whole = (out->sco_staged / SCO_PCM_PACKET_FRAMES) * SCO_PCM_PACKET_FRAMES;
    int ret;

    if (whole == 0)
        return 0;
    ret = pcm_write(out->pcm, out->sco_buf, whole * sizeof(int16_t));
    if (ret != 0) {
        ALOGW("%s: pcm_write failed (%s), preparing and retrying once", __func__,
              pcm_get_error(out->pcm));
        if (pcm_prepare(out->pcm) == 0)
            ret = pcm_write(out->pcm, out->sco_buf, whole * sizeof(int16_t));
        if (ret != 0)
            return ret;
    }
    out->sco_staged -= whole;
    if (out->sco_staged)
        memmove(out->sco_buf, out->sco_buf + whole, out->sco_staged * sizeof(int16_t));
    return 0;
}

/* pcm_write() with one recovery from an underrun. After an xrun the substream sits in the
 * XRUN state and every further write fails the same way until someone calls pcm_prepare();
 * AudioFlinger only does that indirectly, by putting the stream into standby after a timeout,
 * which is heard as a second of silence. Preparing and retrying once here turns that into a
 * short glitch. Anything else is returned to the caller as before. Caller holds out->lock. */
static int pcm_write_recovering(struct mindone_stream_out *out, const void *buffer, size_t bytes)
{
    int ret = pcm_write(out->pcm, buffer, bytes);

    if (ret == 0)
        return 0;
    ALOGW("%s: pcm_write failed (%s), preparing and retrying once", __func__,
          pcm_get_error(out->pcm));
    if (pcm_prepare(out->pcm) != 0)
        return ret;
    return pcm_write(out->pcm, buffer, bytes);
}

static ssize_t out_write(struct audio_stream_out *stream, const void *buffer,
                          size_t bytes)
{
    struct mindone_stream_out *out = (struct mindone_stream_out *)stream;
    int ret;

    pthread_mutex_lock(&out->lock);
    /* A device change that needs another PCM (SCO <-> the rest) was noted by the patch path;
     * it could not restart the stream itself without taking this lock under adev->lock. */
    pthread_mutex_lock(&out->dev->lock);
    ret = out->reopen_pending && !out->standby;
    pthread_mutex_unlock(&out->dev->lock);
    if (ret)
        do_out_standby(out);

    if (out->standby) {
        ret = start_output_stream(out);
        if (ret != 0) {
            pthread_mutex_unlock(&out->lock);
            /* Sleep for the nominal duration so AudioFlinger's timing
             * doesn't spin on a hard failure loop. */
            usleep((uint64_t)bytes * 1000000 /
                   audio_stream_out_frame_size(stream) / out->config.rate);
            return bytes;
        }
    }

    if (out->routed_device == OUT_DEVICE_BT_SCO) {
        const size_t frames = bytes / audio_stream_out_frame_size(stream);

        ret = sco_out_convert(out, (const int16_t *)buffer, frames);
        if (ret >= 0)
            ret = sco_out_flush(out);
        if (ret == 0)
            out->written_frames += frames;
        goto written;
    }

    /* The lock stays held across the write. It costs standby()/set_parameters() a wait of at
     * most one buffer (~320 ms on the deep buffer), and it is what keeps out->pcm and the
     * conditioning state from being torn down by standby() on another thread in the middle of
     * pcm_write() -- before this the write ran unlocked and could dereference a freed pcm. */

    /* Speaker-only conditioning. The framework's buffer is const and may be reused by the caller,
     * so the processing runs on a copy rather than in place. */
    if (out->routed_device == OUT_DEVICE_SPEAKER && out->config.channels > 0) {
        const size_t frames = bytes / (out->config.channels * sizeof(int16_t));

        if (out->dsp_frames < frames) {
            int16_t *nb = (int16_t *)realloc(out->dsp_buf,
                                             frames * out->config.channels * sizeof(int16_t));
            if (nb) {
                out->dsp_buf = nb;
                out->dsp_frames = frames;
            }
        }
        if (out->dsp_buf && out->dsp_frames >= frames) {
            memcpy(out->dsp_buf, buffer, bytes);
            speaker_dsp_process(out, out->dsp_buf, frames);
            ret = pcm_write_recovering(out, out->dsp_buf, bytes);
            if (ret == 0)
                out->written_frames += bytes / audio_stream_out_frame_size(stream);
            goto written;
        }
        /* Out of memory: passing the audio through unprocessed is better than dropping it. */
    }

    ret = pcm_write_recovering(out, buffer, bytes);
    if (ret == 0)
        out->written_frames += bytes / audio_stream_out_frame_size(stream);
written:
    pthread_mutex_unlock(&out->lock);

    return (ret == 0) ? (ssize_t)bytes : ret;
}

/* Defined below; out_get_render_position derives from it. */
static int out_get_presentation_position(const struct audio_stream_out *stream,
                                         uint64_t *frames, struct timespec *timestamp);

/* The older, coarser form of the same question. It answered written_frames for the same
 * reason and with the same consequence, so it now derives from the corrected position and
 * only falls back to written_frames when the stream is in standby and there is nothing to
 * ask the hardware about. */
static int out_get_render_position(const struct audio_stream_out *stream,
                                    uint32_t *dsp_frames)
{
    struct mindone_stream_out *out = (struct mindone_stream_out *)stream;
    uint64_t frames = 0;
    struct timespec ts;

    if (!dsp_frames)
        return -EINVAL;

    if (out_get_presentation_position(stream, &frames, &ts) == 0)
        *dsp_frames = (uint32_t)frames;
    else
        *dsp_frames = (uint32_t)out->written_frames;
    return 0;
}

/* 15.09: accept and ignore, rather than -ENOSYS. Effects on this device are software effects
 * owned by the effects HAL (android.hardware.audio.effect) and applied by AudioFlinger before the
 * buffer ever reaches us; there is no hardware effect block on mt6789 we could attach one to.
 * Returning an error here makes AudioFlinger log a failure for a perfectly normal attach, so the
 * honest answer is "accepted, nothing for me to do". */
static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    (void)stream; (void)effect;
    return 0; /* v1/v2: no effects chain, see plan section 3 */
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    (void)stream; (void)effect;
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                         int64_t *timestamp)
{
    (void)stream; (void)timestamp;
    return -ENOSYS; /* optional */
}

/* Frames the speaker has actually produced, and when.
 *
 * MINDONE-AVSYNC: this used to answer written_frames plus "now", i.e. it claimed
 * everything handed to the buffer had already been heard. The position then ran ahead of
 * reality by the whole buffer depth -- period_size * period_count, hundreds of milliseconds
 * here. A ringtone never notices, because nothing compares it to anything. A video player
 * does: it drives audio/video sync off this clock, sees audio apparently far ahead of the
 * picture, and stops feeding audio to let the picture catch up. That is the "video plays
 * silently while ringtones work" symptom, and it is why the trail led to pcm_get_htimestamp --
 * the call that answers this question honestly.
 *
 * The kernel tells us how much room is free in the ring (avail) at a given timestamp;
 * buffer_size - avail is therefore what is still queued and not yet heard, so what HAS been
 * heard is written_frames minus that. -ENODATA while in standby is what the framework
 * expects: there is no stream to ask. */
static int out_get_presentation_position(const struct audio_stream_out *stream,
                                          uint64_t *frames, struct timespec *timestamp)
{
    struct mindone_stream_out *out = (struct mindone_stream_out *)stream;
    int ret = -ENODATA;

    if (!frames || !timestamp)
        return -EINVAL;

    pthread_mutex_lock(&out->lock);
    if (out->pcm && !out->standby) {
        unsigned int avail = 0;
        if (pcm_get_htimestamp(out->pcm, &avail, timestamp) == 0) {
            /* The ring runs at the PCM's own rate (the SCO band rate on btcvsd); the answer is
             * in stream frames, so what is queued -- in the ring and staged for it -- is scaled
             * back up before it is subtracted. */
            const int64_t buffer_frames =
                (int64_t)out->hw_config.period_size * (int64_t)out->hw_config.period_count;
            int64_t queued = buffer_frames - (int64_t)avail;
            if (queued < 0)
                queued = 0;          /* avail can briefly exceed the ring on xrun */
            queued += (int64_t)out->sco_staged;
            queued = queued * (int64_t)out->config.rate / (int64_t)out->hw_config.rate;
            int64_t presented = (int64_t)out->written_frames - queued;
            if (presented < 0)
                presented = 0;       /* right after start nothing has been heard yet */
            *frames = (uint64_t)presented;
            ret = 0;
        }
    }
    pthread_mutex_unlock(&out->lock);
    return ret;
}

/* ---- audio_stream_in -------------------------------------------------------- */

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct mindone_stream_in *in = (struct mindone_stream_in *)stream;
    return in->config.rate;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    (void)stream; (void)rate;
    return -ENOSYS;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct mindone_stream_in *in = (struct mindone_stream_in *)stream;
    return in->config.period_size * in->config.period_count *
           audio_stream_in_frame_size(&in->stream);
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    (void)stream;
    return AUDIO_CHANNEL_IN_MONO;
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    (void)stream;
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    (void)stream;
    return (format == AUDIO_FORMAT_PCM_16_BIT) ? 0 : -ENOSYS;
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

static int in_standby(struct audio_stream *stream)
{
    struct mindone_stream_in *in = (struct mindone_stream_in *)stream;
    int ret;

    pthread_mutex_lock(&in->lock);
    ret = do_in_standby(in);
    pthread_mutex_unlock(&in->lock);
    return ret;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    (void)stream; (void)fd;
    return 0;
}

/* v2: now actually re-routes a running capture stream on a device change, mirroring
 * out_set_parameters() -- v1 only stored in->device and never called apply_input_route(), so a
 * capture stream already open would never switch from "mic" to "headset-mic" (or back) even if
 * AudioFlinger asked it to. */
static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct mindone_stream_in *in = (struct mindone_stream_in *)stream;
    struct mindone_audio_device *adev = in->dev;
    struct str_parms *parms;
    char value[32];
    int ret;

    parms = str_parms_create_str(kvpairs);
    if (!parms)
        return -ENOMEM;

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING,
                             value, sizeof(value));
    if (ret >= 0) {
        audio_devices_t new_mask = (audio_devices_t)atoi(value);

        pthread_mutex_lock(&in->lock);
        in->device = new_mask;

        if (!in->standby) {
            enum input_device new_dev = input_device_from_mask(new_mask);

            if ((new_dev == IN_DEVICE_BT_SCO) != (in->routed_device == IN_DEVICE_BT_SCO)) {
                do_in_standby(in);      /* other PCM device: restart on the next read */
            } else {
                pthread_mutex_lock(&adev->lock);
                apply_input_route(adev, in->routed_device, new_dev);
                in->routed_device = new_dev;
                pthread_mutex_unlock(&adev->lock);
            }
        }
        pthread_mutex_unlock(&in->lock);
    }

    str_parms_destroy(parms);
    return 0;
}

static char *in_get_parameters(const struct audio_stream *stream, const char *keys)
{
    (void)stream; (void)keys;
    return strdup("");
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    struct mindone_stream_in *in = (struct mindone_stream_in *)stream;
    struct mindone_audio_device *adev = in->dev;
    struct mixer_ctl *ctl_l, *ctl_r;
    int min, max, value;

    if (!adev->mixer)
        return 0;

    /* "PGA1 Volume" / "PGA2 Volume": single controls, one per ADC channel
     * (mt6358.c:740-745), not a stereo pair -- set both to the same gain. */
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

/* v2: picks the mixer path from the stream's requested device mask (set at
 * adev_open_input_stream() time and by in_set_parameters()) rather than hardcoding
 * IN_DEVICE_MAIN_MIC -- see input_device_from_mask(). */
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

    pthread_mutex_lock(&adev->lock);
    if (new_dev == IN_DEVICE_BT_SCO)
        bt_sco_configure(adev);
    apply_input_route(adev, IN_DEVICE_NONE, new_dev);
    adev->active_in = in;
    /* Read by adev_create_audio_patch() under adev->lock alone, so written under it too. */
    in->routed_device = new_dev;
    in->standby = false;
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static ssize_t in_read(struct audio_stream_in *stream, void *buffer, size_t bytes)
{
    struct mindone_stream_in *in = (struct mindone_stream_in *)stream;
    ssize_t result = (ssize_t)bytes;
    int ret;

    pthread_mutex_lock(&in->lock);
    pthread_mutex_lock(&in->dev->lock);
    ret = in->reopen_pending && !in->standby;
    pthread_mutex_unlock(&in->dev->lock);
    if (ret)
        do_in_standby(in);

    if (in->standby) {
        ret = start_input_stream(in);
        if (ret != 0) {
            pthread_mutex_unlock(&in->lock);
            memset(buffer, 0, bytes);
            usleep((uint64_t)bytes * 1000000 /
                   audio_stream_in_frame_size(stream) / in->config.rate);
            return bytes;
        }
    }

    if (in->routed_device == IN_DEVICE_BT_SCO) {
        /* Mono at the band rate from btcvsd, linearly interpolated up to the stream rate. The
         * ratio is whole (3 or 6) and the framework's reads are period-sized, so the source
         * count is exact; a read that does not divide loses at most ratio-1 frames of silence
         * at its end, which no caller here produces. */
        const unsigned int ratio = in->config.rate / in->hw_config.rate;
        const size_t frames = bytes / sizeof(int16_t);          /* mono S16 out */
        const size_t src_frames = frames / ratio;
        int16_t *dst = (int16_t *)buffer;
        size_t i;

        if (in->sco_in_frames < src_frames) {
            int16_t *nb = (int16_t *)realloc(in->sco_in_buf, src_frames * sizeof(int16_t));
            if (!nb) {
                memset(buffer, 0, bytes);
                goto done;
            }
            in->sco_in_buf = nb;
            in->sco_in_frames = src_frames;
        }
        ret = pcm_read(in->pcm, in->sco_in_buf, src_frames * sizeof(int16_t));
        if (ret != 0) {
            result = ret;
            goto done;
        }
        for (i = 0; i < src_frames * ratio; i++) {
            const size_t k = i / ratio;
            const int32_t prev = (k == 0) ? in->sco_last : in->sco_in_buf[k - 1];
            const int32_t cur = in->sco_in_buf[k];
            const int32_t step = (int32_t)(i % ratio) + 1;
            dst[i] = (int16_t)(prev + (cur - prev) * step / (int32_t)ratio);
        }
        for (; i < frames; i++)
            dst[i] = 0;
        if (src_frames)
            in->sco_last = in->sco_in_buf[src_frames - 1];
        in->read_frames += frames;
        goto done;
    }

    /* The lock stays held across pcm_read(), as out_write() holds it across pcm_write(): standby()
     * from another thread closes in->pcm, and a read still in flight on it would use freed
     * memory. The cost is one buffer of waiting (~20 ms) for standby()/set_parameters(). */

    /* The framework asked for MONO (in_get_channels / adev_open_input_stream), but the PCM is
     * opened with IN_CHANNEL_COUNT channels because that is what the capture memif delivers.
     * Handing the interleaved stereo frames straight to the caller made every right-channel
     * sample look like a second mono sample: twice as many frames as real time, so a recording
     * played back at half speed and an octave down (measured 15.09 - a 1 kHz reference tone came
     * back as 500 Hz in a 2x too long file). Downmix here instead. */
    if (in->config.channels == 1) {
        ret = pcm_read(in->pcm, buffer, bytes);
        if (ret == 0)
            in->read_frames += bytes / audio_stream_in_frame_size(stream);
        else
            result = ret;
        goto done;
    }

    {
        const size_t frames = bytes / sizeof(int16_t);  /* mono, S16 */
        const size_t need = frames * in->config.channels;
        int16_t *dst = (int16_t *)buffer;
        size_t i;

        if (in->downmix_frames < frames) {
            int16_t *nb = (int16_t *)realloc(in->downmix_buf, need * sizeof(int16_t));
            if (!nb) {
                memset(buffer, 0, bytes);
                goto done;
            }
            in->downmix_buf = nb;
            in->downmix_frames = frames;
        }

        ret = pcm_read(in->pcm, in->downmix_buf, need * sizeof(int16_t));
        if (ret != 0) {
            result = ret;
            goto done;
        }

        /* Average the pads rather than dropping one: both carry the same acoustic scene, so the
         * sum is 3 dB quieter in uncorrelated noise and keeps a pad failure from muting capture. */
        for (i = 0; i < frames; i++) {
            const int32_t l = in->downmix_buf[i * in->config.channels];
            const int32_t r = in->downmix_buf[i * in->config.channels + 1];
            int32_t v = ((l + r) / 2) * IN_DIGITAL_GAIN_Q8 >> 8;

            /* Clamp rather than let the int16 store wrap: an overflow inverts the sample and
             * turns a loud passage into a burst of noise, which is far worse than the flat top
             * of a clipped peak. */
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

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    (void)stream;
    return 0;
}

static int in_get_capture_position(const struct audio_stream_in *stream,
                                    int64_t *frames, int64_t *time)
{
    struct mindone_stream_in *in = (struct mindone_stream_in *)stream;
    struct timespec ts;

    if (!frames || !time)
        return -EINVAL;
    pthread_mutex_lock(&in->lock);
    *frames = (int64_t)in->read_frames;
    pthread_mutex_unlock(&in->lock);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *time = ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return 0;
}

static int in_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    (void)stream; (void)effect;
    return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    (void)stream; (void)effect;
    return 0;
}

/* ---- audio_hw_device -------------------------------------------------------- */

static int adev_open_output_stream(struct audio_hw_device *dev,
                                    audio_io_handle_t handle,
                                    audio_devices_t devices,
                                    audio_output_flags_t flags,
                                    struct audio_config *config,
                                    struct audio_stream_out **stream_out,
                                    const char *address)
{
    struct mindone_audio_device *adev = (struct mindone_audio_device *)dev;
    struct mindone_stream_out *out;
    (void)address;

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

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;

    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.get_presentation_position = out_get_presentation_position;

    out->dev = adev;
    out->flags = flags;
    out->device = devices;
    out->standby = true;
    out->routed_device = OUT_DEVICE_NONE;
    out->crossbar_on = false;
    out->hw_config = out->config;
    /* Unity until the limiter has seen audio; calloc would otherwise leave it at 0 and the first
     * second of every stream would fade in from silence. */
    out->lim_gain = 1.0f;
    pthread_mutex_init(&out->lock, NULL);

    config->format = AUDIO_FORMAT_PCM_16_BIT;
    config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    config->sample_rate = OUT_SAMPLING_RATE;

    /* Register the stream so a device change can move it along with the others. The table is
     * sized for far more outputs than the policy ever opens on this device (three); if it does
     * fill up the stream still works, it just will not follow later audio patches. */
    pthread_mutex_lock(&adev->lock);
    if (adev->n_outs < (int)(sizeof(adev->outs) / sizeof(adev->outs[0])))
        adev->outs[adev->n_outs++] = out;
    else
        ALOGW("%s: more than %zu output streams open, this one is not tracked for patches",
              __func__, sizeof(adev->outs) / sizeof(adev->outs[0]));
    pthread_mutex_unlock(&adev->lock);

    *stream_out = &out->stream;
    return 0;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                      struct audio_stream_out *stream)
{
    struct mindone_stream_out *out = (struct mindone_stream_out *)stream;
    struct mindone_audio_device *adev = (struct mindone_audio_device *)dev;
    int i;

    out_standby(&stream->common);       /* gives the endpoint back if it still held one */

    pthread_mutex_lock(&adev->lock);
    for (i = 0; i < adev->n_outs; i++) {
        if (adev->outs[i] == out) {
            adev->outs[i] = adev->outs[--adev->n_outs];
            break;
        }
    }
    pthread_mutex_unlock(&adev->lock);

    pthread_mutex_destroy(&out->lock);
    free(out->dsp_buf);
    free(out->sco_buf);
    free(out);
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   struct audio_config *config,
                                   struct audio_stream_in **stream_in,
                                   audio_input_flags_t flags,
                                   const char *address,
                                   audio_source_t source)
{
    struct mindone_audio_device *adev = (struct mindone_audio_device *)dev;
    struct mindone_stream_in *in;
    (void)flags; (void)address; (void)source;

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

    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;

    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;
    in->stream.get_capture_position = in_get_capture_position;

    in->dev = adev;
    in->device = devices;
    in->standby = true;
    in->routed_device = IN_DEVICE_NONE;
    pthread_mutex_init(&in->lock, NULL);

    config->format = AUDIO_FORMAT_PCM_16_BIT;
    config->channel_mask = AUDIO_CHANNEL_IN_MONO;
    config->sample_rate = IN_SAMPLING_RATE;

    *stream_in = &in->stream;
    return 0;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                     struct audio_stream_in *stream)
{
    struct mindone_stream_in *in = (struct mindone_stream_in *)stream;
    (void)dev;

    in_standby(&stream->common);
    pthread_mutex_destroy(&in->lock);
    free(in->downmix_buf);
    free(in->sco_in_buf);
    free(in);
}

/* 16.09: the SCO band. The Bluetooth stack announces the codec it negotiated with the headset
 * as "bt_wbs=on|off" (mSBC) and, on newer stacks, "bt_swb=on|off" (LC3); btcvsd knows only NB
 * and WB, so super-wideband is served as WB. A SCO stream already open runs at the old rate and
 * is restarted by its next write/read. Everything else is accepted and ignored, as before. */
static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct mindone_audio_device *adev = (struct mindone_audio_device *)dev;
    struct str_parms *parms;
    char value[32];
    bool wb, changed = false;
    int i;

    parms = str_parms_create_str(kvpairs);
    if (!parms)
        return -ENOMEM;

    pthread_mutex_lock(&adev->lock);
    wb = adev->bt_wb;
    if (str_parms_get_str(parms, "bt_wbs", value, sizeof(value)) >= 0) {
        wb = (strcmp(value, "on") == 0);
        changed = true;
    }
    if (str_parms_get_str(parms, "bt_swb", value, sizeof(value)) >= 0 && strcmp(value, "on") == 0) {
        wb = true;
        changed = true;
    }
    if (changed && wb != adev->bt_wb) {
        adev->bt_wb = wb;
        ALOGI("%s: BT SCO band -> %s", __func__, wb ? "WB" : "NB");
        for (i = 0; i < adev->n_outs; i++)
            if (adev->outs[i]->routed_device == OUT_DEVICE_BT_SCO)
                adev->outs[i]->reopen_pending = true;
        if (adev->active_in && adev->active_in->routed_device == IN_DEVICE_BT_SCO)
            adev->active_in->reopen_pending = true;
    }
    pthread_mutex_unlock(&adev->lock);

    str_parms_destroy(parms);
    return 0;
}

static char *adev_get_parameters(const struct audio_hw_device *dev, const char *keys)
{
    (void)dev; (void)keys;
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    (void)dev;
    return 0;
}

/* 15.09: applied for real now. Outside a call we only remember the value - the endpoint gain is
 * owned by the media stream then, and stamping the call volume over it would quietly change
 * music loudness when the framework pre-sets the call level. */
static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    struct mindone_audio_device *adev = (struct mindone_audio_device *)dev;

    pthread_mutex_lock(&adev->lock);
    adev->voice_volume = volume;
    if (adev->in_call)
        set_hw_output_volume(adev, adev->voice_out_device, volume);
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    (void)dev; (void)volume;
    return -ENOSYS; /* per-stream set_volume() is what v1/v2 actually wire up */
}

static int adev_get_master_volume(struct audio_hw_device *dev, float *volume)
{
    (void)dev;
    if (!volume)
        return -EINVAL;
    *volume = 1.0f;
    return -ENOSYS;
}

static int adev_set_master_mute(struct audio_hw_device *dev, bool muted)
{
    (void)dev; (void)muted;
    return -ENOSYS;
}

static int adev_get_master_mute(struct audio_hw_device *dev, bool *muted)
{
    (void)dev;
    if (!muted)
        return -EINVAL;
    *muted = false;
    return -ENOSYS;
}

/* 15.09: this is the single entry point for voice calls. AudioFlinger switches the mode before
 * the call audio starts and back to NORMAL after it ends, so bringing the modem crossbar up and
 * down here is both sufficient and race-free - no other path touches in_call. */
static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    struct mindone_audio_device *adev = (struct mindone_audio_device *)dev;
    bool want_call;

    pthread_mutex_lock(&adev->lock);
    if (mode == adev->mode) {
        pthread_mutex_unlock(&adev->lock);
        return 0;
    }
    adev->mode = mode;

    /* IN_COMMUNICATION (VoIP) does NOT go through the modem - its audio is ordinary playback and
     * capture from the app. Only IN_CALL means a circuit call on the "PCM 2" backend. */
    want_call = (mode == AUDIO_MODE_IN_CALL);

    if (want_call)
        voice_route_enable(adev, voice_output_device(adev));
    else
        voice_route_disable(adev, true);

    pthread_mutex_unlock(&adev->lock);
    return 0;
}

/* 15.09: a real mute during a call, by cutting the uplink crossbar rather than guessing at gain
 * semantics. The v2 note here asked whether zeroing "PGA1/2 Volume" was the intended mechanism -
 * it is not needed: breaking ADDA_UL -> PCM_2_PB guarantees the modem receives nothing, which is
 * exactly what mute has to promise, and it is directly observable with tinymix.
 *
 * Outside a call this stays a stored flag: capture streams are muted by the framework, and
 * tearing down mic routing under a running recorder would be wrong. */
static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct mindone_audio_device *adev = (struct mindone_audio_device *)dev;

    pthread_mutex_lock(&adev->lock);
    if (adev->mic_mute != state) {
        struct mixer_ctl *sco_mute;

        adev->mic_mute = state;
        if (adev->in_call) {
            audio_route_apply_path(adev->route,
                                   state ? "voice-uplink-off" : "voice-uplink");
            audio_route_update_mixer(adev->route);
        }
        /* A SCO call does not go through the AFE uplink at all - btcvsd has its own mute, and it
         * is harmless to set whether or not SCO is currently the active route. */
        sco_mute = adev->mixer ? mixer_get_ctl_by_name(adev->mixer, "BTCVSD Tx Mute Switch")
                               : NULL;
        if (sco_mute)
            mixer_ctl_set_value(sco_mute, 0, state ? 1 : 0);
    }
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    struct mindone_audio_device *adev = (struct mindone_audio_device *)dev;
    if (!state)
        return -EINVAL;
    *state = adev->mic_mute;
    return 0;
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                          const struct audio_config *config)
{
    size_t frame_size;
    (void)dev;

    if (!config)
        return 0;

    frame_size = audio_bytes_per_sample(config->format) *
                 audio_channel_count_from_in_mask(config->channel_mask);
    return IN_PERIOD_SIZE * IN_PERIOD_COUNT * frame_size;
}

/* ---- Audio patches (AUDIO_DEVICE_API_VERSION_3_0) -----------------------------
 * 15.09. A patch is the framework saying "connect these sources to these sinks". For a primary
 * HAL the only ones that reach us are mix->device (playback routing) and device->mix (capture
 * routing); everything else (A2DP, USB, remote submix) belongs to another HAL.
 *
 * These deliberately reuse apply_output_route()/apply_input_route() rather than re-deriving
 * routing: a second code path that decides endpoints is exactly how the two halves drift apart.
 *
 * Patch handles are not tracked individually. Output endpoints are reference-counted across the
 * open streams (see endpoint_acquire()), and a stream gives its endpoint back in standby, which
 * is the one place that knows whether the stream is still running. Tracking a table of handles
 * would buy nothing here and could disagree with the stream state.
 */
static int adev_create_audio_patch(struct audio_hw_device *dev,
                                    unsigned int num_sources,
                                    const struct audio_port_config *sources,
                                    unsigned int num_sinks,
                                    const struct audio_port_config *sinks,
                                    audio_patch_handle_t *handle)
{
    struct mindone_audio_device *adev = (struct mindone_audio_device *)dev;

    if (!sources || !sinks || num_sources < 1 || num_sinks < 1 || !handle)
        return -EINVAL;

    pthread_mutex_lock(&adev->lock);

    if (sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {
        /* Playback: route the output endpoint at the sink device. */
        enum output_device d = output_device_from_mask(sinks[0].ext.device.type);

        if (adev->in_call) {
            /* A device change mid-call (earpiece -> speakerphone) has to move the whole voice
             * route, not just the endpoint, because the echo reference depends on it. */
            voice_route_disable(adev, false);
            voice_route_enable(adev, d);
        } else {
            /* One patch per output thread: the source mix port names it by the io handle we
             * were given at open. With API 3.0 the framework routes ONLY through patches -- no
             * routing key ever reaches out_set_parameters() -- so out->device is updated here
             * for playing and standby streams alike; a standby stream picks it up when it next
             * starts. A playing stream is re-routed in place, unless the new device lives on
             * another PCM (SCO), in which case it restarts from its own write path. Should a
             * patch carry no handle we recognise, every stream is moved, which is what the
             * framework means by a device change anyway. */
            int i, matched = 0;
            unsigned int si;

            for (i = 0; i < adev->n_outs; i++) {
                struct mindone_stream_out *o = adev->outs[i];
                bool mine = false;

                for (si = 0; si < num_sources; si++)
                    if (sources[si].type == AUDIO_PORT_TYPE_MIX &&
                        sources[si].ext.mix.handle == o->io_handle)
                        mine = true;
                if (!mine)
                    continue;
                matched++;
                o->device = sinks[0].ext.device.type;
                if (o->standby || o->routed_device == OUT_DEVICE_NONE)
                    continue;
                if ((d == OUT_DEVICE_BT_SCO) != (o->routed_device == OUT_DEVICE_BT_SCO)) {
                    o->reopen_pending = true;
                } else if (o->routed_device != d) {
                    apply_output_route(adev, o->routed_device, d);
                    o->routed_device = d;
                }
            }
            if (!matched) {
                for (i = 0; i < adev->n_outs; i++) {
                    struct mindone_stream_out *o = adev->outs[i];

                    o->device = sinks[0].ext.device.type;
                    if (o->standby || o->routed_device == d ||
                        o->routed_device == OUT_DEVICE_NONE)
                        continue;
                    if ((d == OUT_DEVICE_BT_SCO) != (o->routed_device == OUT_DEVICE_BT_SCO)) {
                        o->reopen_pending = true;
                    } else {
                        apply_output_route(adev, o->routed_device, d);
                        o->routed_device = d;
                    }
                }
            }
        }
    } else if (sources[0].type == AUDIO_PORT_TYPE_DEVICE) {
        /* Capture: the source device selects which mic path is applied. Only one capture PCM
         * is ever open on this HAL (maxOpenCount=1 in the policy), so active_in is the stream. */
        enum input_device d = input_device_from_mask(sources[0].ext.device.type);
        struct mindone_stream_in *in = adev->active_in;

        if (in) {
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
    }

    pthread_mutex_unlock(&adev->lock);

    *handle = AUDIO_PATCH_HANDLE_NONE + 1;   /* single implicit patch, see the note above */
    return 0;
}

static int adev_release_audio_patch(struct audio_hw_device *dev,
                                     audio_patch_handle_t handle)
{
    (void)dev; (void)handle;
    /* Endpoints are released by standby (do_out_standby()/do_in_standby()), which is what
     * actually knows whether a stream is still running on them. Undoing routing here would cut
     * audio out from under a live stream when the framework rebuilds its patch graph. */
    return 0;
}

static int adev_get_audio_port(struct audio_hw_device *dev, struct audio_port *port)
{
    (void)dev; (void)port;
    /* Port capabilities come from audio_policy_configuration.xml on this device; there is nothing
     * the HAL could report that the policy does not already state. */
    return -ENOSYS;
}

static int adev_set_audio_port_config(struct audio_hw_device *dev,
                                       const struct audio_port_config *config)
{
    struct mindone_audio_device *adev = (struct mindone_audio_device *)dev;

    if (!config)
        return -EINVAL;

    /* 🔴 15.09, fixed same day: this used to apply adev->voice_volume for ANY device-port gain
     * config. voice_volume is only meaningful during a call, it starts at 0, and the framework
     * configures ports outside calls too - so a routine port config drove the codec's analogue
     * gain to its minimum and made everything quiet for no visible reason. Caught on the device:
     * "Lineout Volume: 0 0 (dsrange 0->18)" with audio still playing.
     *
     * The honest answer is -ENOSYS: the gain in a port config is expressed in millibels on the
     * framework's own scale, and we have no mapping from that to this codec's 0..18 steps that is
     * not a guess. Per-stream out_set_volume() and set_voice_volume() are the paths that do have a
     * defined 0..1 scale, and they already drive the same control. */
    (void)adev;
    return -ENOSYS;
}

static int adev_dump(const struct audio_hw_device *dev, int fd)
{
    (void)dev; (void)fd;
    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct mindone_audio_device *adev = (struct mindone_audio_device *)device;

    if (adev->route)
        audio_route_free(adev->route);
    if (adev->mixer)
        mixer_close(adev->mixer);
    pthread_mutex_destroy(&adev->lock);
    free(adev);
    return 0;
}

static int adev_open(const hw_module_t *module, const char *name,
                      hw_device_t **device)
{
    struct mindone_audio_device *adev;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    adev = (struct mindone_audio_device *)calloc(1, sizeof(*adev));
    if (!adev)
        return -ENOMEM;

    /* 🔴 15.09: calloc leaves voice_volume at 0, and 0 on this codec is the QUIETEST analogue
     * step, not silence-until-set. Anything that applies it before the framework has told us a
     * real call volume would therefore turn the sound down rather than leave it alone. Start at
     * full and let set_voice_volume() lower it. */
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

    adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_3_0;
    adev->hw_device.common.module = (struct hw_module_t *)module;
    adev->hw_device.common.close = adev_close;

    adev->hw_device.init_check = adev_init_check;
    adev->hw_device.set_voice_volume = adev_set_voice_volume;
    adev->hw_device.set_master_volume = adev_set_master_volume;
    adev->hw_device.get_master_volume = adev_get_master_volume;
    adev->hw_device.set_master_mute = adev_set_master_mute;
    adev->hw_device.get_master_mute = adev_get_master_mute;
    adev->hw_device.set_mode = adev_set_mode;
    adev->hw_device.set_mic_mute = adev_set_mic_mute;
    adev->hw_device.get_mic_mute = adev_get_mic_mute;
    adev->hw_device.set_parameters = adev_set_parameters;
    adev->hw_device.get_parameters = adev_get_parameters;
    adev->hw_device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream = adev_open_output_stream;
    adev->hw_device.close_output_stream = adev_close_output_stream;
    adev->hw_device.open_input_stream = adev_open_input_stream;
    adev->hw_device.close_input_stream = adev_close_input_stream;
    adev->hw_device.dump = adev_dump;
    /* 15.09: implemented rather than left NULL. The NULL convention is supported (AudioFlinger
     * falls back to routing through set_parameters, which is why v2 worked), but with patches in
     * place routing is driven by the framework's actual port graph instead of a parsed string,
     * and device changes during a call arrive here too. */
    adev->hw_device.create_audio_patch = adev_create_audio_patch;
    adev->hw_device.release_audio_patch = adev_release_audio_patch;
    adev->hw_device.get_audio_port = adev_get_audio_port;
    adev->hw_device.set_audio_port_config = adev_set_audio_port_config;

    *device = &adev->hw_device.common;
    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "MindOne audio HW HAL (tinyalsa, MT6789/MT6366)",
        .author = "mind_one project",
        .methods = &hal_module_methods,
    },
};
