/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/gnss/BnGnss.h>
#include <android/binder_ibinder.h>

#include "AGnss.h"
#include "AGnssRil.h"
#include "ExtensionFactory.h"
#include "GnssCallback.h"
#include "GnssAntennaInfo.h"
#include "GnssMeasurement.h"
#include "GnssLegacyExtensions.h"
#include "GnssMeasurementCorrections.h"
#include "GnssVisibilityControl.h"

namespace mindone::gnss {

class Gnss final : public A::BnGnss {
  public:
    explicit Gnss(::android::sp<H::V2_1::IGnss> backend);
    ~Gnss() override;
    ndk::ScopedAStatus setCallback(const std::shared_ptr<A::IGnssCallback>& callback) override;
    ndk::ScopedAStatus close() override;
    ndk::ScopedAStatus start() override;
    ndk::ScopedAStatus stop() override;
    ndk::ScopedAStatus startSvStatus() override;
    ndk::ScopedAStatus stopSvStatus() override;
    ndk::ScopedAStatus startNmea() override;
    ndk::ScopedAStatus stopNmea() override;
    ndk::ScopedAStatus injectTime(int64_t timeMs, int64_t referenceMs,
                                  int32_t uncertaintyMs) override;
    ndk::ScopedAStatus injectLocation(const A::GnssLocation& location) override;
    ndk::ScopedAStatus injectBestLocation(const A::GnssLocation& location) override;
    ndk::ScopedAStatus deleteAidingData(GnssAidingData flags) override;
    ndk::ScopedAStatus setPositionMode(const PositionModeOptions& options) override;

#define GNSS_EXTENSION(method, type, nullable)                          \
    ndk::ScopedAStatus method(std::shared_ptr<type>* result) override { \
        return makeExtension(mBackend, result, nullable);               \
    }
    GNSS_EXTENSION(getExtensionGnssConfiguration, A::IGnssConfiguration, false)
    GNSS_EXTENSION(getExtensionGnssBatching, A::IGnssBatching, true)
    GNSS_EXTENSION(getExtensionGnssGeofence, A::IGnssGeofence, true)
    GNSS_EXTENSION(getExtensionGnssDebug, A::IGnssDebug, false)
#undef GNSS_EXTENSION
    ndk::ScopedAStatus getExtensionMeasurementCorrections(
            std::shared_ptr<MC::IMeasurementCorrectionsInterface>* result) override {
        *result = mCorrections->available() ? mCorrections : nullptr;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getExtensionPsds(std::shared_ptr<A::IGnssPsds>* result) override {
        *result = mPsds->available() ? mPsds : nullptr;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getExtensionGnssNavigationMessage(
            std::shared_ptr<A::IGnssNavigationMessageInterface>* result) override {
        *result = mNavigation->available() ? mNavigation : nullptr;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getExtensionGnssPowerIndication(
            std::shared_ptr<A::IGnssPowerIndication>* result) override {
        *result = mPower;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getExtensionAGnss(std::shared_ptr<A::IAGnss>* result) override {
        *result = mAGnss;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getExtensionAGnssRil(std::shared_ptr<A::IAGnssRil>* result) override {
        *result = mAGnssRil;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getExtensionGnssMeasurement(
            std::shared_ptr<A::IGnssMeasurementInterface>* result) override {
        *result = mMeasurement;
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus getExtensionGnssVisibilityControl(
            std::shared_ptr<V::IGnssVisibilityControl>* result) override {
        *result = mVisibility;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getExtensionGnssAntennaInfo(
            std::shared_ptr<A::IGnssAntennaInfo>* result) override {
        *result = mAntenna;
        return ndk::ScopedAStatus::ok();
    }

  private:
    ndk::ScopedAStatus closeSession(bool wait, const void* cookie = nullptr);
    ndk::ScopedAStatus stream(int index, bool enabled);
    template <typename F>
    ndk::ScopedAStatus run(F operation) {
        std::unique_lock lock(mMutex, std::try_to_lock);
        if (!lock || !mBridge)
            return ndk::ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
        return operation();
    }
    const ::android::sp<H::V2_1::IGnss> mBackend;
    const std::shared_ptr<PowerIndication> mPower;
    const std::shared_ptr<GnssNavigationMessage> mNavigation;
    const std::shared_ptr<GnssPsds> mPsds;
    const std::shared_ptr<GnssMeasurementCorrections> mCorrections;
    const std::shared_ptr<GnssMeasurement> mMeasurement;
    const std::shared_ptr<AGnss> mAGnss;
    const std::shared_ptr<AGnssRil> mAGnssRil;
    const std::shared_ptr<GnssVisibilityControl> mVisibility;
    const std::shared_ptr<GnssAntennaInfo> mAntenna;
    std::mutex mMutex;
    ::android::sp<GnssCallback> mBridge;
    ndk::SpAIBinder mClient;
    ndk::ScopedAIBinder_DeathRecipient mDeath;
    std::weak_ptr<Gnss>* mCookie = nullptr;
    bool mStreams[3] = {};
};

}
