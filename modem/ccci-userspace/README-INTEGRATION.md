# mind_one open CCCI userspace daemons -- integration plan

Status: research implementation, not shipped and not accepted as a complete modem stack on
hardware. The stock modem services remain the production configuration. Earlier syntax checks
against Android headers do not establish a native build, enforcing-policy compatibility or a
working modem lifecycle for the current source.

## What is implemented, per daemon

### 1. `mdinit/ccci_mdinit.c` -- boot orchestrator + exception/reset/flight loop

Faithful (cited against our own kernel source, `ccci_md_all/` in the kernel repository):
- Every `CCCI_IOC_*` ioctl number, `_IO*()` macro shape, and the
  `strncmp(current->comm, "ccci_mdinit", ...)` gate on `CCCI_IOC_DO_START_MD`
  (`fsm/ccci_fsm_ioctl.c:403,459-462`). The implementation sets the required task name with
  `prctl(PR_SET_NAME, ...)`; this does not require replacing the stock executable path.
- `/dev/ccci_monitor` liveness tie (`fsm/ccci_fsm_monitor.c:35-52`): held
  open for the daemon's whole life; closing it force-stops the modem.
- `struct ccci_header` wire format for monitor messages
  (`mtk_ccci_common.h:77-84`) and the full `CCCI_MD_MSG_*` enum
  (`fsm/ccci_fsm_internal.h:88-100`), including the two message classes the
  kernel doc proves are NOT autonomously recovered
  (`CCCI_MD_MSG_RESET_REQUEST`/`_EXCEPTION`) -- both drive a real
  stop+backoff+start cycle, not just a log line.
- `md_boot_data[16]` array layout (`inc/ccci_modem.h:92-105`) for
  `CCCI_IOC_SET_BOOT_DATA`, with the exact stock log string reproduced
  verbatim for diffability against real device logs.
- Flight mode (`CCCI_IOC_ENTER/LEAVE_DEEP_FLIGHT`'s monitor-side
  `CCCI_MD_MSG_FLIGHT_{STOP,START}_REQUEST` notifications): reacted to with
  a real `DO_STOP_MD(flight=1)` / `DO_START_MD()` pair, using the exact
  stock log strings ("MD%d enter/leave flight mode").
- `CCCI_IOC_MD_RESET` is not *issued* by mdinit (that ioctl is for other
  callers per the kernel header comment -- mdlogger/META/muxreport), but
  its monitor-side effect (`CCCI_MD_MSG_RESET_REQUEST`) is handled.
- `vendor.mtk.md1.status`/`vendor.mtk.md1.starttime` property names and the
  4 literal status values (`invalid`/`ready`/`reset`/`exception`) recovered
  from the stock binary's own string table -- not guessed names.

Best-effort / explicitly gated (see source comments for exact provenance):
- MD_SBP NVRAM read (`NVM_GetLIDByName`/`GetFileDesc`/`CloseFileDesc`,
  signatures recovered from disassembling the stock binary's own call
  sites, confidence HIGH on shape, MEDIUM on exact field semantics) -- this
  targets the Sales/Branding code LID, **not** IMEI/calibration.
- `NVM_RestoreFromBinRegion_OneFile` -- implemented but **compiled out by
  default** (`MINDONE_ENABLE_BINREGION_RESTORE=0`). See Risk register below.
- `/vendor/firmware/modem_1_*_n.img` existence probing for
  `CCCI_IOC_SET_MD_IMG_EXIST` -- bit-order inferred from string-table order,
  not disassembly-confirmed; low risk (metadata only, no NVRAM content).

Out of scope, cited as provably inapplicable, not silently skipped:
- SIM hot-plug / `CCCI_IOC_SIM_SWITCH*` -- the kernel's own
  `switch_sim_mode()`/`get_sim_switch_type()` are `__weak` no-op stubs on
  our platform (`fsm/ccci_fsm_ioctl.c:29-39`); our board is single-SIM.

### 2. `fsd/ccci_fsd.c` -- `/dev/ccci_fs` file-service proxy

Faithful: the confirmed real directory-root table (8 roots + `nvcfg`,
cross-checked against which of md/md2/md3/md5 are live on our single-modem
board), path sanitization (root allowlist + `..` rejection, enforced
regardless of what the wire parser decodes), the `file_handle` table, and
the exact generic error codes (`FS_NO_ERROR` .. `FS_MEM_OVERFLOW`, shared
with the RPC protocol per `port_rpc.h`).

**Explicitly unverified** (flagged in loud comments in the source, not
hidden): the exact byte layout of the request/response struct on
`/dev/ccci_fs` itself. The peer is the MD1 baseband *firmware*, not
another Android process, and `ccci_fsd` is a stripped 32-bit ARM/Thumb2
binary with no mapping symbols, which defeated a bounded disassembly
attempt in this pass (plain `objdump -d` misdecodes ARM/Thumb boundaries
without them; `-M force-thumb` didn't recover the literal-pool string
cross-reference either). The field *list* is proven from the stock
binary's own un-stripped log strings (`{filename, opid_map, flag, offset,
whence, length, file_handle}`); the field *order/widths* in
`struct ccci_fs_req`/`ccci_fs_resp` are our own best-effort layout. Every
packet is hex-dumped at `LOGI` before parsing specifically so a live
capture can correct this without guesswork.

Deliberately stubbed: `FS_CCCI_OP_BIN_REGION_ACCESS` answers `FS_NO_FEATURE` and
logs loudly rather than calling `NVM_RestoreFromBinRegion_OneFile` against
an unverified wire-decoded path.

### 3. `rpcd/ccci_rpcd.c` -- `/dev/ccci_rpc`, the 4 userspace-side ops

Faithful: the exact op-id split (kernel-answered vs. userspace-answered),
proven by reading `port_rpc_recv_match()` directly
(`port/port_rpc.c:1416-1480`); `IPC_RPC_QUERY_AP_SYS_PROPERTY` (exact
`property_get` semantics). `IPC_RPC_SAR_TABLE_IDX_QUERY_OP` calls the
stock, unmodified `libsysenv.so`'s `mtk_sar_table_id_get()` rather than
reimplementing SAR-table selection (RF safety calibration -- out of scope
to re-derive). `IPC_RPC_SAVE_MD_CAPID` is log-only, matching what the
existing `ccci_rpcd.te` sepolicy domain would even allow the *stock*
binary to persist (no nvram/property-set grants in that domain).

Documented gap: `IPC_RPC_AMMS_DRDI_CONTROL` (AGPS delta-reference-data
image handling) answers a clear "not implemented" rather than attempting
the `md1drdi` image protocol, which was not reverse-engineered in this
pass and is unrelated to core telephony boot/registration/SMS.

### 4. Mux -- design only, `n_gsm` ldisc-setup code as a draft

`mux/n_gsm_ldisc_setup.c` is a working, cited draft of the
`TIOCSETD(N_GSM0710)` + `GSMIOC_SETCONF` sequence against mainline n_gsm
(the earlier inspected kernel configuration had `CONFIG_N_GSM` disabled; check the exact
candidate configuration before relying on it). It is **not** the
recommended path and is not wired into any daemon: `mtkfusionrild` is
proven (by `strings` on the actual guest binary) to open gsm0710muxd's own
`/dev/radio/ptty*`/`atci*` naming scheme, not n_gsm's generic
`/dev/gsmttyN`, and that RIL is closed-source, so its naming expectation
cannot be changed. The recommended path -- reimplementing gsm0710muxd's own
userspace 07.10 framing (real prior art: Tuukka Karvonen's 2003 `gsmMuxd`,
the proven common ancestor of MediaTek's own mux) -- was not implemented as code in this
pass (out of the time budget for this task); the design and full rationale
are in the source-file header comment of `n_gsm_ldisc_setup.c` and
duplicated in this section for visibility. Earlier tests already proved the classic userspace-mux shape works end-to-end
against this exact vendor RIL/MD firmware once the (already-fixed)
ldisc-hijack bug is out of the way.

## Risk register: IMEI / calibration

| Item | Risk | Mitigation in this deliverable |
|---|---|---|
| `NVM_RestoreFromBinRegion_OneFile` (mdinit) | Silent IMEI/calibration corruption if signature/target wrong -- the single highest-consequence function in the whole libnvram surface | Implemented but **compiled out by default**; when enabled, failure is logged and non-fatal (kernel does not gate boot on it, proven by exhaustive negative grep) |
| `ccci_fsd` wire struct | Live MD firmware requests silently misparsed -> broken SIM/calibration file access | Every packet hex-dumped before parsing; path sandboxed to confirmed real roots regardless of parse correctness; bin-region op hard-stubbed |
| `FS_CCCI_OP_BIN_REGION_ACCESS` (fsd) | Same class as above, reached via the live MD wire path (worse: parser-decoded target, not our own fixed string) | Never calls `NVM_RestoreFromBinRegion_OneFile`; answers `FS_NO_FEATURE` |
| MD_SBP NVRAM read (mdinit) | Low -- confirmed (disassembly) to target Sales/Branding code, not IMEI | Read-only; failure falls back to `sbp=0`, matching kernel's own no-gate behavior |
| `/vendor/firmware/modem_1_*.img` bit order (mdinit) | Low -- metadata bitmap only, wrong order is visible in `getprop`/logcat, not silent | N/A, documented inference |

## Integration prerequisites -- not hardware accepted

Both slots have booted through Virtual A/B updates. Shared logical-partition extents do not
make an inactive slot an independent rescue copy: verify the current slot metadata and snapshot
state before an update, and do not assume the old system remains available after snapshot merge.
See [FLASHING.md](../../docs/FLASHING.md).

The current Soong modules produce separate executables:

| Module | Executable |
|---|---|
| `mindone_mdinit` | `/vendor/bin/mindone_mdinit` |
| `mindone_fsd` | `/vendor/bin/mindone_fsd` |
| `mindone_rpcd` | `/vendor/bin/mindone_rpcd` |

The adjacent historical init fragments still name the stock `ccci_mdinit`, `ccci_fsd` and
`vendor.ccci_rpcd` services and stock executable paths. They are not a ready integration for
these module names and must not be installed unchanged. Before a test, prepare separate disabled
services with distinct names and the executable paths above; review their lifecycle and startup
order against the installed vendor configuration. Do not run both implementations against the
same modem channels. In particular, the stock file server may be a thread inside its modem-init
process rather than a separately stoppable service.

Preserve stock executable files and their product entries. Do not overwrite live vendor
executables or treat restarting services as a guaranteed modem recovery procedure. A candidate
needs a reviewed installation and recovery plan, an exact native build, and an owner-supervised
validation window. Calls and SMS require the owner's explicit authorization.

## SELinux prerequisites

The device tree labels the three `/vendor/bin/mindone_*` executables with the existing
`ccci_mdinit_exec`, `ccci_fsd_exec` and `ccci_rpcd_exec` types. This is a starting point, not
proof that the full replacement stack can run under the compiled policy.

Before starting a candidate service, compile and validate its init configuration and SELinux
policy, confirm the installed executable labels and process domains, and check the required
CCCI, NVRAM and filesystem access. Keep SELinux enforcing throughout the test. Successful
registration alone does not validate reset recovery, filesystem requests or call audio.

## Exact paths written by this task

```
modem/ccci-userspace/common/ccci_ioctl.h
modem/ccci-userspace/common/ccci_rpc.h
modem/ccci-userspace/common/nvram_shim.h
modem/ccci-userspace/mdinit/ccci_mdinit.c
modem/ccci-userspace/mdinit/init.mindone_mdinit.rc
modem/ccci-userspace/fsd/ccci_fsd.c
modem/ccci-userspace/fsd/init.mindone_fsd.rc
modem/ccci-userspace/rpcd/ccci_rpcd.c
modem/ccci-userspace/rpcd/init.mindone_rpcd.rc
modem/ccci-userspace/mux/n_gsm_ldisc_setup.c
modem/ccci-userspace/Android.bp
modem/ccci-userspace/README-INTEGRATION.md   (this file)
```

`device/ikko/mindone/` was not touched. No binaries were committed.
