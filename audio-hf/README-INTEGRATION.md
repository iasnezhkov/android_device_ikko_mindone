# Audio HAL integration and validation

## Installed configuration

The source lives in `device/ikko/mindone/audio-hf/`. `device.mk` already packages
`audio.primary.mindone` and installs `mixer_paths.xml` at `/vendor/etc/mixer_paths.xml`.
`ro.hardware.audio.primary=mindone` selects this module.

The running audio service is 64-bit, selected by `android_hardware_audio.run_64bit=true`
in `BoardConfig.mk`. `Android.bp` builds both library ABIs; the accepted installation loads
`/vendor/lib64/hw/audio.primary.mindone.so`. The device manifest exposes the HIDL 7.0
`IDevicesFactory/default` interface. This HAL is not an AIDL audio service.

The stock primary HAL is excluded from `proprietary-files.txt`. Changing the selection property
alone is therefore not a stock-HAL rollback on a normal build. Proprietary speech-parser
libraries and audio parameters are still required and must be extracted from the device;
see [BLOBS.md](../docs/BLOBS.md).

## Cellular speech dependencies

For this device's Gen95 modem, the HAL obtains real device parameters from
`libspeechparser_vendor.so`, reads the modem version through `/dev/ccci_raw_audio`, and transfers
the parameter blob through `/dev/usip`. The observed blob is 49,152 bytes. Parameters are enabled
by default; disabling `vendor.audio.mindone.sph_param` is a diagnostic override, not the accepted
configuration. SetMode, SpeechOn and SpeechOff replies are validated before their transitions
are treated as successful.

Hostless speech uses PCM device 22 at 48 kHz, stereo S16_LE, 960 frames per period and four
periods. Capture and playback are both opened before either starts; playback starts first.
The stop threshold is `UINT32_MAX`. Endpoint changes keep the modem speech session running
and preserve another endpoint's ownership of the shared receiver route.

The matching kernel must provide `/dev/usip` from `snd_soc_mtk_common` with the CCCI dependencies
present. Its ADDA driver must preserve the DAPM-owned playback-enable bit when `hw_params`
updates `AFE_ADDA_DL_SRC2_CON0`. Repeated route changes can invoke `hw_params` after DAPM has
already enabled playback; an unconditional register write clears the bit and silences receive
sound even though mixer controls still show the expected route. Updating only the HAL is
insufficient on that kernel. Use the kernel and module set built and validated together;
the audio modules are installed under `/vendor_dlkm/lib/modules`.

The HAL runs in `hal_audio_default`. Node permissions and SELinux access for the CCCI and USIP
interfaces must match the installed vendor policy. A visible node or a successful PCM open
alone does not establish working call audio.

## Validation on 11 October 2026

The owner confirmed the cellular microphone, followed by both receive paths while switching
earpiece → speaker → earpiece. The accepted call logged successful SetMode, SpeechOn and
SpeechOff acknowledgements, no PCM startup failures and no CCCI timeouts. The kernel still
executed the previously failing enable-before-`hw_params` order during route changes, exercising
the ADDA fix in the successful call.

The accepted HAL and kernel module were subsequently installed through a signed OTA. Partition
readback, reboot, loaded-file hashes and 50 post-install checks confirmed persistent installation
without a temporary HAL overlay. There was no additional call after the final persistence-only
reboot. See [FLASHING.md](../docs/FLASHING.md) for installation and Virtual A/B constraints.

This test does not establish incoming-call acceptance, VoIP, Bluetooth SCO calls, wired or USB
headset calls, or recovery after deliberately restarting the modem. Bluetooth SCO media and
headset microphone support remain unverified on hardware; cellular routing over SCO is not
implemented. Existing BLE DSP initialization messages did not prevent the accepted ordinary
cellular call and are not evidence of working BLE call audio.

## Diagnostic controls

Two optional `set_parameters` keys remain available for controlled diagnosis:

- `mindone_speech_dl_gain` accepts an integer from -255 through 0 and sends a modem downlink
  gain command only during an active speech session.
- `mindone_speech_rx_pump=1` performs a bounded drain of pending modem messages and validates
  the specific codec request before acknowledging it.

Neither key runs automatically or is required by the accepted call configuration. Sending a
command successfully does not prove its acoustic effect. Normal routing and gain behavior
should be tested separately from these controls.

For a future owner-operated call test, capture audio-service logs and kernel events before the
call, check microphone and both receive endpoints, then confirm SpeechOff and route teardown.
Mixer state alone cannot replace listening or prove that the underlying hardware is enabled.
