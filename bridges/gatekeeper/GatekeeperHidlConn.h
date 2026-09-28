/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <android/hardware/gatekeeper/1.0/IGatekeeper.h>

#include <functional>
#include <mutex>

namespace mindone {

class GkDeathRecipient;  // defined in GatekeeperHidlConn.cpp

// Notifies of a HIDL availability transition: true = (re)connected, false = just died.
using GkStateCallback = std::function<void(bool available)>;

class GatekeeperHidlConn {
  public:
    GatekeeperHidlConn();
    ~GatekeeperHidlConn();

    // Binds IGatekeeper::getService() and installs the death recipient. Returns false if the
    // HIDL service is not registered right now.
    bool connect();

    // Current live handle, or nullptr if the HIDL service is currently down (death detected,
    // reconnect in progress). Thread-safe; take a local copy rather than caching this pointer.
    ::android::sp<::android::hardware::gatekeeper::V1_0::IGatekeeper> get() const;

    void setStateCallback(GkStateCallback cb);

  private:
    friend class GkDeathRecipient;
    void onServiceDied();

    mutable std::mutex mLock;
    ::android::sp<::android::hardware::gatekeeper::V1_0::IGatekeeper> mHidl;
    ::android::sp<GkDeathRecipient> mDeathRecipient;
    GkStateCallback mCb;
};

}  // namespace mindone
