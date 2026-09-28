# What this tree implements itself

A normal device tree is configuration: makefiles, overlays, a list of blobs. This one also
carries working code, because several stock components either do not exist for this device or
had to be replaced.

## Audio HAL — `audio-hf/`

A complete primary audio HAL, not a wrapper. Selected by `ro.hardware.audio.primary=mindone`;
the stock MediaTek HAL is excluded from the blob list.

Notable device-specific parts:

- **Microphone.** The codec is wired as a digital mic. The mixer path must not force the analog
  mic type, and `MTKAIF_DMIC` has to be enabled for capture — with the analog type forced, the
  microphone records silence with no error anywhere.
- **Speaker processing**, applied only on the loudspeaker path: a second-order high-pass at
  180 Hz (the driver cannot reproduce below it and only gains excursion and distortion from
  trying) and a limiter with a −1 dB ceiling, shared across channels so peaks do not shift the
  stereo image. Filter coefficients are computed once per stream, not per buffer.
- **Per-path buffering.** Deep-buffer and low-latency paths use different period sizes rather
  than one compromise.
- **Voice-call routing.** The modem's speech path is brought up on a voice call and stays up
  when the call moves between earpiece and speaker: only the AFE path changes, as in the stock
  HAL, instead of a speech off/on pair that left a gap in the call audio. Bluetooth SCO media and the SCO headset
  microphone are implemented but not yet verified on hardware, and calls over SCO are not wired
  up — see the README's Status table.

## Sensors HAL — `sensors-hf/`

Talks to the kernel's `hf_manager` interface directly. Calibration is read from the device's own
`nvcfg` partition. Both worker threads block on real events instead of polling: with every
sensor off, the HAL causes no wake-ups.

## HIDL bridges — `bridges/`

Four AIDL services proxying to the stock HIDL HALs: fingerprint (AIDL IFingerprint V4 over HIDL
@2.1), gatekeeper (V1 over @1.0), secure element (V1 over @1.2), tethering offload (V1 over
IOffloadConfig@1.0 + IOffloadControl@1.1).

Groundwork for Android 17 rather than a requirement of 16: A17 carries no VINTF compatibility
matrix at level 6, and raising `target-level` means providing AIDL instances where it expects
them. The stock HALs are closed and HIDL-only. The stock HIDL manifest entries are kept until
the framework is confirmed to use the AIDL side, and each bridge handles its HIDL peer dying
(`linkToDeath` plus reconnect) instead of binding once at process start.

A fifth bridge, `bridges/camera/`, is a different kind of proxy: it forwards the same
`ICameraProvider@2.6` HIDL interface from the stock camera provider's real instance name to a
second, public one, rather than crossing to AIDL. It exists behind `MINDONE_CAMERA_PROXY` (off by
default) and, as of this writing, has only been confirmed to build — hwservicemanager reads VINTF
manifests at start, so the declared instance only takes effect in a full device boot, which has
not happened yet. Extraction also patches the stock camera provider binary's declared instance
name in place, through `camerahalserver_instance_fixup.py`, but only when that flag is set — the
default build leaves the binary untouched.

## Video decode/encode — `patches/external_v4l2_codec2-*`

Not a directory in this tree but a 27-patch series against the open `external_v4l2_codec2`
project, applied at build time (`MINDONE_OWN_CODEC2`, on by default) in place of the stock MediaTek
OMX/C2 HAL chain. It adds what MediaTek's SoC needs and the open project does not provide on its
own: detiling MediaTek's block (tiled) pixel format through the MDP hardware block, with a CPU
fallback when MDP is unavailable; HEVC and MPEG-4/H.263 decode; an HEVC encoder; and 10-bit
(P010) output down-converted to 8-bit for apps that request plain YUV — decoding straight into a
10-bit `ByteBuffer` still does not work. `ro.vendor.v4l2_codec2.*` properties advertise exactly
the codecs this device supports; `media.c2.hal.selection=aidl` selects the AIDL-side HAL.

## Wi-Fi — `wifi/wpa_supplicant_8_lib/` and a patch to the vendor HAL wrapper

Private `DRIVER` commands for MediaTek gen4m: interface flags for START/STOP, `SIOCGIFHWADDR`
for MACADDR, and the rest passed as strings through the driver's private ioctl. A separate patch
(`patches/hardware_mediatek_wlan-0001-*`) implements `wifi_set_dtim_config` in the vendor HAL
wrapper the same way, since the vendor library itself does not export it and every
`setDtimMultiplier()` call failed with `NOT_SUPPORTED` before this.

## Modem — `modem/`

An open implementation of the CCCI userspace stack: the file service the modem needs during
boot, the boot/state daemon, the RPC daemon, a line-discipline mux and a minimal RIL.

🔴 **Not shipped, mostly.** The file service, the boot/state daemon, the RPC daemon and the RIL
itself are not in `PRODUCT_PACKAGES`, and the ROM runs the stock modem stack. The RIL implements
27 of 201 methods and its `emergencyDial` is a stub, which disqualifies it from a device anyone
actually carries. The code is here because it is complete enough to be worth continuing, and it
is exercised by swapping one component at a time against the live stock stack, not by shipping
it. The line-discipline mux (`modem/mux/`) is the one exception: it is built into the vendor
image on its own, ahead of the rest of the stack.

Two small pieces sit next to this stack and are not part of the RIL. `volte_md_status/` *is*
shipped: a small binary that reads modem state-change events off the CCCI status device and
mirrors them into a vendor property for other components to read. `wfca/` is a Wi-Fi-calling
(IMS) agent talking to the same CCCI channel; it builds but is not yet wired into
`PRODUCT_PACKAGES`.

## Why the code is commented the way it is

Every non-obvious line says what the vendor original did, why it was wrong for this board, and
what the evidence was. That is what lets someone without the hardware review a change, and what
lets the reasoning be re-checked later instead of re-discovered.
