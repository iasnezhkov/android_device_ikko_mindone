/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "TetherOffloadBridge"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <hidl/HidlTransportSupport.h>

#include <memory>
#include <thread>

#include "OffloadBridge.h"
#include "OffloadHidlConn.h"

using mindone::bridges::tetheroffload::OffloadBridge;
using mindone::bridges::tetheroffload::OffloadHidlConn;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(1);
    // hwbinder looper for the stock HAL's onEvent/updateTimeout callbacks (see fingerprint/service.cpp)
    android::hardware::configureRpcThreadpool(1, true /*callerWillJoin*/);
    std::thread([] { android::hardware::joinRpcThreadpool(); }).detach();
    auto conn = std::make_shared<OffloadHidlConn>();
    auto bridge = ndk::SharedRefBase::make<OffloadBridge>(conn);
    if (!bridge->connect()) return 1;
    const std::string name = std::string(OffloadBridge::descriptor) + "/default";
    binder_status_t st = AServiceManager_addService(bridge->asBinder().get(), name.c_str());
    if (st != STATUS_OK) {
        LOG(ERROR) << "addService(" << name << ") failed: " << st;
        return 1;
    }
    LOG(INFO) << "registered " << name;
    ABinderProcess_joinThreadPool();
    return 0;
}
