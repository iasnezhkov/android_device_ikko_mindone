/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "LockoutTracker.h"
#include "RemovalBatch.h"
#include <aidl/android/hardware/biometrics/common/BnCancellationSignal.h>
#include <aidl/android/hardware/biometrics/fingerprint/BnSession.h>
#include <aidl/android/hardware/biometrics/fingerprint/ISessionCallback.h>
#include <android/hardware/biometrics/fingerprint/2.1/IBiometricsFingerprint.h>
#include <android/hardware/biometrics/fingerprint/2.1/IBiometricsFingerprintClientCallback.h>
#include <atomic>
#include <mutex>
#include <vector>

namespace mindone {

namespace fp = ::aidl::android::hardware::biometrics::fingerprint;
namespace hfp = ::android::hardware::biometrics::fingerprint::V2_1;

class SessionBridge;

class HidlCallback : public hfp::IBiometricsFingerprintClientCallback {
  public:
    HidlCallback(std::shared_ptr<fp::ISessionCallback> cb,
                 std::shared_ptr<LockoutTracker> lockout,
                 ::android::sp<hfp::IBiometricsFingerprint> hidl)
        : cb_(std::move(cb)), lockout_(std::move(lockout)), hidl_(std::move(hidl)) {}
    bool beginAuthentication();
    void beginEnrollment();
    ::android::hardware::Return<void> onEnrollResult(uint64_t deviceId, uint32_t fingerId, uint32_t groupId, uint32_t remaining) override;
    ::android::hardware::Return<void> onAcquired(uint64_t deviceId, hfp::FingerprintAcquiredInfo acquiredInfo, int32_t vendorCode) override;
    ::android::hardware::Return<void> onAuthenticated(uint64_t deviceId, uint32_t fingerId, uint32_t groupId, const ::android::hardware::hidl_vec<uint8_t>& token) override;
    ::android::hardware::Return<void> onError(uint64_t deviceId, hfp::FingerprintError error, int32_t vendorCode) override;
    ::android::hardware::Return<void> onRemoved(uint64_t deviceId, uint32_t fingerId, uint32_t groupId, uint32_t remaining) override;
    ::android::hardware::Return<void> onEnumerate(uint64_t deviceId, uint32_t fingerId, uint32_t groupId, uint32_t remaining) override;

    void deactivate() {
        active_.store(false, std::memory_order_relaxed);
        lockout_->detach(cb_);
        removal_.cancel();
    }
    bool removeEnrollments(const std::vector<int32_t>& ids,
                           ::android::sp<hfp::IBiometricsFingerprint> hidl, uint32_t userId);

  private:
    std::shared_ptr<fp::ISessionCallback> cb_;
    std::shared_ptr<LockoutTracker> lockout_;
    ::android::sp<hfp::IBiometricsFingerprint> hidl_;
    std::mutex authenticationLock_;
    bool authenticating_ = false;
    bool lockedOut_ = false;
    std::atomic<bool> active_{true};
    std::mutex lock_;
    std::vector<int32_t> enumerated_;
    RemovalBatch removal_;
};

class CancellationSignal : public ::aidl::android::hardware::biometrics::common::BnCancellationSignal {
  public:
    explicit CancellationSignal(::android::sp<hfp::IBiometricsFingerprint> hidl) : hidl_(std::move(hidl)) {}
    ndk::ScopedAStatus cancel() override;
  private:
    ::android::sp<hfp::IBiometricsFingerprint> hidl_;
};

class SessionBridge : public fp::BnSession {
  public:
    SessionBridge(::android::sp<hfp::IBiometricsFingerprint> hidl, int32_t userId,
                  std::shared_ptr<fp::ISessionCallback> cb, std::shared_ptr<LockoutTracker> lockout);

    void onHidlDied();

    ndk::ScopedAStatus generateChallenge() override;
    ndk::ScopedAStatus revokeChallenge(int64_t challenge) override;
    ndk::ScopedAStatus enroll(const ::aidl::android::hardware::keymaster::HardwareAuthToken& hat, std::shared_ptr<::aidl::android::hardware::biometrics::common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus authenticate(int64_t operationId, std::shared_ptr<::aidl::android::hardware::biometrics::common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus detectInteraction(std::shared_ptr<::aidl::android::hardware::biometrics::common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus enumerateEnrollments() override;
    ndk::ScopedAStatus removeEnrollments(const std::vector<int32_t>& enrollmentIds) override;
    ndk::ScopedAStatus getAuthenticatorId() override;
    ndk::ScopedAStatus invalidateAuthenticatorId() override;
    ndk::ScopedAStatus resetLockout(const ::aidl::android::hardware::keymaster::HardwareAuthToken& hat) override;
    ndk::ScopedAStatus close() override;
    ndk::ScopedAStatus onPointerDown(int32_t pointerId, int32_t x, int32_t y, float minor, float major) override;
    ndk::ScopedAStatus onPointerUp(int32_t pointerId) override;
    ndk::ScopedAStatus onUiReady() override;
    ndk::ScopedAStatus authenticateWithContext(int64_t operationId, const ::aidl::android::hardware::biometrics::common::OperationContext& context, std::shared_ptr<::aidl::android::hardware::biometrics::common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus enrollWithContext(const ::aidl::android::hardware::keymaster::HardwareAuthToken& hat, const ::aidl::android::hardware::biometrics::common::OperationContext& context, std::shared_ptr<::aidl::android::hardware::biometrics::common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus detectInteractionWithContext(const ::aidl::android::hardware::biometrics::common::OperationContext& context, std::shared_ptr<::aidl::android::hardware::biometrics::common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus onPointerDownWithContext(const fp::PointerContext& context) override;
    ndk::ScopedAStatus onPointerUpWithContext(const fp::PointerContext& context) override;
    ndk::ScopedAStatus onContextChanged(const ::aidl::android::hardware::biometrics::common::OperationContext& context) override;
    ndk::ScopedAStatus onPointerCancelWithContext(const fp::PointerContext& context) override;
    ndk::ScopedAStatus setIgnoreDisplayTouches(bool shouldIgnore) override;

  private:
    ::android::sp<hfp::IBiometricsFingerprint> hidl_;
    int32_t userId_;
    std::shared_ptr<fp::ISessionCallback> cb_;
    std::shared_ptr<LockoutTracker> lockout_;
    ::android::sp<HidlCallback> hidlCb_;
    std::atomic<bool> dead_{false};
    std::atomic<bool> hwUnavailableNotified_{false};
};

}
