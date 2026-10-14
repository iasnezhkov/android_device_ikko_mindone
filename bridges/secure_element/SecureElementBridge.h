/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
// SecureElementBridge - AIDL ISecureElement (V1) -> HIDL ISecureElement@1.2 (stock MediaTek, SIM1).
#pragma once

#include <aidl/android/hardware/secure_element/BnSecureElement.h>
#include <aidl/android/hardware/secure_element/ISecureElementCallback.h>
#include <android/hardware/secure_element/1.1/ISecureElementHalCallback.h>
#include <android/hardware/secure_element/1.2/ISecureElement.h>

#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <deque>
#include <functional>
#include <string>

namespace mindone::bridges::secure_element {

namespace hidl_se = ::android::hardware::secure_element;

class HidlSeCallback : public hidl_se::V1_1::ISecureElementHalCallback {
  public:
    explicit HidlSeCallback(std::function<void(bool, const std::string&)> cb)
        : mCb(std::move(cb)) {}
    ::android::hardware::Return<void> onStateChange(bool connected) override;
    ::android::hardware::Return<void> onStateChange_1_1(bool connected,
                                                        const ::android::hardware::hidl_string& reason) override;
    void deactivate();

  private:
    std::function<void(bool, const std::string&)> mCb;
    std::atomic<bool> mActive{true};
};

class SecureElementBridge;

struct SeCallbackGate {
    std::mutex lock;
    SecureElementBridge* owner;
};

class SecureElementBridge : public aidl::android::hardware::secure_element::BnSecureElement {
  public:
    explicit SecureElementBridge(std::string instance);
    ~SecureElementBridge();
    void onServiceDied(uint64_t generation);

    // Connection to the HIDL service; false = stock not found (do not register the bridge).
    bool connect(bool wait = true);

    ndk::ScopedAStatus init(
            const std::shared_ptr<aidl::android::hardware::secure_element::ISecureElementCallback>& cb)
            override;
    ndk::ScopedAStatus getAtr(std::vector<uint8_t>* out) override;
    ndk::ScopedAStatus isCardPresent(bool* out) override;
    ndk::ScopedAStatus transmit(const std::vector<uint8_t>& data, std::vector<uint8_t>* out) override;
    ndk::ScopedAStatus openLogicalChannel(
            const std::vector<uint8_t>& aid, int8_t p2,
            aidl::android::hardware::secure_element::LogicalChannelResponse* out) override;
    ndk::ScopedAStatus openBasicChannel(const std::vector<uint8_t>& aid, int8_t p2,
                                        std::vector<uint8_t>* out) override;
    ndk::ScopedAStatus closeChannel(int8_t channelNumber) override;
    ndk::ScopedAStatus reset() override;

  private:
    void reconnectLoop();
    ::android::sp<HidlSeCallback> makeCallback(uint64_t generation, uint64_t client);
    void finishInitialization();
    ::android::sp<hidl_se::V1_2::ISecureElement> getHal();
    std::string mInstance;
    std::shared_ptr<SeCallbackGate> mGate;
    ::android::sp<hidl_se::V1_2::ISecureElement> mHal;
    ::android::sp<HidlSeCallback> mHidlCb;
    std::mutex mLock;
    std::mutex mInitLock;
    std::condition_variable mChanged;
    std::thread mWorker;
    struct Event {
        uint64_t generation;
        uint64_t client;
        bool connected;
        std::string reason;
    };
    std::deque<Event> mEvents;
    uint64_t mClientGeneration = 0;
    bool mInitializing = false;
    bool mStopping = false;
    bool mReconnect = false;
    uint64_t mGeneration = 0;
    ::android::sp<::android::hardware::hidl_death_recipient> mDeath;
    std::shared_ptr<aidl::android::hardware::secure_element::ISecureElementCallback> mClient;
};

}  // namespace mindone::bridges::secure_element
