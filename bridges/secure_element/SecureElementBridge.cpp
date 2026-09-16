/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "SecureElementBridge"

#include "SecureElementBridge.h"

#include <android-base/logging.h>
#include <log/log.h>

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

Return<void> HidlSeCallback::onStateChange(bool connected) {
    if (mCb) mCb->onStateChange(connected, "hidl-1.0");
    return Void();
}

Return<void> HidlSeCallback::onStateChange_1_1(bool connected, const hidl_string& reason) {
    if (mCb) mCb->onStateChange(connected, std::string(reason));
    return Void();
}

bool SecureElementBridge::connect() {
    mHal = hidl_se::V1_2::ISecureElement::getService(mInstance);
    if (mHal == nullptr) {
        LOG(ERROR) << "HIDL ISecureElement@1.2/" << mInstance << " not found";
        return false;
    }
    LOG(INFO) << "bound to HIDL ISecureElement@1.2/" << mInstance;
    return true;
}

ndk::ScopedAStatus SecureElementBridge::init(
        const std::shared_ptr<aidl::android::hardware::secure_element::ISecureElementCallback>& cb) {
    std::lock_guard<std::mutex> l(mLock);
    if (cb == nullptr) return ndk::ScopedAStatus::fromServiceSpecificError(ISecureElement::FAILED);
    mHidlCb = new HidlSeCallback(cb);
    auto r = mHal->init_1_1(mHidlCb);
    if (!r.isOk()) return deadHal(r.description());
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SecureElementBridge::getAtr(std::vector<uint8_t>* out) {
    auto r = mHal->getAtr([&](const hidl_vec<uint8_t>& atr) { *out = atr; });
    if (!r.isOk()) return deadHal(r.description());
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SecureElementBridge::isCardPresent(bool* out) {
    auto r = mHal->isCardPresent();
    if (!r.isOk()) return deadHal(r.description());
    *out = static_cast<bool>(r);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SecureElementBridge::transmit(const std::vector<uint8_t>& data,
                                                 std::vector<uint8_t>* out) {
    std::vector<uint8_t> rsp;
    auto r = mHal->transmit(data, [&](const hidl_vec<uint8_t>& v) { rsp = v; });
    if (!r.isOk()) return deadHal(r.description());
    // HIDL does not return a status: an empty response = a failed exchange (the stock Terminal.java treats it the same way).
    if (rsp.empty()) return ndk::ScopedAStatus::fromServiceSpecificError(ISecureElement::IOERROR);
    *out = std::move(rsp);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus SecureElementBridge::openLogicalChannel(const std::vector<uint8_t>& aid, int8_t p2,
                                                           LogicalChannelResponse* out) {
    HStatus st = HStatus::FAILED;
    auto r = mHal->openLogicalChannel(
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
    auto r = mHal->openBasicChannel(aid, static_cast<uint8_t>(p2),
                                    [&](const hidl_vec<uint8_t>& sel, HStatus s) {
                                        st = s;
                                        *out = sel;
                                    });
    if (!r.isOk()) return deadHal(r.description());
    return fromHidl(st);
}

ndk::ScopedAStatus SecureElementBridge::closeChannel(int8_t channelNumber) {
    auto r = mHal->closeChannel(static_cast<uint8_t>(channelNumber));
    if (!r.isOk()) return deadHal(r.description());
    return fromHidl(static_cast<HStatus>(r));
}

ndk::ScopedAStatus SecureElementBridge::reset() {
    auto r = mHal->reset();
    if (!r.isOk()) return deadHal(r.description());
    return fromHidl(static_cast<HStatus>(r));
}

}  // namespace mindone::bridges::secure_element
