/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
// mindone: hw_auth_token_t (69 bytes) <-> AIDL keymaster HardwareAuthToken.
#pragma once
#include <aidl/android/hardware/keymaster/HardwareAuthToken.h>
#include <endian.h>
#include <string.h>
#include <array>
#include <vector>

namespace mindone {

constexpr size_t kHwAuthTokenSize = 69;

// version u8 | challenge u64 LE | user_id u64 LE | authenticator_id u64 LE | authenticator_type u32 BE | timestamp u64 BE | hmac[32]
inline std::array<uint8_t, kHwAuthTokenSize> hatToBytes(const aidl::android::hardware::keymaster::HardwareAuthToken& t) {
    std::array<uint8_t, kHwAuthTokenSize> out{};
    uint64_t v64; uint32_t v32;
    out[0] = 0;
    v64 = static_cast<uint64_t>(t.challenge);        memcpy(out.data() + 1, &v64, 8);
    v64 = static_cast<uint64_t>(t.userId);           memcpy(out.data() + 9, &v64, 8);
    v64 = static_cast<uint64_t>(t.authenticatorId);  memcpy(out.data() + 17, &v64, 8);
    v32 = htobe32(static_cast<uint32_t>(t.authenticatorType)); memcpy(out.data() + 25, &v32, 4);
    v64 = htobe64(static_cast<uint64_t>(t.timestamp.milliSeconds)); memcpy(out.data() + 29, &v64, 8);
    size_t n = t.mac.size() < 32 ? t.mac.size() : 32;
    memcpy(out.data() + 37, t.mac.data(), n);
    return out;
}

inline bool bytesToHat(const uint8_t* p, size_t len, aidl::android::hardware::keymaster::HardwareAuthToken* t) {
    if (len < kHwAuthTokenSize) return false;
    uint64_t v64; uint32_t v32;
    memcpy(&v64, p + 1, 8);  t->challenge = static_cast<int64_t>(v64);
    memcpy(&v64, p + 9, 8);  t->userId = static_cast<int64_t>(v64);
    memcpy(&v64, p + 17, 8); t->authenticatorId = static_cast<int64_t>(v64);
    memcpy(&v32, p + 25, 4); t->authenticatorType = static_cast<aidl::android::hardware::keymaster::HardwareAuthenticatorType>(be32toh(v32));
    memcpy(&v64, p + 29, 8); t->timestamp.milliSeconds = static_cast<int64_t>(be64toh(v64));
    t->mac.assign(p + 37, p + 37 + 32);
    return true;
}

}  // namespace mindone
