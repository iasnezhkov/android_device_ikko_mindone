# Integration guide -- audio.primary.mindone (v2)

Companion to AUDIO-HAL-PLAN-1309 (design rationale) and AUDIO-ROUTES-1309
(the full source-verified route map, read that first for exact control names/citations). This file
is the mechanical "how do these four files become part of the ROM" checklist for integration,
plus the consolidated `VERIFY-ON-DEVICE` list.

v2 (13/14.09.2026 night) supersedes the v1 draft. Sections 1-6 below are
unchanged from v1 (same install path, same manifest/policy/sepolicy story) -- only section 7's
validation order and section 8's checklist changed, to cover the AFE crossbar and the wired-headset
finding. If you already read the v1 README, skip to section 8.

## 1. Where the files go

Copy this staging directory's contents into the device tree, e.g.
`device/ikko/mindone/audio/` (new directory) or `device/ikko/mindone/hardware/audio/` -- either
is fine, nothing here hardcodes a path except `MIXER_PATHS_XML` in `audio_hw.c`, which is an
on-device install path (`/vendor/etc/mixer_paths.xml`), not a source path.

```
device/ikko/mindone/audio/
  Android.bp
  audio_hw.c
  mixer_paths.xml
```

## 2. `device.mk` changes

Add the module and its config file. Nothing here touches the existing blob entries yet (see
section 6, "coexistence") -- this is additive:

```make
PRODUCT_PACKAGES += \
    audio.primary.mindone

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/audio/mixer_paths.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths.xml
```

(`$(LOCAL_PATH)` is already `device/ikko/mindone` in this tree's `device.mk`; adjust the relative
path if the files land somewhere other than `audio/`.)

Do **not** remove `audio.primary.mediatek.so` / `audio.primary.mt6789.so` from
`proprietary-files.txt` yet, and do not remove `android.hardware.audio@7.0-impl-mediatek.so`
either -- both stay installed as the rollback path until section 7's validation has passed on
several boot cycles (AUDIO-HAL-PLAN-1309 section 5).

## 3. `manifest.xml` -- no changes needed

The vintf manifest already declares:

```xml
<hal format="hidl">
    <name>android.hardware.audio</name>
    <transport>hwbinder</transport>
    <version>7.0</version>
    <interface><name>IDevicesFactory</name><instance>default</instance></interface>
    <fqname>@7.0::IDevicesFactory/default</fqname>
</hal>
```

and `device.mk` already builds the AOSP `android.hardware.audio@7.0-impl` that backs it. Our
`.so` is discovered by that existing passthrough via `hw_get_module_by_class()`
(AUDIO-HAL-PLAN-1309 section 2) -- there is no new HIDL/AIDL service to register, no new
manifest entry, and no change to `android.hardware.audio.service`'s init.rc.

## 4. `audio_policy_configuration.xml` -- no changes needed for v2, AND a real limitation to know

`device/ikko/mindone/configs/audio/audio_policy_configuration.xml` already declares the `primary`
module with `Speaker`, `Earpiece`, `Built-In Mic`, `Built-In Back Mic` devicePorts and the
`primary`/`deep_buffer`/`fast`/`voip_rx`/`mmap_no_irq_*` mixPorts our HAL's flag-based PCM-device
selection (`audio_hw.c`'s `adev_open_output_stream()`) relies on. Leave this file untouched for v2.

**New finding this pass (AUDIO-ROUTES-1309 section 4.4, F4309):** this same file declares
**no** `AUDIO_DEVICE_OUT_WIRED_HEADSET`/`_HEADPHONE`/`AUDIO_DEVICE_IN_WIRED_HEADSET` devicePort or
route at all -- confirmed identical to the stock config pulled from the device. This means v2's
`headphone`/`headset-mic` HAL code paths are currently **unreachable from Android** on this product,
regardless of anything the HAL does, until/unless this policy file is extended (which touches
`device/ikko/mindone/`, out of this task's scope). If the product turns out to have a working
3.5 mm jack, adding the devicePort/route here is the actual next step -- not a HAL change.

## 5. sepolicy

Unchanged from v1: expected to need **no new rules**, because this is not a new service/domain --
the `.so` still loads inside the same `hal_audio_default` domain (`android.hardware.audio.service`)
that already dlopens the MediaTek blob today, and `/vendor/lib[64]/hw/audio.primary.*.so` /
`/vendor/etc/*.xml` are covered by AOSP's generic public file_contexts patterns. Confirm the new
filename inherits the right label:

```
adb shell ls -Z /vendor/etc/mixer_paths.xml
```

If a first boot with the new HAL active shows AVC denials, `dmesg | grep -i avc | grep -i audio`
(or `logcat -b all | grep avc`) is the read-only diagnostic; add a rule under
`device/ikko/mindone/sepolicy/vendor/` following the existing `audioserver.te` pattern only if
that shows a real denial, not speculatively.

## 6. Coexistence with the stock blob (dev-cycle switch)

Unchanged from v1 (AUDIO-HAL-PLAN-1309 addendum "13.09 16:05" already settled the open
question this section originally flagged): the audio service on this ROM is 32-bit, loads
`android.hardware.audio@7.0-impl-mediatek.so` -> `audio.primary.mt6789.so`, resolved through
`ro.board.platform=mt6789` (`ro.hardware=mt8781` does **not** match). The clean switch is
`ro.hardware.audio.primary=mindone` (libhardware tries `audio.primary.<that property>` first) --
no blob file is overwritten and rollback is a property change. Build both ABIs (`compile_multilib:
"both"`, already set) until the 64-bit `android.hardware.audio.service` (BoardConfig
`run_64bit=true`, already built per the plan's addendum) is validated live.

## 7. Validation -- summary, order matters more in v2

Read-only unless stated otherwise; all of this is standard adb, no flashing needed since
a HAL swap is a userspace file change.

1. **Build and push `tinyplay`/`tinymix`/`tinycap` first**, before touching the new HAL at all --
   they are what confirms or refutes every `VERIFY-ON-DEVICE` item below, most importantly the new
   AFE crossbar switches (section 8, items 1-3), which is where v1 would have silently failed.
2. **Confirm the crossbar switches exist and gate audio as expected** (section 8, items 1-3) --
   do this *before* testing the HAL's own device-selection logic, since a crossbar miss looks
   identical to a device-mux miss (both produce silence) and debugging the wrong layer wastes a
   validation cycle.
3. Then the rest of the v1 checklist (variant-name property, period size, aw87xxx firmware timing,
   mixer_paths.xml file label, mic mute) -- items 4-8 below, unchanged in substance from v1.
4. Once `audio.primary.mindone.so` is built and loaded: `dumpsys media.audio_flinger` to confirm
   the HAL reports the expected output/input streams and device masks; speaker/mic via a media
   player + `adb shell tinycap`.
5. **Battery angle**: `standby-snapshot.sh` before/after a screen-off music playback session on our
   HAL vs. the stock blob (AUDIO-HAL-PLAN-1309 section 6 step 5, unchanged).

## 8. Consolidated `VERIFY-ON-DEVICE` checklist (v2)

| # | Question | Where it's flagged | Read-only command |
|---|---|---|---|
| 1 | Does `"ADDA_DL_CH1 DL1_CH1"`/`"ADDA_DL_CH2 DL1_CH2"` exist as live kcontrols, and does setting them to 1 (together with the speaker path) actually produce sound where they were 0 before? | AUDIO-ROUTES-1309 section 5, F4308 | `tinymix get 'ADDA_DL_CH1 DL1_CH1'` (confirm it exists and reads 0 by default); `tinymix set 'ADDA_DL_CH1 DL1_CH1' 1; tinymix set 'ADDA_DL_CH2 DL1_CH2' 1` then `tinyplay <wav> -D 0 -d 0` with the speaker path also applied -- compare against the same test with the crossbar left at 0 |
| 2 | Same for capture: does `"UL1_CH1 ADDA_UL_CH1"`/`"UL1_CH2 ADDA_UL_CH2"` gate whether `tinycap` reads real audio vs. silence? | AUDIO-ROUTES-1309 section 5, F4308 | `tinymix set 'UL1_CH1 ADDA_UL_CH1' 1; tinymix set 'UL1_CH2 ADDA_UL_CH2' 1` then `tinycap <wav> -D 0 -d 9 -c 1 -r 16000` with the mic path also applied |
| 3 | Do `dl2-adda`/`dl3-adda` (fast/deep_buffer) behave the same as `dl1-adda`? | mixer_paths.xml `dl2-adda`/`dl3-adda` paths | Same test on `-D 0 -d 2` and `-D 0 -d 3` |
| 4 | Does the speaker amp's analog input actually come from `LINEOUT L`, or from `Headphone L/R Ext Spk Amp`? | AUDIO-ROUTES-1309 section 2.1 (unchanged from v1) | `tinymix set 'LOL Mux' 'Playback'; tinymix set 'Ext_Speaker_Amp Switch' 1` and listen; if silent, try `tinymix set 'HPL Mux' 'LoudSPK Playback'` + same switch instead |
| 5 | What does `Mic Type Mux` read as before our HAL ever touches it? | AUDIO-ROUTES-1309 section 2.4 (unchanged from v1) | `tinymix contents \| grep -A2 'Mic Type Mux'` (or `tinymix get 'Mic Type Mux'`) |
| 6 | **Does this product even have a working 3.5 mm jack?** (new in v2) | AUDIO-ROUTES-1309 section 4.4, F4309 | Physical inspection -- not an adb command. If yes, plug a wired headset and check `adb shell dumpsys audio \| grep -i wired` and accdet jack-state sysfs/uevent for a plug event even though policy currently can't route to it |
| 7 | Which property (`ro.hardware`/`ro.board.platform`/`ro.product.board`) actually resolves today's `audio.primary.mt6789.so`? | Already answered by the plan's "13.09 16:05" addendum -- `ro.board.platform=mt6789` | `getprop ro.hardware; getprop ro.board.platform; getprop ro.product.board` (re-confirm on the exact build being tested) |
| 8 | Are `OUT_PERIOD_SIZE`/`OUT_PERIOD_COUNT` (960/4 @ 48kHz) actually safe, or does the AFE xrun at that setting? | AUDIO-ROUTES-1309 section 1.1 (unchanged from v1) | `tinyplay <wav> -D 0 -d 0 -p 960 -c 4` and watch for `underrun`/xrun messages; adjust and retest |
| 9 | Does `aw87xxx_acf.bin` finish loading/parsing before our HAL's first speaker-stream open, on a cold boot? | AUDIO-HAL-PLAN-1309 section 7 (F3758-class race, unchanged) | `dmesg \| grep -i aw87` early in boot; compare timestamp against `android.hardware.audio.service` start |
| 10 | Does `mixer_paths.xml` get the expected file label? | README section 5 | `ls -Z /vendor/etc/mixer_paths.xml` |
| 11 | Is there a real "Mic Mute" hardware control? | `audio_hw.c` `adev_set_mic_mute()` comment (unchanged from v1) | `tinymix contents \| grep -i mute` |
| 12 | Does the real `audio.h` in this tree's exact AOSP revision match the struct shapes assumed in `audio_hw.c`? | `Android.bp` top comment (unchanged from v1) | build it (`mm` in the LineageOS tree) and read the first compiler error, if any -- fix the struct literal, not the approach |

Item 6 is the one genuinely new *decision point* in v2 (not just a technical verification) --
everything else is either a straight continuation of v1's list or the new crossbar checks that
follow the exact same "read, then tinymix set, then tinyplay/tinycap" pattern v1 already
established.

None of the above require flashing or a device-state change beyond normal `adb`/build-host
commands -- run them with the device attached.

## Stereo speaker (F4434, 14.09)
The bottom speaker is on DACR (Right); the earpiece/receiver is on DACL (Left). The HAL already opens a
2-channel DL (OUT_CHANNEL_COUNT=2), so DACL/DACR carry L/R. The `speaker` path in mixer_paths.xml now also
sets `RCV Mux = "Media Playback"`, so speaker media plays L from the earpiece and R from the bottom speaker
= real 2-channel stereo. `speaker-off` resets `RCV Mux` to `Open`.

Prerequisite: the codec change adding the `"Media Playback"` RCV selection (snd_soc_mt6366, F4434) — in the
tree's module set. Without it, `RCV Mux` has no `Media Playback` value and the mixer set is ignored (falls
back to mono bottom speaker — no regression).

Caveat: the receiver is a low-power transducer, so the top (L) channel is quieter than the bottom. Trim to
taste with `Handset Volume` (add a `<ctl name="Handset Volume" .../>` to the `speaker` path) or attenuate
the bottom channel in the DL. Voice call / speakerphone routing is untouched (separate `incall`/earpiece
paths), so this cannot affect call audio.

Activation: this stereo behavior is only live when this HAL (audio.primary.mindone) is the device audio HAL.
That swap replaces the stock audio blob and must be smoke-checked at first boot (playback + a call) before it
is the daily ROM — do NOT ship the HAL swap unverified even under "no test", since a HAL regression means no
audio at all. Stereo tuning XML and codec route ship safely regardless.
