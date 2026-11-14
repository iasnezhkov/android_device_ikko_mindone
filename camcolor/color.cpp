/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "color.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace camcolor {
namespace {

constexpr double kPi = 3.14159265358979323846;

constexpr unsigned kCcmBits = 13;
constexpr double kCcmUnit = 512.0;
constexpr int32_t kCcmMin = -4096;
constexpr int32_t kCcmMax = 4095;
constexpr double kCcmLimit = 8.0;
constexpr uint8_t kCcmReg[9] = {0, 0, 1, 2, 2, 3, 4, 4, 5};
constexpr uint8_t kCcmShift[9] = {0, 16, 0, 0, 16, 0, 0, 16, 0};
constexpr double kLuma709[3] = {0.2126, 0.7152, 0.0722};

constexpr unsigned kGgmBits = 10;
constexpr uint32_t kGgmMask = 0x3ff;
constexpr int32_t kGgmMax = 1023;

constexpr double kG2cxPivot = 512.0;

struct Field {
    uint8_t reg;
    uint8_t shift;
    uint8_t bits;
    int32_t limit;
};

constexpr Field kG2cxFields[12] = {
        {0, 0, 11, 1023}, {0, 16, 11, 1023}, {1, 0, 11, 1023}, {1, 16, 11, 1023},
        {2, 0, 11, 1023}, {2, 16, 11, 1023}, {3, 0, 11, 1023}, {3, 16, 10, 511},
        {4, 0, 11, 1023}, {4, 16, 11, 1023}, {5, 0, 11, 1023}, {5, 16, 10, 511},
};

constexpr size_t kMaxLine = 1024;
constexpr uint32_t kMaxMask = 0x3f;

int32_t sign_extend(uint32_t value, unsigned bits) {
    const uint32_t mask = (1u << bits) - 1u;
    const uint32_t sign = 1u << (bits - 1u);
    return static_cast<int32_t>((value & mask) ^ sign) - static_cast<int32_t>(sign);
}

uint32_t put_field(uint32_t reg, unsigned shift, unsigned bits, int32_t value) {
    const uint32_t mask = ((1u << bits) - 1u) << shift;
    return (reg & ~mask) | ((static_cast<uint32_t>(value) << shift) & mask);
}

int32_t round_clamp(double value, int32_t low, int32_t high) {
    if (!(value > low)) {
        return low;
    }
    if (!(value < high)) {
        return high;
    }
    return static_cast<int32_t>(lround(value));
}

double clamp01(double value) {
    if (!(value > 0.0)) {
        return 0.0;
    }
    return value < 1.0 ? value : 1.0;
}

void multiply(const double* a, const double* b, double* out) {
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 3; ++column) {
            double sum = 0.0;
            for (size_t k = 0; k < 3; ++k) {
                sum += a[row * 3 + k] * b[k * 3 + column];
            }
            out[row * 3 + column] = sum;
        }
    }
}

bool is_identity_matrix(const double* matrix) {
    for (size_t i = 0; i < 9; ++i) {
        if (matrix[i] != (i % 4 == 0 ? 1.0 : 0.0)) {
            return false;
        }
    }
    return true;
}

double interpolate(const GgmParams& params, double value) {
    for (size_t i = 1; i < params.curve_points; ++i) {
        if (value <= params.curve_x[i]) {
            const double x0 = params.curve_x[i - 1];
            const double y0 = params.curve_y[i - 1];
            const double t = (value - x0) / (params.curve_x[i] - x0);
            return y0 + t * (params.curve_y[i] - y0);
        }
    }
    return params.curve_y[params.curve_points - 1];
}

double tone(const GgmParams& params, size_t channel, double value) {
    if (params.contrast != 0.0) {
        value -= params.contrast * sin(2.0 * kPi * value) / (2.0 * kPi);
    }
    if (params.gamma[channel] != 1.0) {
        value = pow(clamp01(value), 1.0 / params.gamma[channel]);
    }
    if (params.curve_points > 0) {
        value = interpolate(params, clamp01(value));
    }
    value = params.black + (params.white - params.black) * value;
    return value * params.gain[channel];
}

struct Parser {
    size_t line;
    char* error;
    size_t error_size;

    bool fail(const char* what, const char* key) {
        snprintf(error, error_size, "line %zu: %s: %s", line, key, what);
        return false;
    }
};

const char* skip_separators(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == ',') {
        ++p;
    }
    return p;
}

bool parse_number(const char** cursor, double* out) {
    char* end = nullptr;
    const double value = strtod(*cursor, &end);
    if (end == *cursor || !isfinite(value)) {
        return false;
    }
    *out = value;
    *cursor = end;
    return true;
}

bool parse_list(const char* value, double* out, size_t max_count, size_t* count) {
    *count = 0;
    const char* p = skip_separators(value);
    while (*p != '\0') {
        if (*count == max_count || !parse_number(&p, &out[*count])) {
            return false;
        }
        ++*count;
        p = skip_separators(p);
    }
    return true;
}

bool parse_pairs(const char* value, double* x, double* y, size_t max_count, size_t* count) {
    *count = 0;
    const char* p = skip_separators(value);
    while (*p != '\0') {
        if (*count == max_count || !parse_number(&p, &x[*count]) || *p != ':') {
            return false;
        }
        ++p;
        if (!parse_number(&p, &y[*count])) {
            return false;
        }
        ++*count;
        p = skip_separators(p);
    }
    return true;
}

bool in_range(const double* values, size_t count, double low, double high) {
    for (size_t i = 0; i < count; ++i) {
        if (values[i] < low || values[i] > high) {
            return false;
        }
    }
    return true;
}

bool parse_scalar(Parser* parser, const char* key, const char* value, double low, double high,
                  double* out) {
    size_t count = 0;
    if (!parse_list(value, out, 1, &count) || count != 1) {
        return parser->fail("expected one number", key);
    }
    if (!in_range(out, 1, low, high)) {
        return parser->fail("out of range", key);
    }
    return true;
}

bool parse_triple(Parser* parser, const char* key, const char* value, double low, double high,
                  double* out) {
    size_t count = 0;
    if (!parse_list(value, out, 3, &count) || (count != 1 && count != 3)) {
        return parser->fail("expected one or three numbers", key);
    }
    if (count == 1) {
        out[1] = out[0];
        out[2] = out[0];
    }
    if (!in_range(out, 3, low, high)) {
        return parser->fail("out of range", key);
    }
    return true;
}

bool parse_matrix(Parser* parser, const char* key, const char* value, double* out) {
    size_t count = 0;
    if (!parse_list(value, out, 9, &count) || count != 9) {
        return parser->fail("expected nine numbers", key);
    }
    if (!in_range(out, 9, -kCcmLimit, kCcmLimit)) {
        return parser->fail("out of range", key);
    }
    return true;
}

bool parse_mask(Parser* parser, const char* key, const char* value, uint32_t* out) {
    char* end = nullptr;
    const unsigned long mask = strtoul(value, &end, 0);
    if (end == value || *skip_separators(end) != '\0' || mask > kMaxMask) {
        return parser->fail("expected a mask 0..0x3f", key);
    }
    *out = static_cast<uint32_t>(mask);
    return true;
}

bool parse_curve(Parser* parser, const char* key, const char* value, GgmParams* ggm) {
    size_t count = 0;
    if (!parse_pairs(value, ggm->curve_x, ggm->curve_y, kCurveMax, &count) || count < 2) {
        return parser->fail("expected 2..33 points x:y", key);
    }
    if (ggm->curve_x[0] != 0.0 || ggm->curve_x[count - 1] != 1.0 ||
        !in_range(ggm->curve_y, count, 0.0, 1.0)) {
        return parser->fail("points must span x 0..1 with y in 0..1", key);
    }
    for (size_t i = 1; i < count; ++i) {
        if (!(ggm->curve_x[i] > ggm->curve_x[i - 1]) || ggm->curve_y[i] < ggm->curve_y[i - 1]) {
            return parser->fail("x must increase and y must not decrease", key);
        }
    }
    ggm->curve_points = count;
    return true;
}

bool parse_entry(Parser* parser, const char* key, const char* value, Config* config) {
    double flag = 0.0;
    if (strcmp(key, "log") == 0) {
        if (!parse_scalar(parser, key, value, 0.0, 1.0, &flag)) {
            return false;
        }
        config->log = flag != 0.0;
        return true;
    }
    CcmParams& ccm = config->ccm;
    GgmParams& ggm = config->ggm;
    HsbcParams& hsbc = config->hsbc;
    if (strcmp(key, "ccm.mask") == 0) {
        return parse_mask(parser, key, value, &ccm.mask);
    }
    if (strcmp(key, "ccm.override") == 0) {
        ccm.has_override = true;
        return parse_matrix(parser, key, value, ccm.override_matrix);
    }
    if (strcmp(key, "ccm.matrix") == 0) {
        return parse_matrix(parser, key, value, ccm.matrix);
    }
    if (strcmp(key, "ccm.saturation") == 0) {
        return parse_scalar(parser, key, value, 0.0, 4.0, &ccm.saturation);
    }
    if (strcmp(key, "ggm.mask") == 0) {
        return parse_mask(parser, key, value, &ggm.mask);
    }
    if (strcmp(key, "ggm.contrast") == 0) {
        return parse_scalar(parser, key, value, -0.95, 0.95, &ggm.contrast);
    }
    if (strcmp(key, "ggm.gamma") == 0) {
        return parse_triple(parser, key, value, 0.1, 10.0, ggm.gamma);
    }
    if (strcmp(key, "ggm.curve") == 0) {
        return parse_curve(parser, key, value, &ggm);
    }
    if (strcmp(key, "ggm.black") == 0) {
        return parse_scalar(parser, key, value, 0.0, 0.5, &ggm.black);
    }
    if (strcmp(key, "ggm.white") == 0) {
        return parse_scalar(parser, key, value, 0.5, 1.0, &ggm.white);
    }
    if (strcmp(key, "ggm.gain") == 0) {
        return parse_triple(parser, key, value, 0.01, 4.0, ggm.gain);
    }
    if (strcmp(key, "hsbc.saturation") == 0) {
        return parse_scalar(parser, key, value, 0.0, 4.0, &hsbc.saturation);
    }
    if (strcmp(key, "hsbc.hue") == 0) {
        return parse_scalar(parser, key, value, -180.0, 180.0, &hsbc.hue);
    }
    if (strcmp(key, "hsbc.brightness") == 0) {
        return parse_scalar(parser, key, value, -1023.0, 1023.0, &hsbc.brightness);
    }
    if (strcmp(key, "hsbc.contrast") == 0) {
        return parse_scalar(parser, key, value, 0.0, 4.0, &hsbc.contrast);
    }
    if (strcmp(key, "hsbc.cb_offset") == 0) {
        return parse_scalar(parser, key, value, -511.0, 511.0, &hsbc.cb_offset);
    }
    if (strcmp(key, "hsbc.cr_offset") == 0) {
        return parse_scalar(parser, key, value, -511.0, 511.0, &hsbc.cr_offset);
    }
    return parser->fail("unknown key", key);
}

char* trim(char* begin, char* end) {
    while (begin < end && (*begin == ' ' || *begin == '\t' || *begin == '\r')) {
        ++begin;
    }
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        --end;
    }
    *end = '\0';
    return begin;
}

}

void config_default(Config* config) {
    memset(config, 0, sizeof(*config));
    config->ccm.mask = 1u << 3;
    config->ccm.saturation = 1.0;
    for (size_t i = 0; i < 9; ++i) {
        config->ccm.matrix[i] = i % 4 == 0 ? 1.0 : 0.0;
    }
    config->ggm.mask = 1u << 3;
    config->ggm.white = 1.0;
    for (size_t c = 0; c < 3; ++c) {
        config->ggm.gamma[c] = 1.0;
        config->ggm.gain[c] = 1.0;
    }
    config->hsbc.saturation = 1.0;
    config->hsbc.contrast = 1.0;
}

bool config_parse(const char* text, size_t length, Config* config, char* error, size_t error_size) {
    Config parsed;
    config_default(&parsed);
    Parser parser = {0, error, error_size};
    size_t position = 0;
    while (position < length) {
        ++parser.line;
        size_t end = position;
        while (end < length && text[end] != '\n') {
            ++end;
        }
        if (end - position >= kMaxLine) {
            return parser.fail("line too long", "-");
        }
        if (memchr(text + position, '\0', end - position) != nullptr) {
            return parser.fail("NUL byte", "-");
        }
        char line[kMaxLine];
        memcpy(line, text + position, end - position);
        line[end - position] = '\0';
        position = end + 1;
        char* hash = strchr(line, '#');
        char* stop = hash != nullptr ? hash : line + strlen(line);
        char* content = trim(line, stop);
        if (*content == '\0') {
            continue;
        }
        char* equals = strchr(content, '=');
        if (equals == nullptr) {
            return parser.fail("expected key = value", content);
        }
        char* key = trim(content, equals);
        char* value = trim(equals + 1, equals + 1 + strlen(equals + 1));
        if (!parse_entry(&parser, key, value, &parsed)) {
            return false;
        }
    }
    if (!(parsed.ggm.white > parsed.ggm.black)) {
        parser.line = 0;
        return parser.fail("white must exceed black", "ggm.white");
    }
    *config = parsed;
    return true;
}

bool ccm_is_identity(const CcmParams& params) {
    return !params.has_override && params.saturation == 1.0 && is_identity_matrix(params.matrix);
}

bool ggm_is_identity(const GgmParams& params) {
    if (params.contrast != 0.0 || params.curve_points != 0 || params.black != 0.0 ||
        params.white != 1.0) {
        return false;
    }
    for (size_t c = 0; c < 3; ++c) {
        if (params.gamma[c] != 1.0 || params.gain[c] != 1.0) {
            return false;
        }
    }
    return true;
}

bool hsbc_is_identity(const HsbcParams& params) {
    return params.saturation == 1.0 && params.hue == 0.0 && params.brightness == 0.0 &&
           params.contrast == 1.0 && params.cb_offset == 0.0 && params.cr_offset == 0.0;
}

void ccm_decode(const uint32_t* regs, int32_t* coefficients) {
    for (size_t i = 0; i < 9; ++i) {
        coefficients[i] = sign_extend(regs[kCcmReg[i]] >> kCcmShift[i], kCcmBits);
    }
}

void ccm_apply(const CcmParams& params, uint32_t* regs) {
    double base[9];
    if (params.has_override) {
        memcpy(base, params.override_matrix, sizeof(base));
    } else {
        int32_t coefficients[9];
        ccm_decode(regs, coefficients);
        for (size_t i = 0; i < 9; ++i) {
            base[i] = coefficients[i] / kCcmUnit;
        }
    }
    const double s = params.saturation;
    double saturation[9];
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 3; ++column) {
            saturation[row * 3 + column] = (1.0 - s) * kLuma709[column] + (row == column ? s : 0.0);
        }
    }
    double partial[9];
    double result[9];
    multiply(saturation, base, partial);
    multiply(params.matrix, partial, result);
    for (size_t i = 0; i < 9; ++i) {
        const int32_t value = round_clamp(result[i] * kCcmUnit, kCcmMin, kCcmMax);
        regs[kCcmReg[i]] = put_field(regs[kCcmReg[i]], kCcmShift[i], kCcmBits, value);
    }
}

void ggm_apply(const GgmParams& params, uint32_t* regs) {
    for (size_t i = 0; i < kGgmWords; ++i) {
        uint32_t reg = regs[i];
        for (size_t channel = 0; channel < 3; ++channel) {
            const unsigned shift = static_cast<unsigned>(channel) * kGgmBits;
            const double value = static_cast<double>((reg >> shift) & kGgmMask) / kGgmMax;
            const int32_t out = round_clamp(tone(params, channel, value) * kGgmMax, 0, kGgmMax);
            reg = put_field(reg, shift, kGgmBits, out);
        }
        regs[i] = reg;
    }
}

void g2cx_decode(const uint32_t* regs, int32_t* fields) {
    for (size_t i = 0; i < 12; ++i) {
        const Field& field = kG2cxFields[i];
        fields[i] = sign_extend(regs[field.reg] >> field.shift, field.bits);
    }
}

void hsbc_apply(const HsbcParams& params, uint32_t* regs) {
    int32_t fields[12];
    g2cx_decode(regs, fields);
    double value[12];
    for (size_t i = 0; i < 12; ++i) {
        value[i] = fields[i];
    }
    const double contrast = params.contrast;
    for (size_t k = 0; k < 3; ++k) {
        value[k] *= contrast;
    }
    value[3] = contrast * value[3] + kG2cxPivot * (1.0 - contrast) + params.brightness;
    double cosine = 1.0;
    double sine = 0.0;
    if (params.hue != 0.0) {
        cosine = cos(params.hue * kPi / 180.0);
        sine = sin(params.hue * kPi / 180.0);
    }
    const double s = params.saturation;
    for (size_t k = 0; k < 4; ++k) {
        const double cb = value[4 + k];
        const double cr = value[8 + k];
        value[4 + k] = s * (cosine * cb - sine * cr);
        value[8 + k] = s * (sine * cb + cosine * cr);
    }
    value[7] += params.cb_offset;
    value[11] += params.cr_offset;
    for (size_t i = 0; i < 12; ++i) {
        const Field& field = kG2cxFields[i];
        const int32_t out = round_clamp(value[i], -field.limit, field.limit);
        regs[field.reg] = put_field(regs[field.reg], field.shift, field.bits, out);
    }
}

}
