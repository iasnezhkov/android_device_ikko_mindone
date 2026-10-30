/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "mindone-bridge-fingerprint"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <memory>

#include "FingerprintBridge.h"
#include "FingerprintHidlConn.h"

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    auto conn = std::make_shared<mindone::FingerprintHidlConn>();
    if (!conn->connect()) {
        LOG(ERROR) << "Cannot open local fingerprint module";
        return 1;
    }
    auto bridge = ndk::SharedRefBase::make<mindone::FingerprintBridge>(conn);
    const std::string instance = std::string(mindone::FingerprintBridge::descriptor) + "/default";
    binder_status_t st = AServiceManager_addService(bridge->asBinder().get(), instance.c_str());
    if (st != STATUS_OK) {
        LOG(ERROR) << "addService(" << instance << ") failed: " << st;
        return 1;
    }
    LOG(INFO) << "registered " << instance << " with local fingerprint module";
    ABinderProcess_joinThreadPool();
    return 1;
}
