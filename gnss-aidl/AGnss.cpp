/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "AGnss.h"

#include <android/hardware/gnss/2.0/IAGnssCallback.h>

namespace mindone::gnss {

class AGnssCallback final : public H::V2_0::IAGnssCallback {
  public:
    explicit AGnssCallback(std::shared_ptr<CallbackSlot<A::IAGnssCallback>> slot)
        : mSlot(std::move(slot)) {}
    ::android::hardware::Return<void> agnssStatusCb(AGnssType type,
                                                    AGnssStatusValue status) override {
        if (auto callback = mSlot->get()) {
            callback->agnssStatusCb(static_cast<A::IAGnssCallback::AGnssType>(type),
                                    static_cast<A::IAGnssCallback::AGnssStatusValue>(status));
        }
        return ::android::hardware::Void();
    }

  private:
    const std::shared_ptr<CallbackSlot<A::IAGnssCallback>> mSlot;
};

AGnss::AGnss(const ::android::sp<H::V2_1::IGnss>& backend) {
    auto extension = backend->getExtensionAGnss_2_0();
    if (extension.isOk())
        mBackend = extension;
}

void AGnss::shutdown() {
    std::lock_guard lock(mMutex);
    if (mCallback)
        mCallback->invalidate();
    mCallback.reset();
}

ndk::ScopedAStatus AGnss::setCallback(const std::shared_ptr<A::IAGnssCallback>& callback) {
    if (!callback)
        return extensionError(A::IGnss::ERROR_INVALID_ARGUMENT);
    if (callbacksInFlight)
        return extensionError();
    return forward(
            [&] {
                if (mCallback)
                    mCallback->invalidate();
                mCallback = std::make_shared<CallbackSlot<A::IAGnssCallback>>(callback);
                ::android::sp<AGnssCallback> bridge = new AGnssCallback(mCallback);
                auto result = mBackend->setCallback(bridge);
                if (!result.isOk()) {
                    mCallback->invalidate();
                    mCallback.reset();
                }
                return result;
            },
            true);
}

ndk::ScopedAStatus AGnss::dataConnClosed() {
    return forward([&] { return mBackend->dataConnClosed(); });
}

ndk::ScopedAStatus AGnss::dataConnFailed() {
    return forward([&] { return mBackend->dataConnFailed(); });
}

ndk::ScopedAStatus AGnss::setServer(A::IAGnssCallback::AGnssType type, const std::string& hostname,
                                    int32_t port) {
    int32_t value = static_cast<int32_t>(type);
    if (value < 1 || value > 4 || hostname.empty() || port < 1 || port > 65535) {
        return extensionError(A::IGnss::ERROR_INVALID_ARGUMENT);
    }
    return forward([&] {
        return mBackend->setServer(static_cast<H::V2_0::IAGnssCallback::AGnssType>(type), hostname,
                                   port);
    });
}

ndk::ScopedAStatus AGnss::dataConnOpen(int64_t networkHandle, const std::string& apn,
                                       ApnIpType type) {
    if (static_cast<int32_t>(type) < 0 || static_cast<int32_t>(type) > 3) {
        return extensionError(A::IGnss::ERROR_INVALID_ARGUMENT);
    }
    return forward([&] {
        return mBackend->dataConnOpen(static_cast<uint64_t>(networkHandle), apn,
                                      static_cast<H::V2_0::IAGnss::ApnIpType>(type));
    });
}

}
