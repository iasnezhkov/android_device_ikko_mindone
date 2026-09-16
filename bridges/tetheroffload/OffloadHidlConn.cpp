/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "TetherOffloadBridge"

#include "OffloadHidlConn.h"

#include <android-base/logging.h>

#include <chrono>
#include <thread>

namespace mindone::bridges::tetheroffload {

using ::android::hardware::hidl_death_recipient;

// Delivered on its own binder-death callback thread (NOT the hwbinder pool thread) whenever the
// stock tetheroffloadservice process dies. See OffloadHidlConn::onServiceDied().
class OffloadDeathRecipient : public hidl_death_recipient {
  public:
    explicit OffloadDeathRecipient(OffloadHidlConn* conn) : mConn(conn) {}
    void serviceDied(uint64_t /*cookie*/,
                      const ::android::wp<::android::hidl::base::V1_0::IBase>& /*who*/) override {
        mConn->onServiceDied();
    }

  private:
    OffloadHidlConn* mConn;  // not owned
};

OffloadHidlConn::OffloadHidlConn() = default;
OffloadHidlConn::~OffloadHidlConn() = default;

bool OffloadHidlConn::connect() {
    auto config = hcfg::IOffloadConfig::getService();
    auto control11 = hctl1::IOffloadControl::getService();
    auto control = control11 != nullptr ? ::android::sp<hctl0::IOffloadControl>(control11)
                                        : hctl0::IOffloadControl::getService();
    if (config == nullptr || control == nullptr) {
        LOG(ERROR) << "HIDL tetheroffload not found: config=" << (config != nullptr)
                   << " control=" << (control != nullptr);
        return false;
    }
    // config and control are two independent HIDL proxy objects even though, on this board,
    // they are served by the same physical stock process - link both explicitly rather than
    // assuming one death notification covers the other.
    auto death = ::android::sp<OffloadDeathRecipient>(new OffloadDeathRecipient(this));
    config->linkToDeath(death, 0 /*cookie, unused*/);
    control->linkToDeath(death, 1 /*cookie, unused*/);
    {
        std::lock_guard<std::mutex> l(mLock);
        mConfig = config;
        mControl = control;
        mControl11 = control11;
        mDeathRecipient = death;
    }
    mHandlingDeath.store(false, std::memory_order_relaxed);
    LOG(INFO) << "bound to HIDL IOffloadConfig@1.0 + IOffloadControl@" << (control11 ? "1.1" : "1.0");
    return true;
}

::android::sp<hcfg::IOffloadConfig> OffloadHidlConn::config() const {
    std::lock_guard<std::mutex> l(mLock);
    return mConfig;
}

::android::sp<hctl0::IOffloadControl> OffloadHidlConn::control() const {
    std::lock_guard<std::mutex> l(mLock);
    return mControl;
}

::android::sp<hctl1::IOffloadControl> OffloadHidlConn::control11() const {
    std::lock_guard<std::mutex> l(mLock);
    return mControl11;
}

void OffloadHidlConn::setStateCallback(OffloadStateCallback cb) {
    std::lock_guard<std::mutex> l(mLock);
    mCb = std::move(cb);
}

void OffloadHidlConn::onServiceDied() {
    // config and control both link to the same recipient; on this board they are the same
    // physical process, so a near-simultaneous double death notification (one per interface) is
    // the expected case - only react to the first one.
    if (mHandlingDeath.exchange(true, std::memory_order_relaxed)) return;
    LOG(ERROR) << "HIDL tetheroffload died";
    OffloadStateCallback cb;
    {
        std::lock_guard<std::mutex> l(mLock);
        mConfig = nullptr;
        mControl = nullptr;
        mControl11 = nullptr;
        cb = mCb;
    }
    if (cb) cb(false);

    // Reconnect on our own background thread - never on the hwbinder pool thread that delivered
    // this death notification.
    std::thread([this] {
        while (!connect()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        LOG(INFO) << "reconnected to HIDL tetheroffload after a crash";
        OffloadStateCallback cb2;
        {
            std::lock_guard<std::mutex> l(mLock);
            cb2 = mCb;
        }
        if (cb2) cb2(true);
    }).detach();
}

}  // namespace mindone::bridges::tetheroffload
