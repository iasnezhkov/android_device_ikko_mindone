/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/gnss/BnGnssMeasurementInterface.h>
#include <aidl/android/hardware/gnss/IGnssMeasurementCallback.h>
#include <android/binder_ibinder.h>
#include <android/hardware/gnss/2.1/IGnss.h>
#include <android/hardware/gnss/2.1/IGnssMeasurement.h>

#include <memory>
#include <mutex>

namespace mindone::gnss {

namespace A = ::aidl::android::hardware::gnss;
namespace H = ::android::hardware::gnss;
class MeasurementCallback;

class GnssMeasurement final : public A::BnGnssMeasurementInterface {
  public:
    explicit GnssMeasurement(const ::android::sp<H::V2_1::IGnss>& backend);
    ~GnssMeasurement() override;
    bool available() const { return mBackend != nullptr; }
    static bool isDispatching();
    ndk::ScopedAStatus setCallback(const std::shared_ptr<A::IGnssMeasurementCallback>& callback,
                                   bool fullTracking, bool correlationVectors) override;
    ndk::ScopedAStatus setCallbackWithOptions(
            const std::shared_ptr<A::IGnssMeasurementCallback>& callback,
            const Options& options) override;
    ndk::ScopedAStatus close() override;
    ndk::ScopedAStatus shutdown();

  private:
    ndk::ScopedAStatus closeSession(bool wait, const void* cookie = nullptr);
    ndk::ScopedAStatus reset();
    ::android::sp<H::V2_1::IGnssMeasurement> mBackend;
    ::android::sp<MeasurementCallback> mBridge;
    std::mutex mMutex;
    ndk::SpAIBinder mClient;
    ndk::ScopedAIBinder_DeathRecipient mDeath;
    std::weak_ptr<GnssMeasurement>* mCookie = nullptr;
};

}
