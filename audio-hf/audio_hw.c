/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * audio.primary.mindone -- legacy tinyalsa audio HAL for iKKO MindOne
 * (MT6789 SoC + MT6366 codec, aw87xxx smart PA over I2C).
 *
 * v2 (13/14.09.2026 night) -- supersedes the v1 draft. Full route derivation
 * and every control-name citation is in AUDIO-ROUTES-1309; the HAL-level design rationale
 * is in AUDIO-HAL-PLAN-1309. Both are working notes outside this tree; the comments here are
 * written to stand without them.
 *
 * WHAT CHANGED FROM v1 (functional fixes, found by cross-checking v1 against
 * AUDIO-ROUTES-1309's source-verified route map):
 *
 *   1. AFE crossbar switches (AUDIO-ROUTES-1309 section 5, F4308). v1 only ever applied
 *      the codec-side device mux paths ("speaker"/"headphone"/"mic") and never touched the
 *      separate hardware summing-mixer layer between the DL/UL memifs and ADDA
 *      ("ADDA_DL_CH1 DL1_CH1", "UL1_CH1 ADDA_UL_CH1", ...). Without these, v1 would have opened
 *      PCM successfully and produced no audible output/input at all. Fixed here: every output
 *      stream now also applies/tears down a "dlN-adda" crossbar path keyed to its own PCM device
 *      (independent of, and orthogonal to, the speaker/headphone device-selection path -- see
 *      apply_dl_crossbar()/teardown_dl_crossbar()); the capture crossbar is folded directly into
 *      mixer_paths.xml's mic/mic-back/headset-mic paths since v2 only ever opens one capture PCM
 *      device (UL1) regardless of which mic source is selected.
 *   2. Wired headset mic (AUDIO-ROUTES-1309 section 2.5). v1 had no input-device
 *      selection logic at all -- in_set_parameters() stored the requested device mask but never
 *      acted on it, so a capture stream always used the "mic" (main, AIN0) path regardless of what
 *      AudioFlinger asked for. Fixed here: input_device_from_mask() now recognizes
 *      AUDIO_DEVICE_IN_WIRED_HEADSET and routes to the new "headset-mic" (AIN1) mixer path;
 *      in_set_parameters() now re-routes a running capture stream on a device change, mirroring
 *      what out_set_parameters() already did for output. NOTE: this path is currently
 *      unreachable through Android's audio policy on this product (F4309, no
 *      AUDIO_DEVICE_IN_WIRED_HEADSET devicePort in this device's audio_policy_configuration.xml)
 *      -- implemented anyway as correct, faithful plumbing rather than left as a silent no-op, in
 *      case policy is ever extended.
 *
 * v1 scope, unchanged (AUDIO-HAL-PLAN-1309 section 3):
 *   - media playback to speaker / headphone
 *   - mic capture: main mic (AIN0) and, new in v2, wired headset mic (AIN1) -- back mic (AIN2)
 *     mixer path exists in mixer_paths.xml but is not wired into device selection here (physical
 *     presence of a second mic pad on this board is unconfirmed, AUDIO-ROUTES-1309
 *     section 2.4)
 *   - routing switch on device change
 *   - volume via the codec's own hardware gain kcontrols
 *   - standby that closes the PCM and cuts this stream's AFE crossbar; the speaker amp and
 *     the codec-side device path are shared and go down only with the last stream (F4535)
 *   - VOICE CALLS (new 15.09): the modem "PCM 2" backend crossbar, earpiece/speaker selection,
 *     call volume on the endpoint's hardware gain, real mic mute by cutting the uplink, and echo
 *     reference for the speakerphone case. Driven entirely from adev_set_mode(): AUDIO_MODE_IN_CALL
 *     brings the route up, anything else tears it down. No MD/CCCI/IPI command is sent - the modem
 *     streams on its own once the crossbar is connected; if a measurement ever shows it needs an
 *     explicit kick, voice_route_enable() is the one place to add it.
 *
 *   - BT SCO (15.09, made real 16.09): the kernel btcvsd transport on PCM 46 and mute through
 *     "BTCVSD Tx Mute Switch". It sits beside the AFE, not on its crossbar, so a SCO stream skips
 *     both the dlN-adda crossbar and the endpoint paths. btcvsd carries mono S16 at the SCO band
 *     rate (8 kHz CVSD, 16 kHz mSBC) in 60-byte packets (mtk_btcvsd.c: BTCVSD_TX_PACKET_SIZE, and
 *     a write that is not a multiple of it is refused), while the framework hands this HAL 48 kHz
 *     stereo. So a stream routed to SCO opens PCM 46 with the band's own config and converts in
 *     software: downmix + decimation on the way out (sco_out_convert()), interpolation on the way
 *     in (in_read()). The band follows the bt_wbs / bt_swb parameters the Bluetooth stack sets,
 *     NB until told otherwise as HFP requires. A device change between SCO and anything else
 *     needs a different PCM device and config, so it restarts the stream (reopen_pending)
 *     instead of re-routing it in place.
 *     KNOWN GAP: a VOICE CALL over SCO. On this SoC the modem's speech does not reach Bluetooth by
 *     itself; the stock HAL runs a copy loop between the modem memif ("PCM 2") and btcvsd in both
 *     directions. That loop is not written yet, so voice_route_enable() keeps such a call on the
 *     earpiece and says so in the log, rather than routing it into silence.
 *   - audio patches / ports (new 15.09): create/release_audio_patch drive routing from the
 *     framework's port graph instead of a parsed set_parameters string, reusing the same
 *     apply_output_route()/apply_input_route() so the two cannot drift apart. A device change
 *     mid-call moves the whole voice route, because the echo reference depends on the endpoint.
 *   - effects: accepted as a no-op. Effects here are software, owned by the effects HAL and
 *     applied before the buffer reaches us; there is no hardware effect block on mt6789.
 *
 * Explicitly NOT implemented, with the reason:
 *   - FM: there is no FM hardware exposed on this board - zero mixer controls matching "fm",
 *     no tuner in audio_policy_configuration.xml, no kernel module (checked on device 15.09).
 *     Writing an FM path would be dead code.
 *   - DSP/SCP compressed offload: a power optimisation, not a function. Without it AudioFlinger
 *     decodes in software and everything still plays; wiring it needs the SCP/ADSP IPI protocol,
 *     which is a separate piece of work.
 *
 * PCM device numbers, memif roles and every mixer control name/value here are cited in
 * AUDIO-ROUTES-1309 against our own compiled kernel sources
 * (the kernel tree's mindone/modules/) and cross-checked against the stock firmware's
 * audio_device.xml routing table. Anywhere that cross-check was NOT possible
 * (source and stock config could not agree, or stock config was silent, or the path is not
 * reachable from Android policy at all on this product) is marked VERIFY-ON-DEVICE or NOTE below,
 * with the read-only command/caveat to confirm.
 *
 * !! VERIFY BEFORE FIRST BUILD !! This is written against the classic legacy audio_hw_device_t /
 * audio_stream_out / audio_stream_in struct shape (hardware/libhardware/include/hardware/audio.h).
 * Designated initializers are used everywhere specifically so that a field this tree's exact
 * header revision doesn't have (or expects in a different shape) fails the build loudly instead
 * of silently misordering the vtable. If it doesn't compile as shipped, fix the struct-literal
 * mismatch against the real header -- do not switch to positional init.
 */

#define LOG_TAG "audio_hw_mindone"

#include <errno.h>
#include <inttypes.h>
#include <malloc.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <log/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <hardware/audio.h>
#include <hardware/hardware.h>

#include <tinyalsa/asoundlib.h>
#include <audio_route/audio_route.h>

/* ---- Card / PCM device numbers -------------------------------------------
 * From the device's /proc/asound (card 0, "mt6789-mt6366") and the FE
 * dai_link -> memif decode in AUDIO-ROUTES-1309 section 1.
 */
#define PCM_CARD                0

#define PCM_DEVICE_PRIMARY      0   /* Playback_1 / DL1  -- MT6789_PRIMARY_MEMIF */
#define PCM_DEVICE_FAST         2   /* Playback_2 / DL2  -- MT6789_FAST_MEMIF    */
#define PCM_DEVICE_DEEP_BUFFER  3   /* Playback_3 / DL3  -- MT6789_DEEP_MEMIF    */
#define PCM_DEVICE_CAPTURE_MAIN 9   /* Capture_1  / UL1  -- MT6789_RECORD_MEMIF  */
/* 15.09: BT SCO. Verified on the live device: "00-46: BTCVSD snd-soc-dummy-dai-46, playback 1,
 * capture 1", module mtk_btcvsd loaded. This is a kernel voice de/encoder, NOT a memif on the AFE
 * crossbar (AUDIO-ROUTES-1309 section 4) - so a SCO stream deliberately skips both the
 * dlN-adda crossbar and the speaker/headphone endpoint paths. */
#define PCM_DEVICE_BTCVSD       46
#define SCO_RATE_NB             8000
#define SCO_RATE_WB             16000

/* Rates/formats per AUDIO-ROUTES-1309 section 1.1
 * (mt6789-afe-pcm.c: MTK_PCM_RATES / MTK_PCM_FORMATS). v2 opens everything at a single
 * conservative operating point rather than the full range the AFE advertises -- AudioFlinger
 * resamples above/below this in software.
 *
 * VERIFY-ON-DEVICE: period_size/period_count below are still NOT decoded from source (would need
 * the platform hw_params constraint logic in mt6789-afe-pcm.c's memif ops, not read in this pass
 * either) -- they are the typical MTK reference default. Confirm against actual xrun behavior
 * with tinyplay before trusting this for anything latency-sensitive.
 */
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
    IN_DEVICE_HEADSET_MIC,      /* new in v2: AIN1 pad, AUDIO-ROUTES-1309 section 2.5 */
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
    enum output_device voice_out_device;

    struct mindone_stream_out *active_out;
    struct mindone_stream_in *active_in;

    /* How many non-standby streams currently use each output endpoint ("speaker",
     * "headphone", ...). AudioFlinger keeps several output streams open on the same device at
     * once -- primary, fast and deep_buffer -- and each of them enters standby on its own clock.
     * The codec-side path (LOL Mux, the aw87xxx amp, RCV Mux) is shared by all of them, so it
     * may only be torn down when the LAST user leaves. Before this count existed, a 3-second UI
     * sound going to standby applied "speaker-off" underneath a deep_buffer stream that was still
     * playing: the DMA kept running into an open codec mux and the phone went silent until the
     * next short sound re-applied "speaker" -- for exactly one standby period (F4535). */
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

/* ---- Routing helpers ------------------------------------------------------
 * Path names below match mixer_paths.xml next to this file exactly (see that file and
 * AUDIO-ROUTES-1309 for the mixer control names/values and their kernel-source
 * citations).
 */

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

/* ---- AFE crossbar (new in v2) ----------------------------------------------
 * AUDIO-ROUTES-1309 section 5 / F4308: separate from device selection above. Every DL
 * memif needs its own "ADDA_DL_CHn DLx_CHn" switches turned on before its samples reach ADDA,
 * regardless of which physical output device (speaker/headphone) is currently selected. These
 * three paths touch disjoint control names, so they compose safely if more than one output
 * stream is open at once. */
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

/* ---- Input routing (rewritten in v2) ---------------------------------------
 * v1's apply_input_route()/teardown_input_route() only ever knew about IN_DEVICE_MAIN_MIC and
 * were never actually called from in_set_parameters() -- a running capture stream could not be
 * re-routed on a device change. Fixed to mirror the output side exactly. The capture-side AFE
 * crossbar ("UL1_CHn ADDA_UL_CHn") is folded directly into mixer_paths.xml's mic/mic-back/
 * headset-mic paths (unlike the DL side) since v2 only ever opens one capture PCM device (UL1)
 * regardless of which mic source is selected -- see AUDIO-ROUTES-1309 section 5.1. */

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
    /* Built-in back mic (AIN2) is deliberately not selected here: physical presence of a second
     * mic pad on this board is unconfirmed (AUDIO-ROUTES-1309 section 2.4) -- main mic
     * (AIN0) is the safe default, matching v1's scope. */
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

/* ---- Hardware volume ------------------------------------------------------
 * v1/v2 use the codec's own gain kcontrols rather than a software multiplier, per
 * AUDIO-HAL-PLAN-1309 section 3 ("matches battery-first: no extra DSP/CPU gain stage").
 * "Headset Volume"/"Lineout Volume" are double (L/R) TLV controls (mt6358.c:729-734); read the
 * live range instead of hardcoding it, since the exact max index (0x12 in source) could differ
 * from what the running kernel reports.
 */
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

/* ---- Voice call --------------------------------------------------------------
 * 15.09. The AFE side of a call is nothing but crossbar switches: the modem streams over the
 * "PCM 2" backend on its own, so the HAL's whole job is to connect it to the codec, pick the
 * endpoint, set the gain, and undo all of that on hang-up. Control names in mixer_paths.xml were
 * each verified to exist on the live device before being used (AUDIO-ROUTES-1309 sec. 3).
 *
 * Deliberately NOT done here: no MD/CCCI/IPI command is sent. The modem brings its own audio up
 * when the call connects; if a future measurement shows it needs an explicit kick, that is the
 * one place to add it - not a reason to re-route anything below.
 *
 * Caller must hold adev->lock.
 */
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
    adev->in_call = true;

    set_hw_output_volume(adev, out, adev->voice_volume);
    ALOGI("%s: voice route up on %s", __func__, output_path_name(out) ? output_path_name(out) : "bt-sco");
}

static void voice_route_disable(struct mindone_audio_device *adev)
{
    if (!adev->in_call)
        return;

    audio_route_apply_path(adev->route, "voice-echo-ref-off");
    audio_route_apply_path(adev->route, "voice-uplink-off");
    audio_route_apply_path(adev->route, "voice-downlink-off");
    audio_route_apply_path(adev->route, "mic-off");
    audio_route_update_mixer(adev->route);
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

/* Cuts the PCM, the AFE crossbar and the amp -- the concrete "standby/idle" behavior
 * AUDIO-HAL-PLAN-1309 section 3 calls for: closing pcm_close() stops the digital path,
 * tearing down the crossbar (new in v2) stops this memif's samples from being summed into ADDA at
 * all, and tearing down the device route drops "Ext_Speaker_Amp Switch" back to 0, which is what
 * actually removes the aw87xxx's supply current -- not just muting digital gain. */
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

/* ---- Speaker conditioning -------------------------------------------------
 * This device has one small bottom speaker (plus the earpiece used as the stereo left channel,
 * F4438), and a transducer that size has two practical limits worth handling in software,
 * because there is no DSP block in the AFE between DL and the codec to do it in hardware:
 *
 *   1. It cannot reproduce low bass at all. Feeding it anyway costs excursion and headroom and
 *      comes back as distortion on everything else, so the content below ~180 Hz is removed with
 *      a second-order high-pass rather than left to the speaker to fail at.
 *
 *   2. Peaks clip long before the average level is loud. A limiter with a fast attack and a slow
 *      release holds the peaks just under full scale, which lets the average sit higher without
 *      the harshness that clipping produces - the usual reason a small speaker sounds strained
 *      rather than loud.
 *
 * Both run in float over the interleaved S16 frames. The cost is a few operations per sample at
 * 48 kHz stereo, which is nothing next to the codec path itself, and the state is per stream so
 * two concurrent outputs cannot disturb each other.
 *
 * Deliberately NOT applied to headphones or Bluetooth: those have their own transducers and
 * their own idea of loudness, and a limiter in front of them would only remove dynamics. */
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

    /* PCM device selection per AUDIO-ROUTES-1309 section 1:
     * DL3 = MT6789_DEEP_MEMIF (deep_buffer -- the screen-off/low-power path), DL2 =
     * MT6789_FAST_MEMIF, DL1 = MT6789_PRIMARY_MEMIF default. */
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
        voice_route_disable(adev);

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
            voice_route_disable(adev);
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
