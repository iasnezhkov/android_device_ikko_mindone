/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "SecureElementBridge"

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <hidl/HidlTransportSupport.h>

#include <thread>

#include "SecureElementBridge.h"

using mindone::bridges::secure_element::SecureElementBridge;

int main() {
    // Comma-separated instances; stock has one - SIM1 (@1.2::ISecureElement/SIM1).
    const std::string list = android::base::GetProperty("ro.vendor.mindone.se.instances", "SIM1");
    ABinderProcess_setThreadPoolMaxThreadCount(2);
    // hwbinder looper for the stock HAL's onStateChange callback (see fingerprint/service.cpp)
    android::hardware::configureRpcThreadpool(1, true /*callerWillJoin*/);
    std::thread([] { android::hardware::joinRpcThreadpool(); }).detach();

    std::vector<std::shared_ptr<SecureElementBridge>> keep;
    int registered = 0;
    for (const auto& raw : android::base::Split(list, ",")) {
        const std::string inst = android::base::Trim(raw);
        if (inst.empty()) continue;
        auto se = ndk::SharedRefBase::make<SecureElementBridge>(inst);
        if (!se->connect()) continue;
        const std::string name = std::string(SecureElementBridge::descriptor) + "/" + inst;
        binder_status_t st = AServiceManager_addService(se->asBinder().get(), name.c_str());
        if (st != STATUS_OK) {
            LOG(ERROR) << "addService(" << name << ") failed: " << st;
            continue;
        }
        LOG(INFO) << "registered " << name;
        keep.push_back(se);
        registered++;
    }
    if (registered == 0) {
        LOG(ERROR) << "no instance registered — exiting";
        return 1;
    }
    ABinderProcess_joinThreadPool();
    return 0;
}
