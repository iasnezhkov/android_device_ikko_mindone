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
#include <string>

namespace mindone::bridges::secure_element {

namespace hidl_se = ::android::hardware::secure_element;

class HidlSeCallback : public hidl_se::V1_1::ISecureElementHalCallback {
  public:
    explicit HidlSeCallback(
            std::shared_ptr<aidl::android::hardware::secure_element::ISecureElementCallback> cb)
        : mCb(std::move(cb)) {}
    ::android::hardware::Return<void> onStateChange(bool connected) override;
    ::android::hardware::Return<void> onStateChange_1_1(bool connected,
                                                        const ::android::hardware::hidl_string& reason) override;

  private:
    std::shared_ptr<aidl::android::hardware::secure_element::ISecureElementCallback> mCb;
};

class SecureElementBridge : public aidl::android::hardware::secure_element::BnSecureElement {
  public:
    explicit SecureElementBridge(std::string instance) : mInstance(std::move(instance)) {}

    // Connection to the HIDL service; false = stock not found (do not register the bridge).
    bool connect();

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
    std::string mInstance;
    ::android::sp<hidl_se::V1_2::ISecureElement> mHal;
    ::android::sp<HidlSeCallback> mHidlCb;
    std::mutex mLock;
};

}  // namespace mindone::bridges::secure_element
