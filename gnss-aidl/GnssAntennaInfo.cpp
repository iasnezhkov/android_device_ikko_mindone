/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "GnssAntennaInfo.h"

#include <android/hardware/gnss/2.1/IGnssAntennaInfoCallback.h>

#include <cmath>

namespace mindone::gnss {

class AntennaCallback final : public H::V2_1::IGnssAntennaInfoCallback {
  public:
    explicit AntennaCallback(std::shared_ptr<CallbackSlot<A::IGnssAntennaInfoCallback>> slot)
        : mSlot(std::move(slot)) {}
    ::android::hardware::Return<void> gnssAntennaInfoCb(
            const ::android::hardware::hidl_vec<GnssAntennaInfo>& sources) override {
        auto callback = mSlot->get();
        if (!callback)
            return ::android::hardware::Void();
        using C = A::IGnssAntennaInfoCallback;
        std::vector<C::GnssAntennaInfo> converted;
        for (const auto& source : sources) {
            double frequency = source.carrierFrequencyMHz * 1e6;
            if (!std::isfinite(frequency) || frequency < 0 || frequency >= 0x1p63)
                return ::android::hardware::Void();
            C::GnssAntennaInfo result;
            result.carrierFrequencyHz = static_cast<int64_t>(frequency);
            const auto& coordinate = source.phaseCenterOffsetCoordinateMillimeters;
            auto& target = result.phaseCenterOffsetCoordinateMillimeters;
            target.x = coordinate.x;
            target.xUncertainty = coordinate.xUncertainty;
            target.y = coordinate.y;
            target.yUncertainty = coordinate.yUncertainty;
            target.z = coordinate.z;
            target.zUncertainty = coordinate.zUncertainty;
            auto rows = [](const auto& input, auto& output) {
                for (const auto& row : input) {
                    C::Row entry;
                    entry.row.assign(row.row.begin(), row.row.end());
                    output.push_back(std::move(entry));
                }
            };
            rows(source.phaseCenterVariationCorrectionMillimeters,
                 result.phaseCenterVariationCorrectionMillimeters);
            rows(source.phaseCenterVariationCorrectionUncertaintyMillimeters,
                 result.phaseCenterVariationCorrectionUncertaintyMillimeters);
            rows(source.signalGainCorrectionDbi, result.signalGainCorrectionDbi);
            rows(source.signalGainCorrectionUncertaintyDbi,
                 result.signalGainCorrectionUncertaintyDbi);
            converted.push_back(std::move(result));
        }
        callback->gnssAntennaInfoCb(converted);
        return ::android::hardware::Void();
    }

  private:
    const std::shared_ptr<CallbackSlot<A::IGnssAntennaInfoCallback>> mSlot;
};

GnssAntennaInfo::GnssAntennaInfo(const ::android::sp<H::V2_1::IGnss>& backend) {
    auto extension = backend->getExtensionGnssAntennaInfo();
    if (extension.isOk())
        mBackend = extension;
}

ndk::ScopedAStatus GnssAntennaInfo::reset() {
    if (!mCallback)
        return ndk::ScopedAStatus::ok();
    mCallback->invalidate();
    mCallback.reset();
    return extensionStatus(mBackend->close());
}

void GnssAntennaInfo::shutdown() {
    std::lock_guard lock(mMutex);
    auto result = reset();
    static_cast<void>(result.isOk());
}

ndk::ScopedAStatus GnssAntennaInfo::close() {
    if (callbacksInFlight)
        return extensionError();
    std::unique_lock lock(mMutex, std::try_to_lock);
    return lock ? reset() : extensionError();
}

ndk::ScopedAStatus GnssAntennaInfo::setCallback(
        const std::shared_ptr<A::IGnssAntennaInfoCallback>& callback) {
    if (!callback)
        return extensionError(A::IGnss::ERROR_INVALID_ARGUMENT);
    if (callbacksInFlight)
        return extensionError();
    std::unique_lock lock(mMutex, std::try_to_lock);
    if (!lock || !mBackend)
        return extensionError();
    if (mCallback)
        return extensionError(A::IGnss::ERROR_ALREADY_INIT);
    mCallback = std::make_shared<CallbackSlot<A::IGnssAntennaInfoCallback>>(callback);
    ::android::sp<AntennaCallback> bridge = new AntennaCallback(mCallback);
    auto result = mBackend->setCallback(bridge);
    using S = H::V2_1::IGnssAntennaInfo::GnssAntennaInfoStatus;
    if (result.isOk() && static_cast<S>(result) == S::SUCCESS)
        return ndk::ScopedAStatus::ok();
    mCallback->invalidate();
    mCallback.reset();
    return extensionError(result.isOk() && static_cast<S>(result) == S::ERROR_ALREADY_INIT
                                  ? A::IGnss::ERROR_ALREADY_INIT
                                  : A::IGnss::ERROR_GENERIC);
}

}
