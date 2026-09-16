/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
// mindone: AIDL ISession (fingerprint V4) over HIDL IBiometricsFingerprint@2.1 + client callback.
//
// Review 14.09 (BRIDGES-REVIEW-1409): added `dead_`/HidlCallback::deactivate() so (a) a
// session created while the HIDL HAL is down, or (b) a session whose HIDL HAL crashes mid-flight,
// reports Error::HW_UNAVAILABLE instead of dereferencing a null/stale sp<> or hanging forever, and
// (c) a stray HIDL callback that arrives after close() is dropped instead of being delivered to a
// session the framework already believes is closed.
#pragma once
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

// HIDL callback from the stock HAL -> AIDL ISessionCallback
class HidlCallback : public hfp::IBiometricsFingerprintClientCallback {
  public:
    explicit HidlCallback(std::shared_ptr<fp::ISessionCallback> cb) : cb_(std::move(cb)) {}
    ::android::hardware::Return<void> onEnrollResult(uint64_t deviceId, uint32_t fingerId, uint32_t groupId, uint32_t remaining) override;
    ::android::hardware::Return<void> onAcquired(uint64_t deviceId, hfp::FingerprintAcquiredInfo acquiredInfo, int32_t vendorCode) override;
    ::android::hardware::Return<void> onAuthenticated(uint64_t deviceId, uint32_t fingerId, uint32_t groupId, const ::android::hardware::hidl_vec<uint8_t>& token) override;
    ::android::hardware::Return<void> onError(uint64_t deviceId, hfp::FingerprintError error, int32_t vendorCode) override;
    ::android::hardware::Return<void> onRemoved(uint64_t deviceId, uint32_t fingerId, uint32_t groupId, uint32_t remaining) override;
    ::android::hardware::Return<void> onEnumerate(uint64_t deviceId, uint32_t fingerId, uint32_t groupId, uint32_t remaining) override;

    // Called once the session is closed or its HIDL HAL has died: further notify() callbacks from
    // the stock HAL (a straggler already in flight, or a stale binder that has not yet noticed
    // its peer is gone) are dropped instead of being delivered to a session the framework already
    // believes is closed/dead.
    void deactivate() { active_.store(false, std::memory_order_relaxed); }

  private:
    std::shared_ptr<fp::ISessionCallback> cb_;
    std::atomic<bool> active_{true};
    std::mutex lock_;
    std::vector<int32_t> enumerated_;
    std::vector<int32_t> removed_;
};

class CancellationSignal : public ::aidl::android::hardware::biometrics::common::BnCancellationSignal {
  public:
    explicit CancellationSignal(::android::sp<hfp::IBiometricsFingerprint> hidl) : hidl_(std::move(hidl)) {}
    ndk::ScopedAStatus cancel() override;
  private:
    ::android::sp<hfp::IBiometricsFingerprint> hidl_;  // may be nullptr - see cancel()
};

class SessionBridge : public fp::BnSession {
  public:
    SessionBridge(::android::sp<hfp::IBiometricsFingerprint> hidl, int32_t userId, std::shared_ptr<fp::ISessionCallback> cb);

    // Called by FingerprintBridge (from FingerprintHidlConn's background reconnect thread) when
    // the stock HAL process backing this session has died. Idempotent.
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
    ::android::sp<hfp::IBiometricsFingerprint> hidl_;  // may be nullptr - see dead_
    int32_t userId_;
    std::shared_ptr<fp::ISessionCallback> cb_;
    ::android::sp<HidlCallback> hidlCb_;
    std::atomic<bool> dead_{false};                  // hidl_ is null/stale - never touch it
    std::atomic<bool> hwUnavailableNotified_{false};  // onHidlDied() fires onError() at most once
};

}  // namespace mindone
