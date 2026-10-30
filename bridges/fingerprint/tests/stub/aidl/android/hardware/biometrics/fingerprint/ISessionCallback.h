/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <atomic>
#include <cstdint>

namespace aidl::android::hardware::biometrics::fingerprint {

enum class Error : int8_t { UNABLE_TO_PROCESS = 2 };

struct ISessionCallback {
    std::atomic<int64_t> timed{0};
    std::atomic<int> permanent{0};
    std::atomic<int> cleared{0};
    std::atomic<int> errors{0};
    void onLockoutTimed(int64_t value) { timed = value; }
    void onLockoutPermanent() { ++permanent; }
    void onLockoutCleared() { ++cleared; }
    template <class Error>
    void onError(Error, int32_t) { ++errors; }
};

}
