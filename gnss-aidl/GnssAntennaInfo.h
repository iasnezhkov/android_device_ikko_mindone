/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/gnss/BnGnssAntennaInfo.h>
#include <aidl/android/hardware/gnss/IGnssAntennaInfoCallback.h>
#include <android/hardware/gnss/2.1/IGnss.h>
#include <android/hardware/gnss/2.1/IGnssAntennaInfo.h>

#include "CallbackSlot.h"

namespace mindone::gnss {

namespace A = ::aidl::android::hardware::gnss;
namespace H = ::android::hardware::gnss;

class GnssAntennaInfo final : public A::BnGnssAntennaInfo {
  public:
    explicit GnssAntennaInfo(const ::android::sp<H::V2_1::IGnss>& backend);
    bool available() const { return mBackend != nullptr; }
    ndk::ScopedAStatus setCallback(
            const std::shared_ptr<A::IGnssAntennaInfoCallback>& callback) override;
    ndk::ScopedAStatus close() override;
    void shutdown();

  private:
    ndk::ScopedAStatus reset();
    std::mutex mMutex;
    ::android::sp<H::V2_1::IGnssAntennaInfo> mBackend;
    std::shared_ptr<CallbackSlot<A::IGnssAntennaInfoCallback>> mCallback;
};

}
