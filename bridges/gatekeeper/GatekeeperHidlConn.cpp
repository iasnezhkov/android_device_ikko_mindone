/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "mindone-bridge-gatekeeper"

#include "GatekeeperHidlConn.h"

#include <android-base/logging.h>

#include <chrono>
#include <thread>

namespace mindone {

using ::android::hardware::gatekeeper::V1_0::IGatekeeper;
using ::android::hardware::hidl_death_recipient;

// Delivered on its own binder-death callback thread (NOT the hwbinder pool thread) whenever the
// stock HIDL gatekeeper process dies. See GatekeeperHidlConn::onServiceDied().
class GkDeathRecipient : public hidl_death_recipient {
  public:
    explicit GkDeathRecipient(GatekeeperHidlConn* conn) : mConn(conn) {}
    void serviceDied(uint64_t /*cookie*/,
                      const ::android::wp<::android::hidl::base::V1_0::IBase>& /*who*/) override {
        mConn->onServiceDied();
    }

  private:
    GatekeeperHidlConn* mConn;  // not owned
};

GatekeeperHidlConn::GatekeeperHidlConn() = default;
GatekeeperHidlConn::~GatekeeperHidlConn() = default;

bool GatekeeperHidlConn::connect() {
    auto hidl = IGatekeeper::getService();
    if (hidl == nullptr) {
        LOG(ERROR) << "HIDL gatekeeper@1.0 not available";
        return false;
    }
    auto death = ::android::sp<GkDeathRecipient>(new GkDeathRecipient(this));
    hidl->linkToDeath(death, 0 /*cookie, unused*/);
    {
        std::lock_guard<std::mutex> l(mLock);
        mHidl = hidl;
        mDeathRecipient = death;
    }
    LOG(INFO) << "bound to HIDL gatekeeper@1.0";
    return true;
}

::android::sp<IGatekeeper> GatekeeperHidlConn::get() const {
    std::lock_guard<std::mutex> l(mLock);
    return mHidl;
}

void GatekeeperHidlConn::setStateCallback(GkStateCallback cb) {
    std::lock_guard<std::mutex> l(mLock);
    mCb = std::move(cb);
}

void GatekeeperHidlConn::onServiceDied() {
    LOG(ERROR) << "HIDL gatekeeper@1.0 died";
    GkStateCallback cb;
    {
        std::lock_guard<std::mutex> l(mLock);
        mHidl = nullptr;
        cb = mCb;
    }
    if (cb) cb(false);

    // Reconnect on our own background thread - never on the hwbinder pool thread that delivered
    // this death notification (same reasoning as RadioSeClient::onServiceDied() in the
    // secure_element v2 bridge, bridges/secure_element/SecureElementBridge.cpp). Any enroll()/verify()
    // call in flight when the TEE service died already returns promptly with a transport error
    // (HIDL synchronous calls fail immediately on a dead binder, they do not hang) - there is no
    // "in-flight request" bookkeeping to unblock here, unlike RadioSeClient's oneway/callback
    // request model.
    std::thread([this] {
        while (!connect()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        LOG(INFO) << "reconnected to HIDL gatekeeper@1.0 after a crash";
        GkStateCallback cb2;
        {
            std::lock_guard<std::mutex> l(mLock);
            cb2 = mCb;
        }
        if (cb2) cb2(true);
    }).detach();
}

}  // namespace mindone
