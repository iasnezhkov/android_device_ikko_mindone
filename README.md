<div align="center">

# iKKO MindOne

### Unofficial LineageOS 23 device tree — an independent research project

[![Android](https://img.shields.io/badge/Android-16-3DDC84?style=flat-square&logo=android&logoColor=white)](https://source.android.com/)
[![LineageOS](https://img.shields.io/badge/LineageOS-23.2-167C80?style=flat-square)](https://lineageos.org/)
[![Kernel](https://img.shields.io/badge/kernel-6.12%20ACK-A42E2B?style=flat-square&logo=linux&logoColor=white)](#-what-else-you-need)
[![SoC](https://img.shields.io/badge/Helio%20G99-MT6789%20%2F%20MT8781-0071C5?style=flat-square)](#)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue?style=flat-square)](LICENSE)

**Credit-card sized · one 50 MP lens flips to be either camera · 96 Hz panel**

</div>

---

> ### ⚠️ Experimental, research project, still moving
> An independent, unofficial bring-up — not a vendor release, built with no vendor support or
> assistance, and no vendor source code. It runs as a daily driver, but things change
> between commits, some hardware is unfinished, and nothing here is certified by anyone.
> Start with [Status](#-status).

## 🧬 Why this one is unusual

No vendor kernel source, no vendor device tree, no reference BSP, no vendor support of any
kind — none of it was available or offered. So the phone runs a **hand-ported Google ACK**
(`android12-5.10` → 6.1 → **6.12**) with ~340 out-of-tree modules (290 loaded at boot), and this
tree carries **~16 400 lines of working code** where a device tree normally carries only
configuration. "No vendor source" is about what was used to build this, not about what the phone
needs to run: the proprietary blobs it still depends on (radio, GPU userspace, camera libraries)
are not part of this repository at all — each user extracts those from their own device, as
[docs/BLOBS.md](docs/BLOBS.md) describes.

<table>
<tr><td width="25%"><b>🎧 Audio HAL</b><br><sub>1 800 lines</sub></td>
<td>A complete primary HAL, written from scratch. Not a wrapper.</td></tr>
<tr><td><b>📐 Sensors HAL</b><br><sub>2 400 lines</sub></td>
<td>Straight onto the kernel's <code>hf_manager</code>, calibration from <code>nvcfg</code>.</td></tr>
<tr><td><b>🌉 HIDL→AIDL bridges</b><br><sub>2 100 lines</sub></td>
<td>Four services keeping closed HIDL HALs alive as VINTF rises toward Android 17.</td></tr>
<tr><td><b>📡 Modem stack</b><br><sub>10 000 lines</sub></td>
<td>Open CCCI userspace. <b>Not shipped</b> — see why below.</td></tr>
<tr><td><b>📶 Wi-Fi commands</b><br><sub>140 lines</sub></td>
<td>Private <code>DRIVER</code> commands for MediaTek gen4m.</td></tr>
</table>

## 🚀 Build

<details open>
<summary><b>What you need first</b></summary>

| | |
|---|---|
| **Host** | Linux, x86_64. 16 GB RAM workable, 32 GB comfortable |
| **Disk** | ~300 GB for the tree and build output |
| **Time** | Hours for the first build; minutes for incremental |
| **Device** | Yours, bootloader unlocked, adb root — for the blob extraction step |

Set up the AOSP host prerequisites first (`repo`, JDK, build packages);
LineageOS documents them at <https://wiki.lineageos.org/current_infrastructure>.

</details>

```sh
# 1 · LineageOS 23 source tree (~250 GB, takes a while)
mkdir lineage && cd lineage
repo init -u https://github.com/LineageOS/android.git -b lineage-23.2
repo sync -c -j8

# 2 · this device tree
git clone <this repo> device/ikko/mindone

# 3 · blobs — from YOUR OWN device, over adb
adb root
(cd device/ikko/mindone && ./extract-files.py --adb)

# 4 · kernel output (build the kernel repository first)
mkdir -p device/ikko/mindone/kernel/dtb device/ikko/mindone/kernel/modules
rm -f device/ikko/mindone/kernel/dtb/*.dtb          # <- see the warning below
cp <kernel-out>/Image.gz                                 device/ikko/mindone/kernel/
cp <kernel-out>/arch/arm64/boot/dts/mediatek/mindone.dtb device/ikko/mindone/kernel/dtb/
cp <k6set>/*.ko                                          device/ikko/mindone/kernel/modules/

# 5 · build
source build/envsetup.sh && breakfast mindone && mka bacon
```

➡️ **`out/target/product/mindone/lineage-23.2-<date>-UNOFFICIAL-mindone.zip`**

> 🔴 **That package will not install as-is.** The device accepts only images signed with the
> certificate in its own `otacerts.zip`; `mka bacon` output carries the AOSP test key. Sign it
> with your own keys — [docs/FLASHING.md](docs/FLASHING.md#signing).

> 🔴 **`kernel/dtb/` must hold exactly one `.dtb`.** The build concatenates *every* `*.dtb` in
> that directory into one blob. Leave an older one beside the new one — easy, since the directory
> is a build product and `.gitignore`d, so it survives a clean checkout of the repository — and
> the `vendor_boot` silently ends up carrying the device tree twice. Nothing warns you; the phone
> just stops booting, with no adb and a preloader window every ~33 seconds. Hence the `rm -f`
> in step 4.

<details>
<summary><b>When the build breaks</b></summary>

| Symptom | Cause |
|---|---|
| `breakfast` cannot find the device | the tree is not at `device/ikko/mindone` |
| Build fails on a missing vendor file | step 3 did not run, or ran against different firmware |
| Boots to a black screen, adb alive | wrong slot or `vendor_dlkm` region — see [docs/FLASHING.md](docs/FLASHING.md) |
| Does not start at all, no console | a raw `Image` instead of `Image.gz` in step 4 |
| `ModuleNotFoundError: extract_utils` | `extract-files.py` needs a synced LineageOS tree around it |
| `cp: ... is not a directory` in step 4 | `kernel/dtb` and `kernel/modules` are build products, not in the repository — the `mkdir -p` creates them |
| Built fine, then the phone does not boot and shows a preloader window every ~33 s | two `.dtb` files in `kernel/dtb/`, concatenated into a doubled device tree. Compare the `dtb` size inside `vendor_boot` against the blob you built: an exact multiple means this |

</details>

## 📊 Status

| ✅ Works | |
|---|---|
| **Audio** | works day to day: speaker, calls on earpiece and speaker, Bluetooth A2DP, microphone. Still being worked on — see below |
| **Cellular** | calls, SMS, mobile data *(stock modem stack)* |
| **Wireless** | Wi-Fi, Bluetooth, NFC |
| **Camera** | both logical cameras of the flip module |
| **System** | fingerprint, USB, charging, thermal, suspend/resume, 96 Hz |

| 🚧 Unfinished | |
|---|---|
| **RPMB** | hardware-backed key storage fails its MAC check; falls back safely, boot unaffected |
| **Slot `_b`** | not currently bootable: this device's `super` gives both slots the same physical region, and the recommended install writes only the active slot, so the other slot's partition metadata goes stale (`EXT4-fs: bad geometry`). Not a hardware fault — a consequence of how installs are done today, fixable by rewriting that slot's metadata before using it |
| **vSIM** | parked deliberately |
| **Our RIL** | 27 of 201 methods; not shipped |
| **Audio HAL** | in use, but not finished: Bluetooth SCO media and headset mic are implemented and not yet verified on hardware, calls over SCO are not wired, and an external keyboard with its own DAC and headphone jack is coming — the HAL will be finished against it |

Current focus is testing and optimisation. Kernel 6.18 and Android 17 are where this goes next;
both have been looked at, neither is committed to.

### What has and has not been tested

Worth being explicit, because "it works" means different things to different people.

| | |
|---|---|
| **Daily use** | Yes — this is the author's phone. Calls, data, camera, audio, sensors, charging, suspend |
| **Own `vendor` partition** | Yes. The whole `super` built here (system, vendor, product, system_ext, odm_dlkm, vendor_dlkm) is written to the device and booted. Earlier builds ran on the factory `vendor`; that is no longer the case |
| **VTS / CTS** | **No.** Neither has been run. There is no compatibility evidence of any kind, and none is claimed |
| **Any certification** | None. Not by Google, not by LineageOS, not by anyone |
| **Reproducible builds** | Kernel only, on one machine into one path (byte-identical `Image`). Cross-machine untested |

If you need a device that passes a compliance suite, this is not it yet.

## 🔬 The interesting parts

**🎧 Audio.** A full primary HAL. The microphone needed the codec's digital-mic path enabled and
the analog type *not* forced — with stock mixer settings it recorded silence and reported no
error. The loudspeaker gets a 180 Hz high-pass (the driver cannot reproduce below it and only
gains excursion from trying) and a −1 dB limiter shared across channels, so peaks do not shift
the stereo image.

**📐 Sensors.** Both worker threads block on real events: with every sensor off, the HAL causes
no wake-ups at all.

**🌉 Bridges.** Four AIDL services proxying to closed HIDL HALs — fingerprint, gatekeeper, secure
element, tethering offload. Groundwork for **Android 17**, not a requirement of 16: A17 carries
no VINTF matrix at level 6, and raising `target-level` means providing AIDL instances where it
expects them. Each bridge survives its HIDL peer dying (`linkToDeath` + reconnect) instead of
binding once at start.

**📡 Modem.** An open CCCI userspace stack — the file service the modem needs at boot (all 37
operations), the boot/state daemon, the RPC daemon, a line-discipline mux and a minimal RIL.
🔴 **Deliberately not in `PRODUCT_PACKAGES`:** the RIL implements 27 of 201 methods and
`emergencyDial` is still a stub, which disqualifies it from a phone anyone carries. The ROM runs
the stock stack. The code is here because it is far enough along to continue, and it is exercised
by swapping one component at a time against the live stock stack.

## ⚡ Device requirements

- An unlocked bootloader.
- 🔴 **All six logical partitions of both A/B slots start at the same offset inside `super`.**
  "Flash to the spare slot and try it" does not exist here: after installing this ROM the other
  slot will not boot at all. Keep a full dump of the stock partitions **including the partition
  table** — without it there is nothing to restore from.

📖 Read **[docs/FLASHING.md](docs/FLASHING.md)** before writing anything.

## 📚 Documentation

| | |
|---|---|
| **[docs/FLASHING.md](docs/FLASHING.md)** | How to install, why the usual two ways do not work here, and the traps |
| **[docs/BLOBS.md](docs/BLOBS.md)** | How proprietary files are extracted, and what is deliberately left out |
| **[docs/OWN-COMPONENTS.md](docs/OWN-COMPONENTS.md)** | The HALs and daemons this tree implements, and why each exists |
| **[CONTRIBUTING.md](CONTRIBUTING.md)** | The one rule that matters, and how to report something usefully |

## 🧩 What else you need

The kernel and all ~340 out-of-tree drivers live in **one separate repository**, Google
ACK-based, with a branch per kernel version — the same layout the Android Common Kernel itself
uses. Take branch **`android16-6.12`**; that is what this tree is built against, and the only
branch published so far. (The 6.1 step of the forward-port is not imported yet.)

Build it first, then drop its output here:

```
kernel/Image.gz          kernel/dtb/mindone.dtb          kernel/modules/*.ko
```

Those paths are excluded from this repository on purpose — they are build products, not source.

## 🔎 Useful beyond this phone

Most of what is here is SoC- or Android-version-level rather than specific to this board, so it
transfers to any **MediaTek MT6789 / MT8781 (Helio G99)** bring-up:

- a **primary audio HAL written from scratch** for MediaTek AFE — mixer paths, digital-mic
  enablement, DL/UL routing through `tinyalsa`, speaker processing;
- **HIDL→AIDL bridges** — the pattern for keeping closed HIDL HALs usable as VINTF target level
  rises toward Android 17;
- a **sensors HAL over `hf_manager`**, calibration from `nvcfg`;
- **wpa_supplicant private driver commands for gen4m** (`lib_driver_cmd_mt66xx`);
- an **open CCCI modem userspace stack** — normally shipped only as `ccci_mdinit`, `ccci_rpcd`
  and `ccci_fsd` binaries;
- a device tree for hardware with **one shared `super` across both A/B slots**, offsets and
  failure modes written down.

<details>
<summary><sub>Search terms</sub></summary>
<sub>

MT6789, MT8781, MT8781V-CA, Helio G99, MediaTek, LineageOS 23, Android 16,
`audio.primary.mediatek`, tinyalsa, MTK AFE, `mt6366` codec, HIDL to AIDL bridge, VINTF
target-level, `hf_manager`, gen4m, `lib_driver_cmd_mt66xx`, CCCI, `ccci_mdinit`, `extract-files`,
proprietary-files, A/B slots, dynamic partitions, `super` layout.

</sub>
</details>

---

<div align="center">
<sub>

**Apache-2.0**, matching LineageOS convention — see [LICENSE](LICENSE). The few headers that mirror
the kernel's user-space ABI (`modem/ccci-userspace/common/ccci_*.h`, `sensors-hf/hf_manager_uapi.h`)
are GPL-2.0, like the kernel headers they mirror.
Vendor blobs are not covered by this license and are not distributed here.

</sub>
</div>
