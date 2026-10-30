/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/gnss/visibility_control/BnGnssVisibilityControl.h>
#include <aidl/android/hardware/gnss/visibility_control/IGnssVisibilityControlCallback.h>
#include <android/hardware/gnss/2.1/IGnss.h>
#include <android/hardware/gnss/visibility_control/1.0/IGnssVisibilityControl.h>

#include <string>
#include <vector>

#include "CallbackSlot.h"

namespace mindone::gnss {

namespace V = ::aidl::android::hardware::gnss::visibility_control;
namespace HV = ::android::hardware::gnss::visibility_control::V1_0;

class GnssVisibilityControl final : public V::BnGnssVisibilityControl {
  public:
    explicit GnssVisibilityControl(
            const ::android::sp<::android::hardware::gnss::V2_1::IGnss>& backend);
    ndk::ScopedAStatus setCallback(
            const std::shared_ptr<V::IGnssVisibilityControlCallback>& callback) override;
    ndk::ScopedAStatus enableNfwLocationAccess(const std::vector<std::string>& proxyApps) override;
    void shutdown();

  private:
    std::mutex mMutex;
    ::android::sp<HV::IGnssVisibilityControl> mBackend;
    std::shared_ptr<CallbackSlot<V::IGnssVisibilityControlCallback>> mCallback;
};

}
