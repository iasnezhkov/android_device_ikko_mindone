/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * mind_one shim for the stock libnvram.so C API, as called by ccci_mdinit
 * and ccci_fsd.
 *
 * We do NOT reimplement libnvram.so / libcustom_nvram.so / libnvram_sec.so /
 * libfile_op.so -- NVRAM-LID-1409 S5/S8 explicitly recommends against
 * reimplementing NVM_RestoreFromBinRegion_OneFile's on-disk bin-region
 * format (block geometry / bad-block bitmap unknown, and it is the single
 * highest-consequence function in the whole surface to get wrong: silent
 * IMEI/calibration corruption, not a crash). Per this task's own brief we
 * keep libnvram.so etc. as linked, unmodified, stock shared libraries and
 * only declare enough of an extern C ABI to call the same entry points the
 * stock ccci_mdinit/ccci_fsd binaries call.
 *
 * SIGNATURE PROVENANCE -- read this before touching this file:
 * We have no header for libnvram.so (never shipped one). The signatures
 * below were recovered by disassembling the actual call sites inside the
 * stock /vendor/bin/ccci_mdinit binary (objdump -d, aarch64, read at two
 * independent NVM_GetLIDByName/NVM_GetFileDesc/NVM_CloseFileDesc call
 * sites -- offsets 0x26194/0x261cc/0x26204 and 0x2a07c/0x2a0bc/0x2a124 --
 * plus one NVM_RestoreFromBinRegion_OneFile call site at offset 0x1a4d0),
 * not from any header or public documentation. Confidence is HIGH for
 * argument *count* and general shape (register-level evidence is
 * unambiguous: x0 set once before NVM_GetLIDByName's bl, four registers
 * x0-x3 set before NVM_GetFileDesc's bl, the fd it returns is fed straight
 * into __read_chk's fd argument), MEDIUM for exact field semantics (the
 * "token" second word NVM_GetFileDesc returns in x1 and NVM_CloseFileDesc
 * consumes back is real -- disassembly shows it explicitly saved across the
 * two calls -- but its meaning was not further reverse-engineered), and
 * LOW/UNVERIFIED for NVM_RestoreFromBinRegion_OneFile's exact parameter
 * semantics (see the comment on that declaration). This is GAP, not
 * PROVEN, for anything beyond "these are the functions and call order the
 * stock binary uses."
 */
#ifndef MINDONE_NVRAM_SHIM_H
#define MINDONE_NVRAM_SHIM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NVM_GetLIDByName(name) -- returns the LID (>=0) whose registered
 * NVRAM-table path CONTAINS `name` as a substring (strstr-based scan, NOT
 * exact match -- disassembly-confirmed independently by
 * NVRAM-LID-1409 S7 against libcustom_nvram.so's own
 * NVM_GetLIDByName, and re-confirmed here by finding ccci_mdinit passes
 * full absolute paths like "/vendor/nvdata/APCFG/APRDCL/MD_SBP" and
 * "/mnt/vendor/nvdata/APCFG/APRDCL/MD_SBP", both of which are substrings
 * of the *real* table entry's path -- see NVRAM-LID-1409 S1 for the
 * decoded LID table). Returns a negative value on no-match (checked via
 * `tbnz w0, #31` at both call sites, i.e. "branch if bit 31 set" = "if
 * negative").
 *
 * ccci_mdinit calls this exactly twice, both times to resolve the "MD_SBP"
 * (Sales/Branding Programming code) LID -- NOT an IMEI/calibration LID.
 * This lowers the practical risk of the *GetFileDesc/CloseFileDesc* path
 * specifically (see "Get: usp_sbp=%d, cip_sbp=%d, project_sbp=%d,
 * nvram_sbp=%d, set sbp=%d" in ccci_mdinit's own strings -- it merges SBP
 * from multiple sources, of which the live NVRAM file is only one).
 */
int NVM_GetLIDByName(const char *name);

/*
 * NVM_GetFileDesc(lid, size_out, reserved, mode) -- opens the LID's backing
 * file and returns a REAL POSIX file descriptor (disassembly-confirmed: the
 * returned value in x0 is passed directly as the first argument to
 * __read_chk(fd, buf, count, buflen) at the call site, i.e. libnvram opens
 * the file itself and hands back an fd, it does not hand back an opaque
 * handle needing translation).
 *   - `size_out`: an out-parameter (int*) the disassembly shows filled in
 *     by the call and then used, sign-extended, as the read() length --
 *     almost certainly "how many bytes to read for this record."
 *   - `reserved`: a second pointer argument (mov x2, sp) whose target is
 *     never read back by ccci_mdinit's own code after the call in either
 *     traced call site -- pass a valid zeroed word, do not pass NULL
 *     (unverified whether the callee itself dereferences it
 *     unconditionally).
 *   - `mode`: 0 in both traced call sites (both are reads). The write path
 *     was not observed in ccci_mdinit (it only ever reads MD_SBP), so the
 *     nonzero value(s) for write are UNVERIFIED -- do not guess a write
 *     call against this shim without confirming mode's write value first
 *     (a wrong mode on a real NVRAM LID risks corrupting it).
 * Return: a struct-by-value in x0:x1 (AAPCS64 packs a <=16-byte, two
 * 8-byte-field aggregate return into x0/x1) -- x0 is consumed as the fd,
 * x1 ("token") is disassembly-confirmed saved across the call and fed
 * verbatim into NVM_CloseFileDesc's second argument. We model this as a
 * `long`-pair struct rather than committing to the field types the real
 * implementation uses internally, since only the register-level ABI
 * outcome (which we replicate) was actually observed, not the source type.
 */
struct nvm_file_desc {
	long fd;	/* x0 on return: real POSIX fd, negative on error */
	long token;	/* x1 on return: opaque, must be echoed back to NVM_CloseFileDesc */
};
struct nvm_file_desc NVM_GetFileDesc(int lid, int *size_out, void *reserved, int mode);

/* NVM_CloseFileDesc(fd, token) -- `token` is whatever NVM_GetFileDesc
 * returned in the paired `.token` field for that same fd; disassembly shows
 * it masked to 32 bits before the call (`and x1, x23, #0xffffffff`), so a
 * 32-bit-clean value is sufficient even though the parameter slot is 64-bit.
 * Return: nonzero on success (checked via `tbnz w0, #0` -- "branch if bit 0
 * set"), matching ccci_mdinit treating bit0==1 as success.
 */
int NVM_CloseFileDesc(long fd, long token);

/*
 * NVM_RestoreFromBinRegion_OneFile(variant, path) -- the single
 * highest-risk function in this whole surface (NVRAM-LID-1409 S5:
 * "the single highest-consequence function ... to get wrong"). Disassembly
 * of ccci_mdinit's one call site (offset 0x1a4d0) shows:
 *   - x0 ("variant"): either -1 (the literal branch taken in the traced
 *     path) or a small nonnegative index computed from a per-MD-load-type
 *     lookup table a few dozen instructions earlier (indexed by a value
 *     read out of stack storage that this pass did not trace back to its
 *     ultimate source -- plausibly an MD_LOAD_TYPE / RAT-variant selector).
 *     UNVERIFIED which branch is "the common case" on our board.
 *   - x1 ("path"): a heap/stack-built absolute file path, NOT a bare LID
 *     name or integer LID -- confirmed by the adjacent error strings
 *     "Restore: [error]file path not find %s" / "file path too long: %s".
 *     The path is assembled by the RAT-variant table lookup above, i.e. it
 *     is a *per-image-variant* NVRAM restore, not a fixed single file.
 * Return: nonzero on success (`tbz w0, #0` branches PAST the error path,
 * i.e. bit0==0 is the error case here -- note this is the OPPOSITE polarity
 * from NVM_CloseFileDesc above; re-verified independently at the
 * disassembly, not assumed to be consistent across the two functions).
 *
 * RISK DECISION (this project): mindone_mdinit calls this function but
 * DEFAULTS THE STEP TO DISABLED (compile-time MINDONE_ENABLE_BINREGION_RESTORE,
 * 0 unless overridden) because:
 *   (1) MODEM-STACK-1409 S3 PROVES (exhaustive negative grep across
 *       ccci_md_all/ + ccci_util_lib/) the kernel FSM has NO gate on NVRAM
 *       readiness at all -- CCCI_IOC_DO_START_MD works whether or not this
 *       step ever runs, so skipping it does not block modem boot;
 *   (2) the exact argument semantics above are inferred from one
 *       disassembled call site, not proven against a header or a second
 *       independent trace;
 *   (3) a wrong call into a bin-region restore is silent-corruption risk
 *       (IMEI/calibration), not a crash you'd notice immediately.
 * When enabled, failure is logged and treated as non-fatal (matching (1)):
 * we proceed to CCCI_IOC_DO_START_MD regardless of this call's result,
 * exactly as the kernel-side proof says is safe.
 */
struct nvm_file_desc; /* fwd decl already above; kept for reading order */
int NVM_RestoreFromBinRegion_OneFile(int variant, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* MINDONE_NVRAM_SHIM_H */
