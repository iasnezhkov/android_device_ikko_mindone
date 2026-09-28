/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
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
