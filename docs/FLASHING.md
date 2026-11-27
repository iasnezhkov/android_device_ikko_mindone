<!-- SPDX-FileCopyrightText: 2026 The LineageOS Project -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Flashing

Read this before writing anything. The normal way to install a build here is the standard
Android A/B path: sign the package, then let `update_engine` write the inactive slot through a
Virtual A/B snapshot. Direct `fastboot flash` of the whole partition set still works and is kept
further down as a fallback and recovery path — not as the everyday installer.

## TL;DR

```sh
# 1. sign the build with your own keys -- the device accepts nothing else
#    (see "Signing"; unsigned `mka bacon` output is rejected). ota_from_target_files
#    produces the installable package, rom.zip.

# 2a. install through the LineageOS Updater
#     Settings -> System -> System updates -> (menu) -> Local update ->
#     pick rom.zip -> Install -> Restart

# 2b. or install headless, from a host with adb
unzip -o rom.zip payload.bin payload_properties.txt -d /tmp/ota
adb shell mkdir -p /data/ota_package
adb push /tmp/ota/payload.bin /tmp/ota/payload_properties.txt /data/ota_package/
adb shell update_engine_client --payload=file:///data/ota_package/payload.bin \
    --headers="$(cat /tmp/ota/payload_properties.txt)" --update
# poll: adb shell update_engine_client --status

# 3. reboot once it reports success -- the device boots the slot update_engine just wrote
adb reboot
```

`update_engine` writes the *other* (inactive) slot through Virtual A/B snapshots, switches the
active slot, boots it, marks it successful and merges the snapshots into the shared partitions —
seconds, not the minutes a full image write takes. If the new slot does not come up, the
bootloader falls back to the slot that was already working, before any merge, and none of this
touches `/data`. See "How installing works here" below.

## How installing works here (Virtual A/B)

Both slots install and boot through `update_engine`, from the Updater app and from
`update_engine_client`. An image whose `vendor_boot` carries the device tree twice does not
boot in either slot (see "A duplicated device tree blob" under "Traps").

`BoardConfig.mk` configures seven logical partitions per slot inside `super`:
`odm_dlkm`, `product`, `system`, `system_dlkm`, `system_ext`, `vendor` and `vendor_dlkm`.
The current partition geometry must be read from the matching image/device metadata;
older six-partition offset tables do not describe this layout.

That is the normal shape of a Virtual A/B device, not a defect. The two slots do not each carry a
full separate copy of `system`/`vendor`/etc. — that would need roughly double the space `super`
has. Instead the base extents are shared, and during an update the target slot's new data lives
in a copy-on-write snapshot on top of them (`libsnapshot`, backed by `dm-user`/`snapuserd`, the
same mechanism every current Virtual A/B device uses). Once the new slot has booted and is marked
successful, the snapshot is merged into the shared extents — normally within seconds — and only
then is the previous slot's data gone from underneath it. `vendor_dlkm_a` and `vendor_dlkm_b` are
the same extent for exactly this reason; switching the active slot *without* going through
`update_engine` (for instance by writing images with `fastboot` and flipping the slot by hand)
does not create or refresh a snapshot, and gives a black screen with a live adb.

Consequences worth being explicit about:

- Before the merge, a slot that fails to boot is not committed: the bootloader's normal A/B retry
  counting falls back to the slot that was already working. `/data` is not slotted and is never
  touched by any of this.
- After the merge, the previous slot is marked unbootable. That is how Virtual A/B works, not a
  bug — there is **no rollback once the merge has happened**. If you need to get back to a
  specific earlier build after a merge, that means installing it again as a new update, not
  switching slots.
- 🔴 The fastboot fallback further down writes the shared base extents directly and bypasses the
  snapshot mechanism entirely. It is a real way to get bits onto the device, but it is not a
  substitute for a normal `update_engine` install, and it leaves the other slot's partition
  metadata stale.

## Kernel, DTBO and module updates

Deliver the paired `boot`, `vendor_boot`, `vendor_dlkm`, `system_dlkm` and DTBO
inside a **full signed ROM**, using the signing procedure below. Build it with the exact GKI `Image.gz`, matching DTB/DTBO, vendor modules and all
load lists. The selected `system_dlkm` modules come from Google's pinned
`android17-6.18-2026-09_r15` GKI archive, CI `16990439`
(`system_dlkm_staging_archive.tar.gz`), staged under
`kernel/system_dlkm/flatten/lib/modules/` with `kernel/modules.load.system`.
`BoardConfig.mk` includes `system_dlkm` and `dtbo` in `AB_OTA_PARTITIONS`.

The ROM must also contain the stock iKKO userspace plus all pinned Samsung A075F
graphics/radio/video/DRM, Lenovo TB351FU video and Infinix X6853 IMS donor groups.
The product is 64-bit only. See [Proprietary files](BLOBS.md) for extraction and hashes.
Sign the complete target-files archive and generate the full OTA with
`ota_from_target_files` without `--partial`.

A kernel-only partial OTA cannot carry this paired set: it omits `system_dlkm` and DTBO.
Keep the matching AVB descriptors.

Community builds (`MINDONE_COMMUNITY_BUILD=true`) package **13 partitions** (`boot`,
`vendor_boot`, `dtbo`, `vbmeta`, `vbmeta_system`, `vbmeta_vendor`, and the seven dynamic
partitions inside `super`) and intentionally omit all ten stock firmware partitions.
First-time installation from Stock Android 15 relies on updating Stock to the latest
official iKKO OTA first and sideloading `copy-partitions-mindone.zip` in LineageOS Recovery.

Self-built full-firmware packages (`MINDONE_COMMUNITY_BUILD=false`) also carry the ten
stock firmware images listed in `proprietary-firmware.txt` (`lk`, `tee`, `gz`, `spmfw`,
`sspm`, `scp`, `mcupm`, `dpm`, `pi_img`, `md1img`, 23 partitions total). When using a
pinned firmware directory (`MINDONE_FIRMWARE_SET`), `extract-files.py` checks sizes and
SHA-256 hashes against `firmware-set.json`. All firmware images must match the installed
preloader (never write `preloader`). `tee_a` is 5 MiB (`5,242,880` B) and `tee_b` is
6 MiB (`6,291,456` B), so a TEE image must fit 5 MiB. Do not copy whole donor partitions
or substitute another device's TEE.

## First-time installation from Stock Android 15 (Community RC + GApps)

### Before you start

- Update Stock Android 15 to the latest official iKKO OTA **before** unlocking or flashing.
  Community OTAs do not update the ten stock firmware partitions (`lk`, `tee`, `md1img`, etc.).
- Back up all personal data (including 2FA/TOTP tokens, banking apps, messengers, photos and
  downloads). Installing from stock formats `/data`.
- Use a **Linux** or **Windows** host with current Android `platform-tools` (`adb`, `fastboot`)
  and a data USB-C cable, and charge the phone to at least 50%. On **macOS**, host
  `fastboot flash` hangs the LK USB endpoint — use Linux or a Linux VM with USB passthrough.
- Verify every downloaded release artifact against `SHA256SUMS` before flashing, and check the
  community OTA certificate fingerprint (`unzip -p lineage-24.0-*.zip META-INF/com/android/otacert | openssl x509 -noout -fingerprint -sha256`).

### Unlock the bootloader

1. Enable **Settings → About phone → Build number** (tap 7 times), then open
   **System → Developer options** and enable **OEM unlocking** and **USB debugging**.
2. Reboot to the bootloader: `adb reboot bootloader`.
3. Check unlock state (`fastboot getvar unlocked`) and unlock if locked:
   `fastboot flashing unlock` (confirm on the phone screen; this erases user data).

### Install LineageOS 24 (guided script or manual)

You can run the guided installer `python3 mindone-install.py --release-dir <dir>` (which checks
OS compatibility, verifies all SHA-256 hashes, enforces the 6-partition allowlist, and steps
through recovery/sideload/GApps) or run the exact steps manually:

1. Flash the six initial images to the **active** slot (`_a` or `_b` from `fastboot getvar current-slot`):

```sh
SLOT="_$(fastboot getvar current-slot 2>&1 | awk '/current-slot:/ {print $2}' | tr -d '_ \r')"
for p in boot vendor_boot dtbo vbmeta vbmeta_system vbmeta_vendor; do
    fastboot flash "${p}${SLOT}" "${p}.img"
done
fastboot reboot recovery
```

If needed, enter Recovery by keys from power-off (cable unplugged): hold the **top-left key**
(iKKO OS switch) + **Power** until the boot menu appears, then press **Volume Down** to select
`[Recovery Mode]`. Do not hold Volume Down while powering on (that enters Factory Mode).

2. Copy the stock firmware partitions to the inactive slot (required so the newly installed slot
   has matching `lk`, `tee`, `md1img`, and coprocessor firmware; `copy-partitions-mindone.zip`
   verifies that the trailing 1 MiB of `tee_b` is all zeros before copying 5 MiB to `tee_a` and
   never touches `preloader`, `gpt`, `nvram`/`nvdata`, `protect1/2`, `proinfo`, `seccfg`, `frp`,
   or `otp`):
   - On the phone: **Apply update → Apply from ADB**
   - On the host: `adb sideload copy-partitions-mindone.zip`
3. Format user data: **Factory reset → Format data/factory reset**.
4. Install LineageOS 24:
   - On the phone: **Apply update → Apply from ADB**
   - On the host: `adb sideload lineage-24.0-<YYYYMMDD>-UNOFFICIAL-mindone.zip`
5. **Optional — Install GApps (`MindTheGapps-17.0.0-arm64`) BEFORE first boot:**
   - Recommended package: official `MindTheGapps-17.0.0-arm64` from
     `https://github.com/MindTheGapps/17.0.0-arm64/releases` (alternative: NikGapps Android 17
     `arm64` Core/Basic).
   - **Mandatory slot switch:** immediately after sideloading LineageOS, choose
     **Advanced → Reboot to recovery** (do **not** boot Android yet). This switches to the newly
     flashed slot so GApps installs onto the new `/system`.
   - Choose **Apply update → Apply from ADB** and run:
     `adb sideload MindTheGapps-17.0.0-arm64-<tag>.zip`
   - Confirm the signature verification prompt (**Install anyway**, because MindTheGapps is
     signed with its own key rather than the ROM key).
   - Subsequent OTA updates via **Settings → System → System updates** preserve MindTheGapps
     automatically through `/system/addon.d/30-gapps.sh` (`backuptool_ab`).
6. Choose **Reboot system now** to finish Setup Wizard.

### First-boot checklist and known caveats

- After Setup Wizard, check Wi-Fi, Bluetooth, mobile data/SIM/VoLTE, NFC, fingerprint
  enrollment, flip camera (photo/video/flip), 96/60 Hz display, USB-C charging, and
  **Settings → System → System updates**.
- Known caveats on community builds:
  - Donor Wi-Fi firmware omits custom RXFILTER offload, which can increase idle Wi-Fi wakeups.
  - Camera 60 fps, 50 MP max-resolution, and flip-exclusivity flags stay off by default.
  - Android 17 / LOS 24 builds are 64-bit only (`arm64-v8a`); legacy 32-bit-only APKs are not
    supported.
  - Play Integrity strong attestation does not pass on an unlocked bootloader.

### Return to Stock Android 15

Keep your verified factory backup (`super.bin` converted to sparse `super-stock-sparse.img`,
plus `boot`, `init_boot`, `vendor_boot`, `dtbo`, `vbmeta`, `vbmeta_system`, `vbmeta_vendor`,
and the ten firmware dumps from the **same** stock slot — on factory V13 dumps where `super.bin`
only contains merged `_b` extents, flash to slot `_b`).

1. Enter LK fastboot (`adb reboot bootloader` or top-left key + Power → `[Fastboot Mode]`).
2. Set the matching stock slot active (for a `_b`-merged factory dump, `fastboot --set-active=b`).
3. Flash `super`, `boot`, `init_boot`, `vendor_boot`, `dtbo`, `vbmeta`, `vbmeta_system`,
   `vbmeta_vendor`, and the matching ten stock firmware partitions (`lk`, `tee`, `md1img`, `scp`,
   `sspm`, `spmfw`, `mcupm`, `dpm`, `gz`, `pi_img`) for that slot.
4. Wipe user data (`fastboot -w`) and reboot (`fastboot reboot`).

🔴 **Never write** `preloader`, `gpt`, `nvram`, `nvdata`, `nvcfg`, `protect1`, `protect2`,
`proinfo`, `seccfg`, `frp`, or `otp`.

🔴 **Do not relock the bootloader (`fastboot flashing lock`)** until Stock Android 15 has
booted cleanly with intact original AVB metadata on the active slot.

Install the full signed package through the Updater or `update_engine_client` as above.
Keep a tested signed known-good package: before snapshot merge a failed boot may fall
back to the retained successful slot; after merge, restore by installing the known-good
package again. Selecting the former slot does not restore its previous data.

## Recovery

How this bootloader picks recovery: it never reads the BCB. It boots recovery when the watchdog's
non-reset register (RGU `NONRST2`, `0x10007024`, low nibble) holds 2, and only then leaves
`androidboot.force_normal_boot=1` off the command line. The kernel's `syscon-reboot-mode` writes
that value on `reboot recovery`; the bootloader's `fastboot reboot recovery` and its key menu do
the same. Recovery then boots the same `boot` and `vendor_boot` as the system, and first-stage init
loads `modules.load.recovery` **instead of** `modules.load.ramdisk`. That list must keep the
display, USB and reboot-mode modules: without them recovery starts with a dark screen and no
USB, clears the BCB and reboots two minutes later, which looks exactly like "recovery cannot be
entered".

Recovery's own "Apply update" and `adb sideload` go through `update_engine_sideload`, the same
`update_engine` machinery described above, and are shipped in this build.

Ways in:

- `adb reboot recovery`;
- from the bootloader's fastboot, `fastboot reboot recovery`;
- Settings → System → Developer options → *Advanced restart*, then Restart → Recovery in the power menu;
- keys, powered off with the cable out: hold the **top-left key** (the stock "iKKO OS" switch) and
  Power until "Select Boot Mode" appears. `[Recovery Mode]` is the first item and already selected;
  Volume Down confirms, the top-left key moves between items (`[Fastboot Mode]` is the second).
  Volume Up + Power does nothing here: the volume keys go through the keypad controller, while the
  menu reads the PMIC HOME key. Do not hold Volume Down while powering on -- that is factory mode.

From recovery, `adb reboot bootloader` reaches the bootloader's fastboot and `adb reboot fastboot`
reaches fastbootd (userspace fastboot, USB `18d1:4ee0`).

🔴 In developer builds built with `WITH_ADB_INSECURE=true`, recovery adb is root without
authorization (`ro.adb.secure=0`). Community builds (`MINDONE_COMMUNITY_BUILD=true`,
`WITH_ADB_INSECURE` unset) set `ro.adb.secure=1` in both system and recovery and
`ro.debuggable=0` in system.

**`fastboot flash` from a macOS host.** Every write hangs the USB endpoint, and so does `getvar`.
Read-only commands are fine. Write from a Linux host, or from a VM with real USB passthrough --
that is what the fallback path below assumes. `adb reboot bootloader` reaches the bootloader's
fastboot, which is what that path uses; `adb reboot fastboot` goes to fastbootd inside recovery.

## Fastboot fallback: writing partitions directly

Use this when `update_engine` itself is the problem, when you need to force known-good images
onto the device regardless of what state it is in, or for the initial bring-up before anything has
ever booted. It is not the way to install a routine update — see "How installing works here"
above for why.

### Before you start

🔴 Take a full dump of the stock partitions **including the partition table**. Without the table
there is nothing to restore the layout from, and no recovery image will help: recovery here boots
from the same `boot` and `vendor_boot` as the system and repartitions nothing.

### Write the whole set, or do not write

The current complete set includes 23 partitions: seven logical partitions inside `super`,
six written directly below, and ten firmware partitions that must be checked separately.
This fallback writes the seven logical images together via `super` and the six direct images:

```sh
out/host/linux-x86/bin/build_super_image /tmp/signed.zip super-sparse.img

SLOT=$(adb shell getprop ro.boot.slot_suffix | tr -d '\r')
adb reboot bootloader
fastboot flash super super-sparse.img
for p in boot vendor_boot dtbo vbmeta vbmeta_system vbmeta_vendor; do
    fastboot flash "${p}${SLOT}" "$p.img"
done
fastboot reboot
```

The remaining ten are the bootloader, the modem and the coprocessor firmwares:

```
lk  tee  gz  spmfw  sspm  scp  mcupm  dpm  pi_img  md1img
```

They are usually identical between your build and the device, which is why they can be left
alone -- but check, do not assume. Compare each against the device before writing anything, and
stop if one differs: a partial set leaves the device in a combination nobody built or tested.

🔴 `vbmeta_system` and `vbmeta_vendor` belong in the *written* group, not the checked one. They
change between builds, and a stale `vbmeta*` partition can bootloop the device.
Six written + ten checked + seven inside `super` = 23. Include `system_dlkm` in the
matching `super` image and preserve DTBO with the kernel set.

🔴 This path writes only the *active* slot's images and rebuilds `super` in place; it does not
create or refresh the other slot's Virtual A/B snapshot. After using it, the other slot's
partition metadata is stale in the sense described above -- that is expected of this path, not a
sign that something is broken.

## AVB is disabled here, and that is not permission to skip vbmeta

The top-level `vbmeta` carries flags `0x3` -- `HASHTREE_DISABLED | VERIFICATION_DISABLED` -- and
the bootloader is unlocked (`ro.boot.verifiedbootstate=orange`). So image verification does not
gate booting, and dm-verity is not set up.

🔴 It does not follow that the `vbmeta*` partitions can be left stale. That flag removes exactly
one failure mode -- signature checking. It says nothing about whether the set of partitions is
consistent with itself. Write them.

## Signing

The device verifies OTA payloads and images against the certificate in its own
`/system/etc/security/otacerts.zip`. A stock `mka bacon` package is signed with the **AOSP test
key** and is rejected. Sign with your own keys before installing, whichever path you use to get
the package onto the device.

Every tool below is a host tool the build produces, in `out/host/linux-x86/bin/`. Make your keys
once with `development/tools/make_key`.

```sh
cd <your lineage tree>
source build/envsetup.sh && breakfast mindone
export PATH=$PWD/out/host/linux-x86/bin:$PATH
KEYS=/path/to/your/keys

# 1. target_files is a DIRECTORY, not a zip -- repack it yourself.
#    -y is not optional: without it the symlinks inside become regular files
#    and the resulting images are broken in ways that only show up on the device.
D=$(ls -td out/target/product/mindone/obj/PACKAGING/target_files_intermediates/*-target_files | head -1)
(cd "$D" && zip -r -q -y -X /tmp/tf.zip .)

# 2. sign, then rebuild the images from the signed contents
sign_target_files_apks -o -d "$KEYS" /tmp/tf.zip /tmp/signed.zip
add_img_to_target_files -a --path out/host/linux-x86 /tmp/signed.zip

# 3. the installable OTA package
ota_from_target_files --path out/host/linux-x86 -k "$KEYS/releasekey" /tmp/signed.zip rom.zip
```

🔴 `--path out/host/linux-x86` is required on the last two: without it they cannot find the rest
of the host tools and fail partway, after having already done work.

`rom.zip` is the installable OTA package -- what the Updater and `update_engine_client` take,
above. `/tmp/signed.zip` is also what `build_super_image` takes, for the fastboot fallback further
up.

Check before you send a gigabyte to the device:

```sh
unzip -p rom.zip META-INF/com/android/otacert | openssl x509 -noout -fingerprint -sha256
adb pull /system/etc/security/otacerts.zip && unzip -p otacerts.zip |
  openssl x509 -noout -fingerprint -sha256
```

The two fingerprints must match.

## Writing super (fastboot fallback)

`super` is an Android sparse image and `fastboot` understands it directly: it splits the image
into download-sized pieces and skips the holes, which here are 6.1 GB out of 9. Nothing needs to
be expanded on the host.

```sh
fastboot flash super super-sparse.img     # ~3.3 GB of real traffic, a couple of minutes
```

🔴 The image's logical size must equal the `super` partition on the device, byte for byte. Check
it first -- `blockdev --getsize64 /dev/block/by-name/super` against the sparse header's
`total_blocks * block_size`. An image built for a different size writes past the end.

🔴 **Never write `super` from the running system.** It is running *from* that partition. `dd`,
`sync` and `reboot` are themselves files in `/system`; once the write replaces them they die with
`SIGILL` mid-operation. This applies regardless of which install path put the running system
there.

## Verify, always

A tool printing `OKAY` is not proof. Read the partition back and compare:

```sh
SZ=$(stat -c%s boot.img)                       # macOS: stat -f%z
adb shell "dd if=/dev/block/by-name/boot${SLOT} bs=1M count=$(((SZ+1048575)/1048576)) |
           head -c $SZ | sha256sum"
sha256sum boot.img
```

🔴 Compare **the same number of bytes**, not whole partitions. Partitions of the same name in the
two slots can differ in size, so hashing them whole reports a difference where the content is
identical.

The seven logical partitions are checked the same way, through `/dev/block/mapper/<name>${SLOT}`.

For an `update_engine` install there is no manual byte comparison to do -- the payload carries its
own hashes and `update_engine` refuses to apply or to mark the slot successful if they do not
match. `update_engine_client --status`, `getprop ro.boot.slot_suffix` before and after, and
`bootctl get-snapshot-merge-status` (idle once the merge has finished) are the equivalent checks
for that path.

## Traps

**A duplicated device tree blob.** `BOARD_PREBUILT_DTBIMAGE_DIR` concatenates **every** `*.dtb`
in that directory. Copy a new blob in under a different name without removing the old one and the
build silently produces a `vendor_boot` carrying the DTB twice, with no warning anywhere. The
device then does not boot: no adb, a preloader window every ~33 s. Check the size -- if the
`dtb` inside `vendor_boot` is an exact multiple of the blob you built, that is what happened.
The directory is a build product and is `.gitignore`d, so a clean clone does not protect you;
`BoardConfig.mk` refuses to build with more than one blob there.

**`grep -q` at the end of a pipeline.** With `set -o pipefail`, `grep -q` closes the pipe on its
first match, the stage before it dies of SIGPIPE with 141, and the pipeline reports 141 -- a
successful match that looks like a failure. Capture into a variable, then match.

**Piping flashing-tool output through `grep`/`tail`.** The pipeline buffers and a running flash
looks hung. Write to a file and read the file.

**`adb remount` before an install.** It leaves an overlayfs on six partitions, and `update_engine`
then refuses with `kOverlayfsenabledError`. Anything changed live exists **only** in that
overlay, so copy it out first -- file by file, because a recursive `adb pull` stops at overlayfs
whiteouts (character devices marking deletions).

## If the device does not come back

| what you see | what to do |
|---|---|
| no adb, only a hub or a USB billboard on the bus | force a re-enumeration: on a hub that can switch its downstream data lines, cycle the port. This does **not** power-cycle the phone -- it only clears a stuck enumeration on the host side |
| `0e8d:2000` on the bus, appearing and disappearing | preloader, cycling: the device is trying to boot and failing. Enter the bootloader's fastboot by keys (top-left + Power from power-off, then `[Fastboot Mode]` -- see "Recovery") and write known-good images |
| `0e8d:2000` on the bus **continuously** | preloader stuck; the same data-line cycle gets it out |
| `0e8d:201c` on the bus | the bootloader's fastboot -- write from here, or `fastboot reboot` |
| nothing on the bus at all | the device is not bringing up a USB gadget. Re-enumeration cannot help; hold Power ~12 s, then reconnect the cable |
| adb answers but nothing else works | wait 30 s before acting; a stale transport after a reboot looks like a dead device |

Data is not at risk in any of the above: `/data` is a separate partition and none of this touches
it. After a fastboot-fallback install, enroll fingerprints again: the sensor starts with no
enrollments, and enrolling works immediately.
