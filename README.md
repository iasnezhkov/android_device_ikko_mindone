<!-- SPDX-FileCopyrightText: 2026 The LineageOS Project -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

<div align="center">

# iKKO MindOne

### Unofficial LineageOS 24.0 device tree — an independent research project

**Android 17 · MT6789 / MT8781 (Helio G99) · arm64 only · 96 Hz display**

</div>

---

> An independent, unofficial LineageOS source release.
> Read [Features](#-features) and keep a tested recovery route before installing.

## 🧬 Why this one is unusual

The device shipped with a Linux 5.10 vendor kernel. The companion kernel project ports
its device support to Google's **android17-6.18-2026-09_r12 GKI prebuilt**, CI **16754118**
(`6.18.32-android17-5`).
The core kernel is Google's prebuilt; device drivers are loadable vendor modules built
through Kleaf, with device DTB/DTBO outputs. Device code is not added to the GKI core.

This branch targets **LineageOS 24.0 / Android 17**, uses **FCM 202504** with the kernel at
kernel FCM 202604, and inherits
`core_64_bit_only.mk`. It does not provide a 32-bit application runtime. Android HAL
interfaces use AIDL, including frontends over legacy backends where necessary.
Vendor-specific HIDL interfaces and proprietary backend libraries still remain. IMS
extraction also handles removed Android 17 APIs without a framework exception for IMS.

| Component maintained here | Implementation |
|---|---|
| Audio | AIDL primary service over the local AFE/tinyalsa backend, with modem voice routing and a sound-trigger backend |
| Sensors | AIDL service using `hf_manager` and device calibration |
| GNSS | AIDL frontend over the legacy receiver backend, with session-controlled receiver power |
| Camera provider | AIDL frontend over the proprietary HIDL camera backend; proprietary imaging libraries remain required |
| Video | In-tree `codec2/`, derived from the open V4L2 Codec2 implementation, with MediaTek format and buffer handling |
| Vibrator | In-tree AIDL service for the legacy vibrator backend |
| LiveDisplay | Local service using the display CCORR controls |
| Secure element | Radio-backed AIDL service |
| Tethering offload | AIDL service with a native MDDP connection-mode backend |
| Gatekeeper / fingerprint | AIDL bridges to legacy implementations |
| Modem / Wi-Fi helpers | Rust `gsm0710muxd`, modem status and Wi-Fi-calling components, private gen4m commands; the open RIL lives beside the selected donor radio stack |
| MediaTek power interface | Local `vendor.mediatek.hardware.mtkpower@1.2` service for the vendor clients, with event-driven backend notifications instead of periodic polling |
| Battery gauge | Rust `mindone_gauged` shadow service reading status-carrying fuel-gauge samples beside the stock GM30 gauge, without replacing it |

The local services do not remove the need for proprietary radio, graphics, imaging,
receiver and firmware components. Only extraction lists, source and fixups belong here.

## 🚀 Build

Use a Linux x86_64 host with the LineageOS build prerequisites and sufficient disk space
for a complete Android checkout and outputs. Start with the `lineage-24.0` source branch:

```sh
mkdir lineage
cd lineage
repo init -u https://github.com/LineageOS/android.git -b lineage-24.0
repo sync -c -j8
git clone <device-repository-url> device/ikko/mindone
```

### Obtain proprietary inputs

Extract MindOne files from your own device or firmware backup using
`extract-files.py` in a synced LineageOS checkout. Current extraction also requires
three donor sources; extracting only factory MindOne files is insufficient.

| Donor | Required groups | Obtain locally |
|---|---|---|
| Samsung Galaxy A07 SM-A075F | `graphics,radio,video,drm,connsys` | Graphics list identifies `A075FXXU7DZJ5`; other groups are identified by pinned hashes |
| Lenovo TB351FU | `video` | Unpack files matching the hashes in `proprietary-files-donor-lenovo-tb351fu-video.txt` |
| Infinix X6853 | `ims` | Unpack files matching the hashes in `proprietary-files-donor-x6853-ims.txt` |

Obtain firmware from a source you are entitled to use. Unpack sparse/super images and
filesystems into roots containing paths such as `vendor/bin/...` and `system_ext/lib64/...`.
The extraction lists pin file hashes; a matching model name alone is insufficient.
Keep those files outside Git. Donor bootloaders and complete donor partitions are not
installation inputs for this phone.

Set the donor roots to those unpacked directories, then extract the MindOne files:

```sh
export MINDONE_DONOR_A075F_ROOT="$A075F_ROOT"
export MINDONE_DONOR_GROUPS=graphics,radio,video,drm,connsys
export MINDONE_DONOR_LENOVO_TB351FU_ROOT="$TB351FU_ROOT"
export MINDONE_DONOR_LENOVO_TB351FU_GROUPS=video
export MINDONE_DONOR_X6853_ROOT="$X6853_ROOT"
export MINDONE_DONOR_X6853_GROUPS=ims
(cd device/ikko/mindone && ./extract-files.py --adb)
```

`A075F_ROOT`, `TB351FU_ROOT` and `X6853_ROOT` are local directories you supply.
Extraction verifies donor hashes and rejects conflicting destinations. When roots are
omitted, the extractor can also obtain the selected files from an already matching
source; a factory MindOne source does not contain the required donor set.
[Proprietary-file documentation](docs/BLOBS.md) describes the donor connsys cohorts,
their absent RXFILTER support and the stock firmware patch. Do not substitute arbitrary firmware.

### Stage the kernel set and build

Build device modules and DTB/DTBO in the companion kernel repository on
`android17-6.18`, following the build guide in that repository's documentation.
Use the r12 core image, its exact GKI modules and metadata, and the device modules built
against that base as one reviewed set. Moving an installed 6.12 build to 6.18 changes the
GKI release and therefore takes a full signed ROM. Do not mix outputs from different candidates.

Populate the ignored `kernel/` build inputs before building the ROM:

```text
kernel/Image.gz
kernel/dtb/mindone.dtb
kernel/dtbo.img
kernel/modules/*.ko
kernel/modules.load.ramdisk
kernel/modules.load.recovery
kernel/modules.load
kernel/modules.load.system
kernel/system_dlkm/flatten/lib/modules/*.ko
```

The current companion kernel requests 26 first-stage and 236 vendor-DLKM modules,
a disjoint combined set of 262; recovery requests 131. This device stages 26 entries in
`kernel/modules.load.ramdisk`, 235 in `kernel/modules.load`, 131 in
`kernel/modules.load.recovery` and 24 in `kernel/modules.load.system`. The vendor count
differs because `libarc4` is staged in `system_dlkm` instead. The unused `subaf`,
`sub2af`, `main2af` and `main3af` wrappers are absent; `mainaf` remains.
Counts exclude blank/comment lines and describe requested loading.

The load lists select the actual staged modules. Keep exactly one `.dtb` in `kernel/dtb/`;
concatenating multiple device trees can prevent boot. Preserve the paired DTBO and the
GKI `system_dlkm` modules, including the selected USB-network and PPP modules.

Apply and check the ordered framework/platform patches after syncing:

```sh
bash device/ikko/mindone/apply-patches.sh apply
bash device/ikko/mindone/apply-patches.sh check
source build/envsetup.sh
breakfast mindone
mka bacon
```

The patch helper requires Bash 4+, Python 3 and Git. The default media service is the
in-tree Codec2 implementation; the former external Codec2 patch series is no longer
its source. The resulting unofficial package must be signed with keys trusted by the
installed system before it can be installed as an update.

## 📊 Features

| Area | Implementation |
|---|---|
| Audio | AIDL primary service over the AFE/tinyalsa backend, with media, cellular earpiece/speaker, headset microphone and modem Bluetooth SCO routing; restarts together with audioserver |
| Video | In-tree V4L2 Codec2 with AVC/HEVC encoding and AVC, HEVC, VP8, VP9, MPEG-4 and H.263 decoding |
| Camera | AIDL frontend over the stock camera backend, default sensor pixel mode and separate stock MDP/gralloc libraries beside donor graphics; recordable RGBA8888 EGL configs for CameraX video processing |
| GNSS | AIDL extensions and session-controlled receiver power |
| Display | 96 Hz default and VFP-only 60 Hz idle mode, SurfaceFlinger idle/touch timers, LiveDisplay calibration and Night Light integration |
| Radio / IMS / secure element | Selected donor AIDL radio, 64-bit IMS, Wi-Fi-calling helper and radio-backed AIDL secure-element service |
| Tethering offload | AIDL service with native MDDP connection-mode support |
| Sound trigger | Backend over the kernel voice-wakeup interface |
| Vibrator | In-tree AIDL service for the AW862xx haptics driver |
| Own radio HAL crates | Rust AT transport, registration/signal/SIM state, network/modem handlers, shared core and event-driven reactor beside the selected donor radio stack |

## 🔬 The interesting parts

The tree contains AIDL integration for legacy MediaTek backends, a primary audio
implementation, an event-driven sensor service, a GNSS frontend and the camera provider
frontend. `codec2/` maintains the V4L2 media implementation in-tree, including MediaTek
block-format conversion and Surface/YUV encoder handling. Its upstream notices remain
part of the source distribution.

LiveDisplay waits for boot completion before opening DRM and replays retained colour
on authenticated display events. Native
MDDP support supplies the tethering-offload backend. Gatekeeper and fingerprint bridges
keep legacy services reachable through the selected AIDL interfaces.

## ⚡ Device requirements

Use an unlocked bootloader and preserve a verified stock firmware/partition-table backup
and a signed known-good restore package before the initial installation.

Deliver the paired `boot`, `vendor_boot`, `vendor_dlkm`, `system_dlkm` and DTBO
inside a full signed ROM. Sign the complete target-files archive and generate the full OTA without `--partial`, using the Android releasetools in [docs/FLASHING.md](docs/FLASHING.md#signing). The signing
certificate must match the installed trust store. Install the signed package using
**Settings → System → System updates → Local update** or the documented
`update_engine_client` path, and reboot only after successful installation.

Virtual A/B updates use snapshots over shared logical-partition extents. Keep the
kernel, DTB/DTBO, GKI modules, vendor modules, userspace and AVB descriptors paired.
The full package also carries the ten stock firmware images listed in
`proprietary-firmware.txt`; extract them from one stock firmware version. The two TEE
partitions have different sizes, so a TEE image must fit the smaller one. A partial-OTA
partition list cannot carry a set that also changes DTBO or `system_dlkm`.

Before snapshot merge, a failed trial may return to the retained successful slot.
After merge, restore an earlier build by installing a compatible signed known-good
package; merely switching slots is not a rollback. For initial installation or recovery
when OTA is unavailable, use a complete matched image set and the Linux-host fastboot
fallback in [docs/FLASHING.md](docs/FLASHING.md). Check current partition geometry,
AVB coverage and exact readback lengths before writing; never overwrite a mounted
`super` from the running system.

## 📚 Documentation

- [Flashing, signing and restore constraints](docs/FLASHING.md)
- [Proprietary files and matching Wi-Fi firmware](docs/BLOBS.md)
- [Component background](docs/OWN-COMPONENTS.md)
- [Contributing](CONTRIBUTING.md)

## 🧩 What else you need

The companion kernel source branch is `android17-6.18`. It supplies the device DTB/DTBO
and loadable drivers over Google's r12 GKI core. The previous `android16-6.12` line
matches the `lineage-23.2` branch of this repository. Firmware, proprietary userspace and
kernel build products must be obtained or built separately; none belong in this Git tree.

## 🔎 Useful beyond this phone

The AFE audio backend, `hf_manager` sensors, AIDL frontends for legacy services,
MediaTek V4L2 Codec2 handling and gen4m private commands can inform other MT6789/MT8781
ports. Partition geometry, panel setup, calibration and donor compatibility remain
board-specific and must be measured again.

---

See [LICENSE](LICENSE) and individual file notices. Imported code retains its own
license, including the BSD notices in [codec2/NOTICE](codec2/NOTICE).
