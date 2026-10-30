/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <hidl/HidlTransportSupport.h>
#include <hwbinder/ProcessState.h>

#include <cstdlib>

#include "Gnss.h"

int main() {
    ::android::hardware::configureRpcThreadpool(4, false);
    ::android::hardware::ProcessState::self()->startThreadPool();
    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();
    auto backend = mindone::gnss::loadBackend();
    CHECK(backend != nullptr);
    auto gnss = ndk::SharedRefBase::make<mindone::gnss::Gnss>(backend);
    CHECK_EQ(AServiceManager_addService(gnss->asBinder().get(),
                                        "android.hardware.gnss.IGnss/default"),
             STATUS_OK);
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
