/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "AGnssRil.h"

#include <android/hardware/gnss/1.0/IAGnssRilCallback.h>

#include <limits>

namespace mindone::gnss {

class AGnssRilCallback final : public H::V1_0::IAGnssRilCallback {
  public:
    explicit AGnssRilCallback(std::shared_ptr<CallbackSlot<A::IAGnssRilCallback>> slot)
        : mSlot(std::move(slot)) {}
    ::android::hardware::Return<void> requestSetIdCb(uint32_t flags) override {
        if (auto callback = mSlot->get())
            callback->requestSetIdCb(flags & 3);
        return ::android::hardware::Void();
    }
    ::android::hardware::Return<void> requestRefLocCb() override {
        if (auto callback = mSlot->get())
            callback->requestRefLocCb();
        return ::android::hardware::Void();
    }

  private:
    const std::shared_ptr<CallbackSlot<A::IAGnssRilCallback>> mSlot;
};

AGnssRil::AGnssRil(const ::android::sp<H::V2_1::IGnss>& backend) {
    auto extension = backend->getExtensionAGnssRil_2_0();
    if (extension.isOk())
        mBackend = extension;
}

void AGnssRil::shutdown() {
    std::lock_guard lock(mMutex);
    if (mCallback)
        mCallback->invalidate();
    mCallback.reset();
}

ndk::ScopedAStatus AGnssRil::setCallback(const std::shared_ptr<A::IAGnssRilCallback>& callback) {
    if (!callback)
        return extensionError(A::IGnss::ERROR_INVALID_ARGUMENT);
    if (callbacksInFlight)
        return extensionError();
    return forward([&] {
        if (mCallback)
            mCallback->invalidate();
        mCallback = std::make_shared<CallbackSlot<A::IAGnssRilCallback>>(callback);
        ::android::sp<AGnssRilCallback> bridge = new AGnssRilCallback(mCallback);
        auto result = mBackend->setCallback(bridge);
        if (!result.isOk()) {
            mCallback->invalidate();
            mCallback.reset();
        }
        return result;
    });
}

ndk::ScopedAStatus AGnssRil::setRefLocation(const AGnssRefLocation& location) {
    const auto& cell = location.cellID;
    if (location.type == AGnssRefLocationType::NR_CELLID)
        return extensionError();
    int32_t type = static_cast<int32_t>(location.type);
    auto fits16 = [](int32_t value) { return value >= 0 && value <= 65535; };
    if ((type != 1 && type != 2 && type != 4) || cell.type != location.type || !fits16(cell.mcc) ||
        !fits16(cell.mnc) || !fits16(cell.lac) || !fits16(cell.tac) || !fits16(cell.pcid) ||
        cell.cid < 0 || cell.cid > std::numeric_limits<uint32_t>::max()) {
        return extensionError(A::IGnss::ERROR_INVALID_ARGUMENT);
    }
    H::V1_0::IAGnssRil::AGnssRefLocation converted{};
    converted.type = static_cast<H::V1_0::IAGnssRil::AGnssRefLocationType>(type);
    converted.cellID.type = converted.type;
    converted.cellID.mcc = cell.mcc;
    converted.cellID.mnc = cell.mnc;
    converted.cellID.lac = cell.lac;
    converted.cellID.cid = cell.cid;
    converted.cellID.tac = cell.tac;
    converted.cellID.pcid = cell.pcid;
    return forward([&] { return mBackend->setRefLocation(converted); });
}

ndk::ScopedAStatus AGnssRil::setSetId(SetIdType type, const std::string& id) {
    if (static_cast<int32_t>(type) < 0 || static_cast<int32_t>(type) > 2 ||
        (type == SetIdType::NONE && !id.empty())) {
        return extensionError(A::IGnss::ERROR_INVALID_ARGUMENT);
    }
    return forward([&] {
        return mBackend->setSetId(static_cast<H::V1_0::IAGnssRil::SetIDType>(type), id);
    });
}

ndk::ScopedAStatus AGnssRil::updateNetworkState(const NetworkAttributes& attributes) {
    if (attributes.isConnected && (attributes.capabilities & ~3) != 0) {
        return extensionError(A::IGnss::ERROR_INVALID_ARGUMENT);
    }
    H::V2_0::IAGnssRil::NetworkAttributes converted{};
    converted.networkHandle = static_cast<uint64_t>(attributes.networkHandle);
    converted.isConnected = attributes.isConnected;
    if (attributes.isConnected) {
        converted.capabilities = attributes.capabilities;
        converted.apn = attributes.apn;
    }
    return forward([&] { return mBackend->updateNetworkState_2_0(converted); });
}

}
