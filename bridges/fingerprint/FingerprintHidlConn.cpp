/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "mindone-bridge-fingerprint"

#include "FingerprintHidlConn.h"

#include <android-base/logging.h>

#include <chrono>
#include <thread>

namespace mindone {

using ::android::hardware::biometrics::fingerprint::V2_1::IBiometricsFingerprint;
using ::android::hardware::hidl_death_recipient;

// Delivered on its own binder-death callback thread (NOT the hwbinder pool thread) whenever the
// stock HIDL fingerprint process dies. See FingerprintHidlConn::onServiceDied().
class FpDeathRecipient : public hidl_death_recipient {
  public:
    explicit FpDeathRecipient(FingerprintHidlConn* conn) : mConn(conn) {}
    void serviceDied(uint64_t /*cookie*/,
                      const ::android::wp<::android::hidl::base::V1_0::IBase>& /*who*/) override {
        mConn->onServiceDied();
    }

  private:
    FingerprintHidlConn* mConn;  // not owned
};

FingerprintHidlConn::FingerprintHidlConn() = default;
FingerprintHidlConn::~FingerprintHidlConn() = default;

bool FingerprintHidlConn::connect() {
    auto hidl = IBiometricsFingerprint::getService();
    if (hidl == nullptr) {
        LOG(ERROR) << "HIDL biometrics.fingerprint@2.1 not available";
        return false;
    }
    auto death = ::android::sp<FpDeathRecipient>(new FpDeathRecipient(this));
    hidl->linkToDeath(death, 0 /*cookie, unused*/);
    {
        std::lock_guard<std::mutex> l(mLock);
        mHidl = hidl;
        mDeathRecipient = death;
    }
    LOG(INFO) << "bound to HIDL biometrics.fingerprint@2.1";
    return true;
}

::android::sp<IBiometricsFingerprint> FingerprintHidlConn::get() const {
    std::lock_guard<std::mutex> l(mLock);
    return mHidl;
}

void FingerprintHidlConn::setStateCallback(FpStateCallback cb) {
    std::lock_guard<std::mutex> l(mLock);
    mCb = std::move(cb);
}

void FingerprintHidlConn::onServiceDied() {
    LOG(ERROR) << "HIDL biometrics.fingerprint@2.1 died";
    FpStateCallback cb;
    {
        std::lock_guard<std::mutex> l(mLock);
        mHidl = nullptr;
        cb = mCb;
    }
    if (cb) cb(false);

    // Reconnect on our own background thread - never on the hwbinder pool thread that delivered
    // this death notification (same reasoning as RadioSeClient::onServiceDied() in the
    // secure_element v2 bridge, bridges/secure_element/SecureElementBridge.cpp).
    std::thread([this] {
        while (!connect()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        LOG(INFO) << "reconnected to HIDL biometrics.fingerprint@2.1 after a crash";
        FpStateCallback cb2;
        {
            std::lock_guard<std::mutex> l(mLock);
            cb2 = mCb;
        }
        if (cb2) cb2(true);
    }).detach();
}

}  // namespace mindone
