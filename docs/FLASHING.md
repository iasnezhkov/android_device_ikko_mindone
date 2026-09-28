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
touches `/data`. Signed packages built this way were installed on the device on four separate
days in a row, alternating slots, and both slots booted. One of those builds could not boot (a bad
init change): the bootloader used up its tries and returned to the previous slot on its own, with
nothing lost — the fallback doing its job. See "How installing works here" below for why this is
the expected behaviour, not a lucky run.

## How installing works here (Virtual A/B)

An earlier version of this page said slot `_b` did not boot, and that `update_engine`, the
Updater and `adb sideload` did not work on this device. That was wrong. What actually failed,
every time it was checked, was a specific bad image — one whose `vendor_boot` carried the device
tree twice (see "A duplicated device tree blob" under "Traps"). Once that stopped happening,
`update_engine` writes have installed correctly and repeatedly, through both the Updater app and
`update_engine_client` directly.

All six logical partitions inside `super` do start at the same offset for slot A and slot B:

| partition | offset (sectors) |
|---|---|
| `odm_dlkm` | 2048 |
| `product` | 4096 |
| `system` | 6 264 832 |
| `system_ext` | 9 383 936 |
| `vendor` | 10 813 440 |
| `vendor_dlkm` | 12 421 120 |

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
  substitute for a normal `update_engine` install, and it is what left the other slot's partition
  metadata stale in the past.

## Kernel-only updates

A change that only touches the kernel does not need a full rebuild-and-reinstall cycle. Start
from the signed `target_files` of the exact build currently on the device (see "Signing"), put the
new kernel in it — `BOOT/kernel`, the first-stage modules under `VENDOR_BOOT/RAMDISK/lib/modules`,
the rest under `VENDOR_DLKM/lib/modules`, the `dtb` — delete `IMAGES/boot.img`,
`IMAGES/vendor_boot.img`, `IMAGES/vendor_dlkm.img` and `IMAGES/vbmeta.img`, and let
`add_img_to_target_files` rebuild them. Then make a *partial* package from that one
`target_files`:

```sh
add_img_to_target_files -a --path out/host/linux-x86 /tmp/signed-new.zip
ota_from_target_files --path out/host/linux-x86 -k "$KEYS/releasekey" \
    --partial "boot vendor_boot vendor_dlkm vbmeta tee" \
    /tmp/signed-new.zip kernel-update.zip
```

This is not an incremental OTA: the listed partitions are carried whole (the package is tens of
megabytes), and every partition *not* listed is copied by `update_engine` from the running slot.

🔴 `tee` has to be in that list even though nothing about a kernel change touches it: `tee_a` is
5 MiB and `tee_b` is 6 MiB on this device, and `update_engine` copies every partition that is
*not* in the payload straight from the source slot to the target slot — which fails outright on a
size mismatch between the two slots' `tee`. Carrying `tee` in the partial payload avoids that copy.

🔴 `vbmeta` has to be in it too: `boot`, `vendor_boot` and `vendor_dlkm` are described directly
inside `vbmeta`'s partition descriptors, so a new kernel means a new `vbmeta`, and it has to travel
in the same payload as the images it describes.

The `target_files` you start from must be the build currently installed on the device: everything
outside the list comes from the running slot, and the new `vbmeta` still describes that build's
other partitions.

Install the result exactly like a full package, through the Updater or `update_engine_client`.

## Recovery

How this bootloader picks recovery: it never reads the BCB. It boots recovery when the watchdog's
non-reset register (RGU `NONRST2`, `0x10007024`, low nibble) holds 2, and only then leaves
`androidboot.force_normal_boot=1` off the command line. The kernel's `syscon-reboot-mode` writes
that value on `reboot recovery`; the bootloader's `fastboot reboot recovery` and its key menu do
the same. Recovery then boots the same `boot` and `vendor_boot` as the system, and first-stage init
loads `modules.load.recovery` **instead of** `modules.load.ramdisk`. That list is what used to be
broken: it had lost the display, USB and reboot-mode modules, so recovery did start, with a dark
screen and no USB, cleared the BCB and rebooted two minutes later — which looked exactly like
"recovery cannot be entered". An earlier version of this page said so; it was wrong.

Recovery's own "Apply update" and `adb sideload` go through `update_engine_sideload`, the same
`update_engine` machinery described above, and are shipped in this build. Installing through them
was not separately exercised in the round of installs this page is based on — the Updater app and
`update_engine_client` were — so treat sideload as architecturally the same mechanism, not as
independently proven here yet.

Ways in, each verified on the device:

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

🔴 In recovery adb is root and asks for no authorization (`ro.adb.secure=0` in a userdebug build).
Anyone with a cable gets a root shell on a locked phone -- `/data` stays encrypted, the rest does
not. Build `user`, or change that, before the phone leaves your hands.

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

A full package for this device holds 22 partitions. This path writes twelve of them by hand --
six logical ones inside `super`, and six written directly:

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
do change between builds, and an earlier version of this procedure wrote neither -- the same
class of omission that had already bootlooped this device once on a forgotten `vbmeta`.
Six written + ten checked + six inside `super` = 22, with nothing left over. Keep it that way.

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
source build/envsetup.sh && lunch lineage_mindone-bp4a-userdebug
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
`SIGILL` mid-operation. That was tried here and very nearly cost the device -- the write only
finished because `dd` happened to already be resident in memory. This applies regardless of which
install path put the running system there.

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
identical. That produced a wrong conclusion here once.

The six logical partitions are checked the same way, through `/dev/block/mapper/<name>${SLOT}`.

For an `update_engine` install there is no manual byte comparison to do -- the payload carries its
own hashes and `update_engine` refuses to apply or to mark the slot successful if they do not
match. `update_engine_client --status`, `getprop ro.boot.slot_suffix` before and after, and
`bootctl get-snapshot-merge-status` (idle once the merge has finished) are the equivalent checks
for that path.

## Traps that have actually bitten

**A duplicated device tree blob.** `BOARD_PREBUILT_DTBIMAGE_DIR` concatenates **every** `*.dtb`
in that directory. Copy a new blob in under a different name without removing the old one and the
build silently produces a `vendor_boot` carrying the DTB twice, with no warning anywhere. The
device then does not boot: no adb, a preloader window every ~33 s. Check the size -- if the
`dtb` inside `vendor_boot` is an exact multiple of the blob you built, that is what happened.
The directory is a build product and is `.gitignore`d, so a clean clone does not protect you;
`BoardConfig.mk` now refuses to build with more than one blob there. This was the actual cause of
every "slot `_b` does not boot" report this page used to carry.

**`grep -q` at the end of a pipeline.** With `set -o pipefail`, `grep -q` closes the pipe on its
first match, the stage before it dies of SIGPIPE with 141, and the pipeline reports 141 -- a
successful match that looks like a failure. A check written that way here reported "fastboot
never appeared" while the device was sitting in fastboot. Capture into a variable, then match.

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
it.

🔴 **One thing does not survive a fastboot-fallback install, though: enrolled fingerprints.**
After writing images by hand the sensor reports zero enrollments even though `/data` was never
written. The hardware is fine -- the kernel module loads, the HAL is alive, and re-enrolling works
immediately. Why the templates are dropped is not yet understood; it has not been specifically
checked against an `update_engine` install.

## What here has actually been run

Being explicit, because a procedure nobody has executed is a guess with formatting.

| | |
|---|---|
| Install via the LineageOS Updater (Local update) | **Run.** Repeatedly, on separate days, alternating slots; both slots boot |
| Install headless via `update_engine_client` | **Run.** Same result |
| Partial (kernel-only) OTA via `ota_from_target_files --partial` | **Run.** Many times, each followed by a boot of the new slot and a merge |
| Fallback after a build that does not boot | **Observed once, unplanned.** A build with a bad init change used up its tries; the bootloader returned to the previous slot, nothing lost |
| Snapshot merge after a successful boot | **Observed.** Completes within seconds, not separately timed |
| Recovery `Apply update` / `adb sideload` | **Not exercised this round** -- shipped and mechanically the same as the above, but not separately installed through recovery |
| Signing chain (repack, sign, `add_img_to_target_files`, `ota_from_target_files`) | **Run.** This is how every installed package was produced |
| `build_super_image` | **Run** |
| Fingerprint comparison | **Run** |
| `fastboot flash super` with a full 9 GB sparse image | **Run.** 3.3 GB of traffic, 53 sparse pieces, ~140 s |
| Writing `boot`/`vendor_boot`/`dtbo`/`vbmeta`/`vbmeta_system`/`vbmeta_vendor` into the active slot with fastboot | **Run** |
| Reading all twelve fastboot-written partitions back and comparing | **Run.** All twelve matched |
| Completeness check of the ten unwritten firmwares | **Run.** All ten matched the device |
| `adb reboot recovery` | **Run.** Works with a `vendor_boot` built after 19.09.2026; before that recovery came up dark and without USB |
| `fastboot reboot recovery` | **Run.** Works |
| Key menu (top-left + Power) into recovery and fastboot | **Run.** Works |
| fastbootd (`adb reboot fastboot`) | **Run.** Reachable; writing logical partitions from it not tried |
| VTS / CTS | **Not run** |
