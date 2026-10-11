# mind_one minimal RIL -- phase 1: AT map + HIDL skeleton (Q23d)

Status: research implementation, not shipped and not hardware accepted as a replacement RIL.
The stock RIL remains the production configuration. Earlier syntax checks do not establish a
native build or a working enforcing-policy integration for the current source. Emergency
calling remains unimplemented, so this service is not suitable for a daily-use phone.

This continues the earlier modem-stack work (the original 37/39-handler,
96+256-AT-command reconnaissance) and sits alongside the other Q23 CCCI-userspace work
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
                                      needs them re-fetched from the build tree first.
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
  fully regenerable from the build tree; not committed.
None of these were "binaries" in the git-diff sense except the one `.rodata` slice; all are
excluded for the same reason: derived, regenerable, and bulky, not because of any license issue
(at_tok.{h,c} IS committed, verbatim, since it's small, Apache-2.0, and directly used).

## HAL version choice, briefly

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

## Integration prerequisites -- not hardware accepted

Both slots have booted through Virtual A/B updates. Before installing a candidate, verify the
current slot metadata and snapshot state. Shared logical-partition extents do not provide an
independent rescue copy after snapshot merge; see [FLASHING.md](../../docs/FLASHING.md).

The module `android.hardware.radio@1.6-service.mindone` installs separately at
`/vendor/bin/hw/android.hardware.radio@1.6-service.mindone`. Its init service is
`vendor.mindone_ril`, disabled by default. Keep the stock executable and product entries intact;
do not overwrite live vendor executables.

Before testing, complete a native build and validate the service's labels, domain transitions
and required access against the compiled SELinux policy. Keep SELinux enforcing. Resolve the
VINTF ownership of `IRadio/slot1` and confirm the actual stock service and mux-channel names;
the two RILs must not compete for the same instance or channels.

Prepare a bounded test and recovery plan with the owner before changing services. Do not treat
stopping the candidate and starting the stock process as proven recovery after modem state has
changed. Registration, unsolicited indications and error handling need validation independently
of call audio. Any real call or SMS attempt requires the owner's explicit authorization.

## Risks

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
