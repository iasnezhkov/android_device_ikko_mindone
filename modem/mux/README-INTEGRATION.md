# mindone_mux -- open replacement for gsm0710muxd

Status: staged, device-untouched (`device/ikko/mindone/` was not modified by
this task). All device reads were read-only (`adb shell` + `/debug_ramdisk/su
-c`: `ls -la /dev/radio /dev/ccci* /dev/ttyC*`, `ps -A`, `/proc/<pid>/cmdline`,
`/proc/<pid>/fd`, `getprop`, `/proc/tty/ldiscs`, `/proc/net/unix`) -- nothing
was written, killed, or rebooted. Static analysis was `strings`/`file` only,
on a build environment's own copy of the shipped blob, no `m`/`ninja`
(the overnight ROM build was left untouched). Syntax-checked in
the LineageOS tree against the real bionic/liblog headers this device's ROM build
uses (`clang --target=aarch64-linux-android31 -std=gnu11 -fsyntax-only -Wall
-Wextra -Werror`) -- passes cleanly. Not build-tested with `m`/`ninja` and
not booted.

## 1. The recovered stock contract

### 1.1 Command line

Confirmed two independent ways, both giving the identical string:

- The shipped service definition,
  `vendor/ikko/mindone/proprietary/vendor/etc/init/gsm0710muxd.rc`:
  ```
  service vendor.gsm0710muxd /vendor/bin/gsm0710muxd -s /dev/ttyC0 -f 512 -n 8 -m basic
      class main
      user radio
      group radio cache inet misc
      disabled
      oneshot
  ```
- A live read on-device, 14.09.2026 (`ps -A | grep gsm0710muxd` -> pid 24391,
  `cat /proc/24391/cmdline`):
  ```
  /vendor/bin/gsm0710muxd -s /dev/ttyC0 -f 512 -n 8 -m basic
  ```

`gsm0710muxd.rc` is `disabled`+`oneshot` -- nothing in it triggers its own
start. The trigger is elsewhere: `device/mediatek/sepolicy_vndr/base/vendor/
ccci_mdinit.te:25` grants `ccci_mdinit` (not `mtkrild`) `set_prop(ccci_mdinit,
vendor_mtk_ctl_gsm0710muxd_prop)`, and `property_contexts:5` maps
`ctl.vendor.gsm0710muxd` to that exact property type -- i.e. **`ccci_mdinit`
starts `gsm0710muxd` itself**, presumably once the modem FSM reaches `READY`
(HS2 complete), not `mtkrild`/`mtkfusionrild`. `mtkfusionrild` itself starts
independently on `vendor.ril.mtk=1` (and three other property triggers,
`vendor/ikko/mindone/proprietary/vendor/etc/init/mtkrild.rc`).

### 1.2 The tty being multiplexed

`/dev/ttyC0` only -- confirmed by the live daemon's own fd table
(`ls -la /proc/24391/fd`): fd0 -> `/dev/ttyC0` is the *only* serial tty fd
held; every other real fd (fd5..fd17, 13 of them) is `/dev/ptmx` (a pty
master). This also settles an open question from
MODEM-STACK-1409 S4.1: `atci1`-style nodes are **not** a separate
non-muxed raw passthrough tty family as that document speculated -- since
the daemon holds no other serial fd, `atci1` must be one of its own 07.10
DLCs on `ttyC0`, like every other `/dev/radio/*` node. Kernel side: `ttyC0`
is `CCCI_UART2`, queue `DATA_AT_CMD_Q`=5
(`ccci_md_all/port/port_cfg.c` in the kernel repository, per MODEM-STACK-1409 S2.1),
sepolicy label `gsm0710muxd_device` (`device.mediatek/sepolicy_vndr/base/
vendor/file_contexts:489` -- note this is a *specific* label for `ttyC0`,
distinct from the generic `/dev/ccci.*` -> `ccci_device` pattern one line
above it).

### 1.3 `+CMUX` handshake -- MediaTek's own reduced form, not generic 3GPP

Confirmed by `strings -a` on the shipped 70392-byte, ELF32 ARM, stripped
binary (`vendor/ikko/mindone/proprietary/vendor/bin/gsm0710muxd`,
dumped with `strings`, 14.09.2026):

```
ATZ
ATE0
AT+CMUX=1                    <- the literal command actually sent
chatCmux
+CMUX: READY
"%d:%s(): Received CMUX: READY, it is new modem, start to init control channel"
"%d:%s(): Received OK, it is old modem, so sleep(1)"
AT+CMUX=%d,%d,%d,%d          <- a 4-param sprintf format ALSO present but
                                 never seen as an actual on-wire literal;
                                 not used by this board's basic-mode path
```

Sequence: `ATZ` -> `ATE0` -> `AT+CMUX=1`, then branch on the response: a
"new modem" answers the URC `+CMUX: READY` and the daemon starts the 07.10
control channel immediately; an "old modem" answers plain `OK` and the
daemon sleeps 1s first. This is **not** the generic 3GPP `AT+CMUX=<mode>,
<subset>,<port_speed>,<N1>,<T1>,<N2>,<T2>,<T3>,<k>` parameter list -- no
literal use of the 4-parameter format was found, only the single-parameter
`AT+CMUX=1`. `mindone_mux` (`mux_start()`) reproduces exactly this observed
sequence, not generic `AT+CMUX` parameter negotiation.

### 1.4 Framing: basic option only

`-m basic` is the only mode this board's `.rc` uses (the binary's own usage
text also offers `advanced`, confirmed by the strings `-m <modem>: Mode
(basic, advanced)`/`basic`/`advanced`, but nothing on this board invokes
advanced). Cross-checked against the kernel: `tools/kconfig/
running-6.12-1209.config:3130` = `# CONFIG_N_GSM is not set` -- our kernel
does not build the in-kernel `n_gsm` line discipline at all, and the
daemon's own strings contain zero occurrences of `N_GSM`/`TIOCSETD`/any
ldisc API name, confirming the framing is done entirely in userspace, not
via the kernel ldisc (MODEM-STACK-1409 S4.1 already established
this; re-confirmed independently in this pass).

Basic option means: flag byte `0xF9`, **no byte-stuffing/transparency**
(unlike advanced/HDLC-style option), EA-encoded length field (1 or 2 bytes),
FCS (reflected CRC-8, polynomial 0xE0) computed over Address+Control+Length
only for UIH frames (Information field excluded). `mindone_mux` implements
only basic option; `-m advanced` is refused with a logged, non-zero exit
rather than a silent fallback.

### 1.5 PTY naming -- the actual RIL compatibility contract

Live, 14.09.2026 (`adb shell '/debug_ramdisk/su -c "ls -la /dev/radio/"'`):

```
atci1      -> /dev/pts/8    (radio:radio)
pttycmd1   -> /dev/pts/5    (radio:radio)
pttycmd2   -> /dev/pts/6    (radio:radio)
pttycmd3   -> /dev/pts/7    (radio:radio)
pttycmd4   -> /dev/pts/3    (radio:radio)
pttycmd7   -> /dev/pts/9    (radio:radio)
pttycmd8   -> /dev/pts/10   (radio:radio)
pttycmd9   -> /dev/pts/11   (radio:radio)
pttycmd10  -> /dev/pts/12   (radio:radio)
pttycmd11  -> /dev/pts/13   (radio:radio)
pttynoti   -> /dev/pts/4    (radio:radio)
pttynwcmd  -> /dev/pts/14   (radio:radio)
pttynwurc  -> /dev/pts/15   (radio:radio)
pttyims    -> /dev/pts/0    (system:system)  -- see note below
ptty2ims   -> /dev/pts/1    (system:system)  -- see note below
ptty3ims   -> /dev/pts/2    (system:system)  -- see note below
```

`mtkfusionrild` (pid 24623) opens exactly the radio:radio set above plus
`pttyims` (`ls -la /proc/24623/fd`, fds map 1:1 onto these pts numbers).

Exhaustive `grep -oE 'pttycmd[0-9]+' / 'ptty[0-9]cmd[0-9]+' / 'atci[0-9]+'`
over the *entire* string table additionally proves:

- `pttycmd5` and `pttycmd6` **do not exist anywhere** in the string table,
  for MD1 or for the `ptty2*`/`ptty3*`/`ptty4*` families (confirmed dead,
  additional-modem-instance naming, MD2/MD3/MD4, all sharing the identical
  cmd1-4,7-11 gap) -- a genuine, deliberate property of the naming scheme,
  not a live/SIM-state artifact.
- `atci1`..`atci4` all exist as literals; only `atci1` is live right now.
- `pttyims`/`ptty2ims`/`ptty3ims` are live but are **not** literal strings
  anywhere in `gsm0710muxd`'s own binary. `gsm0710muxd`'s own fd table (13
  `/dev/ptmx` masters) is exactly 3 short of the 16 live `/dev/radio/*`
  symlinks -- the pttyims/ptty2ims/ptty3ims gap. `volte_imcb` (pid 24399,
  the obvious candidate, runs as `system`) was checked and does **not**
  hold them either -- its own fd table shows it opens `/dev/ccci_imsc`
  directly (a raw CCCI channel), matching MODEM-STACK-1409 S4.4's
  finding that IMS bypasses the AT mux entirely. The actual creator of
  these three nodes is **unresolved** and out of scope: IMS/VoLTE is a
  documented non-goal of this project (MODEM-STACK-1409 S7/S8).
  **`mindone_mux` therefore implements only the 16 channels `gsm0710muxd`
  itself is proven (by literal strings) to own** -- it does not create
  `pttyims`/`ptty2ims`/`ptty3ims`.

sepolicy: `/dev/radio(/.*)?` -> `u:object_r:mtk_radio_device:s0`
(`file_contexts:550`). `gsm0710muxd.te` grants the daemon `capability {chown
fowner setuid}`, `rw_dir_perms` + `create lnk_file` on `mtk_radio_device`,
`setattr` on `devpts:chr_file`, matching the binary's own libc imports
(`chown`, `chmod`, `setuid`, `symlink`, `grantpt`, `ptsname`, `unlockpt` --
all present in the import table) and the observed "muxd switch to user
radio" log string (the daemon starts as root under init, opens
privileged nodes, then drops to `radio`). `mindone_mux` reproduces this
exact drop-privilege pattern in `drop_privileges()`.

### 1.6 DLCI numbers -- not recoverable, a documented assumption

The binary is stripped with no numeric DLCI ever appearing in a log string
(only `%d` placeholders) -- exact stock DLCI-to-name mapping could not be
recovered without a live 07.10 capture on `ttyC0` or a full disassembly,
neither of which this task's read-only/no-write constraints permit.
`mindone_mux` assigns DLCI 1..16 sequentially to the table in section 1.5's
order (`g_chan_defs[]` in `mindone_mux.c`). This is justified by: the
kernel's own MIPC alternative (`ttyCMIPC0`..`ttyCMIPC9`) exposes
textually-identical, mutually interchangeable raw AT channels straight from
CCCI hardware queues with no per-channel semantic distinction on the MD
side -- i.e. the MD firmware's AT parser instances are symmetric, and
channel "purpose" is a pure AP/RIL-side convention (which `/dev/radio/*`
path the RIL happens to read/write), not something encoded in the DLCI
number. **If live testing shows this assumption wrong, only the one table
in `mindone_mux.c` needs to change** -- nothing else in the design depends
on the specific numbering.

### 1.7 Flow control and power saving

Per-DLC flow control **is** used by the stock daemon -- confirmed by
strings: `"Notify by FC On siganl,try to read data and to send it"`,
`"Set FC_OFF_SENDING and rx_fc_off as 1"`, `"Frames allowed, channel
id=%d,tx_fc_off=%d"` / `"No frames allowed...tx_fc_off=%d"`, plus a full MSC
vocabulary (`"start to send msc response"`, `"The mobile station receives
acknowledgment of MSC msg"`, `"tx_msc_response_cache is invalid/null"`).
This is 07.10's per-DLC **Modem Status Command (MSC)** FC bit, not the
global `CMD_FCON`/`CMD_FCOFF` command pair. `mindone_mux` implements MSC
FC fully (both directions: honoring the peer's FC bit, and asserting our
own when a pty's write queue backs up) and additionally implements the
global FCON/FCOFF pair (cheap given the shared control-command dispatcher,
and a real spec feature, not a stub).

**Power Saving Control (PSC) is confirmed NOT used** by the stock daemon:
an exhaustive case-insensitive `grep` for `"psc"` over the entire string
table returns zero hits. `mindone_mux` does not send PSC. If a peer ever
sends PSC to it, it replies the standard Non-Supported-Command (NSC)
response and logs loudly, rather than falsely acknowledging a power-saving
transition it does not actually implement (no low-power UART sleep/wake
path exists in this daemon).

### 1.8 The `+EIND: 128` gate

Strings `"Received +EIND: 128"` / `"Wait +EIND: 128 timeout! Continue..."`
show the stock daemon waits (with a timeout, then proceeds anyway) for a
proprietary `+EIND: 128` URC before declaring channel setup fully done.
Which DLC delivers this URC could not be determined from strings alone.
**Not implemented** in `mindone_mux` (an honest scope cut, logged as such
in the source rather than silently ignored) -- the daemon declares the mux
ready as soon as all configured DLCs have their UA, which is sufficient for
the RIL to start opening `/dev/radio/*` nodes; if live testing shows the
modem needs this extra gate, it can be added as a bounded follow-up once
the delivering channel is identified by capture.

## 2. What is implemented vs not

**Implemented, faithfully:**
- Basic-option 07.10 framing: flag `0xF9`, EA length (1 and 2 byte), FCS
  (reflected CRC-8 poly 0xE0, generated at startup rather than transcribed
  as a literal 256-byte table -- see the self-check in `fcs_table_init()`).
- Control channel (DLCI 0): SABM/UA bring-up, DISC/UA teardown (both
  directions), MSC (both directions, real per-DLC flow control), FCON/FCOFF
  (global), TEST (keep-alive echo, also used for our own `-p` ping
  watchdog), NSC (sent for anything unsupported: PN, RPN, SNC, RLS, PSC).
- 16 data DLCs mapped to the exact stock `/dev/radio/*` names (section 1.5),
  created via `open(/dev/ptmx)` + `grantpt`/`unlockpt`/`ptsname_r` +
  `chown(radio:radio)` + `chmod(0660)` + `symlink()`, matching the stock
  daemon's own libc import list.
- The exact observed `ATZ`/`ATE0`/`AT+CMUX=1`/`+CMUX: READY`-or-`OK`+sleep
  handshake (section 1.3), not generic 3GPP `AT+CMUX` negotiation.
- SABM retry with a bounded N2=3 attempts and a ~1.5s per-attempt timeout,
  so one unresponsive DLC cannot hang the whole startup sequence (the stock
  daemon's own string `"Logical channel %d couldn't be opened"` shows it
  has an equivalent give-up path).
- Clean teardown on `SIGTERM`/`SIGINT`: DISC on every open DLC, DISC on
  DLCI0, symlinks removed, best-effort final TX flush.
- Privilege drop (`setuid`/`setgid` to `radio`, matching the stock "muxd
  switch to user radio" behavior) when launched as root.
- `-t <timeout>` silence watchdog and `-p <n>` ping-based watchdog (built
  on the same CMD_TEST control command the stock daemon's keep-alive design
  implies), `-d` daemonize, `-o <file>` file logging alongside logcat.

**Explicitly NOT implemented (logged, not silent):**
- `-m advanced` (HDLC-style byte-stuffing) -- refused at startup.
- PSC (Power Saving Control) -- replies NSC if requested by the peer.
- PN/RPN/SNC/RLS control commands -- reply NSC if requested by the peer.
- `+EIND: 128` gating (section 1.8).
- `pttyims`/`ptty2ims`/`ptty3ims` (section 1.5 -- not owned by
  `gsm0710muxd` in the first place).
- `-P <pin-code>` is accepted for command-line compatibility (so a script
  invoking the same argv as the stock service doesn't fail) but is a no-op
  -- SIM PIN unlock is the RIL's job over the AT channel, not the mux's.
- `-b <baudrate>` is accepted but unused -- `ttyC0` is a CCCI virtual tty,
  not a real UART, so there is no baud rate to set.

## 3. License note

3GPP TS 07.10 / 27.010's wire-format constants (SABM/UA/DM/DISC/UIH control
values, the EA/CR/PF bit positions, the reflected CRC-8 FCS, the
CMD_MSC/CMD_FCON/CMD_FCOFF/CMD_TEST/CMD_PSC/CMD_NSC command-type values,
the virtual-modem-status bits) are specification-mandated, not
copyrightable expression. To obtain *exact, verified* numeric values
rather than relying on fallible recollection, this implementation's design
notes (in `mindone_mux.c`'s header comment and inline citations) reference
our own git-tracked kernel source, `kernel612-common/drivers/tty/n_gsm.c`
(mainline Linux `n_gsm` line discipline, GPL-2.0) -- **no code from
`n_gsm.c` is reproduced**; every constant is independently re-derived and
cited by line number, and the FCS table is generated by a from-scratch
CRC-8 algorithm at startup (verified against two known table entries)
rather than copied. `mindone_mux`'s own architecture (single-threaded,
`poll()`-based, one process, PTYs created directly by the daemon) is
structurally unrelated to `n_gsm`'s kernel tty-layer/workqueue design.

Real GPL prior art for *this class* of userspace daemon exists in this
project's own research: Tuukka Karvonen's 2003 `gsmMuxd`, the documented
common ancestor of MediaTek's own `gsm0710muxd` (MODEM-STACK-1409
S6.5 -- shared function names like `logical_channel_establish`/
`setup_pty_interface` persist in MediaTek's binary 20+ years later). It was
used here only as a design reference for the overall shape (one AT tty in,
N pseudo-ttys out, a control DLC plus data DLCs) -- no source from it was
read or copied during this task (its source was not fetched in this pass;
the reference is to the *documented existence and shape* of that lineage,
established in the prior MODEM-STACK-1409 research pass).

## 3a. Sepolicy (14.09)

`mindone_mux` is labeled `gsm0710muxd_exec` in `sepolicy/vendor/file_contexts` -> stock `gsm0710muxd`
domain (`device/mediatek/sepolicy_vndr/base/vendor/gsm0710muxd.te`: `gsm0710muxd_device` = /dev/ttyC0,
`mtk_radio_device` = /dev/radio). One script drives the A/B: `tools/rigs/modem-ab.sh swap-mux`.

## 4. A/B test plan

> 🔴 **There is no rescue slot on this device.** Both slots share one `super` region -- the six
> logical partitions of `_a` and `_b` start at the same offsets -- so the system is physically one
> copy, and slot `_b` does not boot at all. "Try it in the spare slot" is not available here; see
> `docs/FLASHING.md`. What makes the test below safe is not a second slot but that nothing is
> replaced: the stock binary stays in place and stays in `proprietary-files.txt`, ours is started
> by hand next to it, and restoring the stock behaviour is `stop` + `start` of the stock service.

1. Build `mindone_mux` (`cc_binary`, `Android.bp`) against the running
   system's ROM tree, alongside the stock `gsm0710muxd` blob (do not remove it from
   `proprietary-files.txt` yet -- this is an A/B swap, not a replacement,
   until proven).
2. With adb root:
   ```
   stop vendor.ril-daemon-mtk
   stop vendor.gsm0710muxd
   # push mindone_mux to /vendor/bin/ (remount vendor rw, or adb push +
   # chcon u:object_r:gsm0710muxd_exec:s0 to reuse the stock domain, OR
   # push under a new name/label -- either works for a manual test since
   # init_daemon_domain isn't involved when launched by hand)
   /vendor/bin/mindone_mux -s /dev/ttyC0 -f 512 -n 8 -m basic -o /data/local/tmp/mindone_mux.log &
   # wait for "Starting mux mode" / "Allocating logical channel N/N" x16
   # in logcat or the log file
   ls -la /dev/radio/          # expect the 16 names in section 1.5
   start vendor.ril-daemon-mtk
   ```
3. Verify, in order:
   - `logcat -b radio` -- `mtkfusionrild` opens the `/dev/radio/*` nodes
     without "open failed"/ENOENT errors.
   - `getprop gsm.version.baseband` / `getprop gsm.sim.state` -- baseband
     version and SIM state populate (matches the known-good AT session
     traced in the fact log F3208: `+EMDVER` ->
     `MOLY.LR13.R2.MP.V195`, `gsm.sim.state=LOADED,ABSENT`).
   - `dumpsys telephony.registry` -- registration state reaches the same
     result as the stock daemon gives on this SIM/carrier (network
     rejection is expected and NOT a mux failure, per F3208 -- that is a
     carrier/HLR fact, not a transport fact).
   - Send/receive a test SMS.
   - Bring up the default APN, confirm a `ccmniN` interface gets an IP
     (data does not flow through the mux at all -- this only validates
     that AT-level PDP activation via the mux didn't break anything).
4. Rollback: `stop vendor.ril-daemon-mtk`; kill `mindone_mux`
   (`SIGTERM`, triggers its own clean DISC teardown); `start
   vendor.gsm0710muxd`; `start vendor.ril-daemon-mtk`. No partition-level
   changes are made by this plan (one executable under `/vendor/bin/`,
   run by hand, not via `init`), so rollback does not need a reflash.

## 5. Exact paths written by this task

```
modem/mux/mindone_mux.c
modem/mux/Android.bp
modem/mux/README-INTEGRATION.md   (this file)
```

`device/ikko/mindone/` was not touched. No binaries were committed. The
phone was cable-attached and read-only throughout (see the command list at
the top of this file); nothing was written, killed, or rebooted on it.
