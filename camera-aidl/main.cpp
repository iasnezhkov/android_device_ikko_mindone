/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "mindone.camera.aidl"

#include "CameraProvider.h"
#include "MaximumResolution.h"
#include "OisControl.h"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <hidl/HidlTransportSupport.h>
#include <hidl/LegacySupport.h>
#include <log/log.h>

#include <csignal>
#include <cstdlib>
#include <dlfcn.h>
#include <thread>
#include <utility>

extern "C" ::android::hardware::camera::provider::V2_4::ICameraProvider*
HIDL_FETCH_ICameraProvider(const char* name);

int main() {
    ALOGW("Skipping vendor C++ ULog and ScenarioRecorder initializers");
    ALOGI("Camera provider starting: ADV_CAM_SUPPORT(0)");
    signal(SIGPIPE, SIG_IGN);
    ::android::hardware::configureRpcThreadpool(16, true);
    ABinderProcess_setThreadPoolMaxThreadCount(16);
    ABinderProcess_startThreadPool();

    const int32_t maximumResolutionMode = mindone::camera::applyMaximumResolutionSwitch();
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

    const std::pair<const char*, const char*> services[] = {
        {"vendor.mediatek.hardware.camera.isphal@1.0::IISPModule", "internal/0"},
        {"vendor.mediatek.hardware.camera.isphal@1.1::IISPModule", "internal/0"},
        {"vendor.mediatek.hardware.camera.bgservice@1.1::IBGService", "internal/0"},
        {"vendor.mediatek.hardware.camera.atms@1.0::IATMs", "default"},
    };
    ALOGW("Skipping vendor C++ IGrallocHelper initialization");
    for (const auto& [descriptor, instance] : services) {
        auto result = ::android::hardware::registerPassthroughServiceImplementation(
                descriptor, descriptor, instance);
        if (result != ::android::OK) ALOGW("Private registration %s failed: %d", descriptor, result);
    }
    ALOGW("Skipping vendor C++ getMtkcamModuleFactory DebugExif call");
    const bool ois = mindone::camera::oisAvailable();
    ALOGI("Optical stabilization: %s", ois ? "on" : "off");
    auto provider = ndk::SharedRefBase::make<mindone::camera::CameraProvider>(
            backend, mindone::camera::loadMaximumResolution(maximumResolutionMode, backend), ois);
    auto result = AServiceManager_addService(provider->asBinder().get(),
            "android.hardware.camera.provider.ICameraProvider/internal/0");
    LOG_ALWAYS_FATAL_IF(result != STATUS_OK, "AIDL provider registration failed: %d", result);
    std::thread([] { ABinderProcess_joinThreadPool(); }).detach();
    ::android::hardware::joinRpcThreadpool();
    return EXIT_FAILURE;
}
