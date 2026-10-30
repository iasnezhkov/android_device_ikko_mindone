/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <algorithm>
#include <string>
#include <vector>

inline std::string resourceHint(int id, int value) {
    const char* prefix = nullptr;
    std::vector<int> values;
    switch (id) {
        case 0x00400000:
            prefix = "MTK_CPU_MIN_0_";
            values = {2000000, 1700000, 1500000, 1200000, 1000000, 500000};
            if (value == 4000000) value = 2000000;
            break;
        case 0x00400100:
            prefix = "MTK_CPU_MIN_1_";
            values = {2200000, 1900000, 1600000, 1300000, 1000000, 725000};
            if (value == 4000000) value = 2200000;
            break;
        case 0x00404000:
            prefix = "MTK_CPU_MAX_0_";
            values = {1000000, 1200000, 2000000};
            break;
        case 0x00404100:
            prefix = "MTK_CPU_MAX_1_";
            values = {1000000, 1300000, 2200000};
            break;
        case 0x00c00000:
            prefix = "MTK_GPU_MIN_";
            values = {0, 17, 44};
            break;
        default:
            return {};
    }
    return std::find(values.begin(), values.end(), value) == values.end()
                   ? std::string() : std::string(prefix) + std::to_string(value);
}

inline std::string scenarioHint(int hint) {
    switch (hint) {
        case 11:
        case 21:
        case 22:
        case 23:
        case 30:
        case 31:
        case 34:
        case 81:
            return "MTK_LAUNCH";
        case 4:
        case 74:
            return "MTK_CAMERA_LAUNCH";
        case 25:
            return "MTK_TOUCH";
        case 18:
        case 88:
            return "MTK_CAMERA_LOW";
        case 19:
        case 89:
            return "MTK_CAMERA_MID";
        case 20:
        case 90:
            return "MTK_CAMERA_HIGH";
        default:
            return {};
    }
}
