/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
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
