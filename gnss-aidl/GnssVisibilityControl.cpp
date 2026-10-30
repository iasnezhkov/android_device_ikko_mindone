/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "GnssVisibilityControl.h"

#include <android/hardware/gnss/visibility_control/1.0/IGnssVisibilityControlCallback.h>

namespace mindone::gnss {

class VisibilityCallback final : public HV::IGnssVisibilityControlCallback {
  public:
    explicit VisibilityCallback(
            std::shared_ptr<CallbackSlot<V::IGnssVisibilityControlCallback>> slot)
        : mSlot(std::move(slot)) {}
    ::android::hardware::Return<void> nfwNotifyCb(const NfwNotification& source) override {
        if (auto callback = mSlot->get()) {
            using C = V::IGnssVisibilityControlCallback;
            C::NfwNotification result;
            result.proxyAppPackageName = source.proxyAppPackageName.c_str();
            result.protocolStack = static_cast<C::NfwProtocolStack>(source.protocolStack);
            result.otherProtocolStackName = source.otherProtocolStackName.c_str();
            result.requestor = static_cast<C::NfwRequestor>(source.requestor);
            result.requestorId = source.requestorId.c_str();
            result.responseType = static_cast<C::NfwResponseType>(source.responseType);
            result.inEmergencyMode = source.inEmergencyMode;
            result.isCachedLocation = source.isCachedLocation;
            callback->nfwNotifyCb(result);
        }
        return ::android::hardware::Void();
    }
    ::android::hardware::Return<bool> isInEmergencySession() override {
        bool emergency = false;
        if (auto callback = mSlot->get()) {
            if (!callback->isInEmergencySession(&emergency).isOk())
                emergency = false;
        }
        return emergency;
    }

  private:
    const std::shared_ptr<CallbackSlot<V::IGnssVisibilityControlCallback>> mSlot;
};

GnssVisibilityControl::GnssVisibilityControl(
        const ::android::sp<::android::hardware::gnss::V2_1::IGnss>& backend) {
    auto extension = backend->getExtensionVisibilityControl();
    if (extension.isOk())
        mBackend = extension;
}

void GnssVisibilityControl::shutdown() {
    std::lock_guard lock(mMutex);
    if (mCallback)
        mCallback->invalidate();
    mCallback.reset();
}

ndk::ScopedAStatus GnssVisibilityControl::setCallback(
        const std::shared_ptr<V::IGnssVisibilityControlCallback>& callback) {
    if (!callback)
        return extensionError(::aidl::android::hardware::gnss::IGnss::ERROR_INVALID_ARGUMENT);
    if (callbacksInFlight)
        return extensionError();
    std::unique_lock lock(mMutex, std::try_to_lock);
    if (!lock || !mBackend)
        return extensionError();
    if (mCallback)
        mCallback->invalidate();
    mCallback = std::make_shared<CallbackSlot<V::IGnssVisibilityControlCallback>>(callback);
    ::android::sp<VisibilityCallback> bridge = new VisibilityCallback(mCallback);
    auto result = extensionStatus(mBackend->setCallback(bridge));
    if (!result.isOk()) {
        mCallback->invalidate();
        mCallback.reset();
    }
    return result;
}

ndk::ScopedAStatus GnssVisibilityControl::enableNfwLocationAccess(
        const std::vector<std::string>& proxyApps) {
    std::unique_lock lock(mMutex, std::try_to_lock);
    if (!lock || !mBackend)
        return extensionError();
    ::android::hardware::hidl_vec<::android::hardware::hidl_string> converted;
    converted.resize(proxyApps.size());
    for (size_t i = 0; i < proxyApps.size(); ++i) converted[i] = proxyApps[i];
    return extensionStatus(mBackend->enableNfwLocationAccess(converted));
}

}
