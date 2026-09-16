/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "mindone.sensors"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "Sensors.h"

using ::aidl::android::hardware::sensors::ISensors;
using ::mindone::sensors::Sensors;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    std::shared_ptr<Sensors> sensors = ndk::SharedRefBase::make<Sensors>();

    const std::string instance = std::string(ISensors::descriptor) + "/default";
    binder_status_t status =
            AServiceManager_addService(sensors->asBinder().get(), instance.c_str());
    CHECK_EQ(status, STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should never reach here
}
