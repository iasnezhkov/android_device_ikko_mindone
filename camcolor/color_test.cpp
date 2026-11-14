/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "color.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        fprintf(stderr, "FAIL line %d: %s\n", line, what);
        ++g_failures;
    }
}

#define CHECK(x) check((x), #x, __LINE__)

uint32_t g_seed = 12345;

uint32_t next_random() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed;
}

uint32_t pack(int32_t low, int32_t high, unsigned bits_low, unsigned bits_high) {
    const uint32_t lo = static_cast<uint32_t>(low) & ((1u << bits_low) - 1u);
    const uint32_t hi = static_cast<uint32_t>(high) & ((1u << bits_high) - 1u);
    return lo | (hi << 16);
}

int32_t vendor_round(double value, int32_t limit) {
    const int32_t rounded = static_cast<int32_t>(value + (value > 0.0 ? 0.5 : -0.5));
    if (rounded > limit) {
        return limit;
    }
    return rounded < -limit ? -limit : rounded;
}

void vendor_hsbc(double hue, double saturation, double brightness, double contrast, uint32_t* regs) {
    const double angle = hue * 3.1415926 / 180.0;
    const double sc = saturation * cos(angle);
    const double ss = saturation * sin(angle);
    const int32_t y0 = vendor_round(153.0 * contrast, 1023);
    const int32_t y1 = vendor_round(301.0 * contrast, 1023);
    const int32_t y2 = vendor_round(58.0 * contrast, 1023);
    const int32_t yo = vendor_round(brightness - 512.0 * (contrast - 1.0), 1023);
    const int32_t cb0 = vendor_round(sc * -86.0 - ss * 256.0, 1023);
    const int32_t cb1 = vendor_round(sc * -170.0 + ss * 214.0, 1023);
    const int32_t cb2 = vendor_round(sc * 256.0 + ss * 42.0, 1023);
    const int32_t cr0 = vendor_round(ss * -86.0 + sc * 256.0, 1023);
    const int32_t cr1 = vendor_round(ss * -170.0 - sc * 214.0, 1023);
    const int32_t cr2 = vendor_round(ss * 256.0 - sc * 42.0, 1023);
    regs[0] = pack(y0, y1, 11, 11);
    regs[1] = pack(y2, yo, 11, 11);
    regs[2] = pack(cb0, cb1, 11, 11);
    regs[3] = pack(cb2, 0, 11, 10);
    regs[4] = pack(cr0, cr1, 11, 11);
    regs[5] = pack(cr2, 0, 11, 10);
}

void test_defaults() {
    camcolor::Config config;
    camcolor::config_default(&config);
    CHECK(camcolor::ccm_is_identity(config.ccm));
    CHECK(camcolor::ggm_is_identity(config.ggm));
    CHECK(camcolor::hsbc_is_identity(config.hsbc));
    CHECK(config.ccm.mask == 0x08);
    CHECK(config.ggm.mask == 0x08);
    CHECK(!config.log);
    const char empty[] = "  \n\n   \n";
    char error[128];
    CHECK(camcolor::config_parse(empty, strlen(empty), &config, error, sizeof(error)));
    CHECK(camcolor::ccm_is_identity(config.ccm));
}

void test_parse() {
    const char text[] =
            "log = 1\n"
            "ccm.mask = 0x18\n"
            "ccm.matrix = 1.1 0 0, 0 1 0, 0 0 0.9\n"
            "ccm.saturation = 1.2\n"
            "ggm.mask = 0x08\n"
            "ggm.contrast = 0.2\n"
            "ggm.gamma = 1.1\n"
            "ggm.curve = 0:0 0.5:0.55 1:1\n"
            "ggm.black = 0.01\n"
            "ggm.white = 0.99\n"
            "ggm.gain = 1 1 0.9\n"
            "hsbc.saturation = 1.1   \n"
            "hsbc.hue = -5 # trailing note\n"
            "hsbc.brightness = 16\n"
            "hsbc.contrast = 1.05\r\n"
            "hsbc.cb_offset = -3\n"
            "hsbc.cr_offset = 4";
    camcolor::Config config;
    char error[128] = {};
    CHECK(camcolor::config_parse(text, strlen(text), &config, error, sizeof(error)));
    CHECK(config.log);
    CHECK(config.ccm.mask == 0x18);
    CHECK(config.ccm.matrix[0] == 1.1 && config.ccm.matrix[8] == 0.9 && config.ccm.matrix[4] == 1.0);
    CHECK(config.ccm.saturation == 1.2);
    CHECK(config.ggm.gamma[0] == 1.1 && config.ggm.gamma[2] == 1.1);
    CHECK(config.ggm.curve_points == 3 && config.ggm.curve_y[1] == 0.55);
    CHECK(config.ggm.gain[2] == 0.9 && config.ggm.gain[0] == 1.0);
    CHECK(config.hsbc.hue == -5.0 && config.hsbc.contrast == 1.05);
    CHECK(config.hsbc.cr_offset == 4.0);

    const char* bad[] = {
            "unknown.key = 1\n",
            "ccm.matrix = 1 0 0 0 1 0 0 0\n",
            "ccm.matrix = 1 0 0 0 1 0 0 0 9\n",
            "ccm.mask = 0x40\n",
            "ggm.gamma = 1 2\n",
            "ggm.curve = 0:0 0.6:0.5 0.5:0.6 1:1\n",
            "ggm.curve = 0.1:0 1:1\n",
            "ggm.contrast = 1\n",
            "hsbc.hue = abc\n",
            "hsbc.brightness = nan\n",
            "ggm.black = 0.5\nggm.white = 0.5\n",
            "no equals sign\n",
    };
    for (const char* entry : bad) {
        camcolor::Config previous;
        camcolor::config_default(&previous);
        previous.log = true;
        CHECK(!camcolor::config_parse(entry, strlen(entry), &previous, error, sizeof(error)));
        CHECK(previous.log);
    }
}

void test_ccm_roundtrip() {
    for (int round = 0; round < 1000; ++round) {
        uint32_t regs[camcolor::kCcmWords];
        for (uint32_t& reg : regs) {
            reg = next_random();
        }
        uint32_t copy[camcolor::kCcmWords];
        memcpy(copy, regs, sizeof(regs));
        camcolor::Config config;
        camcolor::config_default(&config);
        camcolor::ccm_apply(config.ccm, regs);
        CHECK(memcmp(copy, regs, sizeof(regs)) == 0);
    }
}

void test_ccm_gain() {
    const int32_t stock[9] = {600, -60, -28, -80, 680, -88, -10, -150, 672};
    uint32_t regs[camcolor::kCcmWords] = {
            pack(stock[0], stock[1], 13, 13) | 0xe000e000u, pack(stock[2], 0, 13, 13) | 0x00a00000u,
            pack(stock[3], stock[4], 13, 13), pack(stock[5], 0, 13, 13),
            pack(stock[6], stock[7], 13, 13), pack(stock[8], 0, 13, 13),
    };
    int32_t decoded[9];
    camcolor::ccm_decode(regs, decoded);
    CHECK(memcmp(decoded, stock, sizeof(stock)) == 0);
    camcolor::Config config;
    camcolor::config_default(&config);
    config.ccm.matrix[0] = 1.1;
    camcolor::ccm_apply(config.ccm, regs);
    camcolor::ccm_decode(regs, decoded);
    CHECK(decoded[0] == 660 && decoded[1] == -66 && decoded[2] == -31);
    CHECK(memcmp(decoded + 3, stock + 3, 6 * sizeof(int32_t)) == 0);
    CHECK((regs[0] & 0xe000e000u) == 0xe000e000u);
    CHECK((regs[1] & 0xffffe000u) == 0x00a00000u);

    config.ccm.matrix[0] = 0.0;
    config.ccm.matrix[2] = 1.0;
    config.ccm.matrix[6] = 1.0;
    config.ccm.matrix[8] = 0.0;
    uint32_t swapped[camcolor::kCcmWords] = {
            pack(stock[0], stock[1], 13, 13), pack(stock[2], 0, 13, 13),
            pack(stock[3], stock[4], 13, 13), pack(stock[5], 0, 13, 13),
            pack(stock[6], stock[7], 13, 13), pack(stock[8], 0, 13, 13),
    };
    camcolor::ccm_apply(config.ccm, swapped);
    camcolor::ccm_decode(swapped, decoded);
    CHECK(decoded[0] == stock[6] && decoded[1] == stock[7] && decoded[2] == stock[8]);
    CHECK(decoded[6] == stock[0] && decoded[7] == stock[1] && decoded[8] == stock[2]);

    camcolor::config_default(&config);
    config.ccm.saturation = 0.0;
    uint32_t grey[camcolor::kCcmWords] = {
            pack(stock[0], stock[1], 13, 13), pack(stock[2], 0, 13, 13),
            pack(stock[3], stock[4], 13, 13), pack(stock[5], 0, 13, 13),
            pack(stock[6], stock[7], 13, 13), pack(stock[8], 0, 13, 13),
    };
    camcolor::ccm_apply(config.ccm, grey);
    camcolor::ccm_decode(grey, decoded);
    for (int column = 0; column < 3; ++column) {
        CHECK(decoded[column] == decoded[3 + column] && decoded[column] == decoded[6 + column]);
    }

    camcolor::config_default(&config);
    config.ccm.has_override = true;
    const double override_matrix[9] = {1.5, -0.3, -0.2, -0.2, 1.4, -0.2, 0.0, -0.5, 1.5};
    memcpy(config.ccm.override_matrix, override_matrix, sizeof(override_matrix));
    camcolor::ccm_apply(config.ccm, grey);
    camcolor::ccm_decode(grey, decoded);
    CHECK(decoded[0] == 768 && decoded[1] == -154 && decoded[4] == 717 && decoded[7] == -256);

    config.ccm.has_override = false;
    config.ccm.matrix[0] = 8.0;
    camcolor::ccm_apply(config.ccm, grey);
    camcolor::ccm_decode(grey, decoded);
    CHECK(decoded[0] == 4095);
}

void test_hsbc_against_vendor() {
    uint32_t middle[camcolor::kG2cxWords];
    vendor_hsbc(0.0, 1.0, 0.0, 1.0, middle);
    int32_t fields[12];
    camcolor::g2cx_decode(middle, fields);
    const int32_t expected[12] = {153, 301, 58, 0, -86, -170, 256, 0, 256, -214, -42, 0};
    CHECK(memcmp(fields, expected, sizeof(expected)) == 0);

    const double levels[3][4] = {
            {-40.0, 0.6875, -64.0, 0.84375},
            {40.0, 1.3125, 64.0, 1.15625},
            {40.0, 0.6875, -64.0, 1.15625},
    };
    for (const auto& level : levels) {
        uint32_t ours[camcolor::kG2cxWords];
        memcpy(ours, middle, sizeof(ours));
        camcolor::HsbcParams params = {level[1], level[0], level[2], level[3], 0.0, 0.0};
        camcolor::hsbc_apply(params, ours);
        uint32_t vendor[camcolor::kG2cxWords];
        vendor_hsbc(level[0], level[1], level[2], level[3], vendor);
        int32_t a[12];
        int32_t b[12];
        camcolor::g2cx_decode(ours, a);
        camcolor::g2cx_decode(vendor, b);
        for (int i = 0; i < 12; ++i) {
            if (abs(a[i] - b[i]) > 1) {
                fprintf(stderr, "field %d ours %d vendor %d\n", i, a[i], b[i]);
            }
            CHECK(abs(a[i] - b[i]) <= 1);
        }
    }

    for (int round = 0; round < 1000; ++round) {
        uint32_t regs[camcolor::kG2cxWords];
        for (uint32_t& reg : regs) {
            reg = next_random();
        }
        int32_t before[12];
        camcolor::g2cx_decode(regs, before);
        bool in_limits = true;
        for (int i = 0; i < 12; ++i) {
            const int32_t limit = (i == 7 || i == 11) ? 511 : 1023;
            in_limits = in_limits && before[i] >= -limit && before[i] <= limit;
        }
        if (!in_limits) {
            continue;
        }
        uint32_t copy[camcolor::kG2cxWords];
        memcpy(copy, regs, sizeof(regs));
        camcolor::HsbcParams identity = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
        camcolor::hsbc_apply(identity, regs);
        CHECK(memcmp(copy, regs, sizeof(regs)) == 0);
    }

    uint32_t offsets[camcolor::kG2cxWords];
    memcpy(offsets, middle, sizeof(offsets));
    offsets[1] |= 0xf800f800u;
    offsets[3] |= 0xfc00f800u;
    camcolor::HsbcParams shift = {0.0, 0.0, 600.0, 1.0, -600.0, 7.0};
    camcolor::hsbc_apply(shift, offsets);
    camcolor::g2cx_decode(offsets, fields);
    CHECK(fields[3] == 600 && fields[7] == -511 && fields[11] == 7);
    CHECK(fields[4] == 0 && fields[5] == 0 && fields[10] == 0);
    CHECK((offsets[1] & 0xf800f800u) == 0xf800f800u);
    CHECK((offsets[3] & 0xfc00f800u) == 0xfc00f800u);
}

void test_ggm() {
    uint32_t regs[camcolor::kGgmWords];
    for (size_t i = 0; i < camcolor::kGgmWords; ++i) {
        const uint32_t v = static_cast<uint32_t>(i * 1023 / (camcolor::kGgmWords - 1));
        regs[i] = v | (v << 10) | (v << 20) | 0xc0000000u;
    }
    uint32_t copy[camcolor::kGgmWords];
    memcpy(copy, regs, sizeof(regs));
    camcolor::Config config;
    camcolor::config_default(&config);
    camcolor::ggm_apply(config.ggm, regs);
    CHECK(memcmp(copy, regs, sizeof(regs)) == 0);

    config.ggm.gain[2] = 0.5;
    camcolor::ggm_apply(config.ggm, regs);
    for (size_t i = 0; i < camcolor::kGgmWords; ++i) {
        const uint32_t r = regs[i] & 0x3ff;
        const uint32_t g = (regs[i] >> 10) & 0x3ff;
        const uint32_t b = (regs[i] >> 20) & 0x3ff;
        CHECK(r == (copy[i] & 0x3ff) && g == r);
        CHECK(static_cast<long>(b) == lround(r * 0.5));
        CHECK((regs[i] & 0xc0000000u) == 0xc0000000u);
    }

    camcolor::config_default(&config);
    config.ggm.contrast = 0.5;
    config.ggm.gamma[0] = config.ggm.gamma[1] = config.ggm.gamma[2] = 1.2;
    config.ggm.black = 0.02;
    config.ggm.white = 0.98;
    memcpy(regs, copy, sizeof(regs));
    camcolor::ggm_apply(config.ggm, regs);
    uint32_t previous = 0;
    for (size_t i = 0; i < camcolor::kGgmWords; ++i) {
        const uint32_t g = (regs[i] >> 10) & 0x3ff;
        CHECK(g >= previous);
        previous = g;
    }
    CHECK(((regs[0] >> 10) & 0x3ff) == 20);
    CHECK(((regs[camcolor::kGgmWords - 1] >> 10) & 0x3ff) == 1003);

    camcolor::config_default(&config);
    config.ggm.curve_points = 3;
    config.ggm.curve_x[0] = 0.0;
    config.ggm.curve_y[0] = 0.0;
    config.ggm.curve_x[1] = 0.5;
    config.ggm.curve_y[1] = 0.75;
    config.ggm.curve_x[2] = 1.0;
    config.ggm.curve_y[2] = 1.0;
    uint32_t mid[camcolor::kGgmWords] = {};
    mid[0] = 511u | (511u << 10) | (511u << 20);
    camcolor::ggm_apply(config.ggm, mid);
    CHECK((mid[0] & 0x3ff) == 767);
}

}

int main() {
    test_defaults();
    test_parse();
    test_ccm_roundtrip();
    test_ccm_gain();
    test_hsbc_against_vendor();
    test_ggm();
    if (g_failures != 0) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("camcolor_test: all checks passed\n");
    return 0;
}
