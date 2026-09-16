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

## Wi-Fi — `wifi/wpa_supplicant_8_lib/`

Private `DRIVER` commands for MediaTek gen4m: interface flags for START/STOP, `SIOCGIFHWADDR`
for MACADDR, and the rest passed as strings through the driver's private ioctl.

## Modem — `modem/`

An open implementation of the CCCI userspace stack: the file service the modem needs during
boot, the boot/state daemon, the RPC daemon, a line-discipline mux and a minimal RIL.

🔴 **Not shipped.** These are not in `PRODUCT_PACKAGES` and the ROM runs the stock modem stack.
The RIL implements 27 of 201 methods and its `emergencyDial` is a stub, which disqualifies it
from a device anyone actually carries. The code is here because it is complete enough to be
worth continuing, and it is exercised by swapping one component at a time against the live stock
stack, not by shipping it.

## Why the code is commented the way it is

Every non-obvious line says what the vendor original did, why it was wrong for this board, and
what the evidence was. That is what lets someone without the hardware review a change, and what
lets the reasoning be re-checked later instead of re-discovered.
