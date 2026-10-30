/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "TetherOffloadBridge"
#include "OffloadBridge.h"
#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
using mindone::bridges::tetheroffload::OffloadBridge;
int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(1);
    auto bridge = ndk::SharedRefBase::make<OffloadBridge>();
    if (!bridge->connect()) return 1;
    const std::string name = std::string(OffloadBridge::descriptor) + "/default";
    if (AServiceManager_addService(bridge->asBinder().get(), name.c_str()) != STATUS_OK) return 1;
    LOG(INFO) << "registered " << name;
    ABinderProcess_joinThreadPool();
    return 0;
}
