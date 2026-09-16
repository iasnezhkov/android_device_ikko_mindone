/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * mind_one: runtime binding for the stock libnvram.so / libsysenv.so entry points.
 *
 * WHY THIS EXISTS (15.09, F4442). These daemons were excluded from the build entirely -
 * Android.bp renamed to .disabled - because linking libnvram/libsysenv is impossible from here:
 * those blobs live in the soong namespace `vendor/ikko/mindone`, which is not visible from
 * `device/ikko/mindone`, and the obvious fix (import it) cannot be done because vendor already
 * imports device, so the namespaces would form a cycle.
 *
 * Binding at RUNTIME instead of at link time removes the dependency from the build graph without
 * changing what actually happens on the device: dlopen() resolves through the vendor namespace's
 * own search path, which is exactly where these blobs are. Nothing is reimplemented - the same
 * stock entry points are called, for the same reasons NVRAM-LID-1409 gives for not
 * reimplementing them (the bin-region on-disk format is the single highest-consequence thing in
 * this surface to get wrong: silent IMEI/calibration corruption, not a crash).
 *
 * The signatures come from nvram_shim.h, which documents how each was recovered by disassembling
 * the stock binaries' call sites. This file only adds the plumbing.
 *
 * FAILURE BEHAVIOUR is deliberate: if a library or symbol is missing we log once and return the
 * value the caller already treats as failure, rather than crashing. A modem daemon that cannot
 * read NVRAM must fail visibly and keep the process alive for the log to be read, which is the
 * whole point of running it as an A/B experiment next to the stock one.
 */

#include "nvram_shim.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stddef.h>

#include <log/log.h>

/* Declared at its call site (rpcd/ccci_rpcd.c) rather than in nvram_shim.h, because it belongs to
 * libsysenv, not libnvram. Repeated here so this file's definition has a visible prototype. */
extern int mtk_sar_table_id_get(int *out_value);

#define LIB_NVRAM   "libnvram.so"
#define LIB_SYSENV  "libsysenv.so"

struct nvram_syms {
    int (*get_lid_by_name)(const char *);
    struct nvm_file_desc (*get_file_desc)(int, int *, void *, int);
    int (*close_file_desc)(long, long);
    int (*restore_from_bin_region_one_file)(int, const char *);
    int (*sar_table_id_get)(int *);
};

static struct nvram_syms g_syms;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static void *bind_one(void *handle, const char *lib, const char *sym)
{
    void *p;

    if (!handle)
        return NULL;
    p = dlsym(handle, sym);
    if (!p)
        ALOGE("%s: %s has no symbol '%s' (%s)", __func__, lib, sym, dlerror());
    return p;
}

static void nvram_shim_init(void)
{
    /* RTLD_NOW so a broken blob is reported here, at first use, instead of as a jump through a
     * null PLT slot somewhere deep in a daemon - which is exactly the failure that cost a day
     * chasing the 64-bit audio blob (F4464). */
    void *nvram = dlopen(LIB_NVRAM, RTLD_NOW);
    void *sysenv = dlopen(LIB_SYSENV, RTLD_NOW);

    if (!nvram)
        ALOGE("%s: dlopen(%s) failed: %s", __func__, LIB_NVRAM, dlerror());
    if (!sysenv)
        ALOGE("%s: dlopen(%s) failed: %s", __func__, LIB_SYSENV, dlerror());

    g_syms.get_lid_by_name =
        (int (*)(const char *))bind_one(nvram, LIB_NVRAM, "NVM_GetLIDByName");
    g_syms.get_file_desc =
        (struct nvm_file_desc (*)(int, int *, void *, int))
            bind_one(nvram, LIB_NVRAM, "NVM_GetFileDesc");
    g_syms.close_file_desc =
        (int (*)(long, long))bind_one(nvram, LIB_NVRAM, "NVM_CloseFileDesc");
    g_syms.restore_from_bin_region_one_file =
        (int (*)(int, const char *))
            bind_one(nvram, LIB_NVRAM, "NVM_RestoreFromBinRegion_OneFile");
    g_syms.sar_table_id_get =
        (int (*)(int *))bind_one(sysenv, LIB_SYSENV, "mtk_sar_table_id_get");

    /* Handles are intentionally not closed: these daemons hold them for their whole lifetime, and
     * dlclose()ing a library whose function pointers we keep would be a use-after-free. */
}

int NVM_GetLIDByName(const char *name)
{
    pthread_once(&g_once, nvram_shim_init);
    if (!g_syms.get_lid_by_name)
        return -1;                      /* callers treat a negative LID as "not found" */
    return g_syms.get_lid_by_name(name);
}

struct nvm_file_desc NVM_GetFileDesc(int lid, int *size_out, void *reserved, int mode)
{
    /* .fd negative is the documented error convention (nvram_shim.h), so an unbound symbol looks
     * to the caller exactly like a failed open rather than a silent success on fd 0. */
    struct nvm_file_desc bad = { .fd = -1, .token = 0 };

    pthread_once(&g_once, nvram_shim_init);
    if (!g_syms.get_file_desc) {
        if (size_out)
            *size_out = 0;
        return bad;
    }
    return g_syms.get_file_desc(lid, size_out, reserved, mode);
}

int NVM_CloseFileDesc(long fd, long token)
{
    pthread_once(&g_once, nvram_shim_init);
    if (!g_syms.close_file_desc)
        return 0;                       /* nonzero means success here, so 0 == failure */
    return g_syms.close_file_desc(fd, token);
}

int NVM_RestoreFromBinRegion_OneFile(int variant, const char *path)
{
    pthread_once(&g_once, nvram_shim_init);
    if (!g_syms.restore_from_bin_region_one_file)
        return -1;
    return g_syms.restore_from_bin_region_one_file(variant, path);
}

int mtk_sar_table_id_get(int *out_value)
{
    pthread_once(&g_once, nvram_shim_init);
    if (!g_syms.sar_table_id_get) {
        if (out_value)
            *out_value = 0;
        return -1;
    }
    return g_syms.sar_table_id_get(out_value);
}
