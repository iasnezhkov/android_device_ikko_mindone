/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <android/hardware/biometrics/fingerprint/2.1/IBiometricsFingerprint.h>

#include <functional>
#include <mutex>

namespace mindone {

using FpStateCallback = std::function<void(bool available)>;

class FingerprintHidlConn {
  public:
    FingerprintHidlConn();
    ~FingerprintHidlConn();
    bool connect();
    ::android::sp<::android::hardware::biometrics::fingerprint::V2_1::IBiometricsFingerprint> get() const;
    void setStateCallback(FpStateCallback cb);

  private:
    mutable std::mutex mLock;
    ::android::sp<::android::hardware::biometrics::fingerprint::V2_1::IBiometricsFingerprint> mHidl;
    bool mOpenAttempted = false;
    FpStateCallback mCb;
};

}
