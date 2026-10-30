/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "mindone.camera.aidl"

#include "CameraProvider.h"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <hidl/HidlTransportSupport.h>
#include <hidl/LegacySupport.h>
#include <log/log.h>

#include <csignal>
#include <cstdlib>
#include <dlfcn.h>
#include <thread>

extern "C" ::android::hardware::camera::provider::V2_4::ICameraProvider*
HIDL_FETCH_ICameraProvider(const char* name);

int main() {
    ALOGW("Skipping vendor C++ ULog and ScenarioRecorder initializers");
    ALOGI("Camera provider starting: ADV_CAM_SUPPORT(0)");
    signal(SIGPIPE, SIG_IGN);
    ::android::hardware::configureRpcThreadpool(16, true);
    ABinderProcess_setThreadPoolMaxThreadCount(16);
    ABinderProcess_startThreadPool();

    void* library = dlopen("/vendor/lib64/hw/"
                          "android.hardware.camera.provider@2.6-impl-mediatek.so", RTLD_NOW);
    LOG_ALWAYS_FATAL_IF(!library, "Provider dlopen failed: %s", dlerror());
    auto fetch = reinterpret_cast<decltype(&HIDL_FETCH_ICameraProvider)>(
            dlsym(library, "HIDL_FETCH_ICameraProvider"));
    LOG_ALWAYS_FATAL_IF(!fetch, "Provider factory missing: %s", dlerror());
    ::android::sp<mindone::camera::hp::V2_4::ICameraProvider> base = fetch("internal/0");
    LOG_ALWAYS_FATAL_IF(!base, "Provider factory returned null");
    auto cast = mindone::camera::hp::V2_6::ICameraProvider::castFrom(base);
    LOG_ALWAYS_FATAL_IF(!cast.isOk(), "Provider 2.6 cast transaction failed");
    ::android::sp<mindone::camera::hp::V2_6::ICameraProvider> backend = cast;
    LOG_ALWAYS_FATAL_IF(!backend, "Provider does not implement 2.6");

    const char* descriptors[] = {
        "vendor.mediatek.hardware.camera.isphal@1.0::IISPModule",
        "vendor.mediatek.hardware.camera.isphal@1.1::IISPModule",
        "vendor.mediatek.hardware.camera.postproc@1.0::IPostDevice",
        "vendor.mediatek.hardware.camera.bgservice@1.1::IBGService",
        "vendor.mediatek.hardware.camera.atms@1.0::IATMs",
    };
    for (size_t i = 0; i < 5; ++i) {
        if (i == 4) ALOGW("Skipping vendor C++ IGrallocHelper initialization");
        auto result = ::android::hardware::registerPassthroughServiceImplementation(
                descriptors[i], descriptors[i], i == 4 ? "default" : "internal/0");
        if (result != ::android::OK) ALOGW("Private registration %s failed: %d",
                                          descriptors[i], result);
    }
    ALOGW("Skipping vendor C++ getMtkcamModuleFactory DebugExif call");
    auto provider = ndk::SharedRefBase::make<mindone::camera::CameraProvider>(backend);
    auto result = AServiceManager_addService(provider->asBinder().get(),
            "android.hardware.camera.provider.ICameraProvider/internal/0");
    LOG_ALWAYS_FATAL_IF(result != STATUS_OK, "AIDL provider registration failed: %d", result);
    std::thread([] { ABinderProcess_joinThreadPool(); }).detach();
    ::android::hardware::joinRpcThreadpool();
    return EXIT_FAILURE;
}
