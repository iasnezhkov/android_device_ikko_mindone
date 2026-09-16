/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
// mindone: AIDL IGatekeeper (V1) → HIDL IGatekeeper@1.0 bridge. See BRIDGES-HIDL-AIDL.
#define LOG_TAG "mindone-bridge-gatekeeper"

#include "GatekeeperBridge.h"

#include <android-base/logging.h>
#include <endian.h>
#include <string.h>

namespace mindone {

using ::aidl::android::hardware::gatekeeper::GatekeeperEnrollResponse;
using ::aidl::android::hardware::gatekeeper::GatekeeperVerifyResponse;
using ::aidl::android::hardware::gatekeeper::IGatekeeper;
using ::aidl::android::hardware::security::keymint::HardwareAuthenticatorType;
using ::aidl::android::hardware::security::keymint::HardwareAuthToken;
using ::android::hardware::hidl_vec;
using ::android::hardware::gatekeeper::V1_0::GatekeeperResponse;
using ::android::hardware::gatekeeper::V1_0::GatekeeperStatusCode;

namespace {

// hw_auth_token_t (libhardware/include/hardware/hw_auth_token.h): version u8, challenge u64,
// user_id u64, authenticator_id u64, authenticator_type u32 (network order), timestamp u64
// (network order), hmac[32]. 69 bytes total.
constexpr size_t kHwAuthTokenSize = 69;
// gatekeeper password_handle_t (system/gatekeeper/include/gatekeeper/password_handle.h):
// version u8, user_id u64 (secure user id), flags u64, salt u64, signature[32], hardware_backed bool.
constexpr size_t kHandleUserIdOffset = 1;

int64_t secureUserIdFromHandle(const hidl_vec<uint8_t>& handle) {
    if (handle.size() < kHandleUserIdOffset + sizeof(uint64_t)) return 0;
    uint64_t sid;
    memcpy(&sid, handle.data() + kHandleUserIdOffset, sizeof(sid));
    return static_cast<int64_t>(sid);
}

bool parseHwAuthToken(const hidl_vec<uint8_t>& raw, HardwareAuthToken* tok) {
    if (raw.size() < kHwAuthTokenSize) return false;
    const uint8_t* p = raw.data();
    uint64_t v64;
    uint32_t v32;
    memcpy(&v64, p + 1, 8);  tok->challenge = static_cast<int64_t>(v64);
    memcpy(&v64, p + 9, 8);  tok->userId = static_cast<int64_t>(v64);
    memcpy(&v64, p + 17, 8); tok->authenticatorId = static_cast<int64_t>(v64);
    memcpy(&v32, p + 25, 4); tok->authenticatorType = static_cast<HardwareAuthenticatorType>(be32toh(v32));
    memcpy(&v64, p + 29, 8); tok->timestamp.milliSeconds = static_cast<int64_t>(be64toh(v64));
    tok->mac.assign(p + 37, p + 37 + 32);
    return true;
}

ndk::ScopedAStatus failure(int32_t code) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificError(code));
}

}  // namespace

ndk::ScopedAStatus GatekeeperBridge::enroll(int32_t uid,
                                            const std::vector<uint8_t>& currentPasswordHandle,
                                            const std::vector<uint8_t>& currentPassword,
                                            const std::vector<uint8_t>& desiredPassword,
                                            GatekeeperEnrollResponse* out) {
    auto hidl = conn_->get();
    if (hidl == nullptr) {
        LOG(ERROR) << "enroll: HIDL gatekeeper@1.0 not connected right now";
        return failure(IGatekeeper::ERROR_GENERAL_FAILURE);
    }
    GatekeeperResponse rsp;
    auto ret = hidl->enroll(static_cast<uint32_t>(uid), currentPasswordHandle, currentPassword,
                            desiredPassword, [&](const GatekeeperResponse& r) { rsp = r; });
    if (!ret.isOk()) {
        LOG(ERROR) << "enroll: HIDL transport error: " << ret.description();
        return failure(IGatekeeper::ERROR_GENERAL_FAILURE);
    }
    if (rsp.code == GatekeeperStatusCode::ERROR_RETRY_TIMEOUT) {
        *out = {IGatekeeper::ERROR_RETRY_TIMEOUT, static_cast<int32_t>(rsp.timeout), 0, {}};
        return ndk::ScopedAStatus::ok();
    }
    if (static_cast<int32_t>(rsp.code) < 0) {
        LOG(WARNING) << "enroll: HIDL status " << static_cast<int32_t>(rsp.code);
        return failure(static_cast<int32_t>(rsp.code));
    }
    // rsp.code is STATUS_OK (0) or STATUS_REENROLL (1) here - both are success per
    // GatekeeperStatusCode's own doc comment ("success >= 0; error < 0"), verified 14.09 against
    // the real generated HIDL/AIDL headers (identical enum values on both sides).
    *out = {static_cast<int32_t>(rsp.code), 0, secureUserIdFromHandle(rsp.data),
            std::vector<uint8_t>(rsp.data.begin(), rsp.data.end())};
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus GatekeeperBridge::verify(int32_t uid, int64_t challenge,
                                            const std::vector<uint8_t>& enrolledPasswordHandle,
                                            const std::vector<uint8_t>& providedPassword,
                                            GatekeeperVerifyResponse* out) {
    auto hidl = conn_->get();
    if (hidl == nullptr) {
        LOG(ERROR) << "verify: HIDL gatekeeper@1.0 not connected right now";
        return failure(IGatekeeper::ERROR_GENERAL_FAILURE);
    }
    GatekeeperResponse rsp;
    auto ret = hidl->verify(static_cast<uint32_t>(uid), static_cast<uint64_t>(challenge),
                            enrolledPasswordHandle, providedPassword,
                            [&](const GatekeeperResponse& r) { rsp = r; });
    if (!ret.isOk()) {
        LOG(ERROR) << "verify: HIDL transport error: " << ret.description();
        return failure(IGatekeeper::ERROR_GENERAL_FAILURE);
    }
    if (rsp.code == GatekeeperStatusCode::ERROR_RETRY_TIMEOUT) {
        *out = {IGatekeeper::ERROR_RETRY_TIMEOUT, static_cast<int32_t>(rsp.timeout), {}};
        return ndk::ScopedAStatus::ok();
    }
    if (static_cast<int32_t>(rsp.code) < 0) {
        LOG(WARNING) << "verify: HIDL status " << static_cast<int32_t>(rsp.code);
        return failure(static_cast<int32_t>(rsp.code));
    }
    HardwareAuthToken tok;
    if (!parseHwAuthToken(rsp.data, &tok)) {
        LOG(ERROR) << "verify: auth token too short: " << rsp.data.size();
        return failure(IGatekeeper::ERROR_GENERAL_FAILURE);
    }
    *out = {static_cast<int32_t>(rsp.code), 0, tok};
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus GatekeeperBridge::deleteUser(int32_t uid) {
    auto hidl = conn_->get();
    if (hidl == nullptr) {
        LOG(ERROR) << "deleteUser: HIDL gatekeeper@1.0 not connected right now";
        return failure(IGatekeeper::ERROR_GENERAL_FAILURE);
    }
    GatekeeperResponse rsp;
    auto ret = hidl->deleteUser(static_cast<uint32_t>(uid),
                                [&](const GatekeeperResponse& r) { rsp = r; });
    if (!ret.isOk()) return failure(IGatekeeper::ERROR_GENERAL_FAILURE);
    if (static_cast<int32_t>(rsp.code) < 0) return failure(static_cast<int32_t>(rsp.code));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus GatekeeperBridge::deleteAllUsers() {
    auto hidl = conn_->get();
    if (hidl == nullptr) {
        LOG(ERROR) << "deleteAllUsers: HIDL gatekeeper@1.0 not connected right now";
        return failure(IGatekeeper::ERROR_GENERAL_FAILURE);
    }
    GatekeeperResponse rsp;
    auto ret = hidl->deleteAllUsers([&](const GatekeeperResponse& r) { rsp = r; });
    if (!ret.isOk()) return failure(IGatekeeper::ERROR_GENERAL_FAILURE);
    if (static_cast<int32_t>(rsp.code) < 0) return failure(static_cast<int32_t>(rsp.code));
    return ndk::ScopedAStatus::ok();
}

}  // namespace mindone
