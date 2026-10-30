/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/gnss/BnAGnssRil.h>
#include <aidl/android/hardware/gnss/IAGnssRilCallback.h>
#include <android/hardware/gnss/2.0/IAGnssRil.h>
#include <android/hardware/gnss/2.1/IGnss.h>

#include "CallbackSlot.h"

namespace mindone::gnss {

namespace A = ::aidl::android::hardware::gnss;
namespace H = ::android::hardware::gnss;

class AGnssRil final : public A::BnAGnssRil {
  public:
    explicit AGnssRil(const ::android::sp<H::V2_1::IGnss>& backend);
    bool available() const { return mBackend != nullptr; }
    ndk::ScopedAStatus setCallback(const std::shared_ptr<A::IAGnssRilCallback>& callback) override;
    ndk::ScopedAStatus setRefLocation(const AGnssRefLocation& location) override;
    ndk::ScopedAStatus setSetId(SetIdType type, const std::string& id) override;
    ndk::ScopedAStatus updateNetworkState(const NetworkAttributes& attributes) override;
    void shutdown();

  private:
    template <typename F>
    ndk::ScopedAStatus forward(F operation) {
        std::unique_lock lock(mMutex, std::try_to_lock);
        if (!lock || !mBackend)
            return extensionError();
        return extensionStatus(operation());
    }
    std::mutex mMutex;
    ::android::sp<H::V2_0::IAGnssRil> mBackend;
    std::shared_ptr<CallbackSlot<A::IAGnssRilCallback>> mCallback;
};

}
