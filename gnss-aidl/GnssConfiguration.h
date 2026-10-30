/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/gnss/BnGnssConfiguration.h>
#include <aidl/android/hardware/gnss/IGnss.h>
#include <android/hardware/gnss/2.1/IGnssConfiguration.h>

namespace mindone::gnss {

namespace A = ::aidl::android::hardware::gnss;
namespace H = ::android::hardware::gnss;

class GnssConfiguration final : public A::BnGnssConfiguration {
  public:
    explicit GnssConfiguration(::android::sp<H::V2_1::IGnssConfiguration> backend);
    ndk::ScopedAStatus setSuplVersion(int32_t version) override;
    ndk::ScopedAStatus setSuplMode(int32_t mode) override;
    ndk::ScopedAStatus setLppProfile(int32_t profile) override;
    ndk::ScopedAStatus setGlonassPositioningProtocol(int32_t protocol) override;
    ndk::ScopedAStatus setEmergencySuplPdn(bool enable) override;
    ndk::ScopedAStatus setEsExtensionSec(int32_t seconds) override;
    ndk::ScopedAStatus setBlocklist(const std::vector<A::BlocklistedSource>& sources) override;

  private:
    template <typename F>
    ndk::ScopedAStatus forward(F operation) {
        if (!mBackend) {
            return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
                    A::IGnss::ERROR_GENERIC, "HIDL GNSS configuration unavailable");
        }
        auto result = operation();
        if (!result.isOk() || !static_cast<bool>(result)) {
            return ndk::ScopedAStatus::fromServiceSpecificError(A::IGnss::ERROR_GENERIC);
        }
        return ndk::ScopedAStatus::ok();
    }
    const ::android::sp<H::V2_1::IGnssConfiguration> mBackend;
};

}
