/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "mindone-bridge-fingerprint"

#include "FingerprintBridge.h"
#include "SessionBridge.h"

#include <android-base/logging.h>
#include <android-base/properties.h>

namespace mindone {

using ::aidl::android::hardware::biometrics::common::SensorStrength;
using ::aidl::android::hardware::biometrics::fingerprint::FingerprintSensorType;
using ::aidl::android::hardware::biometrics::fingerprint::ISession;
using ::aidl::android::hardware::biometrics::fingerprint::ISessionCallback;
using ::aidl::android::hardware::biometrics::fingerprint::SensorLocation;
using ::aidl::android::hardware::biometrics::fingerprint::SensorProps;

namespace {
constexpr int32_t kSensorId = 0;
constexpr int32_t kMaxEnrollments = 5;

FingerprintSensorType sensorTypeFromProp() {
    const std::string t = ::android::base::GetProperty("ro.vendor.mindone.fp.type", "rear");
    if (t == "side") return FingerprintSensorType::POWER_BUTTON;
    if (t == "home") return FingerprintSensorType::HOME_BUTTON;
    if (t == "udfps") return FingerprintSensorType::UNDER_DISPLAY_OPTICAL;
    return FingerprintSensorType::REAR;
}
}

FingerprintBridge::FingerprintBridge(std::shared_ptr<FingerprintHidlConn> conn) : conn_(std::move(conn)) {
    conn_->setStateCallback([this](bool available) { onHidlStateChanged(available); });
}

ndk::ScopedAStatus FingerprintBridge::getSensorProps(std::vector<SensorProps>* out) {
    SensorProps p;
    p.commonProps.sensorId = kSensorId;
    p.commonProps.sensorStrength = SensorStrength::STRONG;
    p.commonProps.maxEnrollmentsPerUser = kMaxEnrollments;
    p.sensorType = sensorTypeFromProp();
    SensorLocation loc;
    p.sensorLocations = {loc};
    p.supportsNavigationGestures = false;
    p.supportsDetectInteraction = false;
    p.halHandlesDisplayTouches = false;
    p.halControlsIllumination = false;
    *out = {p};
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus FingerprintBridge::createSession(int32_t sensorId, int32_t userId,
                                                    const std::shared_ptr<ISessionCallback>& cb,
                                                    std::shared_ptr<ISession>* out) {
    LOG(INFO) << "createSession sensor=" << sensorId << " user=" << userId;
    if (sensorId != kSensorId || userId < 0 || cb == nullptr) {
        LOG(ERROR) << "createSession: rejected sensor=" << sensorId << " user=" << userId
                   << " callback=" << (cb != nullptr);
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    std::shared_ptr<SessionBridge> session;
    {
        std::lock_guard<std::mutex> l(lock_);
        if (auto prev = activeSession_.lock()) {
            LOG(ERROR) << "createSession: a previous session is still open (framework contract "
                          "violation) - its HIDL callback registration will be replaced";
        }
        auto& lockout = lockouts_[userId];
        if (!lockout) lockout = std::make_shared<LockoutTracker>(userId);
        session = ndk::SharedRefBase::make<SessionBridge>(conn_->get(), userId, cb, lockout);
        activeSession_ = session;
    }
    *out = session;
    return ndk::ScopedAStatus::ok();
}

void FingerprintBridge::onHidlStateChanged(bool available) {
    if (available) {
        LOG(INFO) << "HIDL biometrics.fingerprint@2.1 reconnected; new sessions will use the "
                     "fresh handle (an already-open session stays poisoned - see "
                     "SessionBridge::onHidlDied())";
        return;
    }
    std::shared_ptr<SessionBridge> session;
    {
        std::lock_guard<std::mutex> l(lock_);
        session = activeSession_.lock();
    }
    if (session) session->onHidlDied();
}

}
