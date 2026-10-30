/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Gnss.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace mindone::gnss {

static ndk::ScopedAStatus status(const Return<bool>& result) {
    return result.isOk() && static_cast<bool>(result)
                   ? ndk::ScopedAStatus::ok()
                   : ndk::ScopedAStatus::fromServiceSpecificError(A::IGnss::ERROR_GENERIC);
}

static bool validLocation(const A::GnssLocation& location) {
    return std::isfinite(location.latitudeDegrees) && std::isfinite(location.longitudeDegrees) &&
           std::abs(location.latitudeDegrees) <= 90 && std::abs(location.longitudeDegrees) <= 180 &&
           std::isfinite(location.horizontalAccuracyMeters) &&
           location.horizontalAccuracyMeters >= 0;
}

Gnss::Gnss(::android::sp<H::V2_1::IGnss> backend)
    : mBackend(std::move(backend)),
      mPower(ndk::SharedRefBase::make<PowerIndication>()),
      mNavigation(ndk::SharedRefBase::make<GnssNavigationMessage>(mBackend)),
      mPsds(ndk::SharedRefBase::make<GnssPsds>(mBackend)),
      mCorrections(ndk::SharedRefBase::make<GnssMeasurementCorrections>(mBackend)),
      mMeasurement(ndk::SharedRefBase::make<GnssMeasurement>(mBackend)),
      mAGnss(ndk::SharedRefBase::make<AGnss>(mBackend)),
      mAGnssRil(ndk::SharedRefBase::make<AGnssRil>(mBackend)),
      mVisibility(ndk::SharedRefBase::make<GnssVisibilityControl>(mBackend)),
      mAntenna(ndk::SharedRefBase::make<GnssAntennaInfo>(mBackend)),
      mDeath(AIBinder_DeathRecipient_new([](void* cookie) {
          if (auto owner = static_cast<std::weak_ptr<Gnss>*>(cookie)->lock())
              owner->closeSession(true, cookie);
      })) {
    AIBinder_DeathRecipient_setOnUnlinked(
            mDeath.get(), [](void* cookie) { delete static_cast<std::weak_ptr<Gnss>*>(cookie); });
}

Gnss::~Gnss() { closeSession(true); }

ndk::ScopedAStatus Gnss::setCallback(const std::shared_ptr<A::IGnssCallback>& callback) {
    if (!callback)
        return ndk::ScopedAStatus::fromServiceSpecificError(ERROR_INVALID_ARGUMENT);
    if (callbacksInFlight)
        return extensionError();
    std::unique_lock lock(mMutex, std::try_to_lock);
    if (!lock)
        return ndk::ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
    if (mBridge)
        return ndk::ScopedAStatus::fromServiceSpecificError(ERROR_ALREADY_INIT);
    mClient = callback->asBinder();
    auto* cookie = new std::weak_ptr<Gnss>(ref<Gnss>());
    if (AIBinder_linkToDeath(mClient.get(), mDeath.get(), cookie) != STATUS_OK) {
        mClient = ndk::SpAIBinder();
        return ndk::ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
    }
    mCookie = cookie;
    int32_t capabilities = A::IGnssCallback::CAPABILITY_SCHEDULING |
                           A::IGnssCallback::CAPABILITY_SINGLE_SHOT |
                           A::IGnssCallback::CAPABILITY_ON_DEMAND_TIME |
                           A::IGnssCallback::CAPABILITY_LOW_POWER_MODE;
    if (mAGnss->available() && mAGnssRil->available()) {
        capabilities |= A::IGnssCallback::CAPABILITY_MSB | A::IGnssCallback::CAPABILITY_MSA;
    }
    if (mMeasurement->available())
        capabilities |= A::IGnssCallback::CAPABILITY_MEASUREMENTS;
    if (mCorrections->available())
        capabilities |= A::IGnssCallback::CAPABILITY_MEASUREMENT_CORRECTIONS;
    if (mNavigation->available())
        capabilities |= A::IGnssCallback::CAPABILITY_NAV_MESSAGES;
    if (mAntenna->available())
        capabilities |= A::IGnssCallback::CAPABILITY_ANTENNA_INFO;
    mBridge = new GnssCallback(callback, capabilities);
    auto result = status(mBackend->setCallback_2_1(mBridge));
    if (!result.isOk()) {
        mPower->shutdown();
        auto navigation = mNavigation->shutdown();
        auto psds = mPsds->shutdown();
        auto corrections = mCorrections->shutdown();
        static_cast<void>(corrections.isOk());
        static_cast<void>(navigation.isOk());
        static_cast<void>(psds.isOk());
        mVisibility->shutdown();
        mAntenna->shutdown();
        mAGnss->shutdown();
        mAGnssRil->shutdown();
        auto measurement = mMeasurement->shutdown();
        static_cast<void>(measurement.isOk());
        mBridge->invalidate();
        mBridge.clear();
        auto cleanup = mBackend->cleanup();
        static_cast<void>(cleanup.isOk());
        AIBinder_unlinkToDeath(mClient.get(), mDeath.get(), mCookie);
        mCookie = nullptr;
        mClient = ndk::SpAIBinder();
    } else {
        mPower->open();
        mNavigation->open();
        mPsds->open();
        mCorrections->open();
    }
    return result;
}

ndk::ScopedAStatus Gnss::closeSession(bool wait, const void* cookie) {
    if (!wait && (GnssCallback::isDispatching() || GnssMeasurement::isDispatching() ||
                  callbacksInFlight))
        return ndk::ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
    std::unique_lock lock(mMutex, std::defer_lock);
    if (wait)
        lock.lock();
    else if (!lock.try_lock())
        return ndk::ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
    if (cookie && cookie != mCookie)
        return ndk::ScopedAStatus::ok();
    auto measurement = mMeasurement->shutdown();
    mPower->shutdown();
    auto navigation = mNavigation->shutdown();
    auto psds = mPsds->shutdown();
    auto corrections = mCorrections->shutdown();
    static_cast<void>(corrections.isOk());
    static_cast<void>(psds.isOk());
    mVisibility->shutdown();
    mAntenna->shutdown();
    mAGnss->shutdown();
    mAGnssRil->shutdown();
    if (!mBridge)
        return measurement.isOk() ? std::move(navigation) : std::move(measurement);
    mBridge->invalidate();
    mBridge.clear();
    auto result = status(mBackend->stop());
    auto cleanup = mBackend->cleanup();
    if (!cleanup.isOk())
        result = ndk::ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
    std::fill(std::begin(mStreams), std::end(mStreams), false);
    AIBinder_unlinkToDeath(mClient.get(), mDeath.get(), mCookie);
    mCookie = nullptr;
    mClient = ndk::SpAIBinder();
    if (!measurement.isOk())
        return measurement;
    if (!navigation.isOk())
        return navigation;
    return result;
}

ndk::ScopedAStatus Gnss::close() { return closeSession(false); }

ndk::ScopedAStatus Gnss::stream(int index, bool enabled) {
    return run([&] {
        bool wasRunning = mStreams[0];
        bool running = index == 0 ? enabled : wasRunning;
        mBridge->setLocationEnabled(index == 0 ? enabled : mStreams[0]);
        mBridge->setSvStatusEnabled(index == 1 ? enabled : mStreams[1]);
        mBridge->setNmeaEnabled(index == 2 ? enabled : mStreams[2]);
        if (running != wasRunning) {
            auto result = status(running ? mBackend->start() : mBackend->stop());
            if (!result.isOk()) {
                mBridge->setLocationEnabled(mStreams[0]);
                mBridge->setSvStatusEnabled(mStreams[1]);
                mBridge->setNmeaEnabled(mStreams[2]);
                return result;
            }
        }
        mStreams[index] = enabled;
        return ndk::ScopedAStatus::ok();
    });
}

ndk::ScopedAStatus Gnss::start() { return stream(0, true); }
ndk::ScopedAStatus Gnss::stop() { return stream(0, false); }
ndk::ScopedAStatus Gnss::startSvStatus() { return stream(1, true); }
ndk::ScopedAStatus Gnss::stopSvStatus() { return stream(1, false); }
ndk::ScopedAStatus Gnss::startNmea() { return stream(2, true); }
ndk::ScopedAStatus Gnss::stopNmea() { return stream(2, false); }

ndk::ScopedAStatus Gnss::injectTime(int64_t timeMs, int64_t referenceMs, int32_t uncertaintyMs) {
    if (uncertaintyMs < 0)
        return ndk::ScopedAStatus::fromServiceSpecificError(ERROR_INVALID_ARGUMENT);
    return run([&] { return status(mBackend->injectTime(timeMs, referenceMs, uncertaintyMs)); });
}

ndk::ScopedAStatus Gnss::injectLocation(const A::GnssLocation& location) {
    if (!validLocation(location))
        return ndk::ScopedAStatus::fromServiceSpecificError(ERROR_INVALID_ARGUMENT);
    return run([&] {
        return status(mBackend->injectLocation(location.latitudeDegrees, location.longitudeDegrees,
                                               location.horizontalAccuracyMeters));
    });
}

ndk::ScopedAStatus Gnss::injectBestLocation(const A::GnssLocation& location) {
    if (!validLocation(location) || location.elapsedRealtime.timestampNs < 0 ||
        !std::isfinite(location.elapsedRealtime.timeUncertaintyNs) ||
        location.elapsedRealtime.timeUncertaintyNs < 0 ||
        location.elapsedRealtime.timeUncertaintyNs >= 0x1p64) {
        return ndk::ScopedAStatus::fromServiceSpecificError(ERROR_INVALID_ARGUMENT);
    }
    H::V2_0::GnssLocation converted{};
    auto& legacy = converted.v1_0;
    legacy.gnssLocationFlags = location.gnssLocationFlags;
    legacy.latitudeDegrees = location.latitudeDegrees;
    legacy.longitudeDegrees = location.longitudeDegrees;
    legacy.altitudeMeters = location.altitudeMeters;
    legacy.speedMetersPerSec = location.speedMetersPerSec;
    legacy.bearingDegrees = location.bearingDegrees;
    legacy.horizontalAccuracyMeters = location.horizontalAccuracyMeters;
    legacy.verticalAccuracyMeters = location.verticalAccuracyMeters;
    legacy.speedAccuracyMetersPerSecond = location.speedAccuracyMetersPerSecond;
    legacy.bearingAccuracyDegrees = location.bearingAccuracyDegrees;
    legacy.timestamp = location.timestampMillis;
    converted.elapsedRealtime.flags = location.elapsedRealtime.flags;
    converted.elapsedRealtime.timestampNs = location.elapsedRealtime.timestampNs;
    converted.elapsedRealtime.timeUncertaintyNs = location.elapsedRealtime.timeUncertaintyNs;
    return run([&] { return status(mBackend->injectBestLocation_2_0(converted)); });
}

ndk::ScopedAStatus Gnss::deleteAidingData(GnssAidingData flags) {
    return run([&] {
        auto result =
                mBackend->deleteAidingData(static_cast<H::V1_0::IGnss::GnssAidingData>(flags));
        return result.isOk() ? ndk::ScopedAStatus::ok()
                             : ndk::ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
    });
}

ndk::ScopedAStatus Gnss::setPositionMode(const PositionModeOptions& options) {
    if (options.minIntervalMs < 0 || options.preferredAccuracyMeters < 0 ||
        options.preferredTimeMs < 0 || static_cast<int32_t>(options.mode) < 0 ||
        static_cast<int32_t>(options.mode) > 2 || static_cast<int32_t>(options.recurrence) < 0 ||
        static_cast<int32_t>(options.recurrence) > 1) {
        return ndk::ScopedAStatus::fromServiceSpecificError(ERROR_INVALID_ARGUMENT);
    }
    return run([&] {
        return status(mBackend->setPositionMode_1_1(
                static_cast<H::V1_0::IGnss::GnssPositionMode>(options.mode),
                static_cast<H::V1_0::IGnss::GnssPositionRecurrence>(options.recurrence),
                options.minIntervalMs, options.preferredAccuracyMeters, options.preferredTimeMs,
                options.lowPowerMode));
    });
}

}
