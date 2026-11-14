/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace camcolor {

constexpr size_t kCcmWords = 6;
constexpr size_t kGgmWords = 192;
constexpr size_t kG2cxWords = 6;
constexpr size_t kCurveMax = 33;

struct CcmParams {
    uint32_t mask;
    bool has_override;
    double override_matrix[9];
    double matrix[9];
    double saturation;
};

struct GgmParams {
    uint32_t mask;
    double contrast;
    double gamma[3];
    size_t curve_points;
    double curve_x[kCurveMax];
    double curve_y[kCurveMax];
    double black;
    double white;
    double gain[3];
};

struct HsbcParams {
    double saturation;
    double hue;
    double brightness;
    double contrast;
    double cb_offset;
    double cr_offset;
};

struct Config {
    bool log;
    CcmParams ccm;
    GgmParams ggm;
    HsbcParams hsbc;
};

void config_default(Config* config);
bool config_parse(const char* text, size_t length, Config* config, char* error, size_t error_size);

bool ccm_is_identity(const CcmParams& params);
bool ggm_is_identity(const GgmParams& params);
bool hsbc_is_identity(const HsbcParams& params);

void ccm_decode(const uint32_t* regs, int32_t* coefficients);
void ccm_apply(const CcmParams& params, uint32_t* regs);
void ggm_apply(const GgmParams& params, uint32_t* regs);
void g2cx_decode(const uint32_t* regs, int32_t* fields);
void hsbc_apply(const HsbcParams& params, uint32_t* regs);

}
