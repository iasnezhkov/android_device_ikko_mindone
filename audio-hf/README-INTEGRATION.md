# Audio HAL integration

## Installed configuration

The source lives in `device/ikko/mindone/audio-hf/`. `device.mk` packages
`android.hardware.audio.service.mindone` and installs `mixer_paths.xml` at
`/vendor/etc/mixer_paths.xml`. The 64-bit AIDL service links `libmindone_audio_backend`
and calls the typed interface in `AudioBackend.h`. The device manifest declares the
AIDL core and effect services. There is no legacy primary module or selector property.

The stock primary HAL is excluded from `proprietary-files.txt`. Proprietary speech-parser
libraries and audio parameters are still required and must be extracted from the device;
see [BLOBS.md](../docs/BLOBS.md).

## Cellular speech dependencies

For this device's Gen95 modem, the HAL obtains real device parameters from
`libspeechparser_vendor.so`, reads the modem version through `/dev/ccci_raw_audio`, and transfers
the parameter blob through `/dev/usip`. The parameter blob is 49,152 bytes. Parameters are enabled
by default; disabling `vendor.audio.mindone.sph_param` is a diagnostic override, not the default
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
insufficient on that kernel. Use the kernel and module set built together;
the audio modules are installed under `/vendor_dlkm/lib/modules`.

The HAL runs in `hal_audio_default`. Node permissions and SELinux access for the CCCI and USIP
interfaces must match the installed vendor policy.

## Voice-call endpoints

The backend implements cellular speech routing for earpiece, loudspeaker, wired/USB headset
microphone and Bluetooth SCO endpoints over the Gen95 modem speech path. When switching between
earpiece, loudspeaker and Bluetooth SCO during an active call, the modem speech session stays up
and only the AFE and codec routes are reconfigured. See [FLASHING.md](../docs/FLASHING.md) for
installation and Virtual A/B constraints.

## Diagnostic controls

Two typed backend calls are available for controlled diagnosis:

- `mindone_audio_speech_gain` accepts an integer from -255 through 0 and sends a modem downlink
  gain command only during an active speech session.
- `mindone_audio_speech_pump` performs a bounded drain of pending modem messages and validates
  the specific codec request before acknowledging it.

Neither call runs automatically or is required for normal call routing. Sending a
command successfully does not guarantee its acoustic effect; verify routing and gain behavior
independently when diagnosing call audio.
