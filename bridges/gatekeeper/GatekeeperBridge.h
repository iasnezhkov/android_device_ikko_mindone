/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
// mindone: AIDL IGatekeeper (V1) → HIDL IGatekeeper@1.0 bridge. See BRIDGES-HIDL-AIDL.
//
// Review 14.09 (BRIDGES-REVIEW-1409): now holds a GatekeeperHidlConn instead of a raw
// sp<> so a TEE-side crash can be detected (linkToDeath) and recovered from (background
// reconnect); the previous version bound once at process start and never handled the HAL dying,
// which would have permanently locked users out of credential verification after a single crash.
#pragma once

#include <aidl/android/hardware/gatekeeper/BnGatekeeper.h>

#include "GatekeeperHidlConn.h"

#include <memory>

namespace mindone {

class GatekeeperBridge : public aidl::android::hardware::gatekeeper::BnGatekeeper {
  public:
    explicit GatekeeperBridge(std::shared_ptr<GatekeeperHidlConn> conn) : conn_(std::move(conn)) {}

    ndk::ScopedAStatus enroll(int32_t uid, const std::vector<uint8_t>& currentPasswordHandle,
                              const std::vector<uint8_t>& currentPassword,
                              const std::vector<uint8_t>& desiredPassword,
                              aidl::android::hardware::gatekeeper::GatekeeperEnrollResponse* out) override;
    ndk::ScopedAStatus verify(int32_t uid, int64_t challenge,
                              const std::vector<uint8_t>& enrolledPasswordHandle,
                              const std::vector<uint8_t>& providedPassword,
                              aidl::android::hardware::gatekeeper::GatekeeperVerifyResponse* out) override;
    ndk::ScopedAStatus deleteUser(int32_t uid) override;
    ndk::ScopedAStatus deleteAllUsers() override;

  private:
    std::shared_ptr<GatekeeperHidlConn> conn_;
};

}  // namespace mindone
