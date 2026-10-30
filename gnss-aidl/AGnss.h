/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/gnss/BnAGnss.h>
#include <aidl/android/hardware/gnss/IAGnssCallback.h>
#include <android/hardware/gnss/2.0/IAGnss.h>
#include <android/hardware/gnss/2.1/IGnss.h>

#include "CallbackSlot.h"

namespace mindone::gnss {

namespace A = ::aidl::android::hardware::gnss;
namespace H = ::android::hardware::gnss;

class AGnss final : public A::BnAGnss {
  public:
    explicit AGnss(const ::android::sp<H::V2_1::IGnss>& backend);
    bool available() const { return mBackend != nullptr; }
    ndk::ScopedAStatus setCallback(const std::shared_ptr<A::IAGnssCallback>& callback) override;
    ndk::ScopedAStatus dataConnClosed() override;
    ndk::ScopedAStatus dataConnFailed() override;
    ndk::ScopedAStatus setServer(A::IAGnssCallback::AGnssType type, const std::string& hostname,
                                 int32_t port) override;
    ndk::ScopedAStatus dataConnOpen(int64_t networkHandle, const std::string& apn,
                                    ApnIpType type) override;
    void shutdown();

  private:
    template <typename F>
    ndk::ScopedAStatus forward(F operation, bool initializing = false) {
        std::unique_lock lock(mMutex, std::try_to_lock);
        if (!lock || !mBackend || (!initializing && !mCallback))
            return extensionError();
        return extensionStatus(operation());
    }
    std::mutex mMutex;
    ::android::sp<H::V2_0::IAGnss> mBackend;
    std::shared_ptr<CallbackSlot<A::IAGnssCallback>> mCallback;
};

}
