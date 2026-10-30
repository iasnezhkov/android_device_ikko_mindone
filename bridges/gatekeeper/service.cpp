/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "mindone-gatekeeper"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "GatekeeperBridge.h"

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    auto gatekeeper = mindone::GatekeeperBridge::open();
    if (gatekeeper == nullptr) return 1;
    const std::string instance = std::string(mindone::GatekeeperBridge::descriptor) + "/default";
    binder_status_t st = AServiceManager_addService(gatekeeper->asBinder().get(), instance.c_str());
    if (st != STATUS_OK) {
        LOG(ERROR) << "addService(" << instance << ") failed: " << st;
        return 1;
    }
    LOG(INFO) << "registered " << instance;
    ABinderProcess_joinThreadPool();
    return 1;
}
