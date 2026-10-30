/*
 * Copyright (C) 2017 The Android Open Source Project
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "mindone-bridge-fingerprint"

#include "LegacyFingerprint.h"

#include <hardware/hardware.h>
#include <hardware/hw_auth_token.h>
#include <log/log.h>
#include <limits.h>
#include <unistd.h>
#include <cstring>
#include <utility>
#include <vector>

namespace mindone {

std::atomic<LegacyFingerprint*> LegacyFingerprint::sInstance{nullptr};

sp<LegacyFingerprint> LegacyFingerprint::open() {
    static std::mutex openMutex;
    std::lock_guard<std::mutex> lock(openMutex);
    if (sInstance.load(std::memory_order_acquire) != nullptr) {
        ALOGE("Fingerprint module is already open");
        return nullptr;
    }
    sp<LegacyFingerprint> instance = new LegacyFingerprint();
    sInstance.store(instance.get(), std::memory_order_release);
    if (!instance->openHal()) return nullptr;
    return instance;
}

LegacyFingerprint::~LegacyFingerprint() {
    if (mDevice != nullptr) {
        int error = mDevice->common.close(&mDevice->common);
        if (error != 0) ALOGE("Cannot close fingerprint module: %d", error);
    }
    sInstance.store(nullptr, std::memory_order_release);
}

bool LegacyFingerprint::openHal() {
    const hw_module_t* module = nullptr;
    int error = hw_get_module("fingerprint.silead", &module);
    if (error != 0 || module == nullptr) {
        ALOGE("Cannot load fingerprint.silead module: %d", error);
        return false;
    }
    if (module->module_api_version != FINGERPRINT_MODULE_API_VERSION_2_1) {
        ALOGE("Unexpected fingerprint module API version: %u", module->module_api_version);
        return false;
    }
    if (module->methods == nullptr || module->methods->open == nullptr) {
        ALOGE("Fingerprint module has no open method");
        return false;
    }
    hw_device_t* device = nullptr;
    error = module->methods->open(module, nullptr, &device);
    if (error != 0 || device == nullptr) {
        ALOGE("Cannot open fingerprint module: %d", error);
        return false;
    }
    mDevice = reinterpret_cast<fingerprint_device_t*>(device);
    if (device->version != FINGERPRINT_MODULE_API_VERSION_2_1) {
        ALOGE("Unexpected fingerprint device API version: %u", device->version);
        return false;
    }
    if (mDevice->set_notify == nullptr) {
        ALOGE("Fingerprint module has no notify method");
        return false;
    }
    error = mDevice->set_notify(mDevice, notify);
    if (error != 0) {
        ALOGE("Cannot set fingerprint module callback: %d", error);
        return false;
    }
    return true;
}

Return<RequestStatus> LegacyFingerprint::ErrorFilter(int32_t error) {
    switch(error) {
        case 0: return RequestStatus::SYS_OK;
        case -2: return RequestStatus::SYS_ENOENT;
        case -4: return RequestStatus::SYS_EINTR;
        case -5: return RequestStatus::SYS_EIO;
        case -11: return RequestStatus::SYS_EAGAIN;
        case -12: return RequestStatus::SYS_ENOMEM;
        case -13: return RequestStatus::SYS_EACCES;
        case -14: return RequestStatus::SYS_EFAULT;
        case -16: return RequestStatus::SYS_EBUSY;
        case -22: return RequestStatus::SYS_EINVAL;
        case -28: return RequestStatus::SYS_ENOSPC;
        case -110: return RequestStatus::SYS_ETIMEDOUT;
        default:
            ALOGE("An unknown error returned from fingerprint vendor library: %d", error);
            return RequestStatus::SYS_UNKNOWN;
    }
}

FingerprintError LegacyFingerprint::VendorErrorFilter(int32_t error,
            int32_t* vendorCode) {
    *vendorCode = 0;
    switch(error) {
        case FINGERPRINT_ERROR_HW_UNAVAILABLE:
            return FingerprintError::ERROR_HW_UNAVAILABLE;
        case FINGERPRINT_ERROR_UNABLE_TO_PROCESS:
            return FingerprintError::ERROR_UNABLE_TO_PROCESS;
        case FINGERPRINT_ERROR_TIMEOUT:
            return FingerprintError::ERROR_TIMEOUT;
        case FINGERPRINT_ERROR_NO_SPACE:
            return FingerprintError::ERROR_NO_SPACE;
        case FINGERPRINT_ERROR_CANCELED:
            return FingerprintError::ERROR_CANCELED;
        case FINGERPRINT_ERROR_UNABLE_TO_REMOVE:
            return FingerprintError::ERROR_UNABLE_TO_REMOVE;
        case FINGERPRINT_ERROR_LOCKOUT:
            return FingerprintError::ERROR_LOCKOUT;
        default:
            if (error >= FINGERPRINT_ERROR_VENDOR_BASE) {
                *vendorCode = error - FINGERPRINT_ERROR_VENDOR_BASE;
                return FingerprintError::ERROR_VENDOR;
            }
    }
    ALOGE("Unknown error from fingerprint vendor library: %d", error);
    return FingerprintError::ERROR_UNABLE_TO_PROCESS;
}

FingerprintAcquiredInfo LegacyFingerprint::VendorAcquiredFilter(
        int32_t info, int32_t* vendorCode) {
    *vendorCode = 0;
    switch(info) {
        case FINGERPRINT_ACQUIRED_GOOD:
            return FingerprintAcquiredInfo::ACQUIRED_GOOD;
        case FINGERPRINT_ACQUIRED_PARTIAL:
            return FingerprintAcquiredInfo::ACQUIRED_PARTIAL;
        case FINGERPRINT_ACQUIRED_INSUFFICIENT:
            return FingerprintAcquiredInfo::ACQUIRED_INSUFFICIENT;
        case FINGERPRINT_ACQUIRED_IMAGER_DIRTY:
            return FingerprintAcquiredInfo::ACQUIRED_IMAGER_DIRTY;
        case FINGERPRINT_ACQUIRED_TOO_SLOW:
            return FingerprintAcquiredInfo::ACQUIRED_TOO_SLOW;
        case FINGERPRINT_ACQUIRED_TOO_FAST:
            return FingerprintAcquiredInfo::ACQUIRED_TOO_FAST;
        default:
            if (info >= FINGERPRINT_ACQUIRED_VENDOR_BASE) {
                *vendorCode = info - FINGERPRINT_ACQUIRED_VENDOR_BASE;
                return FingerprintAcquiredInfo::ACQUIRED_VENDOR;
            }
    }
    ALOGE("Unknown acquiredmsg from fingerprint vendor library: %d", info);
    return FingerprintAcquiredInfo::ACQUIRED_INSUFFICIENT;
}

Return<uint64_t> LegacyFingerprint::setNotify(
        const sp<IBiometricsFingerprintClientCallback>& clientCallback) {
    sp<IBiometricsFingerprintClientCallback> previous;
    {
        std::lock_guard<std::mutex> lock(mClientCallbackMutex);
        previous = std::move(mClientCallback);
        mClientCallback = clientCallback;
    }

    return reinterpret_cast<uint64_t>(mDevice);
}

Return<uint64_t> LegacyFingerprint::preEnroll()  {
    if (mDevice == nullptr) {
        ALOGE("No valid device");
        return 0;
    }
    return mDevice->pre_enroll(mDevice);
}

Return<RequestStatus> LegacyFingerprint::enroll(const hidl_array<uint8_t, 69>& hat,
        uint32_t gid, uint32_t timeoutSec) {
    if (mDevice == nullptr) {
        ALOGE("No valid device");
        return RequestStatus::SYS_UNKNOWN;
    }
    hw_auth_token_t authToken{};
    static_assert(sizeof(authToken) == 69);
    std::memcpy(&authToken, hat.data(), sizeof(authToken));
    return ErrorFilter(mDevice->enroll(mDevice, &authToken, gid, timeoutSec));
}

Return<RequestStatus> LegacyFingerprint::postEnroll() {
    if (mDevice == nullptr) {
        ALOGE("No valid device");
        return RequestStatus::SYS_UNKNOWN;
    }
    return ErrorFilter(mDevice->post_enroll(mDevice));
}

Return<uint64_t> LegacyFingerprint::getAuthenticatorId() {
    if (mDevice == nullptr) {
        ALOGE("No valid device");
        return 0;
    }
    return mDevice->get_authenticator_id(mDevice);
}

Return<RequestStatus> LegacyFingerprint::cancel() {
    if (mDevice == nullptr) {
        ALOGE("No valid device");
        return RequestStatus::SYS_UNKNOWN;
    }
    return ErrorFilter(mDevice->cancel(mDevice));
}

Return<RequestStatus> LegacyFingerprint::enumerate()  {
    if (mDevice == nullptr) {
        ALOGE("No valid device");
        return RequestStatus::SYS_UNKNOWN;
    }
    return ErrorFilter(mDevice->enumerate(mDevice));
}

Return<RequestStatus> LegacyFingerprint::remove(uint32_t gid, uint32_t fid) {
    if (mDevice == nullptr) {
        ALOGE("No valid device");
        return RequestStatus::SYS_UNKNOWN;
    }
    return ErrorFilter(mDevice->remove(mDevice, gid, fid));
}

Return<RequestStatus> LegacyFingerprint::setActiveGroup(uint32_t gid,
        const hidl_string& storePath) {
    if (storePath.size() >= PATH_MAX || storePath.size() == 0) {
        ALOGE("Bad path length: %zu", storePath.size());
        return RequestStatus::SYS_EINVAL;
    }
    if (access(storePath.c_str(), W_OK)) {
        return RequestStatus::SYS_EINVAL;
    }
    if (mDevice == nullptr) {
        ALOGE("No valid device");
        return RequestStatus::SYS_UNKNOWN;
    }

    return ErrorFilter(mDevice->set_active_group(mDevice, gid,
                                                    storePath.c_str()));
}

Return<RequestStatus> LegacyFingerprint::authenticate(uint64_t operationId,
        uint32_t gid) {
    if (mDevice == nullptr) {
        ALOGE("No valid device");
        return RequestStatus::SYS_UNKNOWN;
    }
    return ErrorFilter(mDevice->authenticate(mDevice, operationId, gid));
}

void LegacyFingerprint::notify(const fingerprint_msg_t *msg) {
    LegacyFingerprint* thisPtr = sInstance.load(std::memory_order_acquire);
    if (thisPtr == nullptr || msg == nullptr) return;
    sp<IBiometricsFingerprintClientCallback> callback;
    {
        std::lock_guard<std::mutex> lock(thisPtr->mClientCallbackMutex);
        callback = thisPtr->mClientCallback;
    }
    if (callback == nullptr) return;
    const uint64_t devId = reinterpret_cast<uint64_t>(thisPtr->mDevice);
    switch (msg->type) {
        case FINGERPRINT_ERROR: {
                int32_t vendorCode = 0;
                FingerprintError result = VendorErrorFilter(msg->data.error, &vendorCode);
                ALOGD("onError(%d)", static_cast<int32_t>(result));
                if (!callback->onError(devId, result, vendorCode).isOk()) {
                    ALOGE("failed to invoke fingerprint onError callback");
                }
            }
            break;
        case FINGERPRINT_ACQUIRED: {
                int32_t vendorCode = 0;
                FingerprintAcquiredInfo result =
                    VendorAcquiredFilter(msg->data.acquired.acquired_info, &vendorCode);
                ALOGD("onAcquired(%d)", static_cast<int32_t>(result));
                if (!callback->onAcquired(devId, result, vendorCode).isOk()) {
                    ALOGE("failed to invoke fingerprint onAcquired callback");
                }
            }
            break;
        case FINGERPRINT_TEMPLATE_ENROLLING:
            ALOGD("onEnrollResult(fid=%d, gid=%d, rem=%d)",
                msg->data.enroll.finger.fid,
                msg->data.enroll.finger.gid,
                msg->data.enroll.samples_remaining);
            if (!callback->onEnrollResult(devId,
                    msg->data.enroll.finger.fid,
                    msg->data.enroll.finger.gid,
                    msg->data.enroll.samples_remaining).isOk()) {
                ALOGE("failed to invoke fingerprint onEnrollResult callback");
            }
            break;
        case FINGERPRINT_TEMPLATE_REMOVED:
            ALOGD("onRemove(fid=%d, gid=%d, rem=%d)",
                msg->data.removed.finger.fid,
                msg->data.removed.finger.gid,
                msg->data.removed.remaining_templates);
            if (!callback->onRemoved(devId,
                    msg->data.removed.finger.fid,
                    msg->data.removed.finger.gid,
                    msg->data.removed.remaining_templates).isOk()) {
                ALOGE("failed to invoke fingerprint onRemoved callback");
            }
            break;
        case FINGERPRINT_AUTHENTICATED:
            if (msg->data.authenticated.finger.fid != 0) {
                ALOGD("onAuthenticated(fid=%d, gid=%d)",
                    msg->data.authenticated.finger.fid,
                    msg->data.authenticated.finger.gid);
                const uint8_t* hat =
                    reinterpret_cast<const uint8_t *>(&msg->data.authenticated.hat);
                const hidl_vec<uint8_t> token(
                    std::vector<uint8_t>(hat, hat + sizeof(msg->data.authenticated.hat)));
                if (!callback->onAuthenticated(devId,
                        msg->data.authenticated.finger.fid,
                        msg->data.authenticated.finger.gid,
                        token).isOk()) {
                    ALOGE("failed to invoke fingerprint onAuthenticated callback");
                }
            } else {
                if (!callback->onAuthenticated(devId,
                        msg->data.authenticated.finger.fid,
                        msg->data.authenticated.finger.gid,
                        hidl_vec<uint8_t>()).isOk()) {
                    ALOGE("failed to invoke fingerprint onAuthenticated callback");
                }
            }
            break;
        case FINGERPRINT_TEMPLATE_ENUMERATING:
            ALOGD("onEnumerate(fid=%d, gid=%d, rem=%d)",
                msg->data.enumerated.finger.fid,
                msg->data.enumerated.finger.gid,
                msg->data.enumerated.remaining_templates);
            if (!callback->onEnumerate(devId,
                    msg->data.enumerated.finger.fid,
                    msg->data.enumerated.finger.gid,
                    msg->data.enumerated.remaining_templates).isOk()) {
                ALOGE("failed to invoke fingerprint onEnumerate callback");
            }
            break;
    }
}

}
