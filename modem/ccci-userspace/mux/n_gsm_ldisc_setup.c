/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * mindone mux -- n_gsm line-discipline setup DRAFT (design deliverable,
 * item 4 of this task). NOT built or wired into any init.rc by this
 * commit -- see the recommendation at the bottom of this comment and in
 * README-INTEGRATION.md's "Mux" section for why the project's actual
 * recommended path is a userspace 27.010 daemon instead, with this file
 * kept as a documented alternative for whoever revisits the question.
 *
 * ============================================================================
 * Option A: kernel n_gsm line discipline (this file)
 * ============================================================================
 * PROVEN: our kernel does NOT build n_gsm today --
 * tools/kconfig/running-6.12-1209.config:3130 = "# CONFIG_N_GSM is not set".
 * mainline n_gsm lives at kernel612-common/drivers/tty/n_gsm.c and the
 * uapi it exposes is kernel612-common/include/uapi/linux/gsmmux.h (struct
 * gsm_config, GSMIOC_*, N_GSM0710 = 21 -- kernel612-common/include/uapi/
 * linux/tty.h:32). Enabling it means: flip CONFIG_N_GSM=y, rebuild, and the
 * kernel then does 07.10 framing itself over whatever raw tty you attach
 * the ldisc to, exposing /dev/gsmttyN pseudo-ttys (one per DLCI) instead of
 * gsm0710muxd's userspace-created /dev/radio/ptty* PTYs.
 *
 * No line-discipline-number conflict: N_GSM0710=21 vs. our board's other
 * ldisc user, the connectivity-combo-chip's stp_uart_ldisc ("n_mtkstp",
 * number 16, the kernel repository's mindone/modules/wmt_drv/common_main/linux/stp_uart.c) -- these are
 * different numbers, different ttys, no interaction. The August ldisc-
 * hijack bug this project already fixed (the fact log F3207/F3208/F3210:
 * stp_uart_ldisc missing its .num field on kernel >=5.15's
 * tty_register_ldisc() API, silently taking over slot 0/N_TTY and
 * breaking every new tty including gsm0710muxd's ptys) is orthogonal to
 * n_gsm either way -- it is confirmed fixed in the current kernel module tree
 * (stp_uart.c:835 sets .num = N_MTKSTP explicitly) regardless of which
 * mux implementation ends up in front of /dev/ttyC0.
 *
 * The setup sequence below is the standard, well-documented one (see
 * Linux Documentation/networking/gsm and mainline `gsmMuxd`/util-linux
 * `ldattach` implementations for prior art):
 *   1. open the raw tty (our board's single physical AT channel, ttyC0)
 *   2. ioctl(fd, TIOCSETD, &ldisc) with ldisc = N_GSM0710 -- attaches the
 *      ldisc to this tty. From this point the fd's read/write semantics
 *      change: writes go out as 07.10 control-channel frames, and the
 *      ldisc creates /dev/gsmttyN nodes for the negotiated DLCIs.
 *   3. ioctl(fd, GSMIOC_GETCONF, &cfg) / fill in adaption, encapsulation,
 *      initiator=1 (we are the initiator, matching gsm0710muxd's own role
 *      as the AP-side mux master against the MD's mux responder), timers,
 *      window size, then ioctl(fd, GSMIOC_SETCONF, &cfg) to push it and
 *      start the multiplexer control channel (DLCI 0) handshake.
 *   4. ioctl(fd, GSMIOC_GETFIRST, &first) to learn the base minor number
 *      the kernel assigned, so higher layers know where /dev/gsmttyN..
 *      starts.
 *
 * ============================================================================
 * Which does the RIL actually expect? -- decisive against Option A as-is
 * ============================================================================
 * PROVEN (MODEM-STACK-1409 S4.1, confirmed independently in this
 * pass by `strings` on the actual mtkfusionrild/libmtk-ril.so pulled from
 * the build tree): mtkfusionrild opens gsm0710muxd's OWN, specifically-named
 * PTY set -- /dev/radio/pttycmd1..pttycmd11 (11 command sub-channels),
 * /dev/radio/pttynoti (URC), /dev/radio/pttynwcmd + pttynwurc
 * (network-specific), /dev/radio/pttyims (a dedicated IMS pseudo-tty,
 * also opened directly by volte_imcb), /dev/radio/atci1..atci4 (a
 * separate non-muxed raw AT passthrough family), plus /dev/ttyC0..ttyC3,
 * ttyC6 and /dev/ttyCMIPC1 as fallback/alternate paths -- NOT the generic
 * /dev/gsmttyN naming n_gsm produces. This is the closed
 * librilfusion.so/libmtk-ril.so RIL stack (4.83 MB, no source), so this
 * naming expectation cannot be changed without RIL disassembly/patching,
 * which is out of this task's scope (MODEM-STACK-1409 S6.1 already
 * scopes a from-scratch RIL as a 3-6 month project by itself).
 *
 * Consequence: switching to n_gsm is NOT a drop-in replacement for
 * gsm0710muxd against THIS RIL -- it would additionally require a small
 * shim daemon that (a) does the same DLCI-to-channel-purpose assignment
 * gsm0710muxd performs at mux-establishment time (which command sub-
 * channel is "1", which is IMS, etc. -- a convention the MD firmware and
 * gsm0710muxd agree on that this pass did not fully reverse-engineer) and
 * (b) creates /dev/radio/ptty* symlinks (or bind-mounts) onto the
 * kernel-assigned /dev/gsmttyN nodes in the right order. That shim would
 * be nontrivial and would NOT reduce total effort versus just
 * reimplementing gsm0710muxd's own userspace framing directly (Option B).
 *
 * ============================================================================
 * RECOMMENDATION (this project, this pass)
 * ============================================================================
 * Implement Option B: a userspace 07.10 multiplexer that reproduces
 * gsm0710muxd's own external contract (the exact /dev/radio/ptty* naming
 * table + DLCI assignment convention), NOT Option A. Rationale:
 *   1. the fact log F3207/F3208/F3210 (confirmed 2026-08-30) already
 *      proved the classic userspace-mux path works end-to-end against our
 *      exact vendor RIL/MD firmware once the ldisc-hijack bug (unrelated
 *      to which mux you run) was fixed -- that fix is confirmed present in
 *      the current kernel module tree, so the proven-working path is Option B's shape.
 *   2. Option A needs a kernel config change AND a compatibility shim on
 *      top; Option B needs only a userspace daemon -- strictly less
 *      surface area for the same RIL compatibility requirement.
 *   3. Real, GPL-heritage prior art exists directly in this lineage
 *      (MODEM-STACK-1409 S6.5: Tuukka Karvonen's 2003 gsmMuxd is
 *      the proven common ancestor of MediaTek's own gsm0710muxd, sharing
 *      function names like logical_channel_establish/setup_pty_interface
 *      even 15+ years and multiple chip generations later) -- a
 *      from-scratch userspace 07.10 framer is a known-tractable rewrite,
 *      not a blank-page protocol implementation.
 * This file is kept anyway as a working, cited draft in case a future,
 * NOT-RIL-compatible use case (e.g. a from-scratch minimal RIL that is
 * free to pick its own tty naming) wants the kernel-ldisc path instead.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/gsmmux.h>
#include <linux/tty.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <android/log.h>

#define LOG_TAG "mindone_mux_ngsm_draft"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/*
 * mindone_setup_n_gsm() -- attach N_GSM0710 to `tty_path` (our board's
 * single physical AT channel is /dev/ttyC0, kernel612-common uapi tty.h:32
 * for N_GSM0710=21, the kernel repository's mindone/modules/ccci_md_all/port/port_cfg.c for ttyC0 itself
 * being CCCI_UART2, queue=DATA_AT_CMD_Q=5).
 *
 * Returns the negotiated first gsmtty minor via *first_minor, or -1 on
 * error (errno set). NOT called from any daemon's main() in this
 * deliverable -- see the recommendation above.
 */
int mindone_setup_n_gsm(const char *tty_path, unsigned int *first_minor)
{
	int fd, ldisc = N_GSM0710;
	struct gsm_config cfg;

	fd = open(tty_path, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		LOGE("open(%s) failed: %s", tty_path, strerror(errno));
		return -1;
	}

	if (ioctl(fd, TIOCSETD, &ldisc) < 0) {
		LOGE("TIOCSETD(N_GSM0710) on %s failed: %s", tty_path, strerror(errno));
		close(fd);
		return -1;
	}

	if (ioctl(fd, GSMIOC_GETCONF, &cfg) < 0) {
		LOGE("GSMIOC_GETCONF failed: %s", strerror(errno));
		close(fd);
		return -1;
	}

	/*
	 * initiator=1: we (the AP) are the mux control-channel initiator,
	 * matching gsm0710muxd's own role as the side that opens ttyC0 and
	 * drives 07.10 SABM/UA setup against the MD's responder role --
	 * gsm0710muxd's invocation ("-s /dev/ttyC0 -f 512 -n 8 -m basic")
	 * confirms frame size 512 and 8 logical channels as the values this
	 * exact MD firmware expects; mru/mtu below mirror -f 512, k below
	 * mirrors a conservative default (n_gsm's own default is 7).
	 * encapsulation=0 ("basic option") matches gsm0710muxd's "-m basic".
	 */
	cfg.initiator = 1;
	cfg.encapsulation = 0;
	cfg.mru = 512;
	cfg.mtu = 512;
	cfg.k = 7;

	if (ioctl(fd, GSMIOC_SETCONF, &cfg) < 0) {
		LOGE("GSMIOC_SETCONF failed: %s", strerror(errno));
		close(fd);
		return -1;
	}

	if (ioctl(fd, GSMIOC_GETFIRST, first_minor) < 0) {
		LOGE("GSMIOC_GETFIRST failed: %s", strerror(errno));
		close(fd);
		return -1;
	}

	LOGI("n_gsm attached to %s, first gsmtty minor = %u (draft path, not wired to any init.rc)",
	     tty_path, *first_minor);
	/*
	 * Deliberately NOT closing fd: per mainline n_gsm semantics the
	 * ldisc detaches (and every gsmttyN DLCI it owns is torn down) if
	 * this fd is closed or the ldisc is switched back, exactly the same
	 * "hold the fd for the daemon's lifetime" shape as mindone_mdinit's
	 * /dev/ccci_monitor liveness tie -- caller owns `fd` from here on.
	 */
	return fd;
}
