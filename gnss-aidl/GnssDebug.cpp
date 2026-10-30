/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "GnssDebug.h"

#include <aidl/android/hardware/gnss/IGnss.h>
#include <aidl/android/hardware/gnss/SatellitePvt.h>

#include <utility>

#include "ExtensionFactory.h"

namespace mindone::gnss {

GnssDebug::GnssDebug(::android::sp<H::V2_0::IGnssDebug> backend) : mBackend(std::move(backend)) {}

ndk::ScopedAStatus GnssDebug::getDebugData(DebugData* result) {
    *result = {};
    if (!mBackend) {
        return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
                A::IGnss::ERROR_GENERIC, "HIDL GNSS debug unavailable");
    }
    DebugData converted;
    bool received = false;
    auto status = mBackend->getDebugData_2_0([&](const auto& data) {
        converted.position.valid = data.position.valid;
        converted.position.latitudeDegrees = data.position.latitudeDegrees;
        converted.position.longitudeDegrees = data.position.longitudeDegrees;
        converted.position.altitudeMeters = data.position.altitudeMeters;
        converted.position.speedMetersPerSec = data.position.speedMetersPerSec;
        converted.position.bearingDegrees = data.position.bearingDegrees;
        converted.position.horizontalAccuracyMeters = data.position.horizontalAccuracyMeters;
        converted.position.verticalAccuracyMeters = data.position.verticalAccuracyMeters;
        converted.position.speedAccuracyMetersPerSecond =
                data.position.speedAccuracyMetersPerSecond;
        converted.position.bearingAccuracyDegrees = data.position.bearingAccuracyDegrees;
        converted.position.ageSeconds = data.position.ageSeconds;
        converted.time.timeEstimateMs = data.time.timeEstimate;
        converted.time.timeUncertaintyNs = data.time.timeUncertaintyNs;
        converted.time.frequencyUncertaintyNsPerSec = data.time.frequencyUncertaintyNsPerSec;
        converted.satelliteDataArray.clear();
        converted.satelliteDataArray.reserve(data.satelliteDataArray.size());
        for (const auto& satellite : data.satelliteDataArray) {
            const auto& legacy = satellite.v1_0;
            SatelliteData entry;
            entry.svid = legacy.svid;
            entry.constellation = static_cast<A::GnssConstellationType>(satellite.constellation);
            entry.ephemerisType = static_cast<SatelliteEphemerisType>(legacy.ephemerisType);
            entry.ephemerisHealth = static_cast<SatelliteEphemerisHealth>(legacy.ephemerisHealth);
            using AS = A::SatellitePvt::SatelliteEphemerisSource;
            using HS = H::V1_0::IGnssDebug::SatelliteEphemerisSource;
            entry.ephemerisSource = AS::OTHER;
            if (legacy.ephemerisSource == HS::DEMODULATED) {
                entry.ephemerisSource = AS::DEMODULATED;
            } else if (legacy.ephemerisSource == HS::SUPL_PROVIDED) {
                entry.ephemerisSource = AS::SERVER_NORMAL;
            }
            entry.ephemerisAgeSeconds = legacy.ephemerisAgeSeconds;
            entry.serverPredictionIsAvailable = legacy.serverPredictionIsAvailable;
            entry.serverPredictionAgeSeconds = legacy.serverPredictionAgeSeconds;
            converted.satelliteDataArray.push_back(entry);
        }
        received = true;
    });
    if (!status.isOk() || !received) {
        return ndk::ScopedAStatus::fromServiceSpecificError(A::IGnss::ERROR_GENERIC);
    }
    *result = std::move(converted);
    return ndk::ScopedAStatus::ok();
}

template <>
ndk::ScopedAStatus makeExtension(const ::android::sp<H::V2_1::IGnss>& backend,
                                 std::shared_ptr<A::IGnssDebug>* result, bool) {
    result->reset();
    auto extension = backend->getExtensionGnssDebug_2_0();
    if (!extension.isOk()) {
        return ndk::ScopedAStatus::fromServiceSpecificError(A::IGnss::ERROR_GENERIC);
    }
    ::android::sp<H::V2_0::IGnssDebug> implementation = extension;
    *result = ndk::SharedRefBase::make<GnssDebug>(implementation);
    return ndk::ScopedAStatus::ok();
}

}
