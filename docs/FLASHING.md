# Flashing

Read this before writing anything. This device punishes the usual A/B assumptions: of the three
standard ways to install a build, two do not work here at all, and the third needs care.

## TL;DR

```sh
# 1. sign the build with your own keys -- the device accepts nothing else
#    (see "Signing"; unsigned `mka bacon` output is rejected)
# 2. pull the partition images out of the signed package, and build super from the
#    signed target-files
out/host/linux-x86/bin/build_super_image /tmp/signed.zip super-sparse.img

# 3. write from the bootloader's fastboot, into the ACTIVE slot
SLOT=$(adb shell getprop ro.boot.slot_suffix | tr -d '\r')
adb reboot bootloader
fastboot flash super super-sparse.img
for p in boot vendor_boot dtbo vbmeta vbmeta_system vbmeta_vendor; do
    fastboot flash "${p}${SLOT}" "$p.img"
done
fastboot reboot
# 4. read every partition back and compare -- see "Verify, always"
```

One reboot, no intermediate state the device is ever asked to boot.

## What does not work here, and why

**`adb sideload`, `update_engine`, and the LineageOS Updater app.** These are one mechanism: the
Updater only hands the package to the engine. The engine writes the *other* slot, and slot `_b` has
not booted on this device yet -- the package applies cleanly, `_b` is marked active with six tries,
the bootloader spends all six and returns to `_a`. Seen three times, all with one package whose
`vendor_boot` carried the device tree twice (the first trap under "Traps that have actually
bitten"); that same image did not boot on `_a` either, and the images in `_b` were byte-for-byte
what the package held. So the failure is explained by the image, not by the slot, but an install of
a correct package into `_b` has not been done yet: until it has, treat this path as unproven.
Separately, the super metadata this tree produces sizes every `_b` partition at zero after a
fastboot install: there is no system there until an OTA lays one out.

**Recovery works -- with a `vendor_boot` built from this tree after 19.09.2026** -- but it does not
help with installing: its "Apply update" and `adb sideload` write the other slot, exactly like the
Updater above.

How this bootloader picks recovery: it never reads the BCB. It boots recovery when the watchdog's
non-reset register (RGU `NONRST2`, `0x10007024`, low nibble) holds 2, and only then leaves
`androidboot.force_normal_boot=1` off the command line. The kernel's `syscon-reboot-mode` writes
that value on `reboot recovery`; the bootloader's `fastboot reboot recovery` and its key menu do
the same. Recovery then boots the same `boot` and `vendor_boot` as the system, and first-stage init
loads `modules.load.recovery` **instead of** `modules.load`. That list is what used to be broken:
it had lost the display, USB and reboot-mode modules, so recovery did start, with a dark screen and
no USB, cleared the BCB and rebooted two minutes later -- which looked exactly like "recovery cannot
be entered". An earlier version of this page said so; it was wrong.

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
that is what the flashing path above assumes. `adb reboot bootloader` reaches the bootloader's
fastboot, which is what that path uses; `adb reboot fastboot` goes to fastbootd inside recovery.

## The one thing that matters

🔴 **Both slots share one `super` region.** All six logical partitions of slot A and slot B start
at the same offsets inside it:

| partition | offset (sectors) |
|---|---|
| `odm_dlkm` | 2048 |
| `product` | 4096 |
| `system` | 6 264 832 |
| `system_ext` | 9 383 936 |
| `vendor` | 10 813 440 |
| `vendor_dlkm` | 12 421 120 |

Consequences:

- There is **no** "install to the spare slot and try it". The system is physically one copy; the
  two slots' metadata merely describe it with different sizes.
- After installing this ROM the other slot does not boot at all -- it fails while mounting, with
  `EXT4-fs: bad geometry ... exceeds size of device`. That is expected, not a kernel bug.
- `vendor_dlkm_a` and `vendor_dlkm_b` are the same extent. Switching slots without rewriting that
  region gives a black screen with a live adb.

## Before you start

🔴 Take a full dump of the stock partitions **including the partition table**. Without the table
there is nothing to restore the layout from, and no recovery image will help: recovery here boots
from the same `boot` and `vendor_boot` as the system and repartitions nothing.

## Write the whole set, or do not write

A full package for this device holds 22 partitions. The procedure above writes twelve of them --
six logical ones inside `super`, and six written directly. The remaining ten are the bootloader,
the modem and the coprocessor firmwares:

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
key** and is rejected. Sign with your own keys before installing.

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

`/tmp/signed.zip` is also what `build_super_image` takes.

Check before you send a gigabyte to the device:

```sh
unzip -p rom.zip META-INF/com/android/otacert | openssl x509 -noout -fingerprint -sha256
adb pull /system/etc/security/otacerts.zip && unzip -p otacerts.zip |
  openssl x509 -noout -fingerprint -sha256
```

The two fingerprints must match.

## Writing super

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
finished because `dd` happened to already be resident in memory.

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

## Traps that have actually bitten

**A duplicated device tree blob.** `BOARD_PREBUILT_DTBIMAGE_DIR` concatenates **every** `*.dtb`
in that directory. Copy a new blob in under a different name without removing the old one and the
build silently produces a `vendor_boot` carrying the DTB twice, with no warning anywhere. The
device then does not boot: no adb, a preloader window every ~33 s. Check the size -- if the
`dtb` inside `vendor_boot` is an exact multiple of the blob you built, that is what happened.
The directory is a build product and is `.gitignore`d, so a clean clone does not protect you;
`BoardConfig.mk` now refuses to build with more than one blob there.

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

🔴 **One thing does not survive, though: enrolled fingerprints.** After installing a build the
sensor reports zero enrollments even though `/data` was never written. The hardware is fine --
the kernel module loads, the HAL is alive, and re-enrolling works immediately. Expect to add your
finger again after an install, and do not read a dead fingerprint reader into it. Why the
templates are dropped is not yet understood.

## What here has actually been run

Being explicit, because a procedure nobody has executed is a guess with formatting.

| | |
|---|---|
| Signing chain (repack, sign, `add_img_to_target_files`, `ota_from_target_files`) | **Run.** This is how the installed packages were produced |
| `build_super_image` | **Run** |
| Fingerprint comparison | **Run** |
| `fastboot flash super` with a full 9 GB sparse image | **Run.** 3.3 GB of traffic, 53 sparse pieces, ~140 s |
| Writing `boot`/`vendor_boot`/`dtbo`/`vbmeta`/`vbmeta_system`/`vbmeta_vendor` into the active slot | **Run** |
| Reading all twelve written partitions back and comparing | **Run.** All twelve matched |
| Completeness check of the ten unwritten firmwares | **Run.** All ten matched the device |
| `adb reboot recovery` | **Run.** Works with a `vendor_boot` built after 19.09.2026; before that recovery came up dark and without USB |
| `fastboot reboot recovery` | **Run.** Works |
| Key menu (top-left + Power) into recovery and fastboot | **Run.** Works |
| fastbootd (`adb reboot fastboot`) | **Run.** Reachable; writing logical partitions from it not tried |
| VTS / CTS | **Not run** |
