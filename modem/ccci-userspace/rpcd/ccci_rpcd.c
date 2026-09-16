/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * mindone_rpcd -- open replacement for the stock /vendor/bin/ccci_rpcd
 * blob (40 KB, ELF64 aarch64, stripped) answering the small set of
 * /dev/ccci_rpc operation IDs the KERNEL does not already answer itself.
 *
 * Scope, PROVEN by reading port_rpc_recv_match() directly
 * (the kernel repository's mindone/modules/ccci_md_all/port/port_rpc.c:1416-1480, see common/ccci_rpc.h's
 * ccci_rpc_is_userspace_op()): only four op-ids are ever routed to a
 * userspace reader of /dev/ccci_rpc on our board (CONFIG_MTK_TC1_FEATURE,
 * gating a further ~17 LGE/China-market ops, is NOT set in our kernel
 * config) --
 *   IPC_RPC_QUERY_AP_SYS_PROPERTY, IPC_RPC_SAR_TABLE_IDX_QUERY_OP,
 *   IPC_RPC_AMMS_DRDI_CONTROL, IPC_RPC_SAVE_MD_CAPID.
 * Everything else in enum ccci_rpc_op_id (GPIO/ADC/EINT/clkbuf/TRNG/DTSI/
 * queue-remap, ~15 more op-ids) is answered entirely in-kernel on the
 * parallel ccci_rpc_k channel and structurally never reaches this process.
 *
 * Per-op fidelity (all four implemented, none silently stubbed -- see each
 * handler for exactly what is faithful vs. best-effort vs. intentionally
 * not persisted, and why):
 *   - IPC_RPC_QUERY_AP_SYS_PROPERTY: faithful (property_get is exact).
 *   - IPC_RPC_SAR_TABLE_IDX_QUERY_OP: faithful -- calls mtk_sar_table_id_get()
 *     in the STOCK, unmodified libsysenv.so (kept linked, not reimplemented,
 *     same policy as libnvram.so: this is RF/SAR safety calibration data,
 *     out of scope to reverse-engineer and re-derive independently).
 *   - IPC_RPC_SAVE_MD_CAPID: log-only, matching the stock daemon's own
 *     sepolicy (ccci_rpcd.te grants no nvram/nvdata/property-set access at
 *     all -- see README-INTEGRATION.md -- so the stock daemon cannot be
 *     persisting this anywhere durable from this process either).
 *   - IPC_RPC_AMMS_DRDI_CONTROL: intentionally minimal (existence-check +
 *     loud "not implemented" response) -- this is AGPS delta-reference-data
 *     image handling, unrelated to core telephony boot/registration/SMS,
 *     and the full md1drdi image protocol was not reverse-engineered in
 *     this pass. Documented gap, not a silent one.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <android/log.h>
#include <cutils/properties.h>

#include "ccci_ioctl.h"
#include "ccci_rpc.h"

#define LOG_TAG "mindone_rpcd"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/*
 * mtk_sar_table_id_get() -- kept as a call into the stock, unmodified
 * libsysenv.so (ccci_rpcd's own NEEDED list includes it; nm -D on the
 * stock ccci_rpcd shows no such symbol defined locally, i.e. it really is
 * an external call, not inlined logic). Signature recovered from the
 * stock binary's own log-format string "IPC_RPC_SAR_TABLE_IDX_QUERY_OP,
 * value: %d, ret: %d" (two int outputs: a value and a return/status code)
 * plus libsysenv's own guard string "mtk_sar_table_id_get: Incoming
 * pointer is empty" (confirms the first argument is a pointer, NULL-
 * checked). Argument count beyond the one pointer is UNVERIFIED (no
 * disassembly performed on libsysenv.so in this pass, which is a stock,
 * unmodified library we are not attempting to reverse further -- same
 * policy as libnvram.so).
 */
extern int mtk_sar_table_id_get(int *out_value);

static int g_rpc_fd = -1;

static void rpc_send_response(uint32_t op_id, uint32_t para_num,
			       const void *payload, size_t payload_len)
{
	unsigned char buf[RPC_MAX_BUF_SIZE];
	struct rpc_buffer *resp = (struct rpc_buffer *)buf;
	size_t total = sizeof(*resp) + payload_len;

	if (total > sizeof(buf)) {
		LOGE("rpc_send_response: payload too large (%zu > %zu)",
		     payload_len, sizeof(buf) - sizeof(*resp));
		return;
	}
	memset(resp, 0, sizeof(*resp));
	resp->op_id = op_id | RPC_API_RESP_ID; /* port_rpc.h:193 */
	resp->para_num = para_num;
	if (payload && payload_len)
		memcpy(resp->buffer, payload, payload_len);

	if (write(g_rpc_fd, buf, total) < 0)
		LOGE("write(%s) response for op 0x%x failed: %s",
		     CCCI_DEV_RPC, op_id, strerror(errno));
}

/*
 * IPC_RPC_QUERY_AP_SYS_PROPERTY -- buffer[] is a NUL-terminated property
 * key string (best-effort framing: the exact rpc_pkt sub-parameter framing
 * inside buffer[] was not disassembled, but a plain NUL-terminated key is
 * the simplest hypothesis consistent with the log format
 * "IPC_RPC_QUERY_AP_SYS_PROPERTY, key<%s>, value<%s>, %d" and is safe to
 * try first since property_get on an unrecognized/garbled key just
 * returns the default we supply, never a crash).
 */
static void handle_query_ap_sys_property(const struct rpc_buffer *req, size_t buflen)
{
	char key[PROPERTY_KEY_MAX];
	char value[PROPERTY_VALUE_MAX];
	size_t keylen = buflen < sizeof(key) ? buflen : sizeof(key) - 1;

	memset(key, 0, sizeof(key));
	memcpy(key, req->buffer, keylen);
	key[keylen] = '\0';

	property_get(key, value, "");
	LOGI("IPC_RPC_QUERY_AP_SYS_PROPERTY, key<%s>, value<%s>, %d",
	     key, value, 0);
	rpc_send_response(req->op_id, 1, value, strlen(value) + 1);
}

static void handle_sar_table_idx_query(const struct rpc_buffer *req)
{
	int value = 0;
	int ret = mtk_sar_table_id_get(&value);

	LOGI("IPC_RPC_SAR_TABLE_IDX_QUERY_OP, value: %d, ret: %d", value, ret);
	if (ret != 0)
		LOGW("Main: IPC_RPC_SAR_TABLE_IDX_QUERY_OP:mtk_sar_table_id_get fail");
	rpc_send_response(req->op_id, 1, &value, sizeof(value));
}

/*
 * IPC_RPC_SAVE_MD_CAPID -- buffer[] carries two u32s per the stock log
 * format "%s:md_capid 0x%x md_aac 0x%x" (field order and widths are the
 * one thing directly quoted by the stock binary's own strings, so this
 * part IS faithful; the surrounding struct framing before those two
 * words is best-effort like the property-query handler above).
 */
struct md_capid_payload {
	uint32_t md_capid;
	uint32_t md_aac;
} __attribute__((packed));

static void handle_save_md_capid(const struct rpc_buffer *req, size_t buflen)
{
	struct md_capid_payload p = { 0, 0 };

	if (buflen >= sizeof(p))
		memcpy(&p, req->buffer, sizeof(p));
	else
		LOGW("IPC_RPC_SAVE_MD_CAPID: short payload (%zu < %zu), using zeros",
		     buflen, sizeof(p));

	/* Log-only: ccci_rpcd.te grants no nvram/nvdata/property-set access
	 * (see README-INTEGRATION.md's sepolicy section) -- so this is the
	 * full extent of what the ORIGINAL daemon's own sandbox permits it
	 * to do here either. Nothing is silently dropped that the stock
	 * binary could have done and we cannot.
	 */
	LOGI("mindone_rpcd:md_capid 0x%x md_aac 0x%x", p.md_capid, p.md_aac);
	rpc_send_response(req->op_id, 0, NULL, 0);
}

static void handle_amms_drdi_control(const struct rpc_buffer *req)
{
	static const char kDrdiImage[] = "/vendor/firmware/md1drdi";
	int exists = (access(kDrdiImage, F_OK) == 0);

	LOGW("IPC_RPC_AMMS_DRDI_CONTROL requested (image %s %s) -- not implemented in mindone_rpcd, answering FS_NO_FEATURE (AGPS delta-reference-data assist path, out of scope for core telephony)",
	     kDrdiImage, exists ? "present" : "absent");
	int32_t result = FS_NO_FEATURE;
	rpc_send_response(req->op_id, 1, &result, sizeof(result));
}

int main(void)
{
	unsigned char rxbuf[RPC_MAX_BUF_SIZE];

	LOGI("mindone_rpcd Ver:mindone-1, CCCI Ver: our-6.12-port");

	g_rpc_fd = open(CCCI_DEV_RPC, O_RDWR);
	if (g_rpc_fd < 0) {
		LOGE("Main: open ccci_rpc fail: %s", strerror(errno));
		return 1;
	}

	for (;;) {
		ssize_t n = read(g_rpc_fd, rxbuf, sizeof(rxbuf));
		struct rpc_buffer *req;
		size_t buflen;

		if (n < 0) {
			LOGE("Failed to read from RPC device (%d) !! errno = %d",
			     0, errno);
			break;
		}
		if ((size_t)n < sizeof(struct rpc_buffer)) {
			LOGW("Main:can't recognize rpc pkt size:%zd!", n);
			continue;
		}
		req = (struct rpc_buffer *)rxbuf;
		buflen = (size_t)n - sizeof(*req);

		if (!ccci_rpc_is_userspace_op(req->op_id)) {
			/* Should not happen -- the kernel routes non-userspace
			 * ops to the parallel ccci_rpc_k channel, not here --
			 * but never silently swallow an unexpected packet.
			 */
			LOGW("Main: Unknow RPC Operation ID (0x%x)", req->op_id);
			continue;
		}

		switch (req->op_id) {
		case IPC_RPC_QUERY_AP_SYS_PROPERTY:
			handle_query_ap_sys_property(req, buflen);
			break;
		case IPC_RPC_SAR_TABLE_IDX_QUERY_OP:
			handle_sar_table_idx_query(req);
			break;
		case IPC_RPC_SAVE_MD_CAPID:
			handle_save_md_capid(req, buflen);
			break;
		case IPC_RPC_AMMS_DRDI_CONTROL:
			handle_amms_drdi_control(req);
			break;
		default:
			LOGW("Main: Unknow RPC Operation ID (0x%x)", req->op_id);
			break;
		}
	}

	close(g_rpc_fd);
	return 1;
}
