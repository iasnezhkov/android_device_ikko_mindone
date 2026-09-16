/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
// mindone: connection lifecycle for the stock HIDL biometrics.fingerprint@2.1 service (Silead).
// Handles linkToDeath() + a background reconnect loop, so a HAL crash does not permanently wedge
// the AIDL bridge (see BRIDGES-REVIEW-1409 "fingerprint: death/reconnect"). Modeled
// directly on bridges/secure_element/SecureElementBridge's death/reconnect pattern - none of the three
// bridges reviewed in that pass (fingerprint, gatekeeper, tetheroffload) had any such handling
// before this review; the secure_element v2 bridge was the only one.
#pragma once

#include <android/hardware/biometrics/fingerprint/2.1/IBiometricsFingerprint.h>

#include <functional>
#include <mutex>

namespace mindone {

class FpDeathRecipient;  // defined in FingerprintHidlConn.cpp

// Notifies of a HIDL availability transition: true = (re)connected, false = just died.
using FpStateCallback = std::function<void(bool available)>;

class FingerprintHidlConn {
  public:
    FingerprintHidlConn();
    ~FingerprintHidlConn();

    // Binds IBiometricsFingerprint::getService() and installs the death recipient. Returns false
    // if the HIDL service is not registered right now (getService() itself blocks/retries
    // internally until hwservicemanager has it, so a false return here means something is
    // actually wrong, not just "not up yet" - see service.cpp).
    bool connect();

    // Current live handle, or nullptr if the HIDL service is currently down (death detected,
    // reconnect in progress). Thread-safe; take a local copy rather than caching this pointer.
    ::android::sp<::android::hardware::biometrics::fingerprint::V2_1::IBiometricsFingerprint> get() const;

    void setStateCallback(FpStateCallback cb);

  private:
    friend class FpDeathRecipient;
    void onServiceDied();

    mutable std::mutex mLock;
    ::android::sp<::android::hardware::biometrics::fingerprint::V2_1::IBiometricsFingerprint> mHidl;
    ::android::sp<FpDeathRecipient> mDeathRecipient;
    FpStateCallback mCb;
};

}  // namespace mindone
