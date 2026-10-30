/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "mindone-bridge-fingerprint"

#include "SessionBridge.h"
#include "HatCodec.h"

#include <android-base/chrono_utils.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>

#include <chrono>
#include <thread>

namespace mindone {

using ::aidl::android::hardware::biometrics::common::ICancellationSignal;
using ::aidl::android::hardware::keymaster::HardwareAuthToken;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::hidl_vec;

namespace {
constexpr uint32_t kEnrollTimeoutSec = 60;

template <class T>
bool check(const Return<T>& r, const char* what) {
    if (!r.isOk()) {
        LOG(ERROR) << what << ": HIDL transport error: " << r.description();
        return false;
    }
    return true;
}
constexpr int64_t kLockoutTimedMs = 30000;
constexpr int64_t kMaxResetLockoutHatAgeMs = 60000;

fp::AcquiredInfo mapAcquired(hfp::FingerprintAcquiredInfo a) {
    int32_t v = static_cast<int32_t>(a);
    if (v < 0 || v > 6) return fp::AcquiredInfo::UNKNOWN;
    return static_cast<fp::AcquiredInfo>(v + 1);
}

fp::Error mapError(hfp::FingerprintError e) {
    switch (e) {
        case hfp::FingerprintError::ERROR_HW_UNAVAILABLE:   return fp::Error::HW_UNAVAILABLE;
        case hfp::FingerprintError::ERROR_UNABLE_TO_PROCESS: return fp::Error::UNABLE_TO_PROCESS;
        case hfp::FingerprintError::ERROR_TIMEOUT:          return fp::Error::TIMEOUT;
        case hfp::FingerprintError::ERROR_NO_SPACE:         return fp::Error::NO_SPACE;
        case hfp::FingerprintError::ERROR_CANCELED:         return fp::Error::CANCELED;
        case hfp::FingerprintError::ERROR_UNABLE_TO_REMOVE: return fp::Error::UNABLE_TO_REMOVE;
        case hfp::FingerprintError::ERROR_VENDOR:           return fp::Error::VENDOR;
        default:                                            return fp::Error::UNKNOWN;
    }
}
}

Return<void> HidlCallback::onEnrollResult(uint64_t, uint32_t fingerId, uint32_t, uint32_t remaining) {
    if (!active_.load(std::memory_order_relaxed)) return Void();
    cb_->onEnrollmentProgress(static_cast<int32_t>(fingerId), static_cast<int32_t>(remaining));
    return Void();
}

Return<void> HidlCallback::onAcquired(uint64_t, hfp::FingerprintAcquiredInfo info, int32_t vendorCode) {
    std::lock_guard<std::mutex> guard(authenticationLock_);
    if (!active_.load(std::memory_order_relaxed)) return Void();
    if (lockedOut_) return Void();
    cb_->onAcquired(mapAcquired(info), vendorCode);
    return Void();
}

Return<void> HidlCallback::onAuthenticated(uint64_t, uint32_t fingerId, uint32_t, const hidl_vec<uint8_t>& token) {
    std::lock_guard<std::mutex> guard(authenticationLock_);
    if (!active_.load(std::memory_order_relaxed)) return Void();
    if (!authenticating_ || lockedOut_) return Void();
    if (fingerId == 0) {
        lockedOut_ = lockout_->addFailedAttempt();
        cb_->onAuthenticationFailed();
        if (lockedOut_) {
            authenticating_ = false;
            if (hidl_ != nullptr) {
                std::thread([hidl = hidl_] { check(hidl->cancel(), "cancel after lockout"); }).detach();
            }
            lockout_->notifyLockout(cb_);
        }
        return Void();
    }
    authenticating_ = false;
    HardwareAuthToken hat;
    if (!bytesToHat(token.data(), token.size(), &hat)) {
        LOG(ERROR) << "onAuthenticated: token too short (" << token.size() << ")";
        cb_->onError(fp::Error::UNABLE_TO_PROCESS, 0);
        return Void();
    }
    lockout_->reset();
    cb_->onAuthenticationSucceeded(static_cast<int32_t>(fingerId), hat);
    return Void();
}

Return<void> HidlCallback::onError(uint64_t, hfp::FingerprintError error, int32_t vendorCode) {
    std::lock_guard<std::mutex> guard(authenticationLock_);
    if (!active_.load(std::memory_order_relaxed)) return Void();
    if (removal_.fail()) return Void();
    authenticating_ = false;
    const int32_t raw = static_cast<int32_t>(error);
    if (raw == 7) {
        cb_->onLockoutTimed(kLockoutTimedMs);
    } else if (raw == 9) {
        cb_->onLockoutPermanent();
    } else if (!lockedOut_) {
        cb_->onError(mapError(error), vendorCode);
    }
    return Void();
}

bool HidlCallback::beginAuthentication() {
    std::lock_guard<std::mutex> guard(authenticationLock_);
    lockedOut_ = lockout_->notifyLockout(cb_);
    authenticating_ = !lockedOut_;
    return authenticating_;
}

void HidlCallback::beginEnrollment() {
    std::lock_guard<std::mutex> guard(authenticationLock_);
    authenticating_ = false;
    lockedOut_ = false;
}

Return<void> HidlCallback::onRemoved(uint64_t, uint32_t fingerId, uint32_t, uint32_t remaining) {
    if (!active_.load(std::memory_order_relaxed)) return Void();
    removal_.result(static_cast<int32_t>(fingerId), remaining);
    return Void();
}

bool HidlCallback::removeEnrollments(const std::vector<int32_t>& ids,
                                    ::android::sp<hfp::IBiometricsFingerprint> hidl,
                                    uint32_t userId) {
    return removal_.start(ids, [hidl, userId](int32_t id) {
        auto ret = hidl->remove(userId, static_cast<uint32_t>(id));
        return ret.isOk() && static_cast<hfp::RequestStatus>(ret) == hfp::RequestStatus::SYS_OK;
    }, [this](const std::vector<int32_t>& removed) {
        if (active_.load(std::memory_order_relaxed)) cb_->onEnrollmentsRemoved(removed);
    });
}

Return<void> HidlCallback::onEnumerate(uint64_t, uint32_t fingerId, uint32_t, uint32_t remaining) {
    if (!active_.load(std::memory_order_relaxed)) return Void();
    std::lock_guard<std::mutex> g(lock_);
    if (fingerId != 0) enumerated_.push_back(static_cast<int32_t>(fingerId));
    if (remaining == 0) {
        cb_->onEnrollmentsEnumerated(enumerated_);
        enumerated_.clear();
    }
    return Void();
}

ndk::ScopedAStatus CancellationSignal::cancel() {
    if (hidl_ != nullptr) check(hidl_->cancel(), "cancel");
    return ndk::ScopedAStatus::ok();
}

SessionBridge::SessionBridge(::android::sp<hfp::IBiometricsFingerprint> hidl, int32_t userId,
                             std::shared_ptr<fp::ISessionCallback> cb,
                             std::shared_ptr<LockoutTracker> lockout)
    : hidl_(std::move(hidl)), userId_(userId), cb_(std::move(cb)), lockout_(std::move(lockout)) {
    lockout_->attach(cb_);
    hidlCb_ = new HidlCallback(cb_, lockout_, hidl_);
    if (hidl_ == nullptr) {
        LOG(ERROR) << "session for user " << userId_
                   << ": HIDL biometrics.fingerprint@2.1 unavailable at session creation";
        dead_.store(true, std::memory_order_relaxed);
        return;
    }
    const std::string storePath = ::android::base::StringPrintf("/data/vendor_de/%d/fpdata", userId_);
    check(hidl_->setNotify(hidlCb_), "setNotify");
    auto group = hidl_->setActiveGroup(static_cast<uint32_t>(userId_), storePath);
    if (!check(group, "setActiveGroup") ||
        static_cast<hfp::RequestStatus>(group) != hfp::RequestStatus::SYS_OK) {
        dead_.store(true, std::memory_order_relaxed);
        hidlCb_->deactivate();
        LOG(ERROR) << "session for user " << userId_ << ": cannot activate " << storePath;
        return;
    }
    LOG(INFO) << "session for user " << userId_ << " (" << storePath << ")";
}

void SessionBridge::onHidlDied() {
    dead_.store(true, std::memory_order_relaxed);
    hidlCb_->deactivate();
    if (!hwUnavailableNotified_.exchange(true, std::memory_order_relaxed)) {
        LOG(ERROR) << "session for user " << userId_
                   << ": HIDL biometrics.fingerprint@2.1 died mid-session";
        cb_->onError(fp::Error::HW_UNAVAILABLE, 0);
    }
}

ndk::ScopedAStatus SessionBridge::generateChallenge() {
    uint64_t ch = 0;
    if (!dead_.load(std::memory_order_relaxed)) {
        auto ret = hidl_->preEnroll();
        if (ret.isOk()) {
            ch = ret;
        } else {
            LOG(ERROR) << "generateChallenge: HIDL transport error: " << ret.description();
        }
    }
    cb_->onChallengeGenerated(static_cast<int64_t>(ch));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SessionBridge::revokeChallenge(int64_t challenge) {
    if (!dead_.load(std::memory_order_relaxed)) {
        check(hidl_->postEnroll(), "postEnroll");
    }
    cb_->onChallengeRevoked(challenge);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SessionBridge::enroll(const HardwareAuthToken& hat, std::shared_ptr<ICancellationSignal>* out) {
    if (dead_.load(std::memory_order_relaxed)) {
        cb_->onError(fp::Error::HW_UNAVAILABLE, 0);
        *out = ndk::SharedRefBase::make<CancellationSignal>(nullptr);
        return ndk::ScopedAStatus::ok();
    }
    hidlCb_->beginEnrollment();
    auto bytes = hatToBytes(hat);
    ::android::hardware::hidl_array<uint8_t, 69> arr(bytes.data());
    auto ret = hidl_->enroll(arr, static_cast<uint32_t>(userId_), kEnrollTimeoutSec);
    if (!ret.isOk()) {
        LOG(ERROR) << "enroll: HIDL transport error: " << ret.description();
        cb_->onError(fp::Error::HW_UNAVAILABLE, 0);
    } else if (static_cast<hfp::RequestStatus>(ret) != hfp::RequestStatus::SYS_OK) {
        LOG(ERROR) << "enroll: HIDL status " << static_cast<int32_t>(static_cast<hfp::RequestStatus>(ret));
        cb_->onError(fp::Error::UNABLE_TO_PROCESS, 0);
    }
    *out = ndk::SharedRefBase::make<CancellationSignal>(hidl_);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SessionBridge::authenticate(int64_t operationId, std::shared_ptr<ICancellationSignal>* out) {
    if (!hidlCb_->beginAuthentication()) {
        *out = ndk::SharedRefBase::make<CancellationSignal>(nullptr);
        return ndk::ScopedAStatus::ok();
    }
    if (dead_.load(std::memory_order_relaxed)) {
        cb_->onError(fp::Error::HW_UNAVAILABLE, 0);
        *out = ndk::SharedRefBase::make<CancellationSignal>(nullptr);
        return ndk::ScopedAStatus::ok();
    }
    auto ret = hidl_->authenticate(static_cast<uint64_t>(operationId), static_cast<uint32_t>(userId_));
    if (!ret.isOk()) {
        LOG(ERROR) << "authenticate: HIDL transport error: " << ret.description();
        cb_->onError(fp::Error::HW_UNAVAILABLE, 0);
    } else if (static_cast<hfp::RequestStatus>(ret) != hfp::RequestStatus::SYS_OK) {
        LOG(ERROR) << "authenticate: HIDL status " << static_cast<int32_t>(static_cast<hfp::RequestStatus>(ret));
        cb_->onError(fp::Error::UNABLE_TO_PROCESS, 0);
    }
    *out = ndk::SharedRefBase::make<CancellationSignal>(hidl_);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SessionBridge::detectInteraction(std::shared_ptr<ICancellationSignal>* out) {
    cb_->onError(fp::Error::UNABLE_TO_PROCESS, 0);
    *out = ndk::SharedRefBase::make<CancellationSignal>(nullptr);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SessionBridge::enumerateEnrollments() {
    if (dead_.load(std::memory_order_relaxed)) {
        cb_->onEnrollmentsEnumerated({});
        return ndk::ScopedAStatus::ok();
    }
    auto ret = hidl_->enumerate();
    if (!ret.isOk() || static_cast<hfp::RequestStatus>(ret) != hfp::RequestStatus::SYS_OK) {
        LOG(ERROR) << "enumerate: request rejected, HIDL status "
                   << (ret.isOk() ? static_cast<int32_t>(static_cast<hfp::RequestStatus>(ret)) : -1);
        cb_->onEnrollmentsEnumerated({});
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SessionBridge::removeEnrollments(const std::vector<int32_t>& ids) {
    if (dead_.load(std::memory_order_relaxed)) {
        cb_->onEnrollmentsRemoved({});
        return ndk::ScopedAStatus::ok();
    }
    if (!hidlCb_->removeEnrollments(ids, hidl_, static_cast<uint32_t>(userId_)))
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SessionBridge::getAuthenticatorId() {
    uint64_t id = 0;
    if (!dead_.load(std::memory_order_relaxed)) {
        auto ret = hidl_->getAuthenticatorId();
        if (ret.isOk()) {
            id = ret;
        } else {
            LOG(ERROR) << "getAuthenticatorId: HIDL transport error: " << ret.description();
        }
    }
    cb_->onAuthenticatorIdRetrieved(static_cast<int64_t>(id));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SessionBridge::invalidateAuthenticatorId() {
    uint64_t id = 0;
    if (!dead_.load(std::memory_order_relaxed)) {
        auto ret = hidl_->getAuthenticatorId();
        if (ret.isOk()) {
            id = ret;
        } else {
            LOG(ERROR) << "invalidateAuthenticatorId: HIDL transport error: " << ret.description();
        }
    }
    cb_->onAuthenticatorIdInvalidated(static_cast<int64_t>(id));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SessionBridge::resetLockout(const HardwareAuthToken& hat) {
    const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  ::android::base::boot_clock::now().time_since_epoch())
                                  .count();
    const int64_t timestampMs = hat.timestamp.milliSeconds;
    if (timestampMs < 0 || timestampMs > nowMs ||
        nowMs - timestampMs > kMaxResetLockoutHatAgeMs) {
        LOG(ERROR) << "resetLockout: HAT timestamp not recent (timestamp " << timestampMs
                   << " ms, limit " << kMaxResetLockoutHatAgeMs
                   << " ms) - rejecting (partial check only, HMAC not verifiable over HIDL 2.1)";
        cb_->onError(fp::Error::UNABLE_TO_PROCESS, 0);
        return ndk::ScopedAStatus::ok();
    }
    lockout_->reset(cb_);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SessionBridge::close() {
    hidlCb_->deactivate();
    if (!dead_.load(std::memory_order_relaxed)) {
        check(hidl_->cancel(), "cancel");
    }
    cb_->onSessionClosed();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SessionBridge::onPointerDown(int32_t, int32_t, int32_t, float, float) { return ndk::ScopedAStatus::ok(); }
ndk::ScopedAStatus SessionBridge::onPointerUp(int32_t) { return ndk::ScopedAStatus::ok(); }
ndk::ScopedAStatus SessionBridge::onUiReady() { return ndk::ScopedAStatus::ok(); }
ndk::ScopedAStatus SessionBridge::authenticateWithContext(int64_t operationId, const ::aidl::android::hardware::biometrics::common::OperationContext&, std::shared_ptr<ICancellationSignal>* out) { return authenticate(operationId, out); }
ndk::ScopedAStatus SessionBridge::enrollWithContext(const HardwareAuthToken& hat, const ::aidl::android::hardware::biometrics::common::OperationContext&, std::shared_ptr<ICancellationSignal>* out) { return enroll(hat, out); }
ndk::ScopedAStatus SessionBridge::detectInteractionWithContext(const ::aidl::android::hardware::biometrics::common::OperationContext&, std::shared_ptr<ICancellationSignal>* out) { return detectInteraction(out); }
ndk::ScopedAStatus SessionBridge::onPointerDownWithContext(const fp::PointerContext&) { return ndk::ScopedAStatus::ok(); }
ndk::ScopedAStatus SessionBridge::onPointerUpWithContext(const fp::PointerContext&) { return ndk::ScopedAStatus::ok(); }
ndk::ScopedAStatus SessionBridge::onContextChanged(const ::aidl::android::hardware::biometrics::common::OperationContext&) { return ndk::ScopedAStatus::ok(); }
ndk::ScopedAStatus SessionBridge::onPointerCancelWithContext(const fp::PointerContext&) { return ndk::ScopedAStatus::ok(); }
ndk::ScopedAStatus SessionBridge::setIgnoreDisplayTouches(bool) { return ndk::ScopedAStatus::ok(); }

}
