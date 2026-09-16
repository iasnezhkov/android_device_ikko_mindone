/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
// mindone: entry point of the fingerprint AIDL->HIDL bridge.
//
// Review 14.09: binds through FingerprintHidlConn instead of a raw sp<> so a HAL crash after
// startup can be detected and recovered from (BRIDGES-REVIEW-1409).
#define LOG_TAG "mindone-bridge-fingerprint"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <hidl/HidlTransportSupport.h>

#include <memory>
#include <thread>

#include "FingerprintBridge.h"
#include "FingerprintHidlConn.h"

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    // The stock HAL's HIDL callbacks arrive over hwbinder - a dedicated looper thread is needed
    // (setThreadPoolConfiguration does not spawn threads itself; without a looper, onAcquired/onAuthenticated
    // would never be delivered).
    android::hardware::configureRpcThreadpool(1, true /*callerWillJoin*/);
    std::thread([] { android::hardware::joinRpcThreadpool(); }).detach();

    auto conn = std::make_shared<mindone::FingerprintHidlConn>();
    if (!conn->connect()) {
        LOG(ERROR) << "HIDL biometrics.fingerprint@2.1 not available";
        return 1;
    }
    auto bridge = ndk::SharedRefBase::make<mindone::FingerprintBridge>(conn);
    const std::string instance = std::string(mindone::FingerprintBridge::descriptor) + "/default";
    binder_status_t st = AServiceManager_addService(bridge->asBinder().get(), instance.c_str());
    if (st != STATUS_OK) {
        LOG(ERROR) << "addService(" << instance << ") failed: " << st;
        return 1;
    }
    LOG(INFO) << "registered " << instance << " over HIDL fingerprint@2.1";
    ABinderProcess_joinThreadPool();
    return 1;
}
