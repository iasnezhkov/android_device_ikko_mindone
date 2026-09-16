/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mind_one /dev/ccci_fs wire protocol -- recovered by disassembly of the
 * stock /vendor/bin/ccci_fsd blob (this replaces the "assumed layout" that
 * shipped in fsd/ccci_fsd.c before this pass; see F4396 in the fact log
 * for the earlier failed attempt and why it failed).
 *
 * ============================================================================
 * PROVENANCE
 * ============================================================================
 * Binary disassembled: /vendor/bin/ccci_fsd pulled read-only from the build environment's
 * guest (vendor/ikko/mindone/proprietary/vendor/bin/ccci_fsd),
 * 126140 bytes, ELF32 ARM/EABI5 PIE, stripped.
 *   BuildID (md5/uuid): 76c6fa8f0f2e8f6350ae7f00036f8357
 *   sha256: ff86d1af27a472041ab233166e00f3133f1e6526ac79457fa5ab06fb4b5c7d7e
 *
 * The blob carries a `.gnu_debugdata` section (Android "mini debug info":
 * an xz-compressed ELF holding only a stripped-down .symtab). Decompressing
 * it (`xz -d`) yields a full function symbol table -- every FS_CCCI_* / OTP_*
 * / FS_Init / main / signal_treatment symbol cited below comes from that
 * table, not from guesswork. Per that table every function except the
 * _start/_start_main interworking stubs is compiled in plain ARM mode (not
 * Thumb2) -- this is *why* the previous attempt (F4396) misdecoded the
 * binary: it forced Thumb decoding of what is actually ARM-mode code.
 * Disassembly in this pass used capstone (ARM mode, per-symbol boundaries
 * taken from the recovered symtab) against the real .text bytes (file
 * offset = vaddr-0x1000 for vaddr>=0x8a00, identity below that, per the
 * ELF's own program headers).
 *
 * Addresses below are cited as `<symbol>+0x<off>` (absolute VA in the
 * binary's own address space, i.e. `<symbol VA>+off` -- add up for the
 * absolute address if needed). All of it is reproducible from the same
 * binary+method; nothing here is inferred from another device's firmware.
 *
 * ============================================================================
 * TRANSPORT (kernel side, cross-checked against our own kernel module source)
 * ============================================================================
 * - Port table entry (the kernel repository's mindone/modules/ccci_md_all/port/port_cfg.c:111-113,368-370):
 *     {CCCI_FS_TX=15, CCCI_FS_RX=14, DATA_FSD_Q, ..., MD1_NORMAL_HIF,
 *      PORT_F_USER_HEADER | PORT_F_WITH_CHAR_NODE | PORT_F_CLEAN,
 *      &char_port_ops, minor=4, "ccci_fs"}
 *   PORT_F_WITH_CHAR_NODE additionally sets PORT_F_ADJUST_HEADER at init
 *   (port/port_char.c:94). Combined with PORT_F_USER_HEADER, port_adjust_skb()
 *   (port/port_proxy.c:967-991) does NOT strip the 16-byte struct ccci_header
 *   on RX -- userspace sees the full header on every read(), and must supply
 *   its own header on every write() (port_dev_write(), port_proxy.c:487-599).
 * - struct ccci_header is 16 bytes (see ccci_ioctl.h in this same directory,
 *   sourced from mtk_ccci_common.h:77-84, sizeof() corrected to 16 by F4391
 *   -- data[2] (8B) + channel:16/seq_num:15/assert_bit:1 (4B) + reserved (4B)).
 * - port_dev_write()/mtk_ccci_send_data() (port_proxy.c:513-514,227-228)
 *   compute `header_len = sizeof(ccci_header) + (rx_ch==CCCI_FS_RX ? 4 : 0)`
 *   -- CCCI_FS is the ONLY port in the whole table with this +4 special
 *   case, i.e. the kernel already knows the FS wire format carries one
 *   extra u32 immediately after the 16-byte header before the MTU bound is
 *   checked. port_dump_raw_data() (port_proxy.c:875-898) independently
 *   confirms this: for CCCI_FS_RX specifically it skips exactly one extra
 *   `unsigned int` before dumping raw bytes (`if (port->rx_ch==CCCI_FS_RX)
 *   curr_p++;`, line 897-898).
 * - ccci_fsd itself validates the header on every read() (main+0x1df8..
 *   main+0x1e08, i.e. VA 0xb598-0xb5ac): `uxth r0,[header+8]; cmp r0,#0xE`
 *   -- rejects anything whose `channel` field (the low 16 bits of the u32
 *   at header offset 8) is not 14 (CCCI_FS_RX). This is an independent,
 *   binary-side confirmation of the port table's CCCI_FS_RX=14 and of
 *   F4391's header layout (specifically that `channel` sits at header+8,
 *   not some other offset).
 *
 * ============================================================================
 * PHYSICAL FRAME SIZE AND FRAGMENTATION (main(), VA 0x97a4-0xdaa8)
 * ============================================================================
 * - Single read(2) call: `read(fd, rxbuf, 0xd94)` -- main+0xc70 (VA 0xb418),
 *   `movw r2, #0xd94` at main+0xc6c (VA 0xb410). 0xd94 = 3476 bytes is the
 *   hard cap for ONE physical read() off /dev/ccci_fs.
 * - Per-modem-instance context: main indexes a table of per-instance state
 *   with stride 0x4014 or 0x40dc bytes (main+0xe30..0xe40, VA 0xb5d4-0xb5e8;
 *   the stride is picked by a single global byte flag, purpose not resolved
 *   in this pass) -- consistent with MODEM-STACK-1409 S2.10's
 *   already-documented md1/md2/md3/md5 instance roots (only md1 is populated
 *   on our single-modem board).
 * - FRAGMENTATION IS REAL, PROVEN (main+0xe48-0xe378, VA 0xb5ec-0xb718):
 *   header.data[0] (the first 4 bytes of the 16-byte ccci_header) doubles as
 *   a fragmentation flag (tested with `cmp r1,#0; bmi ...` -- i.e. tested as
 *   SIGNED, sign bit = "continuation fragment"), combined with a 1-byte
 *   per-instance reassembly-state variable (0=empty, 1=complete-ready-for-
 *   dispatch, else=mid-reassembly) stored in a small per-instance byte array.
 *     - First/only fragment (data[0]>=0, state==0): the WHOLE received
 *       buffer (header included) is memcpy'd verbatim into the per-instance
 *       context buffer (main+0xe6c, VA 0xb610-0xb618).
 *     - Continuation fragment (data[0]<0, state==1): the code subtracts
 *       0x14 (20) from the embedded length and memcpy's starting 0x14 bytes
 *       into the per-instance buffer INTO the current write offset (an
 *       accumulator kept in the same per-instance table), i.e. each
 *       continuation fragment carries a 20-byte sub-header of its own
 *       before its share of payload bytes (main+0xf38-0xf5c, VA
 *       0xb6dc-0xb708). The 20-byte continuation sub-header's own layout
 *       was NOT decoded in this pass (OPEN, see the fact log F4414).
 *     - Once state==1 the accumulated per-instance buffer is dispatched
 *       (main+0xec4, VA 0xb668, jumps into the same op dispatch used for a
 *       single, unfragmented request).
 *   PRACTICAL EFFECT: a single LOGICAL /dev/ccci_fs request/response can
 *   exceed 3476 bytes via this fragmentation scheme; the per-instance
 *   buffer (0x4014/0x40dc bytes) is the real upper bound on one logical
 *   message, not the 3476-byte physical read() cap.
 *
 * ============================================================================
 * THE TWO "CMPT" (compatibility/combined) REQUEST SHAPES
 * ============================================================================
 * Once a request is fully reassembled, main() dispatches either to one of
 * ~25 named "modern" one-op-per-message handlers (FS_CCCI_Open/Read/Write/
 * Seek/Close/CloseAll/CreateDir/RemoveDir/GetFileSize/GetFolderSize/Rename/
 * Move/Count/GetDiskInfo/Delete/GetAttributes/FindFirst/FindNext/FindClose/
 * XDelete/GetDrive/GetClusterSize/Restore/GetFileDetail/BinRegion_Access --
 * all confirmed present and called from main() by address, see the call
 * list in the fact log F4414) OR to one of two "CMPT" (compatibility)
 * handlers that BUNDLE several primitive FS ops (open+seek+read/write+close)
 * into a single request/response round trip using a BITMASK, not a single
 * enum value, at the field this project's earlier draft called "opid_map".
 * The CMPT path is the one MODEM-STACK-1409 S2.10 already flagged
 * as carrying the actual NVRAM/calibration file traffic, so it is the one
 * fully decoded here. The exact numeric opid values used to select the ~25
 * individual "modern" ops were NOT recovered in this pass (OPEN, see
 * the fact log F4414) -- only the call target list (which function gets
 * called at all) is proven.
 *
 * IMPORTANT, PROVEN ASYMMETRY: the two CMPT handlers are NOT called with
 * the same base pointer relative to the reassembled buffer:
 *   - FS_CCCI_CMPT_Write (VA 0xe0fc) is called from main+0x1f90 (VA 0xd684-
 *     0xd690) with its struct argument = the reassembled buffer's OWN base
 *     (`add r1, lr, #0xc8` where `lr = sp+0x1000`, i.e. the exact same
 *     address main() uses as the read()-target buffer itself).
 *   - FS_CCCI_CMPT_Read (VA 0xdc24) is called from main+0x1db0 (VA 0xd54c-
 *     0xd554) with its struct argument = reassembled buffer base + 0x30
 *     (`add r1, lr, #0xf8` = base+0x30).
 * Both receive the (already located) filename pointer as a SEPARATE first
 * argument (r0), not as a field inside their own struct argument; where in
 * the wire buffer that filename pointer is computed from was NOT traced to
 * an exact byte offset in this pass (OPEN, see the fact log F4414) -- it is
 * confirmed to be a WIDE-CHAR (UCS-2) string, consumed with no conversion
 * step by FS_CCCI_Open (which immediately calls FS_ConvWcsToCs() on it,
 * FS_CCCI_Open+0x54, VA 0xe9b8, to build a narrow-char path for the real
 * POSIX calls underneath).
 *
 * CAVEAT on the write-side base pointer specifically: by the time
 * FS_CCCI_CMPT_Write is called, the 16-byte header's own fields (channel,
 * length) have already been read out and are dead for the rest of this
 * code path, so an optimizing compiler is free to let a later, unrelated
 * local variable reuse that same stack address -- this project's reading
 * is that main() constructs a fresh write-request struct (not the raw
 * wire bytes) at that reused address before calling FS_CCCI_CMPT_Write,
 * rather than the true on-wire write-shaped payload literally starting
 * where the header used to be. This was NOT independently confirmed
 * (would need to trace what fills buffer+0x00..0x27 before the call, not
 * done in this pass). CONSEQUENCE FOR THIS HEADER'S CONSUMERS: treat
 * struct ccci_fs_cmpt_write_req as based at PAYLOAD START (i.e.
 * wire-buffer + sizeof(struct ccci_header), NOT wire-buffer + 0) unless
 * a live capture proves otherwise -- that is what fsd/ccci_fsd.c does.
 * struct ccci_fs_cmpt_read_req's payload+0x30 base is unaffected by this
 * caveat (its own base pointer is derived the same way, but 0x30 bytes
 * further into the same live region, not into dead header space).
 *
 * Both struct layouts below are cited field-by-field against the actual
 * `ldr r?, [r4, #imm]` / `tst r0, #bit` instructions inside each handler,
 * with the base-pointer relationship documented above -- NOT re-derived
 * offsets, not guesswork.
 */
#ifndef MINDONE_CCCI_FS_WIRE_H
#define MINDONE_CCCI_FS_WIRE_H

#include <stdint.h>
#include "ccci_ioctl.h"		/* struct ccci_header, CCCI_DEV_FS */

/* ------------------------------------------------------------------ */
/* Physical/transport constants                                        */
/* ------------------------------------------------------------------ */
#define CCCI_FS_CHANNEL_RX	14	/* the kernel module tree port_cfg.c CCCI_FS_RX; validated
					 * live by the stock daemon itself,
					 * main+0x1e04 (VA 0xb5a8): `cmp r0,#0xe` */
#define CCCI_FS_READ_CHUNK	0xd94	/* 3476 -- single read(2) cap, main+0xc6c
					 * (VA 0xb410): `movw r2, #0xd94` */
#define CCCI_FS_INST_BUF_A	0x4014	/* per-instance reassembly buffer size,
					 * variant A (main+0xe38, VA 0xb5c4) */
#define CCCI_FS_INST_BUF_B	0x40dc	/* variant B, same call site, selected
					 * by a global byte flag not resolved
					 * in this pass */

/* ------------------------------------------------------------------ */
/* FS_CCCI_CMPT_Write request -- offsets verified against FS_CCCI_CMPT_ */
/* Write, VA 0xe0fc-0xe470. Base pointer used by fsd/ccci_fsd.c: PAYLOAD */
/* START (wire-buffer + sizeof(struct ccci_header)) -- see the CAVEAT    */
/* above the struct definitions on why the stock binary's own base      */
/* pointer (which overlaps the dead header bytes) is not reused as-is.  */
/* ------------------------------------------------------------------ */
struct ccci_fs_cmpt_write_req {
	uint32_t opid_map;	/* +0x00 BITMASK, not an enum. Bits observed  *
				 * live-tested in FS_CCCI_CMPT_Write:         *
				 *   bit0 (0x01) OPEN   -- FS_CCCI_Open+0x2e   *
				 *   bit2 (0x04) SEEK   -- +0x54                *
				 *   bit5 (0x20) WRITE  -- +0x7c (NOTE: bit5,  *
				 *                         not bit3 as in the *
				 *                         read-side bitmask) *
				 *   bit4 (0x10) CLOSE  -- +0xa8                */
	uint8_t  _reserved_04[8];	/* +0x04..+0x0b, not read by this handler */
	uint32_t flag;		/* +0x0c open() flags, passed verbatim as
				 * FS_CCCI_Open's 2nd arg (+0x14: `ldr r1,
				 * [r4,#0xc]; ... bl FS_CCCI_Open`) */
	uint32_t offset;	/* +0x10 seek offset (+0x64: `ldr r1,[r4,#0x10]`) */
	uint32_t whence;	/* +0x14 seek whence (+0x68: `ldr r2,[r4,#0x14]`) */
	uint8_t  _reserved_18[4];	/* +0x18..+0x1b */
	uint32_t length;	/* +0x1c write length (+0x86: `ldr r2,[r4,#0x1c]`),
				 * passed as FS_CCCI_Write's 2nd arg */
	int32_t  file_handle;	/* +0x20 EXISTING handle to reuse; if >= 0 the
				 * OPEN step (and its filename/flag fields) is
				 * SKIPPED entirely and this handle is used
				 * directly for seek/write/close (`ldr sl,
				 * [r7,#-8]` where r7==buf+0x28, i.e. reads
				 * +0x20; `cmn sl,#1; ble ...` = branch to the
				 * open path only if handle < 0) */
	uint32_t _reserved_24;	/* +0x24 zeroed by the daemon before dispatch
				 * (`str r5,[r7,#0x28]!` zeroes buf+0x28 as a
				 * side effect of computing the payload
				 * pointer, but +0x24 itself is set to 0 by an
				 * explicit local store earlier in the function
				 * prologue) */
	uint8_t  data[];	/* +0x28 payload bytes to write -- confirmed by
				 * `mov r3, r7` where r7 == buf+0x28, passed as
				 * FS_CCCI_Write's 4th arg (data pointer) */
} __attribute__((packed));

/* ------------------------------------------------------------------ */
/* FS_CCCI_CMPT_Read request -- base = reassembled buffer OFFSET 0x30   */
/* (0x30 bytes further in than the write-side struct above; the gap    */
/* is presumed to hold the inline wide-char filename for read-shaped   */
/* requests, but the exact filename offset was NOT independently       */
/* confirmed in this pass -- see OPEN item above).                     */
/* Verified against FS_CCCI_CMPT_Read, VA 0xdc24-0xe0fc.                */
/* ------------------------------------------------------------------ */
struct ccci_fs_cmpt_read_req {
	uint32_t opid_map;	/* +0x00 (buffer+0x30) BITMASK. Bits observed:  *
				 *   bit0 (0x01) OPEN     -- +0x44 (VA 0xdc68)  *
				 *   bit1 (0x02) GET_SIZE -- +0x104 (VA 0xdd28) *
				 *   bit2 (0x04) SEEK     -- +0x1a4 (VA 0xddc8) *
				 *   bit3 (0x08) READ     -- +0x1c4 (VA 0xdde8) *
				 *   bit4 (0x10) CLOSE    -- +0x1f0 (VA 0xde14) */
	uint8_t  _reserved_04[8];	/* +0x04..+0x0b (buffer+0x34..0x3b) */
	uint32_t flag;		/* +0x0c (buffer+0x3c) open() flags, 2nd arg to
				 * FS_CCCI_Open */
	uint32_t size_out;	/* +0x10 (buffer+0x40) destination for
				 * FS_CCCI_GetFileSize's 2nd arg (an out-param
				 * pointer, per that function's own signature --
				 * NOT itself the size value) */
	uint32_t offset;	/* +0x14 (buffer+0x44) seek offset */
	uint32_t whence;	/* +0x18 (buffer+0x48) seek whence */
	uint32_t read_arg1;	/* +0x1c (buffer+0x4c) FS_CCCI_Read's 2nd arg;
				 * exact semantics (a 2nd length bound? a flags
				 * word?) not resolved in this pass (OPEN) */
	uint32_t length;	/* +0x20 (buffer+0x50) FS_CCCI_Read's 3rd arg
				 * (read length) */
	/* +0x24 (buffer+0x54): read PAYLOAD DATA starts here. The daemon
	 * zeroes the first word here before calling FS_CCCI_Read
	 * (`str r0,[r6,#0x24]!` at FS_CCCI_CMPT_Read+0x38, VA 0xdc5c, where
	 * r0==0 at that point) and passes this same address as
	 * FS_CCCI_Read's 4th arg (destination buffer). Not represented as a
	 * named field here because C can't express "zero-length array
	 * starting mid-struct with a leading zeroed word" cleanly; a
	 * consumer should treat bytes [0x24, 0x24+length) as the payload. */
} __attribute__((packed));

/* ------------------------------------------------------------------ */
/* Fixed-size ack response -- confirmed at main+0x1894-0x1900           */
/* (VA 0xcbd0-0xcc08), used for the ~25 "modern" individual-op path.    */
/* The SAME reassembled buffer is reused in place for the response:     */
/* the leading ccci_header's data[0] has its top bit cleared (was the   */
/* request/fragment flag, F4414) and data[1] (length) is overwritten    */
/* with the total response size below, then write(2)'d back verbatim.  */
/* ------------------------------------------------------------------ */
struct ccci_fs_ack_resp {
	struct ccci_header header;	/* data[1] set to sizeof(this struct) == 0x18 (24) */
	int32_t  result;		/* +0x10 FS_NO_ERROR..<see result codes below> */
	uint32_t _unused;		/* +0x14 always 0 in the traced ack path */
} __attribute__((packed));		/* total 0x18 (24) bytes -- matches the
					 * `mov r7,#0x18` / `cmp r0,r7` write(2)
					 * size check at main+0x1898 (VA 0xcbd4) */

/* ------------------------------------------------------------------ */
/* Variable-size data response -- confirmed at main+0x18f0-0x1970       */
/* (VA 0xcf40-0xcf88), used for the CMPT_Read payload-bearing reply.    */
/* total size = 0x14 (20) + <bytes actually returned>. 20 = 16-byte     */
/* header + one 4-byte result/status word, i.e. NO secondary field      */
/* before the payload on this path (unlike the fixed ack above, which   */
/* carries an extra always-0 word) -- both are proven independently,    */
/* the difference is real, not a transcription error.                  */
/* ------------------------------------------------------------------ */
struct ccci_fs_data_resp {
	struct ccci_header header;	/* data[1] set to 0x14 + data_len */
	int32_t  result;		/* +0x10 */
	uint8_t  data[];		/* +0x14, data_len bytes, data_len taken
					 * from the byte count FS_CCCI_Read (or
					 * the equivalent handler) actually
					 * returned */
} __attribute__((packed));

/* ------------------------------------------------------------------ */
/* Result codes.
 * -1..-7 are the SAME enum already declared in ccci_rpc.h (shared
 * between /dev/ccci_rpc and /dev/ccci_fs per that header's own comment --
 * both wire protocols are answered from the same kernel header, no
 * FS-specific variant exists). The additional codes below were found by
 * scanning every FS_CCCI_ and OTP_ function's `mvn` (bitwise-NOT-immediate)
 * result-building instructions; only four are independently pinned to a
 * specific errno by FS_ErrorConv (VA 0x1af38-0x1afd0) -- the rest are
 * confirmed to EXIST as distinct result values (with the cited call site
 * as evidence) but their correct symbolic NAME was not established in
 * this pass, so they are listed as OPEN numeric constants rather than
 * guessed at.
 * ------------------------------------------------------------------ */
/* Already known (ccci_rpc.h), repeated here only in a comment for
 * locality -- do NOT redefine, include ccci_rpc.h for these:
 *   FS_NO_ERROR=0 FS_NO_OP=-1 FS_PARAM_ERROR=-2 FS_NO_FEATURE=-3
 *   FS_NO_MATCH=-4 FS_FUNC_FAIL=-5 FS_ERROR_RESERVED=-6 FS_MEM_OVERFLOW=-7
 */
#define FS_ERR_ENOENT_MAPPED	(-9)	/* FS_ErrorConv: errno==ENOENT(2) -> -9,
					 * VA 0x1afac-0x1afb8 */
#define FS_ERR_EACCES_MAPPED	(-16)	/* FS_ErrorConv: errno==EACCES(13) -> -16,
					 * VA 0x1afa0-0x1afa8 */
#define FS_ERR_EMFILE_MAPPED	(-5)	/* FS_ErrorConv: errno==EMFILE(24) -> -5
					 * (coincides with FS_FUNC_FAIL), VA
					 * 0x1af94-0x1af9c */
#define FS_ERR_GENERIC_MAPPED	(-18)	/* FS_ErrorConv: any other errno -> -18,
					 * VA 0x1afac-0x1afb4 (default arm) */
/* Observed but NOT name-confirmed (OPEN, the fact log F4414) -- cited by
 * example site only, each constant appears at multiple call sites:
 *   -10  e.g. FS_CCCI_GetFileSize+0x288 (VA 0xfef8)
 *   -12  e.g. FS_OTPLock+0x1e8         (VA 0x1c720)
 *   -19  e.g. FS_CCCI_Restore+0x3b0    (VA 0xe818)
 *   -45  e.g. FS_CCCI_Restore+0x368    (VA 0xe7d0)
 *   -49  e.g. FS_CCCI_Move+0xf74       (VA 0x148e8)
 */

#endif /* MINDONE_CCCI_FS_WIRE_H */
