/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/gnss/BnGnssPowerIndication.h>
#include <aidl/android/hardware/gnss/IGnssConfiguration.h>
#include <aidl/android/hardware/gnss/IGnssDebug.h>
#include <aidl/android/hardware/gnss/IGnssPowerIndicationCallback.h>
#include <android/binder_auto_utils.h>
#include <utils/SystemClock.h>

#include "Backend.h"
#include "CallbackSlot.h"

namespace mindone::gnss {

namespace A = ::aidl::android::hardware::gnss;

class PowerIndication final : public A::BnGnssPowerIndication {
  public:
    void open() {
        std::lock_guard lock(mMutex);
        mEnabled = true;
    }
    ndk::ScopedAStatus setCallback(
            const std::shared_ptr<A::IGnssPowerIndicationCallback>& callback) override {
        if (!callback)
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        if (callbacksInFlight)
            return extensionError();
        std::unique_lock lock(mMutex, std::try_to_lock);
        if (!lock || !mEnabled)
            return extensionError();
        if (mCallback)
            mCallback->invalidate();
        auto slot = std::make_shared<CallbackSlot<A::IGnssPowerIndicationCallback>>(callback);
        mCallback = slot;
        auto target = slot->get();
        lock.unlock();
        auto result = target->setCapabilitiesCb(0);
        target.reset();
        if (!result.isOk()) {
            lock.lock();
            if (mCallback == slot) {
                slot->invalidate();
                mCallback.reset();
            }
        }
        return result;
    }
    ndk::ScopedAStatus requestGnssPowerStats() override {
        std::shared_ptr<A::IGnssPowerIndicationCallback> callback;
        {
            std::lock_guard lock(mMutex);
            if (mEnabled && mCallback)
                callback = mCallback->get();
        }
        if (!callback)
            return extensionError();
        A::GnssPowerStats stats{};
        stats.elapsedRealtime.flags = A::ElapsedRealtime::HAS_TIMESTAMP_NS;
        stats.elapsedRealtime.timestampNs = ::android::elapsedRealtimeNano();
        return callback->gnssPowerStatsCb(stats);
    }
    void shutdown() {
        std::lock_guard lock(mMutex);
        mEnabled = false;
        if (mCallback)
            mCallback->invalidate();
        mCallback.reset();
    }

  private:
    std::mutex mMutex;
    bool mEnabled = false;
    std::shared_ptr<CallbackSlot<A::IGnssPowerIndicationCallback>> mCallback;
};

template <typename T>
ndk::ScopedAStatus makeExtension(const ::android::sp<::android::hardware::gnss::V2_1::IGnss>&,
                                 std::shared_ptr<T>* result, bool nullable) {
    result->reset();
    return nullable ? ndk::ScopedAStatus::ok()
                    : ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

template <>
ndk::ScopedAStatus makeExtension(const ::android::sp<::android::hardware::gnss::V2_1::IGnss>&,
                                 std::shared_ptr<A::IGnssConfiguration>* result, bool);

template <>
ndk::ScopedAStatus makeExtension(const ::android::sp<::android::hardware::gnss::V2_1::IGnss>&,
                                 std::shared_ptr<A::IGnssDebug>* result, bool);

}
