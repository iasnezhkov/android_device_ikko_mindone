/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
// mindone: connection lifecycle for the stock HIDL tetheroffload.{config@1.0,control@1.0/1.1}
// services. Handles linkToDeath() on both interfaces + a background reconnect loop, so a crash
// of the stock tetheroffloadservice process does not permanently wedge every subsequent AIDL
// call (the previous version bound both sp<>s once at process start and never revisited them -
// see BRIDGES-REVIEW-1409 "tetheroffload: death/reconnect"). Modeled on
// bridges/secure_element/SecureElementBridge's death/reconnect pattern.
#pragma once

#include <android/hardware/tetheroffload/config/1.0/IOffloadConfig.h>
#include <android/hardware/tetheroffload/control/1.1/IOffloadControl.h>

#include <atomic>
#include <functional>
#include <mutex>

namespace mindone::bridges::tetheroffload {

namespace hcfg = ::android::hardware::tetheroffload::config::V1_0;
namespace hctl0 = ::android::hardware::tetheroffload::control::V1_0;
namespace hctl1 = ::android::hardware::tetheroffload::control::V1_1;

class OffloadDeathRecipient;  // defined in OffloadHidlConn.cpp

// Notifies of a HIDL availability transition: true = (re)connected, false = just died.
using OffloadStateCallback = std::function<void(bool available)>;

class OffloadHidlConn {
  public:
    OffloadHidlConn();
    ~OffloadHidlConn();

    bool connect();  // false = the stock HIDL services were not found

    // All three accessors are thread-safe; take a local copy rather than caching the pointer.
    ::android::sp<hcfg::IOffloadConfig> config() const;
    ::android::sp<hctl0::IOffloadControl> control() const;    // base interface, non-null once connected
    ::android::sp<hctl1::IOffloadControl> control11() const;  // may be null (1.0-only backend)

    void setStateCallback(OffloadStateCallback cb);

  private:
    friend class OffloadDeathRecipient;
    void onServiceDied();

    mutable std::mutex mLock;
    ::android::sp<hcfg::IOffloadConfig> mConfig;
    ::android::sp<hctl0::IOffloadControl> mControl;
    ::android::sp<hctl1::IOffloadControl> mControl11;
    ::android::sp<OffloadDeathRecipient> mDeathRecipient;
    std::atomic<bool> mHandlingDeath{false};
    OffloadStateCallback mCb;
};

}  // namespace mindone::bridges::tetheroffload
