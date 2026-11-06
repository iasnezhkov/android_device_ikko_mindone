<!-- SPDX-FileCopyrightText: 2026 The LineageOS Project -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Proprietary files

## How they get here

`proprietary-files.txt` lists stock iKKO MindOne inputs. The product also requires
pinned donor groups; a stock-only extraction is insufficient. No proprietary binaries
are distributed in this repository. Obtain the stock dump and donor firmware locally
from sources you are entitled to use, then unpack them into filesystem roots containing
paths such as `vendor/bin/...` and `system_ext/lib64/...`.

| Donor | Mandatory groups | Extraction lists |
|---|---|---|
| Samsung Galaxy A07 SM-A075F | `graphics`, `radio`, `video`, `drm`, `connsys` | `proprietary-files-donor-a075f-{graphics,radio,video,drm,connsys}.txt` |
| Lenovo TB351FU | `video` | `proprietary-files-donor-lenovo-tb351fu-video.txt` |
| Infinix X6853 | `ims` | `proprietary-files-donor-x6853-ims.txt` |

Each donor entry has a SHA-1 pin. Each list has a companion `.retire.txt` that identifies
stock entries replaced by that group; the Lenovo retirement list is currently empty.
The Samsung graphics list identifies firmware `A075FXXU7DZJ5`. Other donor firmware
release identifiers are not recorded in their lists; those files are identified by the
pinned hashes. A matching model name alone is insufficient.

Run extraction in a synced LineageOS checkout. Set these roots to your unpacked donor
filesystems, then supply the stock MindOne source through the extractor:

```sh
export MINDONE_DONOR_A075F_ROOT="$A075F_ROOT"
export MINDONE_DONOR_GROUPS=graphics,radio,video,drm,connsys
export MINDONE_DONOR_LENOVO_TB351FU_ROOT="$TB351FU_ROOT"
export MINDONE_DONOR_LENOVO_TB351FU_GROUPS=video
export MINDONE_DONOR_X6853_ROOT="$X6853_ROOT"
export MINDONE_DONOR_X6853_GROUPS=ims
./extract-files.py --adb
```

`A075F_ROOT`, `TB351FU_ROOT` and `X6853_ROOT` are local directories you supply.
The extractor can also read a local stock dump instead of a connected device. If donor
roots are omitted, the selected source must already contain the exact pinned donor
files. It verifies hashes, rejects conflicting destinations and writes the resulting
files under `vendor/ikko/mindone/`; `setup-makefiles.py` generates their makefiles.
`extract-files.py` rejects omitted or changed mandatory group selections.

This product inherits `core_64_bit_only.mk`: there is no 32-bit application runtime.
The pinned Infinix IMS group is required for this configuration. Donor bootloaders and
whole donor partitions are not installation inputs for the MindOne.

IMS extraction fixes removed Android 17 APIs and receiver export flags in the APK/jars;
no framework exception for the IMS package is required. Camera extraction fixes the IMX766
stream table and removes the fixed 300 ms sensor power-on sleep using hash-checked fixups. The AIDL camera service loads the
HIDL backend in-process; there is no optional camera-proxy extraction path or instance rename.

The stock camera stack (the only one tuned for this IMX766 module) was built against the stock
`libdpframework`, `libaiselector`, `libgralloc_extra` and `libged`, which the donor graphics
group replaces under the same names. Extraction therefore also ships those four stock libraries
as `libdpfw_cam`, `libaisel_cam`, `libgrextra_cam` and `libged_cam` and points the stock camera
libraries at them; the camera's gralloc helper comes from the donor so it can use the AIDL
allocator. Every other process keeps the donor graphics libraries.

The Samsung graphics list installs its pinned `libtinyxml2.so` as
`libtinyxml2-mtk.so` and fixes its SONAME. `extract-files.py` rewrites the donor
`hwcomposer.mtk_common.so`, `libpqxmlflagparser.so`, `libpqxmlparser.so` and
`libsilkybrightnesscore.so` dependencies to that name; the stock `librt_extamp_intf.so`
fixup uses it too. This keeps these graphics/PQ users on the donor ABI.
The graphics entry pins both the original file SHA-1
`aaa4a64a000c3f3cf718becc81b56b9476c9a216` and post-fixup SHA-1
`9d3a778ffc34ffc9944a62ef27a4b3a60015d1c7`. The extractor requires a post-fixup
pin for each donor file modified by a fixup, in addition to its input SHA-1.

The Samsung `connsys` group replaces all eight MCU, BT and Wi-Fi firmware files for
soc2_2 revisions 1d and 1e. Each revision is taken as a complete cohort; partial cohorts
failed to power connsys on in device testing. Keep the stock `WMT_SOC.cfg` and `BT_FW.cfg`
for the board antenna and TX-power configuration.

## Wi-Fi RXFILTER firmware

The kernel RXFILTER implementation requires matching Wi-Fi firmware. The replacement sources,
placement builder and instruction-simulator checks are in `tools/wifi/` and `tools/rigs/`.
No vendor firmware image is distributed here. The tracked patch description carries only
replacement bytes compiled from our own sources, with hashes for the original regions.

For the known stock input, personal builds apply `tools/wifi/rxfilter-patch.json` during
extraction. `rxfilter_firmware.py` checks the input and each original region, applies our
replacement bytes, recomputes the CRC and requires the reviewed output hash before atomic
replacement. Already patched inputs are checked too. No external artifact is needed;
`MINDONE_PERSONAL_BUILD=false` leaves stock firmware untouched. Unknown inputs and stale sources are rejected.

The stock input SHA-256 is
`453912e5f79ba313986a8a6047a22f4d8033300f6db728f281a7f1c2ee92c37b`.
The reviewed output is 698052 bytes with SHA-256
`bfb11a2b31fdd22aadb603b186e9ae0ebc98a476a7f7adf0f4a572d520abbfec`.
The selected Samsung donor input is 722508 bytes with SHA-256
`b01ef53c5baeb56f5c60a910bb074a604b8cb10a16c0f4660b015b813e08e953`.
The known-input table accepts that exact hash and size, installs it unchanged and records
"RXFILTER firmware support absent". No RXFILTER patch has been retargeted to this image;
the kernel probe reports RXFILTER unsupported. The stock input still produces the reviewed
patched release above, and that release remains accepted for idempotent verification.
All other inputs are rejected. Patch metadata and source checks also run for the donor path.
Other compiler output is not implicitly accepted as a compatible firmware release.

Reproduction uses GCC 14.3.0 configured for `nds32le-elf` (C only, without target headers),
GNU binutils 2.43.1 for the same target, and the NDS32 simulator from `andestech/gdb` commit
`4327fa948f903c0705e0d87eed4cb182e24a6b6c`. Configure GCC with `--with-gnu-as --with-gnu-ld`
and `--with-as`/`--with-ld` pointing at that binutils build. Build GCC with `make all-gcc`;
the scripts take the build-tree `gcc/xgcc`, not an installed compiler wrapper. The binutils
directory must contain `gas/as-new`, `ld/ld-new` and `binutils/{objdump,objcopy,nm-new}`.
Configure the simulator with `--target=nds32le-elf --enable-sim --disable-gdb`
and build `all-sim`; its executable is `sim/nds32/run`. Host build dependencies and flags
depend on the build machine. The firmware builder itself pins the reduced-register NDS32 ABI.

Optional firmware reproduction uses these commands from this repository, with absolute paths for the input,
toolchain and a new output directory. Obtain the stock input from your own stock blob backup
or firmware extraction before the fixup; an already patched image is not a stock build input.

```sh
FW_INPUT=/path/to/stock/WIFI_RAM_CODE_soc2_2_1d_1.bin
NDS_GCC=/path/to/gcc-build/gcc/xgcc
NDS_BINUTILS=/path/to/binutils-build
NDS_SIM=/path/to/simulator-build/sim/nds32/run
RX_OUT=/path/to/new-rxfilter-output
mkdir -p "$RX_OUT"
python3 tools/wifi/rxfilter-build.py --firmware "$FW_INPUT" --gcc "$NDS_GCC" --binutils "$NDS_BINUTILS" --output "$RX_OUT/proof"
python3 tools/rigs/wifi-rxfilter-image-check.py --firmware "$FW_INPUT" --proof "$RX_OUT/proof" --gcc "$NDS_GCC" --binutils "$NDS_BINUTILS" --simulator "$NDS_SIM" --output "$RX_OUT/simulation"
python3 tools/wifi/rxfilter-patch.py --firmware "$FW_INPUT" --proof "$RX_OUT/proof" --composition-receipt "$RX_OUT/simulation/receipt.json" --output "$RX_OUT/WIFI_RAM_CODE_soc2_2_1d_1.bin"
python3 rxfilter_firmware.py --verify-only
./extract-files.py --adb
```

The patcher requires a passing instruction-composition receipt bound to the exact source and
linked code. Simulator checks establish software behavior.
Keep this firmware paired with the kernel and device-tree RXFILTER implementation.

## What is deliberately not taken

A number of entries in the list are commented out, each with the reason next to it. The largest
group is the stock MediaTek audio HAL: the AIDL audio service links the local
`audio-hf/` backend. No legacy primary module or selector property is installed.
Video codec and OMX blobs are excluded the same way now that
hardware decode/encode goes through the open V4L2 Codec2 HAL by default (see
[docs/OWN-COMPONENTS.md](OWN-COMPONENTS.md)).

Read the comments before re-enabling anything. A line commented out here usually means something
in the tree replaces it, not that it was forgotten.

## Versions

The supported userspace combines the stock iKKO dump with all mandatory donor groups
above. Keep their exact SHA-1 pins and extraction fixups together with the kernel,
DTB/DTBO and module set; arbitrary substitutions can break graphics, radio, video or IMS.
Record the stock firmware identity with your private extraction records. Where a donor
release identifier is unknown, identify its files by the hashes in the extraction lists.

Kernel modules for `system_dlkm` come separately from Google's pinned
`android17-6.18-2026-09_r12` GKI archive, CI `16754118`
(`system_dlkm_staging_archive.tar.gz`). They are not stock MindOne or donor userspace
blobs. Stage the selected modules and load list with the exact GKI image and matching
vendor modules before building a full signed ROM. See [Flashing](FLASHING.md).
