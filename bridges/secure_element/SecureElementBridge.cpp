/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "SecureElementBridge"

#include "SecureElementBridge.h"

#include <android-base/logging.h>
#include <log/log.h>
#include <chrono>

namespace mindone::bridges::secure_element {

using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using aidl::android::hardware::secure_element::ISecureElement;
using aidl::android::hardware::secure_element::LogicalChannelResponse;
using HStatus = hidl_se::V1_0::SecureElementStatus;

namespace {

// AIDL codes (ISecureElement::FAILED...IOERROR) numerically match HIDL SecureElementStatus.
ndk::ScopedAStatus fromHidl(HStatus st) {
    if (st == HStatus::SUCCESS) return ndk::ScopedAStatus::ok();
    return ndk::ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(st));
}

ndk::ScopedAStatus deadHal(const std::string& desc) {
    LOG(ERROR) << "HIDL transport error: " << desc;
    return ndk::ScopedAStatus::fromServiceSpecificError(ISecureElement::IOERROR);
}

}  // namespace

class SeDeathRecipient : public ::android::hardware::hidl_death_recipient {
  public:
    explicit SeDeathRecipient(std::shared_ptr<SeCallbackGate> gate) : mGate(std::move(gate)) {}
    void serviceDied(uint64_t cookie,
                     const ::android::wp<::android::hidl::base::V1_0::IBase>&) override {
        std::lock_guard<std::mutex> l(mGate->lock);
        if (mGate->owner) mGate->owner->onServiceDied(cookie);
    }
  private:
    std::shared_ptr<SeCallbackGate> mGate;
};

Return<void> HidlSeCallback::onStateChange(bool connected) {
    return onStateChange_1_1(connected, "hidl-1.0");
}

Return<void> HidlSeCallback::onStateChange_1_1(bool connected, const hidl_string& reason) {
    if (mActive.load(std::memory_order_acquire)) mCb(connected, std::string(reason));
    return Void();
}

void HidlSeCallback::deactivate() {
    mActive.store(false, std::memory_order_release);
}

SecureElementBridge::SecureElementBridge(std::string instance) : mInstance(std::move(instance)) {
    mGate = std::make_shared<SeCallbackGate>();
    mGate->owner = this;
    mWorker = std::thread([this] { reconnectLoop(); });
}

SecureElementBridge::~SecureElementBridge() {
    {
        std::lock_guard<std::mutex> l(mGate->lock);
        mGate->owner = nullptr;
    }
    {
        std::lock_guard<std::mutex> l(mLock);
        mStopping = true;
        ++mGeneration;
    }
    mChanged.notify_all();
    mWorker.join();
    if (mHidlCb) mHidlCb->deactivate();
    if (mHal && mDeath) {
        auto ret = mHal->unlinkToDeath(mDeath);
        (void)ret.isOk();
    }
}

::android::sp<hidl_se::V1_2::ISecureElement> SecureElementBridge::getHal() {
    std::lock_guard<std::mutex> l(mLock);
    return mHal;
}

::android::sp<HidlSeCallback> SecureElementBridge::makeCallback(uint64_t generation, uint64_t client) {
    auto gate = mGate;
    return new HidlSeCallback([gate, generation, client](bool connected, const std::string& reason) {
        std::lock_guard<std::mutex> gateLock(gate->lock);
        if (auto self = gate->owner) {
            {
                std::lock_guard<std::mutex> l(self->mLock);
                if (self->mStopping || generation != self->mGeneration ||
                    client != self->mClientGeneration) return;
                self->mEvents.push_back({generation, client, connected, reason});
            }
            self->mChanged.notify_all();
        }
    });
}

void SecureElementBridge::finishInitialization() {
    {
        std::lock_guard<std::mutex> l(mLock);
        mInitializing = false;
    }
    mChanged.notify_all();
}

bool SecureElementBridge::connect(bool wait) {
    std::lock_guard<std::mutex> initializing(mInitLock);
    auto hal = wait ? hidl_se::V1_2::ISecureElement::getService(mInstance)
                    : hidl_se::V1_2::ISecureElement::tryGetService(mInstance);
    if (hal == nullptr) return false;
    uint64_t generation, client;
    bool initializedClient;
    {
        std::lock_guard<std::mutex> l(mLock);
        if (mStopping) return false;
        generation = ++mGeneration;
        client = mClientGeneration;
        initializedClient = mClient != nullptr;
        mInitializing = true;
    }
    auto death = ::android::sp<SeDeathRecipient>(new SeDeathRecipient(mGate));
    auto linked = hal->linkToDeath(death, generation);
    if (!linked.isOk() || !static_cast<bool>(linked)) {
        finishInitialization();
        return false;
    }
    auto callback = initializedClient ? makeCallback(generation, client) : nullptr;
    bool ready = true;
    if (callback) {
        auto ret = hal->init_1_1(callback);
        ready = ret.isOk();
    }
    {
        std::lock_guard<std::mutex> l(mLock);
        ready = ready && generation == mGeneration && !mStopping;
        if (ready) {
            mHal = hal;
            mDeath = death;
            mHidlCb = callback;
            mReconnect = false;
        }
    }
    if (!ready) {
        if (callback) callback->deactivate();
        auto ret = hal->unlinkToDeath(death);
        (void)ret.isOk();
        onServiceDied(generation);
    }
    finishInitialization();
    return ready;
}

void SecureElementBridge::onServiceDied(uint64_t generation) {
    ::android::sp<HidlSeCallback> callback;
    {
        std::lock_guard<std::mutex> l(mLock);
        if (generation != mGeneration || mStopping) return;
        ++mGeneration;
        mHal = nullptr;
        callback = mHidlCb;
        mHidlCb = nullptr;
        mReconnect = true;
        if (mClient) mEvents.push_back({mGeneration, mClientGeneration, false,
                                       "HIDL backend unavailable"});
    }
    if (callback) callback->deactivate();
    mChanged.notify_all();
}

void SecureElementBridge::reconnectLoop() {
    std::unique_lock<std::mutex> l(mLock);
    auto retryAt = std::chrono::steady_clock::now();
    while (!mStopping) {
        mChanged.wait(l, [this] {
            return mStopping || (!mInitializing && (mReconnect || !mEvents.empty()));
        });
        if (mStopping) break;
        if (!mEvents.empty()) {
            auto event = std::move(mEvents.front());
            mEvents.pop_front();
            if (event.generation != mGeneration || event.client != mClientGeneration) continue;
            auto client = mClient;
            l.unlock();
            if (client) client->onStateChange(event.connected, event.reason);
            l.lock();
            continue;
        }
        if (std::chrono::steady_clock::now() < retryAt) {
            mChanged.wait_until(l, retryAt, [this] { return mStopping || !mEvents.empty(); });
            continue;
        }
        l.unlock();
        bool connected = connect(false);
        l.lock();
        retryAt = std::chrono::steady_clock::now();
        if (!connected) retryAt += std::chrono::seconds(2);
    }
}

ndk::ScopedAStatus SecureElementBridge::init(
        const std::shared_ptr<aidl::android::hardware::secure_element::ISecureElementCallback>& cb) {
    if (cb == nullptr) return ndk::ScopedAStatus::fromServiceSpecificError(ISecureElement::FAILED);
    std::lock_guard<std::mutex> initializing(mInitLock);
    ::android::sp<hidl_se::V1_2::ISecureElement> hal;
    ::android::sp<HidlSeCallback> old;
    uint64_t generation, client;
    {
        std::lock_guard<std::mutex> l(mLock);
        mClient = cb;
        client = ++mClientGeneration;
        hal = mHal;
        mHal = nullptr;
        generation = mGeneration;
        old = mHidlCb;
        mInitializing = true;
    }
    if (old) old->deactivate();
    if (!hal) {
        {
            std::lock_guard<std::mutex> l(mLock);
            mEvents.push_back({generation, client, false, "HIDL backend unavailable"});
            mReconnect = true;
        }
        finishInitialization();
        return deadHal("HIDL backend unavailable");
    }
    auto callback = makeCallback(generation, client);
    auto r = hal->init_1_1(callback);
    bool ready = r.isOk();
    {
        std::lock_guard<std::mutex> l(mLock);
        ready = ready && generation == mGeneration;
        if (ready) {
            mHal = hal;
            mHidlCb = callback;
        }
    }
    if (!ready) {
        callback->deactivate();
        onServiceDied(generation);
    }
    finishInitialization();
    if (!ready) return deadHal("HIDL backend changed during init");
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SecureElementBridge::getAtr(std::vector<uint8_t>* out) {
    auto hal = getHal();
    if (!hal) return deadHal("HIDL backend unavailable");
    auto r = hal->getAtr([&](const hidl_vec<uint8_t>& atr) { *out = atr; });
    if (!r.isOk()) return deadHal(r.description());
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SecureElementBridge::isCardPresent(bool* out) {
    auto hal = getHal();
    if (!hal) return deadHal("HIDL backend unavailable");
    auto r = hal->isCardPresent();
    if (!r.isOk()) return deadHal(r.description());
    *out = static_cast<bool>(r);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SecureElementBridge::transmit(const std::vector<uint8_t>& data,
                                                 std::vector<uint8_t>* out) {
    std::vector<uint8_t> rsp;
    auto hal = getHal();
    if (!hal) return deadHal("HIDL backend unavailable");
    auto r = hal->transmit(data, [&](const hidl_vec<uint8_t>& v) { rsp = v; });
    if (!r.isOk()) return deadHal(r.description());
    // HIDL does not return a status: an empty response = a failed exchange (the stock Terminal.java treats it the same way).
    if (rsp.empty()) return ndk::ScopedAStatus::fromServiceSpecificError(ISecureElement::IOERROR);
    *out = std::move(rsp);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SecureElementBridge::openLogicalChannel(const std::vector<uint8_t>& aid, int8_t p2,
                                                           LogicalChannelResponse* out) {
    HStatus st = HStatus::FAILED;
    auto hal = getHal();
    if (!hal) return deadHal("HIDL backend unavailable");
    auto r = hal->openLogicalChannel(
            aid, static_cast<uint8_t>(p2),
            [&](const hidl_se::V1_0::LogicalChannelResponse& rsp, HStatus s) {
                st = s;
                out->channelNumber = static_cast<int8_t>(rsp.channelNumber);
                out->selectResponse = rsp.selectResponse;
            });
    if (!r.isOk()) return deadHal(r.description());
    return fromHidl(st);
}

ndk::ScopedAStatus SecureElementBridge::openBasicChannel(const std::vector<uint8_t>& aid, int8_t p2,
                                                         std::vector<uint8_t>* out) {
    HStatus st = HStatus::FAILED;
    auto hal = getHal();
    if (!hal) return deadHal("HIDL backend unavailable");
    auto r = hal->openBasicChannel(aid, static_cast<uint8_t>(p2),
                                    [&](const hidl_vec<uint8_t>& sel, HStatus s) {
                                        st = s;
                                        *out = sel;
                                    });
    if (!r.isOk()) return deadHal(r.description());
    return fromHidl(st);
}

ndk::ScopedAStatus SecureElementBridge::closeChannel(int8_t channelNumber) {
    auto hal = getHal();
    if (!hal) return deadHal("HIDL backend unavailable");
    auto r = hal->closeChannel(static_cast<uint8_t>(channelNumber));
    if (!r.isOk()) return deadHal(r.description());
    return fromHidl(static_cast<HStatus>(r));
}

ndk::ScopedAStatus SecureElementBridge::reset() {
    auto hal = getHal();
    if (!hal) return deadHal("HIDL backend unavailable");
    auto r = hal->reset();
    if (!r.isOk()) return deadHal(r.description());
    return fromHidl(static_cast<HStatus>(r));
}

}  // namespace mindone::bridges::secure_element
