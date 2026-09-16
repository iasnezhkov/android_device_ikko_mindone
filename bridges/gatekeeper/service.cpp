/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
// mindone: entry point of the gatekeeper AIDL->HIDL bridge.
//
// Review 14.09: binds through GatekeeperHidlConn instead of a raw sp<> so a TEE-side crash after
// startup can be detected and recovered from (BRIDGES-REVIEW-1409).
#define LOG_TAG "mindone-bridge-gatekeeper"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <memory>

#include "GatekeeperBridge.h"
#include "GatekeeperHidlConn.h"

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    // Stock HIDL service (gatekeeper@1.0-service + gatekeeper.<board>.so) - wait for it, blocking.
    auto conn = std::make_shared<mindone::GatekeeperHidlConn>();
    if (!conn->connect()) {
        LOG(ERROR) << "HIDL gatekeeper@1.0 not available";
        return 1;
    }
    auto bridge = ndk::SharedRefBase::make<mindone::GatekeeperBridge>(conn);
    const std::string instance = std::string(mindone::GatekeeperBridge::descriptor) + "/default";
    binder_status_t st = AServiceManager_addService(bridge->asBinder().get(), instance.c_str());
    if (st != STATUS_OK) {
        LOG(ERROR) << "addService(" << instance << ") failed: " << st;
        return 1;
    }
    LOG(INFO) << "registered " << instance << " over HIDL gatekeeper@1.0";
    ABinderProcess_joinThreadPool();
    return 1;  // unreachable
}
