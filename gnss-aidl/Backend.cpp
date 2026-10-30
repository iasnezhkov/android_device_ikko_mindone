/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "mindone-gnss"

#include "Backend.h"

#include <dlfcn.h>
#include <log/log.h>

namespace mindone::gnss {

namespace H = ::android::hardware::gnss;
using ::android::sp;

sp<H::V2_1::IGnss> loadBackend() {
    static const sp<H::V2_1::IGnss> backend = []() -> sp<H::V2_1::IGnss> {
        static_assert(sizeof(void*) == 8);
        void* handle = dlopen("/vendor/lib64/hw/android.hardware.gnss@2.1-impl-mediatek.so",
                              RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr) {
            ALOGE("Cannot load GNSS implementation: %s", dlerror());
            return nullptr;
        }
        using Fetch = H::V2_1::IGnss* (*)(const char*);
        auto fetch = reinterpret_cast<Fetch>(dlsym(handle, "HIDL_FETCH_IGnss"));
        if (fetch == nullptr) {
            ALOGE("GNSS implementation has no HIDL_FETCH_IGnss: %s", dlerror());
            return nullptr;
        }
        sp<H::V1_0::IGnss> base = fetch("default");
        if (base == nullptr || base->isRemote()) {
            ALOGE("GNSS factory did not return a local implementation");
            return nullptr;
        }
        auto cast = H::V2_1::IGnss::castFrom(base);
        if (!cast.isOk()) {
            ALOGE("GNSS 2.1 cast failed: %s", cast.description().c_str());
            return nullptr;
        }
        sp<H::V2_1::IGnss> result = cast;
        if (result == nullptr) {
            ALOGE("GNSS implementation does not support HIDL 2.1");
        }
        return result;
    }();
    return backend;
}

}
