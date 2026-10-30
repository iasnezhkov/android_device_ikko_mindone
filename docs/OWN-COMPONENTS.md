<!-- SPDX-FileCopyrightText: 2026 The LineageOS Project -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# What this tree implements itself

A normal device tree is configuration: makefiles, overlays, a list of blobs. This one also
carries working code, because several stock components either do not exist for this device or
had to be replaced.

## Audio HAL — `audio-hf/`

A complete primary audio HAL, not a wrapper. The framework reaches it through our own AIDL
`IModule` service (`bridges/audio/`), which drives the backend through a typed C API
(`audio-hf/AudioBackend.h`) and follows the AIDL stream state machine (start, pause, drain,
flush, standby with real presentation positions). The backend is AIDL-only: no
`audio.primary.mindone` module or legacy vtable shim is installed. The stock MediaTek HAL
is excluded from the blob list.

Notable device-specific parts:

- **Microphone.** The codec is wired as a digital mic. The mixer path must not force the analog
  mic type, and `MTKAIF_DMIC` has to be enabled for capture — with the analog type forced, the
  microphone records silence with no error anywhere.
- **Speaker processing**, applied only on the loudspeaker path: a second-order high-pass at
  180 Hz (the driver cannot reproduce below it and only gains excursion and distortion from
  trying) and a limiter with a −1 dB ceiling, shared across channels so peaks do not shift the
  stereo image. Filter coefficients are computed once per stream, not per buffer.
- **Per-path buffering.** Deep-buffer and low-latency paths use different period sizes rather
  than one compromise. The backend uses tinyalsa v2 throughout, keeping one PCM ABI in
  the AIDL process; startup failures and unavailable capture positions reach callers.
- **Voice-call routing.** The modem's speech path is brought up on a voice call and stays up
  when the call moves between earpiece and speaker: only the AFE path changes, as in the stock
  HAL, instead of a speech off/on pair that left a gap in the call audio. The Gen95 path loads
  device-specific parameters through the extracted speech parser and `/dev/usip`, validates
  modem acknowledgements, and opens both 48 kHz hostless PCM directions before starting them.
  Shared receiver ownership is preserved when media and call endpoints change. This requires
  the matching kernel USIP support and ADDA playback-enable fix: updating the HAL alone does
  not fix a kernel that clears playback enable during a route change. The 11 October cellular
  call passed earpiece/speaker switching. Bluetooth SCO media, the SCO headset
  microphone and calls over SCO use the modem speech path with the Bluetooth endpoint
  selected.

## Sensors HAL — `sensors-hf/`

Talks to the kernel's `hf_manager` interface directly. Calibration is read from the device's own
`nvcfg` partition; temperature calibration reports are persisted locally. Both worker
threads block on real events instead of polling: with every sensor off, the HAL causes
no wake-ups. Queue backpressure retains events, and failed backend changes preserve
accepted batch timing.

## AIDL bridges and native services — `bridges/`

Fingerprint exposes AIDL IFingerprint V4 over the stock HIDL @2.1 backend, and
gatekeeper exposes AIDL V1 over HIDL @1.0. Fingerprint failed-attempt counters and
timed-lockout deadlines persist per user under `/data/vendor/fingerprint_mindone`,
surviving service restarts and reboots. Storage failures fail closed; clearing lockout
requires a successful persistent update before reporting success.

Tethering offload uses the native MDDP connection-mode backend rather than HIDL
IOffloadConfig/IOffloadControl. The secure-element service in `secure-element/`
uses the radio AIDL SIM interface for logical channels to the UICC. The product targets
FCM 202504; vendor-specific HIDL interfaces remain where required.

## Camera provider — `camera-aidl/`

An AIDL `ICameraProvider` service (`android.hardware.camera.provider-service.mindone`, 64-bit)
in front of the stock HIDL camera provider: it translates provider, device and session calls
and stream configurations (`StreamConvert.cpp`) to the HIDL `@2.6`/`@3.7` interfaces of the
closed MediaTek camera stack, so the framework only sees AIDL.

## Other own services

- `vibrator/` — AIDL vibrator HAL for the AW862xx haptics driver, installed under its own name.
- `gnss-aidl/` — AIDL GNSS HAL (`android.hardware.gnss-service.mindone`) over the MediaTek GNSS
  backend, including measurements, antenna info, configuration, visibility control and AGNSS.
- `livedisplay/` — CCORR colour calibration, replayed on authenticated display events;
  waits for boot completion before opening DRM so the composer acquires master first.
- `mtkpower/` and `wfca/` — power-backend notifications and event-driven Wi-Fi-calling
  helper waits replace periodic idle polling.

## Video decode/encode — `codec2/`

The V4L2 Codec2 service, carried in this tree (`codec2/`, based on the open
`external_v4l2_codec2` project plus device changes) and selected by `MINDONE_OWN_CODEC2` (on by
default) in place of the stock MediaTek OMX/C2 HAL chain. It adds what MediaTek's SoC needs and the open project does not provide on its
own: detiling MediaTek's block (tiled) pixel format through the MDP hardware block, with a CPU
fallback when MDP is unavailable; HEVC and MPEG-4/H.263 decode; an HEVC encoder; and 10-bit
(P010) output down-converted to 8-bit for apps that request plain YUV.
`ro.vendor.v4l2_codec2.*` properties advertise exactly the codecs this device supports; `media.c2.hal.selection=aidl` selects the AIDL-side HAL.

The latest encoder patches honor mapped YUV planes and negotiated input layouts, and return
queued work after a fatal encoder failure. Surface and flexible-YUV frame-pattern tests passed
on the device, with unchanged decoder controls.

## Wi-Fi — `wifi/wpa_supplicant_8_lib/` and a patch to the vendor HAL wrapper

Private `DRIVER` commands for MediaTek gen4m: interface flags for START/STOP, `SIOCGIFHWADDR`
for MACADDR, and the rest passed as strings through the driver's private ioctl. A separate patch
(`patches/hardware_mediatek_wlan-0001-*`) implements `wifi_set_dtim_config` in the vendor HAL
wrapper the same way, since the vendor library itself does not export it and every
`setDtimMultiplier()` call failed with `NOT_SUPPORTED` before this.

## Modem — `modem/`

An open implementation of the CCCI userspace stack: the file service the modem needs during
boot, the boot/state daemon, the RPC daemon, a line-discipline mux and a minimal RIL.

The ROM selects the donor MediaTek AIDL radio stack. The shipped multiplexer
is now the Rust daemon in `modem/mux/`, installed as `gsm0710muxd`; the C implementation
remains available separately. Startup parses V.250 lines, accepts the modem's unframed
`AT+CMUX` answer only at a token boundary and bounds its write retries.

The open radio HAL lives beside the selected donor stack in `modem/radio/`: safe Rust crates
for the AT engine (`at`), the typed registration/signal/SIM model and indication-filter policy
(`state`), phase-1 `IRadioNetwork` and `IRadioModem`/SIM/config handlers (`network`, `modem`),
their integration with one transport and one state writer (`core`), and a reactor that owns
the core and the mux PTYs (`runtime`). Indications follow model changes; with the screen off
the policy turns registration and signal reporting down so the modem stops waking the CPU for
state nobody shows. The crates are host-tested against recorded modem transcripts.
`modem/ril/` supplies the earlier HIDL implementation's AT-command map as a reference.

Two small pieces sit next to this stack and are not part of the RIL. `volte_md_status/` *is*
shipped: a small binary that reads modem state-change events off the CCCI status device and
mirrors them into a vendor property for other components to read. `wfca/` is the Wi-Fi-calling
(IMS) agent talking to the same CCCI channel; it is shipped as a 64-bit binary together with the
64-bit MediaTek IMS stack.

## Gauge shadow service — `gauge/`

`mindone_gauged` is shipped as a vendor service. It reads coherent, status-carrying
PTIM samples over the kernel netlink ABI, integrates current for comparison, and reads
GM30 battery properties. It syncs each `shadow.csv` row and archives previous daemon runs
separately. `shadow.bin` format v3 stores charge into the battery, opposite the raw PTIM
discharge-positive sign; v2 state is rejected. Files stay under `/data/vendor/mindone_gauge`.
It does not replace GM30 or write battery SOC.

## Where the reasoning lives

Do not add explanatory comments to source; retain license headers and inherited upstream
comments. Each change's commit message says what the vendor
original did, why it was wrong for this board and how the change was verified. That lets
someone without the hardware review a change and lets the reasoning be re-checked later
instead of re-discovered.
