/*
 * Copyright (C) 2016 The Android Open Source Project
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "mindone-gatekeeper"

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

namespace {

constexpr size_t kHwAuthTokenSize = 69;
constexpr size_t kHandleUserIdOffset = 1;

struct OwnedBuffer {
    uint8_t* data = nullptr;
    uint32_t size = 0;
    ~OwnedBuffer() { delete[] data; }
};

const uint8_t* bytes(const std::vector<uint8_t>& v) {
    return v.empty() ? nullptr : v.data();
}

int64_t secureUserIdFromHandle(const OwnedBuffer& handle) {
    if (handle.data == nullptr || handle.size < kHandleUserIdOffset + sizeof(uint64_t)) return 0;
    uint64_t sid;
    memcpy(&sid, handle.data + kHandleUserIdOffset, sizeof(sid));
    return static_cast<int64_t>(sid);
}

bool parseHwAuthToken(const OwnedBuffer& raw, HardwareAuthToken* tok) {
    if (raw.data == nullptr || raw.size < kHwAuthTokenSize) return false;
    const uint8_t* p = raw.data;
    uint64_t v64;
    uint32_t v32;
    memcpy(&v64, p + 1, 8);
    tok->challenge = static_cast<int64_t>(v64);
    memcpy(&v64, p + 9, 8);
    tok->userId = static_cast<int64_t>(v64);
    memcpy(&v64, p + 17, 8);
    tok->authenticatorId = static_cast<int64_t>(v64);
    memcpy(&v32, p + 25, 4);
    tok->authenticatorType = static_cast<HardwareAuthenticatorType>(be32toh(v32));
    memcpy(&v64, p + 29, 8);
    tok->timestamp.milliSeconds = static_cast<int64_t>(be64toh(v64));
    tok->mac.assign(p + 37, p + 37 + 32);
    return true;
}

ndk::ScopedAStatus failure(int32_t code) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificError(code));
}

ndk::ScopedAStatus deletionResult(const char* op, int ret) {
    if (ret == 0) return ndk::ScopedAStatus::ok();
    LOG(WARNING) << op << ": device returned " << ret;
    return failure(ret > 0 ? IGatekeeper::ERROR_RETRY_TIMEOUT : IGatekeeper::ERROR_GENERAL_FAILURE);
}

}

std::shared_ptr<GatekeeperBridge> GatekeeperBridge::open() {
    const hw_module_t* module = nullptr;
    int ret = hw_get_module_by_class(GATEKEEPER_HARDWARE_MODULE_ID, nullptr, &module);
    if (ret != 0 || module == nullptr) {
        LOG(ERROR) << "hw_get_module_by_class(" << GATEKEEPER_HARDWARE_MODULE_ID << ") failed: " << ret;
        return nullptr;
    }
    gatekeeper_device* device = nullptr;
    ret = gatekeeper_open(module, &device);
    if (ret != 0 || device == nullptr) {
        LOG(ERROR) << "gatekeeper_open(" << module->name << ") failed: " << ret;
        return nullptr;
    }
    LOG(INFO) << "opened " << module->name << " (" << module->author << ")";
    return ndk::SharedRefBase::make<GatekeeperBridge>(device);
}

GatekeeperBridge::~GatekeeperBridge() {
    if (device_ != nullptr) gatekeeper_close(device_);
}

ndk::ScopedAStatus GatekeeperBridge::enroll(int32_t uid,
                                            const std::vector<uint8_t>& currentPasswordHandle,
                                            const std::vector<uint8_t>& currentPassword,
                                            const std::vector<uint8_t>& desiredPassword,
                                            GatekeeperEnrollResponse* out) {
    OwnedBuffer handle;
    int ret = device_->enroll(device_, static_cast<uint32_t>(uid), bytes(currentPasswordHandle),
                              currentPasswordHandle.size(), bytes(currentPassword),
                              currentPassword.size(), bytes(desiredPassword), desiredPassword.size(),
                              &handle.data, &handle.size);
    if (ret > 0) {
        *out = {IGatekeeper::ERROR_RETRY_TIMEOUT, ret, 0, {}};
        return ndk::ScopedAStatus::ok();
    }
    if (ret < 0 || handle.data == nullptr) {
        LOG(WARNING) << "enroll: device returned " << ret;
        return failure(IGatekeeper::ERROR_GENERAL_FAILURE);
    }
    *out = {IGatekeeper::STATUS_OK, 0, secureUserIdFromHandle(handle),
            std::vector<uint8_t>(handle.data, handle.data + handle.size)};
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus GatekeeperBridge::verify(int32_t uid, int64_t challenge,
                                            const std::vector<uint8_t>& enrolledPasswordHandle,
                                            const std::vector<uint8_t>& providedPassword,
                                            GatekeeperVerifyResponse* out) {
    OwnedBuffer token;
    bool reenroll = false;
    int ret = device_->verify(device_, static_cast<uint32_t>(uid), static_cast<uint64_t>(challenge),
                              bytes(enrolledPasswordHandle), enrolledPasswordHandle.size(),
                              bytes(providedPassword), providedPassword.size(), &token.data,
                              &token.size, &reenroll);
    if (ret > 0) {
        *out = {IGatekeeper::ERROR_RETRY_TIMEOUT, ret, {}};
        return ndk::ScopedAStatus::ok();
    }
    if (ret < 0) {
        LOG(WARNING) << "verify: device returned " << ret;
        return failure(IGatekeeper::ERROR_GENERAL_FAILURE);
    }
    HardwareAuthToken tok;
    if (!parseHwAuthToken(token, &tok)) {
        LOG(ERROR) << "verify: auth token too short: " << token.size;
        return failure(IGatekeeper::ERROR_GENERAL_FAILURE);
    }
    *out = {reenroll ? IGatekeeper::STATUS_REENROLL : IGatekeeper::STATUS_OK, 0, tok};
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus GatekeeperBridge::deleteUser(int32_t uid) {
    if (device_->delete_user == nullptr) return failure(IGatekeeper::ERROR_NOT_IMPLEMENTED);
    return deletionResult("deleteUser", device_->delete_user(device_, static_cast<uint32_t>(uid)));
}

ndk::ScopedAStatus GatekeeperBridge::deleteAllUsers() {
    if (device_->delete_all_users == nullptr) return failure(IGatekeeper::ERROR_NOT_IMPLEMENTED);
    return deletionResult("deleteAllUsers", device_->delete_all_users(device_));
}

}
