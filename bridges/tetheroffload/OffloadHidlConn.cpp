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
    void serviceDied(uint64_t cookie,
                      const ::android::wp<::android::hidl::base::V1_0::IBase>& /*who*/) override {
        mConn->onServiceDied(cookie);
    }

  private:
    OffloadHidlConn* mConn;  // not owned
};

OffloadHidlConn::OffloadHidlConn() = default;
OffloadHidlConn::~OffloadHidlConn() = default;

bool OffloadHidlConn::connect() {
    std::lock_guard<std::mutex> connecting(mConnectLock);
    uint64_t generation;
    {
        std::lock_guard<std::mutex> l(mLock);
        generation = ++mGeneration;
    }
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
    auto configLink = config->linkToDeath(death, generation);
    if (!configLink.isOk() || !static_cast<bool>(configLink)) return false;
    auto controlLink = control->linkToDeath(death, generation);
    if (!controlLink.isOk() || !static_cast<bool>(controlLink)) {
        auto unlink = config->unlinkToDeath(death);
        (void)unlink.isOk();
        return false;
    }
    bool ready;
    {
        std::lock_guard<std::mutex> l(mLock);
        ready = generation == mGeneration;
        if (ready) {
            mConfig = config;
            mControl = control;
            mControl11 = control11;
            mDeathRecipient = death;
        }
    }
    if (!ready) {
        auto configUnlink = config->unlinkToDeath(death);
        auto controlUnlink = control->unlinkToDeath(death);
        (void)configUnlink.isOk();
        (void)controlUnlink.isOk();
        return false;
    }
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

void OffloadHidlConn::onServiceDied(uint64_t generation) {
    // config and control both link to the same recipient; on this board they are the same
    // physical process, so a near-simultaneous double death notification (one per interface) is
    // the expected case - only react to the first one.
    std::unique_lock<std::mutex> event(mEventLock);
    OffloadStateCallback cb;
    {
        std::lock_guard<std::mutex> l(mLock);
        if (generation != mGeneration) return;
        ++mGeneration;
        mConfig = nullptr;
        mControl = nullptr;
        mControl11 = nullptr;
        cb = mCb;
    }
    if (cb) cb(false);
    event.unlock();
    if (mHandlingDeath.exchange(true, std::memory_order_relaxed)) return;

    // Reconnect on our own background thread - never on the hwbinder pool thread that delivered
    // this death notification.
    std::thread([this] {
        for (;;) {
            if (!connect()) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            std::lock_guard<std::mutex> event(mEventLock);
            OffloadStateCallback cb2;
            {
                std::lock_guard<std::mutex> l(mLock);
                if (mConfig == nullptr) continue;
                mHandlingDeath.store(false, std::memory_order_relaxed);
                cb2 = mCb;
            }
            if (cb2) cb2(true);
            return;
        }
    }).detach();
}

}  // namespace mindone::bridges::tetheroffload
