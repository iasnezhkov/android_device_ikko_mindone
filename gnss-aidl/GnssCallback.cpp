/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "GnssCallback.h"

#include "CallbackSlot.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace mindone::gnss {

using ::android::hardware::Void;
thread_local size_t GnssCallback::sDispatchDepth = 0;

A::GnssLocation toAidlLocation(const H::V2_0::GnssLocation& location) {
    const auto& legacy = location.v1_0;
    A::GnssLocation result;
    result.gnssLocationFlags = legacy.gnssLocationFlags;
    result.latitudeDegrees = legacy.latitudeDegrees;
    result.longitudeDegrees = legacy.longitudeDegrees;
    result.altitudeMeters = legacy.altitudeMeters;
    result.speedMetersPerSec = legacy.speedMetersPerSec;
    result.bearingDegrees = legacy.bearingDegrees;
    result.horizontalAccuracyMeters = legacy.horizontalAccuracyMeters;
    result.verticalAccuracyMeters = legacy.verticalAccuracyMeters;
    result.speedAccuracyMetersPerSecond = legacy.speedAccuracyMetersPerSecond;
    result.bearingAccuracyDegrees = legacy.bearingAccuracyDegrees;
    result.timestampMillis = legacy.timestamp;
    result.elapsedRealtime.flags = location.elapsedRealtime.flags;
    if (location.elapsedRealtime.timestampNs <= std::numeric_limits<int64_t>::max()) {
        result.elapsedRealtime.timestampNs = location.elapsedRealtime.timestampNs;
    } else {
        result.elapsedRealtime.flags &= ~A::ElapsedRealtime::HAS_TIMESTAMP_NS;
    }
    result.elapsedRealtime.timeUncertaintyNs = location.elapsedRealtime.timeUncertaintyNs;
    return result;
}

static A::IGnssCallback::GnssSvInfo toAidlSv(const H::V1_0::IGnssCallback::GnssSvInfo& legacy,
                                             uint8_t constellation, double basebandCN0DbHz) {
    A::IGnssCallback::GnssSvInfo result;
    result.svid = legacy.svid;
    result.constellation = static_cast<A::GnssConstellationType>(constellation);
    result.cN0Dbhz = legacy.cN0Dbhz;
    result.basebandCN0DbHz = basebandCN0DbHz;
    result.elevationDegrees = legacy.elevationDegrees;
    result.azimuthDegrees = legacy.azimuthDegrees;
    result.svFlag = legacy.svFlag;
    double frequency = legacy.carrierFrequencyHz;
    if (std::isfinite(frequency) && frequency >= 0 &&
        frequency < static_cast<double>(std::numeric_limits<int64_t>::max())) {
        result.carrierFrequencyHz = static_cast<int64_t>(frequency);
    } else {
        result.svFlag &=
                ~static_cast<int32_t>(A::IGnssCallback::GnssSvFlags::HAS_CARRIER_FREQUENCY);
    }
    return result;
}

GnssCallback::GnssCallback(std::shared_ptr<A::IGnssCallback> callback,
                           int32_t supportedCapabilities)
    : mCallback(std::move(callback)), mSupportedCapabilities(supportedCapabilities) {}

std::shared_ptr<A::IGnssCallback> GnssCallback::callback() {
    std::lock_guard lock(mMutex);
    if (!mCallback)
        return nullptr;
    ++mInFlight;
    ++callbacksInFlight;
    ++sDispatchDepth;
    return std::shared_ptr<A::IGnssCallback>(
            mCallback.get(),
            [target = mCallback, owner = ::android::sp<GnssCallback>(this)](auto*) {
                static_cast<void>(target);
                std::lock_guard lock(owner->mMutex);
                --owner->mInFlight;
                --callbacksInFlight;
                --sDispatchDepth;
                owner->mDrained.notify_all();
            });
}

void GnssCallback::invalidate() {
    std::unique_lock lock(mMutex);
    mCallback.reset();
    mLocationEnabled = false;
    mSvStatusEnabled = false;
    mNmeaEnabled = false;
    mDrained.wait(lock, [this] { return mInFlight == 0; });
}

void GnssCallback::setLocationEnabled(bool enabled) { mLocationEnabled = enabled; }

void GnssCallback::setSvStatusEnabled(bool enabled) { mSvStatusEnabled = enabled; }

void GnssCallback::setNmeaEnabled(bool enabled) { mNmeaEnabled = enabled; }

Return<void> GnssCallback::gnssLocationCb(const H::V1_0::GnssLocation& location) {
    H::V2_0::GnssLocation converted{};
    converted.v1_0 = location;
    return gnssLocationCb_2_0(converted);
}

Return<void> GnssCallback::gnssLocationCb_2_0(const H::V2_0::GnssLocation& location) {
    if (auto target = callback(); target && mLocationEnabled) {
        target->gnssLocationCb(toAidlLocation(location));
    }
    return Void();
}

Return<void> GnssCallback::gnssStatusCb(H::V1_0::IGnssCallback::GnssStatusValue status) {
    if (auto target = callback()) {
        target->gnssStatusCb(static_cast<A::IGnssCallback::GnssStatusValue>(status));
    }
    return Void();
}

Return<void> GnssCallback::gnssSvStatusCb(const H::V1_0::IGnssCallback::GnssSvStatus& status) {
    if (auto target = callback(); target && mSvStatusEnabled) {
        std::vector<A::IGnssCallback::GnssSvInfo> result;
        const size_t count = std::min<size_t>(status.numSvs, status.gnssSvList.size());
        result.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const auto& sv = status.gnssSvList[i];
            result.push_back(toAidlSv(sv, static_cast<uint8_t>(sv.constellation), 0));
        }
        target->gnssSvStatusCb(result);
    }
    return Void();
}

Return<void> GnssCallback::gnssSvStatusCb_2_0(
        const hidl_vec<H::V2_0::IGnssCallback::GnssSvInfo>& info) {
    if (auto target = callback(); target && mSvStatusEnabled) {
        std::vector<A::IGnssCallback::GnssSvInfo> result;
        result.reserve(info.size());
        for (const auto& sv : info) {
            result.push_back(toAidlSv(sv.v1_0, static_cast<uint8_t>(sv.constellation), 0));
        }
        target->gnssSvStatusCb(result);
    }
    return Void();
}

Return<void> GnssCallback::gnssSvStatusCb_2_1(
        const hidl_vec<H::V2_1::IGnssCallback::GnssSvInfo>& info) {
    if (auto target = callback(); target && mSvStatusEnabled) {
        std::vector<A::IGnssCallback::GnssSvInfo> result;
        result.reserve(info.size());
        for (const auto& sv : info) {
            result.push_back(toAidlSv(sv.v2_0.v1_0, static_cast<uint8_t>(sv.v2_0.constellation),
                                      sv.basebandCN0DbHz));
        }
        target->gnssSvStatusCb(result);
    }
    return Void();
}

static bool hasNmeaFraming(std::string_view nmea) {
    if (nmea.size() < 4 || nmea.size() > 82 || nmea.front() != '$' ||
        nmea.substr(nmea.size() - 2) != "\r\n") {
        return false;
    }
    nmea.remove_suffix(2);
    return std::all_of(nmea.begin(), nmea.end(), [](unsigned char value) {
        return value >= 0x20 && value <= 0x7e;
    });
}

Return<void> GnssCallback::gnssNmeaCb(int64_t timestamp, const hidl_string& nmea) {
    if (!hasNmeaFraming(std::string_view(nmea.c_str(), nmea.size()))) {
        return Void();
    }
    if (auto target = callback(); target && mNmeaEnabled) {
        target->gnssNmeaCb(timestamp, std::string(nmea.c_str(), nmea.size()));
    }
    return Void();
}

Return<void> GnssCallback::gnssSetCapabilitesCb(uint32_t capabilities) {
    return gnssSetCapabilitiesCb_2_1(capabilities);
}

Return<void> GnssCallback::gnssSetCapabilitiesCb_2_0(uint32_t capabilities) {
    return gnssSetCapabilitiesCb_2_1(capabilities);
}

Return<void> GnssCallback::gnssSetCapabilitiesCb_2_1(uint32_t capabilities) {
    using HC = H::V2_1::IGnssCallback::Capabilities;
    using AC = A::IGnssCallback;
    constexpr std::pair<HC, int32_t> mapping[] = {
            {HC::SCHEDULING, AC::CAPABILITY_SCHEDULING},
            {HC::MSB, AC::CAPABILITY_MSB},
            {HC::MSA, AC::CAPABILITY_MSA},
            {HC::SINGLE_SHOT, AC::CAPABILITY_SINGLE_SHOT},
            {HC::ON_DEMAND_TIME, AC::CAPABILITY_ON_DEMAND_TIME},
            {HC::GEOFENCING, AC::CAPABILITY_GEOFENCING},
            {HC::MEASUREMENTS, AC::CAPABILITY_MEASUREMENTS},
            {HC::NAV_MESSAGES, AC::CAPABILITY_NAV_MESSAGES},
            {HC::LOW_POWER_MODE, AC::CAPABILITY_LOW_POWER_MODE},
            {HC::SATELLITE_BLACKLIST, AC::CAPABILITY_SATELLITE_BLOCKLIST},
            {HC::MEASUREMENT_CORRECTIONS, AC::CAPABILITY_MEASUREMENT_CORRECTIONS},
            {HC::ANTENNA_INFO, AC::CAPABILITY_ANTENNA_INFO},
    };
    int32_t result = 0;
    for (const auto& [source, destination] : mapping) {
        if (capabilities & static_cast<uint32_t>(source)) {
            result |= destination;
        }
    }
    if (auto target = callback()) {
        target->gnssSetCapabilitiesCb(result & mSupportedCapabilities);
    }
    return Void();
}

Return<void> GnssCallback::gnssAcquireWakelockCb() {
    if (auto target = callback()) {
        target->gnssAcquireWakelockCb();
    }
    return Void();
}

Return<void> GnssCallback::gnssReleaseWakelockCb() {
    if (auto target = callback()) {
        target->gnssReleaseWakelockCb();
    }
    return Void();
}

Return<void> GnssCallback::gnssRequestTimeCb() {
    if (auto target = callback()) {
        target->gnssRequestTimeCb();
    }
    return Void();
}

Return<void> GnssCallback::gnssRequestLocationCb(bool independentFromGnss) {
    return gnssRequestLocationCb_2_0(independentFromGnss, false);
}

Return<void> GnssCallback::gnssRequestLocationCb_2_0(bool independentFromGnss,
                                                     bool isUserEmergency) {
    if (auto target = callback()) {
        target->gnssRequestLocationCb(independentFromGnss, isUserEmergency);
    }
    return Void();
}

Return<void> GnssCallback::gnssSetSystemInfoCb(const H::V1_0::IGnssCallback::GnssSystemInfo& info) {
    A::IGnssCallback::GnssSystemInfo result;
    {
        std::lock_guard lock(mMutex);
        mSystemInfo.yearOfHw = info.yearOfHw;
        result = mSystemInfo;
    }
    if (auto target = callback()) {
        target->gnssSetSystemInfoCb(result);
    }
    return Void();
}

Return<void> GnssCallback::gnssNameCb(const hidl_string& name) {
    A::IGnssCallback::GnssSystemInfo result;
    {
        std::lock_guard lock(mMutex);
        mSystemInfo.name.assign(name.c_str(), name.size());
        result = mSystemInfo;
    }
    if (auto target = callback()) {
        target->gnssSetSystemInfoCb(result);
    }
    return Void();
}

}
