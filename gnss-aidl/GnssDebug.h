/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/gnss/BnGnssDebug.h>
#include <android/hardware/gnss/2.0/IGnssDebug.h>

namespace mindone::gnss {

namespace A = ::aidl::android::hardware::gnss;
namespace H = ::android::hardware::gnss;

class GnssDebug final : public A::BnGnssDebug {
  public:
    explicit GnssDebug(::android::sp<H::V2_0::IGnssDebug> backend);
    ndk::ScopedAStatus getDebugData(DebugData* result) override;

  private:
    const ::android::sp<H::V2_0::IGnssDebug> mBackend;
};

}
