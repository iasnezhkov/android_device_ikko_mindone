/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
// mindone: AIDL IFingerprint (V4) over HIDL IBiometricsFingerprint@2.1.
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
    // ro.vendor.mindone.fp.type = rear|side|home|udfps (default rear); confirm against MindOne hardware
    const std::string t = ::android::base::GetProperty("ro.vendor.mindone.fp.type", "rear");
    if (t == "side") return FingerprintSensorType::POWER_BUTTON;
    if (t == "home") return FingerprintSensorType::HOME_BUTTON;
    if (t == "udfps") return FingerprintSensorType::UNDER_DISPLAY_OPTICAL;
    return FingerprintSensorType::REAR;
}
}  // namespace

FingerprintBridge::FingerprintBridge(std::shared_ptr<FingerprintHidlConn> conn) : conn_(std::move(conn)) {
    conn_->setStateCallback([this](bool available) { onHidlStateChanged(available); });
}

ndk::ScopedAStatus FingerprintBridge::getSensorProps(std::vector<SensorProps>* out) {
    SensorProps p;
    p.commonProps.sensorId = kSensorId;
    p.commonProps.sensorStrength = SensorStrength::STRONG;
    p.commonProps.maxEnrollmentsPerUser = kMaxEnrollments;
    p.sensorType = sensorTypeFromProp();
    SensorLocation loc;  // displayId int / display "" - default values (not UDFPS: coordinates are not needed)
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
    if (sensorId != kSensorId) {
        // getSensorProps() only ever advertises kSensorId, so a well-behaved framework never
        // asks for anything else - log loudly rather than silently humoring a bad sensorId.
        LOG(ERROR) << "createSession: unexpected sensorId " << sensorId << " (only " << kSensorId
                   << " is advertised by getSensorProps)";
    }

    std::shared_ptr<SessionBridge> session;
    {
        std::lock_guard<std::mutex> l(lock_);
        if (auto prev = activeSession_.lock()) {
            // IFingerprint.aidl: "Calling this method while there is an active session is
            // considered an error" - this is a framework-side contract, not something we can
            // reject synchronously (createSession has no failure return), but a stale previous
            // session silently losing its HIDL notify() registration used to be invisible. Log it.
            LOG(ERROR) << "createSession: a previous session is still open (framework contract "
                          "violation) - its HIDL callback registration will be replaced";
        }
        // conn_->get() may be nullptr if the HIDL HAL is down right now (mid-reconnect after a
        // crash) - SessionBridge tolerates that by starting in the "dead" state (see its ctor).
        session = ndk::SharedRefBase::make<SessionBridge>(conn_->get(), userId, cb);
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

}  // namespace mindone
