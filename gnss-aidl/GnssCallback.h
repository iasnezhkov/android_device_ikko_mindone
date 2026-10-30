/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/gnss/IGnssCallback.h>
#include <android/hardware/gnss/2.1/IGnssCallback.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace mindone::gnss {

namespace A = ::aidl::android::hardware::gnss;
namespace H = ::android::hardware::gnss;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;

A::GnssLocation toAidlLocation(const H::V2_0::GnssLocation& location);

class GnssCallback final : public H::V2_1::IGnssCallback {
  public:
    GnssCallback(std::shared_ptr<A::IGnssCallback> callback, int32_t supportedCapabilities);
    void invalidate();
    static bool isDispatching() { return sDispatchDepth != 0; }
    void setLocationEnabled(bool enabled);
    void setSvStatusEnabled(bool enabled);
    void setNmeaEnabled(bool enabled);

    Return<void> gnssLocationCb(const H::V1_0::GnssLocation& location) override;
    Return<void> gnssLocationCb_2_0(const H::V2_0::GnssLocation& location) override;
    Return<void> gnssStatusCb(H::V1_0::IGnssCallback::GnssStatusValue status) override;
    Return<void> gnssSvStatusCb(const H::V1_0::IGnssCallback::GnssSvStatus& status) override;
    Return<void> gnssSvStatusCb_2_0(
            const hidl_vec<H::V2_0::IGnssCallback::GnssSvInfo>& info) override;
    Return<void> gnssSvStatusCb_2_1(
            const hidl_vec<H::V2_1::IGnssCallback::GnssSvInfo>& info) override;
    Return<void> gnssNmeaCb(int64_t timestamp, const hidl_string& nmea) override;
    Return<void> gnssSetCapabilitesCb(uint32_t capabilities) override;
    Return<void> gnssSetCapabilitiesCb_2_0(uint32_t capabilities) override;
    Return<void> gnssSetCapabilitiesCb_2_1(uint32_t capabilities) override;
    Return<void> gnssAcquireWakelockCb() override;
    Return<void> gnssReleaseWakelockCb() override;
    Return<void> gnssRequestTimeCb() override;
    Return<void> gnssRequestLocationCb(bool independentFromGnss) override;
    Return<void> gnssRequestLocationCb_2_0(bool independentFromGnss, bool isUserEmergency) override;
    Return<void> gnssSetSystemInfoCb(const H::V1_0::IGnssCallback::GnssSystemInfo& info) override;
    Return<void> gnssNameCb(const hidl_string& name) override;

  private:
    std::shared_ptr<A::IGnssCallback> callback();
    std::mutex mMutex;
    std::condition_variable mDrained;
    size_t mInFlight = 0;
    static thread_local size_t sDispatchDepth;
    std::shared_ptr<A::IGnssCallback> mCallback;
    const int32_t mSupportedCapabilities;
    A::IGnssCallback::GnssSystemInfo mSystemInfo;
    std::atomic<bool> mLocationEnabled{false};
    std::atomic<bool> mSvStatusEnabled{false};
    std::atomic<bool> mNmeaEnabled{false};
};

}
