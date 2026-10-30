/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/gnss/BnGnssNavigationMessageInterface.h>
#include <aidl/android/hardware/gnss/BnGnssPsds.h>
#include <aidl/android/hardware/gnss/IGnssNavigationMessageCallback.h>
#include <aidl/android/hardware/gnss/IGnssPsdsCallback.h>
#include <android/binder_ibinder.h>
#include <android/hardware/gnss/1.0/IGnssNavigationMessage.h>
#include <android/hardware/gnss/1.0/IGnssNavigationMessageCallback.h>
#include <android/hardware/gnss/1.0/IGnssXtra.h>
#include <android/hardware/gnss/1.0/IGnssXtraCallback.h>
#include <limits>
#include <string>
#include <vector>

#include "Backend.h"
#include "CallbackSlot.h"

namespace mindone::gnss {

namespace A = ::aidl::android::hardware::gnss;
namespace H = ::android::hardware::gnss;

template <typename Owner>
class LegacyClient {
  public:
    LegacyClient() : mDeath(AIBinder_DeathRecipient_new([](void* cookie) {
        if (auto owner = static_cast<std::weak_ptr<Owner>*>(cookie)->lock()) {
            auto result = owner->shutdown(cookie);
            static_cast<void>(result.isOk());
        }
    })) {
        AIBinder_DeathRecipient_setOnUnlinked(mDeath.get(), [](void* cookie) {
            delete static_cast<std::weak_ptr<Owner>*>(cookie);
        });
    }
    ~LegacyClient() { reset(); }
    template <typename T>
    bool attach(const std::shared_ptr<T>& callback, const std::shared_ptr<Owner>& owner) {
        mBinder = callback->asBinder();
        auto* cookie = new std::weak_ptr<Owner>(owner);
        if (AIBinder_linkToDeath(mBinder.get(), mDeath.get(), cookie) != STATUS_OK) {
            mBinder = ndk::SpAIBinder();
            return false;
        }
        mCookie = cookie;
        return true;
    }
    bool current(const void* cookie) const { return cookie == mCookie; }
    void reset() {
        if (mCookie) AIBinder_unlinkToDeath(mBinder.get(), mDeath.get(), mCookie);
        mCookie = nullptr;
        mBinder = ndk::SpAIBinder();
    }
  private:
    ndk::SpAIBinder mBinder;
    ndk::ScopedAIBinder_DeathRecipient mDeath;
    std::weak_ptr<Owner>* mCookie = nullptr;
};

class NavigationCallback final : public H::V1_0::IGnssNavigationMessageCallback {
  public:
    explicit NavigationCallback(
            std::shared_ptr<CallbackSlot<A::IGnssNavigationMessageCallback>> slot)
        : mSlot(std::move(slot)) {}
    ::android::hardware::Return<void> gnssNavigationMessageCb(
            const GnssNavigationMessage& source) override {
        if (auto callback = mSlot->get()) {
            A::IGnssNavigationMessageCallback::GnssNavigationMessage result;
            result.svid = source.svid;
            result.type = static_cast<decltype(result.type)>(source.type);
            result.status = source.status;
            result.messageId = source.messageId;
            result.submessageId = source.submessageId;
            result.data.assign(source.data.begin(), source.data.end());
            auto sent = callback->gnssNavigationMessageCb(result);
            static_cast<void>(sent.isOk());
        }
        return ::android::hardware::Void();
    }
  private:
    const std::shared_ptr<CallbackSlot<A::IGnssNavigationMessageCallback>> mSlot;
};

class GnssNavigationMessage final : public A::BnGnssNavigationMessageInterface {
  public:
    explicit GnssNavigationMessage(const ::android::sp<H::V2_1::IGnss>& backend) {
        auto result = backend->getExtensionGnssNavigationMessage();
        if (result.isOk()) mBackend = result;
    }
    ~GnssNavigationMessage() override {
        auto result = shutdown();
        static_cast<void>(result.isOk());
    }
    bool available() const { return mBackend != nullptr; }
    void open() { std::lock_guard lock(mMutex); mEnabled = true; }
    ndk::ScopedAStatus shutdown(const void* cookie = nullptr) {
        std::lock_guard lock(mMutex);
        if (cookie && !mClient.current(cookie)) return ndk::ScopedAStatus::ok();
        if (!cookie) mEnabled = false;
        return reset();
    }
    ndk::ScopedAStatus close() override {
        if (callbacksInFlight) return extensionError();
        std::unique_lock lock(mMutex, std::try_to_lock);
        return lock ? reset() : extensionError();
    }
    ndk::ScopedAStatus setCallback(
            const std::shared_ptr<A::IGnssNavigationMessageCallback>& callback) override {
        if (!callback) return extensionError(A::IGnss::ERROR_INVALID_ARGUMENT);
        if (callbacksInFlight) return extensionError();
        std::unique_lock lock(mMutex, std::try_to_lock);
        if (!lock || !mEnabled || !mBackend) return extensionError();
        if (mCallback) return extensionError(A::IGnss::ERROR_ALREADY_INIT);
        auto closed = reset();
        if (!closed.isOk()) return closed;
        if (!mClient.attach(callback, ref<GnssNavigationMessage>())) return extensionError();
        mCallback = std::make_shared<CallbackSlot<A::IGnssNavigationMessageCallback>>(callback);
        ::android::sp<NavigationCallback> bridge = new NavigationCallback(mCallback);
        mNeedsClose = true;
        auto result = mBackend->setCallback(bridge);
        using S = H::V1_0::IGnssNavigationMessage::GnssNavigationMessageStatus;
        if (result.isOk() && static_cast<S>(result) == S::SUCCESS) {
            return ndk::ScopedAStatus::ok();
        }
        auto cleanup = reset();
        static_cast<void>(cleanup.isOk());
        return extensionError(result.isOk() && static_cast<S>(result) == S::ERROR_ALREADY_INIT
                                      ? A::IGnss::ERROR_ALREADY_INIT : A::IGnss::ERROR_GENERIC);
    }
  private:
    ndk::ScopedAStatus reset() {
        if (mCallback) mCallback->invalidate();
        mCallback.reset();
        mClient.reset();
        if (!mNeedsClose) return ndk::ScopedAStatus::ok();
        auto result = extensionStatus(mBackend->close());
        if (result.isOk()) mNeedsClose = false;
        return result;
    }
    std::mutex mMutex;
    ::android::sp<H::V1_0::IGnssNavigationMessage> mBackend;
    std::shared_ptr<CallbackSlot<A::IGnssNavigationMessageCallback>> mCallback;
    LegacyClient<GnssNavigationMessage> mClient;
    bool mEnabled = false, mNeedsClose = false;
};

class PsdsCallback final : public H::V1_0::IGnssXtraCallback {
  public:
    explicit PsdsCallback(std::shared_ptr<CallbackSlot<A::IGnssPsdsCallback>> slot)
        : mSlot(std::move(slot)) {}
    ::android::hardware::Return<void> downloadRequestCb() override {
        if (auto callback = mSlot->get()) {
            auto sent = callback->downloadRequestCb(A::PsdsType::LONG_TERM);
            static_cast<void>(sent.isOk());
        }
        return ::android::hardware::Void();
    }
  private:
    const std::shared_ptr<CallbackSlot<A::IGnssPsdsCallback>> mSlot;
};

class GnssPsds final : public A::BnGnssPsds {
  public:
    explicit GnssPsds(const ::android::sp<H::V2_1::IGnss>& backend) {
        auto result = backend->getExtensionXtra();
        if (result.isOk()) mBackend = result;
    }
    ~GnssPsds() override {
        auto result = shutdown();
        static_cast<void>(result.isOk());
    }
    bool available() const { return mBackend != nullptr; }
    void open() { std::lock_guard lock(mMutex); mEnabled = true; }
    ndk::ScopedAStatus shutdown(const void* cookie = nullptr) {
        std::lock_guard lock(mMutex);
        if (cookie && !mClient.current(cookie)) return ndk::ScopedAStatus::ok();
        if (!cookie) mEnabled = false;
        reset();
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus setCallback(
            const std::shared_ptr<A::IGnssPsdsCallback>& callback) override {
        if (!callback) return extensionError(A::IGnss::ERROR_INVALID_ARGUMENT);
        if (callbacksInFlight) return extensionError();
        std::unique_lock lock(mMutex, std::try_to_lock);
        if (!lock || !mEnabled || !mBackend) return extensionError();
        reset();
        if (!mClient.attach(callback, ref<GnssPsds>())) return extensionError();
        mCallback = std::make_shared<CallbackSlot<A::IGnssPsdsCallback>>(callback);
        ::android::sp<PsdsCallback> bridge = new PsdsCallback(mCallback);
        auto result = extensionStatus(mBackend->setCallback(bridge));
        if (!result.isOk()) reset();
        return result;
    }
    ndk::ScopedAStatus injectPsdsData(A::PsdsType type,
                                    const std::vector<uint8_t>& data) override {
        if (type == A::PsdsType::NORMAL || type == A::PsdsType::REALTIME)
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        if (type != A::PsdsType::LONG_TERM || data.empty() ||
            data.size() > std::numeric_limits<int32_t>::max())
            return extensionError(A::IGnss::ERROR_INVALID_ARGUMENT);
        std::unique_lock lock(mMutex, std::try_to_lock);
        if (!lock || !mEnabled || !mBackend) return extensionError();
        return extensionStatus(mBackend->injectXtraData(
                std::string(reinterpret_cast<const char*>(data.data()), data.size())));
    }
  private:
    void reset() {
        if (mCallback) mCallback->invalidate();
        mCallback.reset();
        mClient.reset();
    }
    std::mutex mMutex;
    ::android::sp<H::V1_0::IGnssXtra> mBackend;
    std::shared_ptr<CallbackSlot<A::IGnssPsdsCallback>> mCallback;
    LegacyClient<GnssPsds> mClient;
    bool mEnabled = false;
};

}
