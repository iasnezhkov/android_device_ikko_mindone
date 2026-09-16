/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
// mindone: AIDL IFingerprint (V4) over HIDL IBiometricsFingerprint@2.1.
//
// Review 14.09 (BRIDGES-REVIEW-1409): now holds a FingerprintHidlConn instead of a raw
// sp<> so a HIDL crash can be detected (linkToDeath) and recovered from (background reconnect);
// the previous version bound once at process start and never handled the HAL dying. Also tracks
// the currently active session so a death notification can be forwarded to it.
#pragma once
#include <aidl/android/hardware/biometrics/fingerprint/BnFingerprint.h>

#include "FingerprintHidlConn.h"

#include <memory>
#include <mutex>

namespace mindone {

class SessionBridge;

class FingerprintBridge : public ::aidl::android::hardware::biometrics::fingerprint::BnFingerprint {
  public:
    explicit FingerprintBridge(std::shared_ptr<FingerprintHidlConn> conn);
    ndk::ScopedAStatus getSensorProps(std::vector<::aidl::android::hardware::biometrics::fingerprint::SensorProps>* out) override;
    ndk::ScopedAStatus createSession(int32_t sensorId, int32_t userId,
                                     const std::shared_ptr<::aidl::android::hardware::biometrics::fingerprint::ISessionCallback>& cb,
                                     std::shared_ptr<::aidl::android::hardware::biometrics::fingerprint::ISession>* out) override;

  private:
    void onHidlStateChanged(bool available);

    std::shared_ptr<FingerprintHidlConn> conn_;
    std::mutex lock_;
    std::weak_ptr<SessionBridge> activeSession_;  // guarded by lock_
};

}  // namespace mindone
