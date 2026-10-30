/*
 * Copyright (c) 2019 MediaTek Inc.
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <drm.h>

namespace mindone {

enum drm_disp_ccorr_id_t : uint32_t {
    DRM_DISP_CCORR0 = 0,
    DRM_DISP_CCORR1,
    DRM_DISP_CCORR_TOTAL,
};

struct DRM_DISP_CCORR_COEF_T {
    drm_disp_ccorr_id_t hw_id;
    uint32_t coef[3][3];
    uint32_t offset[3];
    int32_t FinalBacklight;
    int32_t silky_bright_flag;
};

struct drm_mtk_ccorr_caps {
    uint32_t ccorr_bit;
    uint32_t ccorr_number;
    uint32_t ccorr_linear;
};

struct mtk_drm_pq_caps_info {
    drm_mtk_ccorr_caps ccorr_caps;
};

constexpr unsigned long kSetCcorr =
        DRM_IOWR(DRM_COMMAND_BASE + 0x20, struct DRM_DISP_CCORR_COEF_T);
constexpr unsigned long kGetPqCaps =
        DRM_IOWR(DRM_COMMAND_BASE + 0x54, struct mtk_drm_pq_caps_info);

static_assert(sizeof(DRM_DISP_CCORR_COEF_T) == 60);
static_assert(offsetof(DRM_DISP_CCORR_COEF_T, coef) == 4);
static_assert(offsetof(DRM_DISP_CCORR_COEF_T, offset) == 40);
static_assert(offsetof(DRM_DISP_CCORR_COEF_T, FinalBacklight) == 52);
static_assert(offsetof(DRM_DISP_CCORR_COEF_T, silky_bright_flag) == 56);
static_assert(sizeof(mtk_drm_pq_caps_info) == 12);
static_assert(sizeof(void*) == 8);

}
