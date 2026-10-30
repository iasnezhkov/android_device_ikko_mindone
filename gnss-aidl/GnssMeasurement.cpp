/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "mindone-gnss-measurement"

#include "GnssMeasurement.h"

#include "CallbackSlot.h"

#include <aidl/android/hardware/gnss/IGnss.h>
#include <android/hardware/gnss/2.1/IGnssMeasurementCallback.h>
#include <log/log.h>

#include <condition_variable>
#include <limits>
#include <utility>

namespace mindone::gnss {

using ::android::hardware::Return;
using ::android::hardware::Void;
using C0 = H::V1_0::IGnssMeasurementCallback;
using C1 = H::V1_1::IGnssMeasurementCallback;
using C2 = H::V2_0::IGnssMeasurementCallback;
using C21 = H::V2_1::IGnssMeasurementCallback;

static ndk::ScopedAStatus error(const char* message, int32_t code = A::IGnss::ERROR_GENERIC) {
    return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(code, message);
}

static A::GnssData convert(const C21::GnssData& data) {
    A::GnssData result;
    const auto& clock = data.clock.v1_0;
    result.clock.gnssClockFlags = clock.gnssClockFlags;
    result.clock.leapSecond = clock.leapSecond;
    result.clock.timeNs = clock.timeNs;
    result.clock.timeUncertaintyNs = clock.timeUncertaintyNs;
    result.clock.fullBiasNs = clock.fullBiasNs;
    result.clock.biasNs = clock.biasNs;
    result.clock.biasUncertaintyNs = clock.biasUncertaintyNs;
    result.clock.driftNsps = clock.driftNsps;
    result.clock.driftUncertaintyNsps = clock.driftUncertaintyNsps;
    result.clock.hwClockDiscontinuityCount = static_cast<int32_t>(clock.hwClockDiscontinuityCount);
    const auto& signal = data.clock.referenceSignalTypeForIsb;
    result.clock.referenceSignalTypeForIsb.constellation =
            static_cast<A::GnssConstellationType>(signal.constellation);
    result.clock.referenceSignalTypeForIsb.carrierFrequencyHz = signal.carrierFrequencyHz;
    result.clock.referenceSignalTypeForIsb.codeType.assign(signal.codeType.c_str(),
                                                           signal.codeType.size());
    result.elapsedRealtime.flags = data.elapsedRealtime.flags;
    if (data.elapsedRealtime.timestampNs <=
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        result.elapsedRealtime.timestampNs = data.elapsedRealtime.timestampNs;
    } else {
        result.elapsedRealtime.flags &= ~A::ElapsedRealtime::HAS_TIMESTAMP_NS;
    }
    result.elapsedRealtime.timeUncertaintyNs = data.elapsedRealtime.timeUncertaintyNs;
    result.measurements.reserve(data.measurements.size());
    for (const auto& source : data.measurements) {
        const auto& legacy = source.v2_0.v1_1.v1_0;
        A::GnssMeasurement measurement;
        using M = A::GnssMeasurement;
        measurement.flags =
                source.flags &
                (M::HAS_SNR | M::HAS_CARRIER_FREQUENCY | M::HAS_CARRIER_CYCLES |
                 M::HAS_CARRIER_PHASE | M::HAS_CARRIER_PHASE_UNCERTAINTY |
                 M::HAS_AUTOMATIC_GAIN_CONTROL | M::HAS_FULL_ISB | M::HAS_FULL_ISB_UNCERTAINTY |
                 M::HAS_SATELLITE_ISB | M::HAS_SATELLITE_ISB_UNCERTAINTY);
        measurement.svid = legacy.svid;
        measurement.signalType.constellation =
                static_cast<A::GnssConstellationType>(source.v2_0.constellation);
        measurement.signalType.carrierFrequencyHz = legacy.carrierFrequencyHz;
        measurement.signalType.codeType.assign(source.v2_0.codeType.c_str(),
                                               source.v2_0.codeType.size());
        measurement.timeOffsetNs = legacy.timeOffsetNs;
        measurement.state = source.v2_0.state;
        measurement.receivedSvTimeInNs = legacy.receivedSvTimeInNs;
        measurement.receivedSvTimeUncertaintyInNs = legacy.receivedSvTimeUncertaintyInNs;
        measurement.antennaCN0DbHz = legacy.cN0DbHz;
        measurement.basebandCN0DbHz = source.basebandCN0DbHz;
        measurement.pseudorangeRateMps = legacy.pseudorangeRateMps;
        measurement.pseudorangeRateUncertaintyMps = legacy.pseudorangeRateUncertaintyMps;
        measurement.accumulatedDeltaRangeState = source.v2_0.v1_1.accumulatedDeltaRangeState;
        measurement.accumulatedDeltaRangeM = legacy.accumulatedDeltaRangeM;
        measurement.accumulatedDeltaRangeUncertaintyM = legacy.accumulatedDeltaRangeUncertaintyM;
        measurement.carrierCycles = legacy.carrierCycles;
        measurement.carrierPhase = legacy.carrierPhase;
        measurement.carrierPhaseUncertainty = legacy.carrierPhaseUncertainty;
        measurement.multipathIndicator =
                static_cast<A::GnssMultipathIndicator>(legacy.multipathIndicator);
        measurement.snrDb = legacy.snrDb;
        measurement.agcLevelDb = legacy.agcLevelDb;
        measurement.fullInterSignalBiasNs = source.fullInterSignalBiasNs;
        measurement.fullInterSignalBiasUncertaintyNs = source.fullInterSignalBiasUncertaintyNs;
        measurement.satelliteInterSignalBiasNs = source.satelliteInterSignalBiasNs;
        measurement.satelliteInterSignalBiasUncertaintyNs =
                source.satelliteInterSignalBiasUncertaintyNs;
        result.measurements.push_back(std::move(measurement));
    }
    return result;
}

class MeasurementCallback final : public C21 {
  public:
    explicit MeasurementCallback(std::shared_ptr<A::IGnssMeasurementCallback> callback)
        : mCallback(std::move(callback)) {}
    static bool isDispatching() { return sDepth != 0; }
    void invalidate() {
        std::unique_lock lock(mMutex);
        mCallback.reset();
        mDrained.wait(lock, [&] { return mInFlight == 0; });
    }
    Return<void> gnssMeasurementCb_2_1(const C21::GnssData& data) override {
        ::android::sp<MeasurementCallback> keepAlive(this);
        std::shared_ptr<A::IGnssMeasurementCallback> callback;
        {
            std::lock_guard lock(mMutex);
            if (!mCallback)
                return Void();
            callback = mCallback;
            ++mInFlight;
            ++callbacksInFlight;
        }
        ++sDepth;
        callback->gnssMeasurementCb(convert(data));
        --sDepth;
        {
            std::lock_guard lock(mMutex);
            --mInFlight;
            --callbacksInFlight;
            mDrained.notify_all();
        }
        return Void();
    }
    Return<void> gnssMeasurementCb_2_0(const C2::GnssData&) override { return legacyCallback(); }
    Return<void> gnssMeasurementCb(const C1::GnssData&) override { return legacyCallback(); }
    Return<void> GnssMeasurementCb(const C0::GnssData&) override { return legacyCallback(); }

  private:
    static Return<void> legacyCallback() {
        static std::once_flag warning;
        std::call_once(warning, [] {
            ALOGE("Expected HIDL 2.1 measurements; legacy payload lacks baseband C/N0");
        });
        return Void();
    }
    static thread_local size_t sDepth;
    std::mutex mMutex;
    std::condition_variable mDrained;
    size_t mInFlight = 0;
    std::shared_ptr<A::IGnssMeasurementCallback> mCallback;
};

thread_local size_t MeasurementCallback::sDepth = 0;
bool GnssMeasurement::isDispatching() { return MeasurementCallback::isDispatching(); }

GnssMeasurement::GnssMeasurement(const ::android::sp<H::V2_1::IGnss>& backend)
    : mDeath(AIBinder_DeathRecipient_new([](void* cookie) {
          if (auto owner = static_cast<std::weak_ptr<GnssMeasurement>*>(cookie)->lock()) {
              owner->closeSession(true, cookie);
          }
      })) {
    auto extension = backend->getExtensionGnssMeasurement_2_1();
    if (extension.isOk())
        mBackend = extension;
    AIBinder_DeathRecipient_setOnUnlinked(mDeath.get(), [](void* cookie) {
        delete static_cast<std::weak_ptr<GnssMeasurement>*>(cookie);
    });
}

GnssMeasurement::~GnssMeasurement() { shutdown(); }

ndk::ScopedAStatus GnssMeasurement::reset() {
    if (!mBridge)
        return ndk::ScopedAStatus::ok();
    mBridge->invalidate();
    mBridge.clear();
    auto result = mBackend->close();
    AIBinder_unlinkToDeath(mClient.get(), mDeath.get(), mCookie);
    mCookie = nullptr;
    mClient = ndk::SpAIBinder();
    return result.isOk() ? ndk::ScopedAStatus::ok() : error("HIDL measurement close failed");
}

ndk::ScopedAStatus GnssMeasurement::closeSession(bool wait, const void* cookie) {
    if ((!wait && callbacksInFlight) || MeasurementCallback::isDispatching())
        return error("Reentrant measurement close");
    std::unique_lock lock(mMutex, std::defer_lock);
    if (wait)
        lock.lock();
    else if (!lock.try_lock())
        return error("Measurement operation busy");
    if (cookie && cookie != mCookie)
        return ndk::ScopedAStatus::ok();
    return reset();
}

ndk::ScopedAStatus GnssMeasurement::shutdown() { return closeSession(true); }
ndk::ScopedAStatus GnssMeasurement::close() { return closeSession(false); }

ndk::ScopedAStatus GnssMeasurement::setCallback(
        const std::shared_ptr<A::IGnssMeasurementCallback>& callback, bool fullTracking,
        bool correlationVectors) {
    Options options;
    options.enableFullTracking = fullTracking;
    options.enableCorrVecOutputs = correlationVectors;
    options.intervalMs = 1000;
    return setCallbackWithOptions(callback, options);
}

ndk::ScopedAStatus GnssMeasurement::setCallbackWithOptions(
        const std::shared_ptr<A::IGnssMeasurementCallback>& callback, const Options& options) {
    if (!callback || options.intervalMs < 0)
        return error("Invalid measurement options", A::IGnss::ERROR_INVALID_ARGUMENT);
    if (options.enableCorrVecOutputs || options.intervalMs != 1000) {
        return error("Bridge supports only 1000 ms intervals without correlation vectors");
    }
    if (!mBackend)
        return error("HIDL measurement extension unavailable");
    if (callbacksInFlight)
        return error("Reentrant measurement registration");
    std::unique_lock lock(mMutex, std::try_to_lock);
    if (!lock)
        return error("Measurement operation busy");
    auto closed = reset();
    if (!closed.isOk())
        return closed;
    mClient = callback->asBinder();
    auto* cookie = new std::weak_ptr<GnssMeasurement>(ref<GnssMeasurement>());
    if (AIBinder_linkToDeath(mClient.get(), mDeath.get(), cookie) != STATUS_OK) {
        mClient = ndk::SpAIBinder();
        return error("Cannot monitor measurement callback death");
    }
    mCookie = cookie;
    mBridge = new MeasurementCallback(callback);
    auto result = mBackend->setCallback_2_1(mBridge, options.enableFullTracking);
    using S = H::V1_0::IGnssMeasurement::GnssMeasurementStatus;
    if (result.isOk() && static_cast<S>(result) == S::SUCCESS)
        return ndk::ScopedAStatus::ok();
    int32_t code = result.isOk() && static_cast<S>(result) == S::ERROR_ALREADY_INIT
                           ? A::IGnss::ERROR_ALREADY_INIT
                           : A::IGnss::ERROR_GENERIC;
    auto cleanup = reset();
    static_cast<void>(cleanup.isOk());
    return error("HIDL measurement registration failed", code);
}

}
