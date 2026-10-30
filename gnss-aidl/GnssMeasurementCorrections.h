/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/gnss/measurement_corrections/BnMeasurementCorrectionsInterface.h>
#include <aidl/android/hardware/gnss/measurement_corrections/IMeasurementCorrectionsCallback.h>
#include <android/hardware/gnss/measurement_corrections/1.1/IMeasurementCorrections.h>
#include <cmath>

#include "GnssLegacyExtensions.h"

namespace mindone::gnss {

namespace MC = A::measurement_corrections;
namespace HC = H::measurement_corrections;

class CorrectionsCallback final : public HC::V1_0::IMeasurementCorrectionsCallback {
  public:
    CorrectionsCallback(std::shared_ptr<CallbackSlot<MC::IMeasurementCorrectionsCallback>> slot,
                        std::shared_ptr<std::atomic<int32_t>> capabilities)
        : mSlot(std::move(slot)), mCapabilities(std::move(capabilities)) {}
    ::android::hardware::Return<void> setCapabilitiesCb(uint32_t capabilities) override {
        if (auto callback = mSlot->get()) {
            if ((capabilities & ~7u) || !(capabilities & 3u)) {
                *mCapabilities = -1;
                return ::android::hardware::Void();
            }
            *mCapabilities = capabilities;
            auto result = callback->setCapabilitiesCb(capabilities);
            if (!result.isOk()) *mCapabilities = -1;
        }
        return ::android::hardware::Void();
    }
  private:
    const std::shared_ptr<CallbackSlot<MC::IMeasurementCorrectionsCallback>> mSlot;
    const std::shared_ptr<std::atomic<int32_t>> mCapabilities;
};

class GnssMeasurementCorrections final : public MC::BnMeasurementCorrectionsInterface {
  public:
    explicit GnssMeasurementCorrections(const ::android::sp<H::V2_1::IGnss>& backend) {
        if (!backend) return;
        auto result = backend->getExtensionMeasurementCorrections_1_1();
        if (result.isOk()) mBackend = result;
    }
    ~GnssMeasurementCorrections() override {
        auto result = shutdown();
        static_cast<void>(result.isOk());
    }
    bool available() const { return mBackend != nullptr; }
    void open() { std::lock_guard lock(mMutex); mEnabled = true; }
    ndk::ScopedAStatus shutdown(const void* cookie = nullptr) {
        std::lock_guard lock(mMutex);
        if (cookie && !mClient.current(cookie)) return ndk::ScopedAStatus::ok();
        if (!cookie) mEnabled = false;
        reset();
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus setCallback(
            const std::shared_ptr<MC::IMeasurementCorrectionsCallback>& callback) override {
        if (!callback) return extensionError(A::IGnss::ERROR_INVALID_ARGUMENT);
        if (callbacksInFlight) return extensionError();
        std::unique_lock lock(mMutex, std::try_to_lock);
        if (!lock || !mEnabled || !mBackend) return extensionError();
        reset();
        if (!mClient.attach(callback, ref<GnssMeasurementCorrections>())) return extensionError();
        mCallback = std::make_shared<CallbackSlot<MC::IMeasurementCorrectionsCallback>>(callback);
        mCapabilities = std::make_shared<std::atomic<int32_t>>(0);
        ::android::sp<CorrectionsCallback> bridge = new CorrectionsCallback(mCallback, mCapabilities);
        auto result = extensionStatus(mBackend->setCallback(bridge));
        if (*mCapabilities < 0) result = unsupported();
        if (!result.isOk()) reset();
        return result;
    }
    ndk::ScopedAStatus setCorrections(const MC::MeasurementCorrections& source) override {
        if (callbacksInFlight) return extensionError();
        std::unique_lock lock(mMutex, std::try_to_lock);
        if (!lock || !mEnabled || !mBackend || !mCallback) return extensionError();
        const int32_t capabilities = mCapabilities->load();
        if (capabilities < 0) return unsupported();
        if (!capabilities) return extensionError();
        if (source.satCorrections.size() > 195) return unsupported();
        if (!position(source.latitudeDegrees, source.longitudeDegrees, source.altitudeMeters) ||
            !nonnegative(source.horizontalPositionUncertaintyMeters) ||
            !nonnegative(source.verticalPositionUncertaintyMeters) ||
            source.toaGpsNanosecondsOfWeek < 0 ||
            source.toaGpsNanosecondsOfWeek >= 604800000000000LL) return invalid();
        if (source.hasEnvironmentBearing &&
            (!range(source.environmentBearingDegrees, 0, 360) ||
             !range(source.environmentBearingUncertaintyDegrees, 0, 180))) return invalid();
        HC::V1_1::MeasurementCorrections result{};
        result.v1_0.latitudeDegrees = source.latitudeDegrees;
        result.v1_0.longitudeDegrees = source.longitudeDegrees;
        result.v1_0.altitudeMeters = source.altitudeMeters;
        result.v1_0.horizontalPositionUncertaintyMeters = source.horizontalPositionUncertaintyMeters;
        result.v1_0.verticalPositionUncertaintyMeters = source.verticalPositionUncertaintyMeters;
        result.v1_0.toaGpsNanosecondsOfWeek = source.toaGpsNanosecondsOfWeek;
        result.hasEnvironmentBearing = source.hasEnvironmentBearing;
        if (source.hasEnvironmentBearing) {
            result.environmentBearingDegrees = source.environmentBearingDegrees == 0
                                                       ? 360 : source.environmentBearingDegrees;
            result.environmentBearingUncertaintyDegrees = source.environmentBearingUncertaintyDegrees;
        }
        result.satCorrections.resize(source.satCorrections.size());
        for (size_t i = 0; i < source.satCorrections.size(); ++i) {
            auto mapped = satellite(source.satCorrections[i], capabilities, result.satCorrections[i]);
            if (!mapped.isOk()) return mapped;
        }
        return extensionStatus(mBackend->setCorrections_1_1(result));
    }
  private:
    static ndk::ScopedAStatus unsupported() {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    static ndk::ScopedAStatus invalid() { return extensionError(A::IGnss::ERROR_INVALID_ARGUMENT); }
    static bool range(double value, double low, double high) {
        return std::isfinite(value) && value >= low && value <= high;
    }
    static bool nonnegative(double value) { return std::isfinite(value) && value >= 0; }
    static bool position(double latitude, double longitude, double altitude) {
        return range(latitude, -90, 90) && range(longitude, -180, 180) && std::isfinite(altitude);
    }
    static bool sdkSvid(int32_t constellation, int32_t svid) {
        switch (constellation) {
            case 1: return svid >= 1 && svid <= 32;
            case 2: return (svid >= 120 && svid <= 151) || (svid >= 183 && svid <= 192);
            case 3: return (svid >= 1 && svid <= 25) || (svid >= 93 && svid <= 106);
            case 4: return svid >= 183 && svid <= 206;
            case 5: return svid >= 1 && svid <= 63;
            case 6: return svid >= 1 && svid <= 36;
            case 7: return svid >= 1 && svid <= 14;
            default: return false;
        }
    }
    static ndk::ScopedAStatus satellite(const MC::SingleSatCorrection& source, int32_t capabilities,
                                        HC::V1_1::SingleSatCorrection& result) {
        const int32_t constellation = static_cast<int32_t>(source.constellation);
        const int32_t flags = source.singleSatCorrectionFlags;
        if (source.svid < 1 || source.svid > UINT16_MAX || source.carrierFrequencyHz <= 0)
            return invalid();
        if (constellation < 1 || constellation > 7) return unsupported();
        if (!sdkSvid(constellation, source.svid)) return invalid();
        if ((flags & ~7) || source.excessPathInfos.size() > 1) return unsupported();
        const float frequency = static_cast<float>(source.carrierFrequencyHz);
        if (frequency >= 0x1p63f || static_cast<int64_t>(frequency) != source.carrierFrequencyHz)
            return unsupported();
        auto& legacy = result.v1_0;
        result.constellation = static_cast<H::V2_0::GnssConstellationType>(constellation);
        legacy.svid = static_cast<uint16_t>(source.svid);
        legacy.carrierFrequencyHz = frequency;
        legacy.singleSatCorrectionFlags = flags;
        if (flags & 1) {
            if (!range(source.probSatIsLos, 0, 1)) return invalid();
            legacy.probSatIsLos = source.probSatIsLos;
        }
        if (flags & 2) {
            if (!nonnegative(source.combinedExcessPathLengthMeters)) return invalid();
            legacy.excessPathLengthMeters = source.combinedExcessPathLengthMeters;
        }
        if (flags & 4) {
            if (!nonnegative(source.combinedExcessPathLengthUncertaintyMeters)) return invalid();
            legacy.excessPathLengthUncertaintyMeters = source.combinedExcessPathLengthUncertaintyMeters;
        }
        if (!source.excessPathInfos.empty()) {
            const auto& path = source.excessPathInfos.front();
            const int32_t pathFlags = path.excessPathInfoFlags;
            if (pathFlags & ~7) return unsupported();
            if (pathFlags & 1) {
                if (!nonnegative(path.excessPathLengthMeters)) return invalid();
                if ((flags & 2) && legacy.excessPathLengthMeters != path.excessPathLengthMeters)
                    return unsupported();
                legacy.singleSatCorrectionFlags |= 2;
                legacy.excessPathLengthMeters = path.excessPathLengthMeters;
            }
            if (pathFlags & 2) {
                if (!nonnegative(path.excessPathLengthUncertaintyMeters)) return invalid();
                if ((flags & 4) && legacy.excessPathLengthUncertaintyMeters !=
                                      path.excessPathLengthUncertaintyMeters) return unsupported();
                legacy.singleSatCorrectionFlags |= 4;
                legacy.excessPathLengthUncertaintyMeters = path.excessPathLengthUncertaintyMeters;
            }
            if (pathFlags & 4) {
                const auto& plane = path.reflectingPlane;
                if (!position(plane.latitudeDegrees, plane.longitudeDegrees, plane.altitudeMeters) ||
                    !range(plane.reflectingPlaneAzimuthDegrees, 0, 360)) return invalid();
                legacy.singleSatCorrectionFlags |= 8;
                legacy.reflectingPlane.latitudeDegrees = plane.latitudeDegrees;
                legacy.reflectingPlane.longitudeDegrees = plane.longitudeDegrees;
                legacy.reflectingPlane.altitudeMeters = plane.altitudeMeters;
                legacy.reflectingPlane.azimuthDegrees = plane.reflectingPlaneAzimuthDegrees;
            }
        }
        const auto mappedFlags = legacy.singleSatCorrectionFlags;
        if (((mappedFlags & 1) && !(capabilities & 1)) ||
            ((mappedFlags & 6) && !(capabilities & 2)) ||
            ((mappedFlags & 8) && !(capabilities & 4))) return unsupported();
        return ndk::ScopedAStatus::ok();
    }
    void reset() {
        if (mCallback) mCallback->invalidate();
        mCallback.reset();
        mCapabilities.reset();
        mClient.reset();
    }
    std::mutex mMutex;
    ::android::sp<HC::V1_1::IMeasurementCorrections> mBackend;
    std::shared_ptr<CallbackSlot<MC::IMeasurementCorrectionsCallback>> mCallback;
    std::shared_ptr<std::atomic<int32_t>> mCapabilities;
    LegacyClient<GnssMeasurementCorrections> mClient;
    bool mEnabled = false;
};

}
