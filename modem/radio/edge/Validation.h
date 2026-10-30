/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
namespace mindone::radio::validation {
inline bool nonnegative(int32_t value) { return value >= 0; }
inline bool positive(int32_t value) { return value > 0; }
inline bool tty(int32_t value) { return value >= 0 && value <= 3; }
inline bool digits(const std::string& text, size_t minimum, size_t maximum) {
    return text.size() >= minimum && text.size() <= maximum && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}
inline bool plmn(const std::string& text) { return text.empty() || digits(text, 5, 6); }
inline bool thresholds(const std::vector<int32_t>& values, int32_t hysteresis, int32_t low, int32_t high) {
    if (hysteresis < 0 || values.size() > 65536) return false;
    for (size_t index = 0; index < values.size(); ++index) {
        if (values[index] < low || values[index] > high) return false;
        if (index && (int64_t(values[index]) - values[index - 1] <= hysteresis)) return false;
    }
    return true;
}
inline bool signalCriteria(int32_t ran, int32_t measurement, int32_t time, int32_t hysteresis, const std::vector<int32_t>& values) {
    if (time < 0) return false;
    switch (measurement) {
        case 1: return ran == 1 && thresholds(values, hysteresis, -113, -51);
        case 2: return ran == 2 && thresholds(values, hysteresis, -120, -25);
        case 3: return ran == 3 && thresholds(values, hysteresis, -140, -44);
        case 4: return ran == 3 && thresholds(values, hysteresis, -34, 3);
        case 5: return ran == 3 && thresholds(values, hysteresis, -20, 30);
        case 6: return ran == 6 && thresholds(values, hysteresis, -140, -44);
        case 7: return ran == 6 && thresholds(values, hysteresis, -43, 20);
        case 8: return ran == 6 && thresholds(values, hysteresis, -23, 40);
        case 9: return ran == 2 && thresholds(values, hysteresis, -24, 1);
        default: return false;
    }
}
inline bool ran(int32_t value) { return value >= 1 && value <= 6; }
}
