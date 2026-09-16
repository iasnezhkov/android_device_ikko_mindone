# mind_one minimal RIL -- phase 1: AT map + HIDL skeleton (Q23d)

Status: design/reconnaissance + a syntax-checked skeleton, **not built, not flashed, not run on
the device**. The phone was on the cable but read-only for this task: allowed reads only
(`ls`/`getprop`/`ps`/`cat /proc/tty/ldiscs` via `/debug_ramdisk/su -c`), never a write, toggle,
or reboot. Everything else was done device-free inside a Linux build environment holding the Android tree,
with `file`/`readelf`/`nm`/`strings`/`llvm-objdump`/`clang++ -fsyntax-only` only -- the build
itself was never touched (`m`/`mm`/`ninja` never invoked). `device/ikko/mindone/` was not modified. No binaries were committed to git (see
"What was deliberately not committed" below). Full narrative and citations:
RIL-MINIMAL-1409. Facts recorded: the fact log F4410-F4413 (and F4374, corrected).

This continues MODEM-STACK-1409 (the original 37/39-handler, 96+256-AT-command
reconnaissance) and sits alongside the other Q23 CCCI-userspace work
(`modem/ccci-userspace/`, `modem/mux/`) -- this task's own slice is the RIL layer
specifically: which AT commands the stock RIL actually sends per handler class, and a real HIDL
`IRadio` service skeleton that could eventually replace it.

## What's in this directory

```
at-map.json                          Full per-method AT-command/URC map, all 39 Rmc*RequestHandler
                                      classes, 922 methods, cited adrp/add/ldr evidence per hit.
AT-MAP-NOTES.md                      Human-readable summary table + method writeup + boot-handshake
                                      findings + corrections vs. the prior recon.
at-map-tools/                        The extraction scripts + one small disasm excerpt, kept for
                                      reproducibility (extract.py does the adrp/add/ldr cross-ref
                                      over one whole-.text objdump pass; build_output.py classifies
                                      and formats the result into at-map.json/AT-MAP-NOTES.md).
codegen/                             The scripts that generated RadioImpl.h/RadioImpl_stubs.cpp
                                      from the REAL android/hardware/radio/1.6/{IRadio,
                                      IRadioResponse}.h (parse_iradio.py, parse_iradioresponse.py,
                                      gen_skeleton.py) plus their small JSON outputs (the exact
                                      201-method table and the 199-entry request->response
                                      mapping, resolved with zero unresolved names). The large
                                      fetched AOSP headers these scripts originally ran against
                                      (types_1.{0,2,4,5,6}.h, IRadio_1.6.h, IRadioResponse_1.6.h,
                                      IRadioIndication_1.6.h, IRadioResponse_1.0.h, ~3 MB total)
                                      were deliberately NOT committed -- see "What was
                                      deliberately not committed" below; re-running these scripts
                                      needs them re-fetched from the build tree first (commands are in
                                      RIL-MINIMAL-1409).
src/                                 The skeleton itself:
  at_tok.{h,c}                         Verbatim copy of hardware/ril/reference-ril/at_tok.{h,c}
                                        (Apache-2.0, AOSP), unmodified.
  AtChannel.{h,cpp}                    Ported design (not a copy) of reference-ril's
                                        atchannel.c: command queue + condvar, reader thread,
                                        intermediate/final/URC line classification, adapted for
                                        this mux's separate URC channel (see AtChannel.h's header
                                        comment for the exact adaptation and why).
  RadioImpl.h                          class MindoneRadio : IRadio@1.6 -- all 201 pure-virtual
                                        methods declared (generated from the real header).
  RadioImpl_core.cpp                   ~24 methods with a real AtChannel-backed implementation
                                        (hand-written) plus the boot handshake.
  RadioImpl_stubs.cpp                  The other ~174, auto-generated, each answering
                                        REQUEST_NOT_SUPPORTED through the exact IRadioResponse
                                        callback the framework expects for that request.
  service.cpp                          main(): opens the two AT channels, registers
                                        IRadio@1.6/slot1, joins the HIDL threadpool.
Android.bp                           cc_binary android.hardware.radio@1.6-service.mindone,
                                      vendor:true, 64-bit only.
init.mindone_ril.rc                  disabled by default -- see "A/B plan" below.
manifest_mindone_ril.xml             VINTF fragment for slot1 -- NOT additive, see its own header
                                      comment (the stock manifest already owns this instance).
```

## What was deliberately not committed

- The stock `libmtk-ril.so` itself, or any byte-for-byte slice of it (the extracted `.rodata`
  copy used during analysis, `_rodata.bin`, was moved to the session scratchpad, not committed).
- The full `.text` disassembly (`_text_disasm.txt`, 30 MB) and the raw dynsym dump
  (`_dynsyms_raw.txt`, 1.3 MB) -- reproducible from the binary in the tree via the exact
  commands cited in `AT-MAP-NOTES.md` "Method"; moved to scratchpad, not committed.
- The fetched copies of AOSP's own generated HIDL headers (`types_1.x.h`, `IRadio_1.6.h`, etc.,
  ~3 MB) used by `codegen/`'s parser scripts -- these are build-tree output, not our code, and
  fully regenerable by the commands cited in
  RIL-MINIMAL-1409; moved to scratchpad, not committed.
None of these were "binaries" in the git-diff sense except the one `.rodata` slice; all are
excluded for the same reason: derived, regenerable, and bulky, not because of any license issue
(at_tok.{h,c} IS committed, verbatim, since it's small, Apache-2.0, and directly used).

## HAL version choice, briefly (full citation trail: RIL-MINIMAL-1409)

`android.hardware.radio@1.6::IRadio`, instance `slot1`. Three independent confirmations: the
framework's own `compatibility_matrix.6.xml` requires "1.5-6" for this instance; the stock
`librilfusion.so` itself links `android.hardware.radio@1.0.so` through `@1.6.so`; and the device
has telephony working live right now over HIDL with no separate `radio-compat` AIDL-wrapper
service built or declared -- meaning this ROM's framework binds legacy HIDL `IRadio` directly, so
implementing 1.6 needs no additional compat component on our side.

## Syntax-check

a clang syntax check in the LineageOS tree, built
for this task, checks against the real generated `android.hardware.radio@1.{0..6}_genc++_headers`
plus `android.hidl.{base,safe_union}@1.0`. All 5 `.c`/`.cpp` files pass
`-std=gnu++20 -Wall -Wextra -Werror -fsyntax-only` cleanly (`-std=gnu11` for `at_tok.c`). Not a
build -- no `m`/`ninja` was run, no `.o`/binary was produced, and cross-translation-unit issues
(duplicate symbols, missing `-l` at link time) are not caught by `-fsyntax-only`.

## Sepolicy (closed 14.09)

Labeled `rild_exec` (`sepolicy/vendor/file_contexts`) -> stock `rild` domain (AOSP `system/sepolicy/vendor/rild.te`
+ `device/mediatek/sepolicy_vndr/base/vendor/rild.te`), same as the stock `mtkfusionrild` (`u:r:rild:s0` live).
A/B via `tools/rigs/modem-ab.sh swap-ril` (B39 boots `ro.boot.selinux=permissive`, so the label matters for the
enforcing trial, not for the first test).

## A/B plan -- NOT run this session

> 🔴 **There is no rescue slot on this device.** Both slots share one `super` region -- the six
> logical partitions of `_a` and `_b` start at the same offsets -- so the system is physically one
> copy, and slot `_b` does not boot at all. "Try it in the spare slot" is not available here; see
> `docs/FLASHING.md`. What makes the test below safe is not a second slot but that nothing is
> replaced: the stock binary stays in place and stays in `proprietary-files.txt`, ours is started
> by hand next to it, and restoring the stock behaviour is `stop` + `start` of the stock service.

1. Build `android.hardware.radio@1.6-service.mindone` from `Android.bp` against the running
   system's ROM tree, alongside the stock blob (do not remove `mtkfusionrild`/`libmtk-ril.so` from
   `proprietary-files.txt` yet).
2. sepolicy is an OPEN ITEM (see `init.mindone_ril.rc`'s own comment) -- no matching domain was
   identified for this binary's path in this session; write and test one BEFORE attempting to
   start the service under enforcing SELinux, or run the test with `setenforce 0`-equivalent
   scoped exception (per this project's own rule, never plain `setenforce 0` -- use
   `magiskpolicy --live` scoped to the one new domain).
3. With adb root: `stop vendor.ril-daemon-mtk`, confirm the mux channels still exist
   (`ls -la /dev/radio/pttycmd1 /dev/radio/pttynoti` -- gsm0710muxd itself does not need to be
   stopped, it owns the ptys independently of which process reads them), `start vendor.mindone_ril`.
4. Verify, in order, read-only: `logcat -b radio` (our own log lines, tagged `mindone_ril*`),
   `getprop gsm.sim.state`, `dumpsys telephony.registry` (registration state reaching something
   other than "unknown"), a manual `ATD`/`AT+CMGS` capability check only if explicitly
   authorizes a real call/SMS attempt (this touches the live network, unlike everything else in
   this plan).
5. Rollback: `stop vendor.mindone_ril`, `start vendor.ril-daemon-mtk`. No partition-level changes
   are made by this plan (one new executable under `/vendor/bin/hw/`, disabled by default), so
   rollback does not need a reflash.

## Risks -- see RIL-MINIMAL-1409 "Risks" for the full blacklist

The short version: never send an `AT+EGMR` write form (IMEI/factory calibration), never send
`AT+ECFGSET`/`AT+ESBP` variants beyond the ones already confirmed read-safe in the boot
handshake, and treat the entire `AT+E*` proprietary surface as write-capable until proven
otherwise for each specific command -- most were never disassembled instruction-by-instruction
in this pass (only their invoking site and literal string were resolved, per `AT-MAP-NOTES.md`'s
own confidence levels).

## Files

```
modem/ril/at-map.json
modem/ril/AT-MAP-NOTES.md
modem/ril/src/at_tok.h
modem/ril/src/at_tok.c
modem/ril/src/AtChannel.h
modem/ril/src/AtChannel.cpp
modem/ril/src/RadioImpl.h
modem/ril/src/RadioImpl_core.cpp
modem/ril/src/RadioImpl_stubs.cpp
modem/ril/src/service.cpp
modem/ril/Android.bp
modem/ril/init.mindone_ril.rc
modem/ril/manifest_mindone_ril.xml
modem/ril/README-INTEGRATION.md   (this file)
```
The AT-map extraction and skeleton-generation helpers that produced `at-map.json` and
`RadioImpl_stubs.cpp` are working tools, not part of this tree; the generated files are.
