/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
// mindone: AIDL ISession (fingerprint V4) over HIDL IBiometricsFingerprint@2.1 (Silead). See BRIDGES-HIDL-AIDL.
#define LOG_TAG "mindone-bridge-fingerprint"

#include "SessionBridge.h"
#include "HatCodec.h"

#include <android-base/chrono_utils.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>

#include <chrono>

namespace mindone {

using ::aidl::android::hardware::biometrics::common::ICancellationSignal;
using ::aidl::android::hardware::keymaster::HardwareAuthToken;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::hidl_vec;

namespace {
constexpr uint32_t kEnrollTimeoutSec = 60;

// An unchecked failed Return<T> in a destructor crashes the process (libhidl: "Failed HIDL return status not
// checked" -> LOG(FATAL)). Every HIDL call from the bridge goes through check().
template <class T>
bool check(const Return<T>& r, const char* what) {
    if (!r.isOk()) {
        LOG(ERROR) << what << ": HIDL transport error: " << r.description();
        return false;
    }
    return true;
}
constexpr int64_t kLockoutTimedMs = 30000;   // HIDL 2.1 does not report a duration - 30s like the stock framework
// ISession.aidl#resetLockout: the timestamp must be "relatively recent (e.g. on the order of
// minutes, not hours)". See resetLockout() below for why this is only a partial check.
constexpr int64_t kMaxResetLockoutHatAgeMs = 60000;

fp::AcquiredInfo mapAcquired(hfp::FingerprintAcquiredInfo a) {
    // HIDL: GOOD=0 ... VENDOR=6; AIDL: UNKNOWN=0, GOOD=1 ... VENDOR=7 - shifted by 1
    // (verified 14.09 against the real generated headers: HIDL 2.1's own types.hal and the AIDL
    // V4 AcquiredInfo.h - see BRIDGES-REVIEW-1409).
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
}  // namespace

// ---- HIDL callback -> AIDL ISessionCallback ----
Return<void> HidlCallback::onEnrollResult(uint64_t, uint32_t fingerId, uint32_t, uint32_t remaining) {
    if (!active_.load(std::memory_order_relaxed)) return Void();
    cb_->onEnrollmentProgress(static_cast<int32_t>(fingerId), static_cast<int32_t>(remaining));
    return Void();
}

Return<void> HidlCallback::onAcquired(uint64_t, hfp::FingerprintAcquiredInfo info, int32_t vendorCode) {
    if (!active_.load(std::memory_order_relaxed)) return Void();
    cb_->onAcquired(mapAcquired(info), vendorCode);
    return Void();
}

Return<void> HidlCallback::onAuthenticated(uint64_t, uint32_t fingerId, uint32_t, const hidl_vec<uint8_t>& token) {
    if (!active_.load(std::memory_order_relaxed)) return Void();
    if (fingerId == 0) {
        cb_->onAuthenticationFailed();
        return Void();
    }
    HardwareAuthToken hat;
    if (!bytesToHat(token.data(), token.size(), &hat)) {
        LOG(ERROR) << "onAuthenticated: token too short (" << token.size() << ")";
        cb_->onError(fp::Error::UNABLE_TO_PROCESS, 0);
        return Void();
    }
    cb_->onAuthenticationSucceeded(static_cast<int32_t>(fingerId), hat);
    return Void();
}

Return<void> HidlCallback::onError(uint64_t, hfp::FingerprintError error, int32_t vendorCode) {
    if (!active_.load(std::memory_order_relaxed)) return Void();
    const int32_t raw = static_cast<int32_t>(error);
    if (raw == 7) {            // ERROR_LOCKOUT
        cb_->onLockoutTimed(kLockoutTimedMs);
    } else if (raw == 9) {     // vendor extension, NOT part of the real HIDL 2.1 FingerprintError
                                // enum (verified 14.09 against hardware/interfaces/biometrics/
                                // fingerprint/2.1/types.hal in the LineageOS tree: it only defines
                                // 0-8). C++ enums are not wire-validated, so a vendor blob can
                                // legally send an out-of-range value; this mapping is UNVERIFIED
                                // against the actual Silead binary (device unavailable during this
                                // review) - see BRIDGES-REVIEW-1409 "fingerprint: dead
                                // code or real vendor quirk?". Left in place (harmless if never
                                // sent, correct if it is) but flagged rather than silently trusted.
        cb_->onLockoutPermanent();
    } else {
        cb_->onError(mapError(error), vendorCode);
    }
    return Void();
}

Return<void> HidlCallback::onRemoved(uint64_t, uint32_t fingerId, uint32_t, uint32_t remaining) {
    if (!active_.load(std::memory_order_relaxed)) return Void();
    std::lock_guard<std::mutex> g(lock_);
    if (fingerId != 0) removed_.push_back(static_cast<int32_t>(fingerId));
    if (remaining == 0) {
        cb_->onEnrollmentsRemoved(removed_);
        removed_.clear();
    }
    return Void();
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

// ---- cancellation ----
ndk::ScopedAStatus CancellationSignal::cancel() {
    if (hidl_ != nullptr) check(hidl_->cancel(), "cancel");
    return ndk::ScopedAStatus::ok();
}

// ---- session ----
SessionBridge::SessionBridge(::android::sp<hfp::IBiometricsFingerprint> hidl, int32_t userId,
                             std::shared_ptr<fp::ISessionCallback> cb)
    : hidl_(std::move(hidl)), userId_(userId), cb_(std::move(cb)) {
    hidlCb_ = new HidlCallback(cb_);
    if (hidl_ == nullptr) {
        // The HIDL HAL was down when this session was created (mid-reconnect after a crash, or
        // it never came up). IFingerprint#createSession has no synchronous failure return, so the
        // best available contract-compliant behavior is to hand back a session that reports the
        // failure via the normal onError() path on first use - see onHidlDied() and the dead_
        // guards on every method below, rather than crashing on a null hidl_ dereference (the
        // previous version of this file always assumed hidl_ was valid).
        LOG(ERROR) << "session for user " << userId_
                   << ": HIDL biometrics.fingerprint@2.1 unavailable at session creation";
        dead_.store(true, std::memory_order_relaxed);
        return;
    }
    // legacy template storage path, as used by the HIDL-era framework
    const std::string storePath = ::android::base::StringPrintf("/data/vendor_de/%d/fpdata", userId_);
    check(hidl_->setNotify(hidlCb_), "setNotify");
    check(hidl_->setActiveGroup(static_cast<uint32_t>(userId_), storePath), "setActiveGroup");
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
            // ISession#generateChallenge has no onError path in the AIDL spec - onChallengeGenerated
            // is its only documented terminal callback - so a transport failure can only be
            // surfaced by logging loudly and returning a degenerate challenge. Previously this was
            // completely silent (no log at all), making a HAL outage here invisible.
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
    auto bytes = hatToBytes(hat);
    ::android::hardware::hidl_array<uint8_t, 69> arr(bytes.data());
    auto ret = hidl_->enroll(arr, static_cast<uint32_t>(userId_), kEnrollTimeoutSec);
    if (!ret.isOk()) {
        // Transport gone (HAL crashed) is a hardware problem, not a "bad fingerprint image"
        // problem - HW_UNAVAILABLE is the correct Error per its own doc comment ("The hardware
        // has an error."), UNABLE_TO_PROCESS is reserved for the hardware being up but rejecting
        // this specific request. The previous version mapped both cases to UNABLE_TO_PROCESS.
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
    // ISession.aidl: "If invoked on HALs that do not support this functionality, the HAL must
    // respond with ISession#onError(UNABLE_TO_PROCESS, 0)." HIDL 2.1 has no detectInteraction and
    // SensorProps.supportsDetectInteraction=false, so the framework should never call this - this
    // path exists only to honor the spec's mandated behavior if it does anyway.
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
        // enumerateEnrollments()'s ONLY documented terminal callback is onEnrollmentsEnumerated -
        // ISession.aidl defines no onError alternative for it. The previous version only checked
        // ret.isOk() (transport), never the actual RequestStatus payload, so a HIDL-level
        // rejection (e.g. SYS_EBUSY) meant onEnumerate() would never fire and the framework would
        // wait for onEnrollmentsEnumerated() forever. Ending the operation's lifecycle here (with
        // an empty list, the closest honest answer to "nothing was enumerated") is required so
        // the operation actually terminates.
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
    bool anyAccepted = false;
    for (int32_t id : ids) {
        auto ret = hidl_->remove(static_cast<uint32_t>(userId_), static_cast<uint32_t>(id));
        if (!ret.isOk() || static_cast<hfp::RequestStatus>(ret) != hfp::RequestStatus::SYS_OK) {
            LOG(ERROR) << "remove(" << id << "): request rejected, HIDL status "
                       << (ret.isOk() ? static_cast<int32_t>(static_cast<hfp::RequestStatus>(ret)) : -1);
        } else {
            anyAccepted = true;
        }
    }
    if (!anyAccepted && !ids.empty()) {
        // Same reasoning as enumerateEnrollments() above: if none of the individual remove()
        // requests were accepted, onRemoved() will never fire for any of them, and
        // removeEnrollments() has no onError alternative either.
        cb_->onEnrollmentsRemoved({});
    }
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
    // HIDL 2.1 has no invalidation call; return the current id (as the AOSP example does for legacy)
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
    // ISession.aidl#resetLockout: the HAL must (1) verify the HAT's authenticity/integrity (HMAC)
    // and (2) verify its timestamp is recent, calling onError(UNABLE_TO_PROCESS) if either check
    // fails; only then may it clear the lockout counter. The PREVIOUS version of this bridge
    // ignored `hat` entirely and unconditionally called onLockoutCleared() - any caller with an
    // arbitrary/expired HAT could clear the anti-bruteforce lockout counter, defeating it.
    //
    // Full fix is not possible over this HIDL generation: HIDL 2.1 has no request that hands a
    // HAT to the TEE/keymaster for HMAC verification (that mechanism postdates this HAL version),
    // and the bridge has no access to the verification key itself. What IS checkable without that
    // key is requirement (2), the timestamp recency check, which at least rejects a stale/replayed
    // HAT. A well-formed *forged* HAT with a fresh timestamp still passes this partial check - see
    // BRIDGES-REVIEW-1409 "fingerprint: resetLockout" for the residual gap and why closing
    // it needs a HIDL-level (not bridge-level) fix.
    if (!dead_.load(std::memory_order_relaxed)) {
        const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      ::android::base::boot_clock::now().time_since_epoch())
                                      .count();
        const int64_t ageMs = nowMs - hat.timestamp.milliSeconds;
        if (ageMs < 0 || ageMs > kMaxResetLockoutHatAgeMs) {
            LOG(ERROR) << "resetLockout: HAT timestamp not recent (age " << ageMs
                       << " ms, limit " << kMaxResetLockoutHatAgeMs
                       << " ms) - rejecting (partial check only, HMAC not verifiable over HIDL 2.1)";
            cb_->onError(fp::Error::UNABLE_TO_PROCESS, 0);
            return ndk::ScopedAStatus::ok();
        }
    }
    // on HIDL 2.1 the lockout is cleared by the HAL itself after a timeout; we just tell the
    // framework it is cleared once the (partial) check above has passed.
    cb_->onLockoutCleared();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SessionBridge::close() {
    if (!dead_.load(std::memory_order_relaxed)) {
        check(hidl_->cancel(), "cancel");
    }
    // Deactivate unconditionally (not just in the dead_ branch): once close() returns
    // onSessionClosed(), the framework considers this session gone and must not receive any
    // further callback from it, even a straggler from the stock HAL that was already in flight.
    hidlCb_->deactivate();
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

}  // namespace mindone
