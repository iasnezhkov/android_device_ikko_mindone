/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "color.h"

#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <time.h>
#include <unistd.h>

#ifndef CAMCOLOR_CONFIG_PATH
#define CAMCOLOR_CONFIG_PATH "/data/vendor/camera/color.conf"
#endif

#define CAMCOLOR_EXPORT __attribute__((visibility("default")))
#define CAMCOLOR_HIDDEN __attribute__((visibility("hidden")))

namespace {

constexpr char kTag[] = "mindone.camcolor";
constexpr char kRealLibrary[] = "libcameracustom.camera.isp_mtk.so";
constexpr char kEnableProperty[] = "persist.vendor.camera.color.enable";
constexpr char kConfigPath[] = CAMCOLOR_CONFIG_PATH;

constexpr char kCreateInstance[] = "_ZN11NSIspTuning15IspTuningCustom14createInstanceEjji";
constexpr char kVtable[] = "_ZTVN11NSIspTuning17CTIspTuningCustomE";
constexpr char kRefineCamInfo[] = "_ZN11NSIspTuning15IspTuningCustom14refine_CamInfoERNS_13RAWIspCamInfoE";
constexpr char kRefineCcm[] =
        "_ZN11NSIspTuning15IspTuningCustom10refine_CCMEhRKNS_13RAWIspCamInfoERNS_15ISP_NVRAM_CCM_TE";
constexpr char kRefineGgm[] =
        "_ZN11NSIspTuning15IspTuningCustom10refine_GGMEhRKNS_13RAWIspCamInfoERNS_15ISP_NVRAM_GGM_TE";
constexpr char kRefineColor[] =
        "_ZN11NSIspTuning15IspTuningCustom12refine_COLOREhRKNS_13RAWIspCamInfoERNS_17ISP_NVRAM_COLOR_TE";
constexpr char kUserSettingHsbc[] =
        "_ZN11NSIspTuning15IspTuningCustom16userSetting_HSBCERKNS_17IspUsrSelectLevelERNS_16ISP_NVRAM_G2CX_TE";
constexpr char kMapZoom[] = "_ZN11NSIspTuning15IspTuningCustom23map_zoom_value_to_indexEi";

constexpr size_t kVtableWords = 52;
constexpr size_t kAddressPoint = 2;
constexpr size_t kSlotRefineCamInfo = 5;
constexpr size_t kSlotRefineCcm = 16;
constexpr size_t kSlotRefineGgm = 17;
constexpr size_t kSlotRefineColor = 27;
constexpr size_t kSlotUserSettingHsbc = 39;
constexpr size_t kSlotMapZoom = 49;
constexpr size_t kProfileOffset = 0x24;
constexpr size_t kMaxConfigBytes = 64 * 1024;
constexpr int64_t kReloadIntervalNs = 1000000000;
constexpr int64_t kLogIntervalNs = 1000000000;

#define CAMCOLOR_FORWARD(index, name, bind) #name,
#define CAMCOLOR_WRAPPED(index, name) #name,
#define CAMCOLOR_DATA(name)
constexpr const char* kSymbolNames[] = {
#include "exports.def"
};
#undef CAMCOLOR_FORWARD
#undef CAMCOLOR_WRAPPED
#undef CAMCOLOR_DATA

constexpr size_t kSymbolCount = sizeof(kSymbolNames) / sizeof(kSymbolNames[0]);

constexpr bool same_string(const char* a, const char* b) {
    return *a == *b && (*a == '\0' || same_string(a + 1, b + 1));
}

static_assert(same_string(kSymbolNames[0], kCreateInstance), "createInstance must be slot 0");

}

extern "C" {
CAMCOLOR_HIDDEN void* camcolor_slots[kSymbolCount];
CAMCOLOR_HIDDEN void* camcolor_resolve(size_t index);
}

namespace {

using RefineFn = void (*)(void*, unsigned char, const void*, void*);
using HsbcFn = void (*)(void*, const void*, void*);

pthread_once_t g_load_once = PTHREAD_ONCE_INIT;
void* g_real = nullptr;
const void* g_real_vtable = nullptr;
RefineFn g_real_ccm = nullptr;
RefineFn g_real_ggm = nullptr;
HsbcFn g_real_hsbc = nullptr;

pthread_once_t g_hooks_once = PTHREAD_ONCE_INIT;
bool g_hooks_ready = false;
void* g_real_vptr = nullptr;
void* g_hook_vptr = nullptr;
alignas(16) void* g_hook_table[kVtableWords];

pthread_mutex_t g_enable_lock = PTHREAD_MUTEX_INITIALIZER;
const prop_info* g_enable_info = nullptr;
uint32_t g_enable_area_serial = 0;
uint32_t g_enable_serial = 0;
bool g_enable_searched = false;
bool g_enable_read = false;
bool g_enable_value = false;

struct ConfigState {
    bool initialized;
    bool checked;
    bool have_signature;
    int last_errno;
    int64_t last_check_ns;
    dev_t dev;
    ino_t ino;
    off_t size;
    int64_t mtime_ns;
    camcolor::Config config;
};

pthread_mutex_t g_config_lock = PTHREAD_MUTEX_INITIALIZER;
ConfigState g_config;
char g_config_text[kMaxConfigBytes + 1];

int64_t g_log_ccm = 0;
int64_t g_log_ggm = 0;
int64_t g_log_hsbc = 0;
int64_t g_log_vptr = 0;

int64_t now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

bool should_log(int64_t* last) {
    const int64_t now = now_ns();
    int64_t seen = __atomic_load_n(last, __ATOMIC_RELAXED);
    if (seen != 0 && now - seen < kLogIntervalNs) {
        return false;
    }
    return __atomic_compare_exchange_n(last, &seen, now, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

void load_real() {
    void* handle = dlopen(kRealLibrary, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "dlopen %s: %s", kRealLibrary, dlerror());
        return;
    }
    void* resolved[kSymbolCount];
    for (size_t i = 0; i < kSymbolCount; ++i) {
        resolved[i] = dlsym(handle, kSymbolNames[i]);
        if (resolved[i] == nullptr) {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "%s lacks %s", kRealLibrary, kSymbolNames[i]);
            return;
        }
    }
    g_real_vtable = dlsym(handle, kVtable);
    g_real_ccm = reinterpret_cast<RefineFn>(dlsym(handle, kRefineCcm));
    g_real_ggm = reinterpret_cast<RefineFn>(dlsym(handle, kRefineGgm));
    g_real_hsbc = reinterpret_cast<HsbcFn>(dlsym(handle, kUserSettingHsbc));
    g_real = handle;
    for (size_t i = 0; i < kSymbolCount; ++i) {
        __atomic_store_n(&camcolor_slots[i], resolved[i], __ATOMIC_RELEASE);
    }
    __android_log_print(ANDROID_LOG_INFO, kTag, "forwarding %zu functions to %s", kSymbolCount,
                        kRealLibrary);
}

__attribute__((constructor)) void camcolor_init() {
    pthread_once(&g_load_once, load_real);
}

void read_flag(void* cookie, const char*, const char* value, uint32_t) {
    *static_cast<bool*>(cookie) = strcmp(value, "1") == 0 || strcmp(value, "true") == 0 ||
                                  strcmp(value, "y") == 0 || strcmp(value, "yes") == 0 ||
                                  strcmp(value, "on") == 0;
}

bool color_enabled() {
    pthread_mutex_lock(&g_enable_lock);
    if (g_enable_info == nullptr) {
        const uint32_t area = __system_property_area_serial();
        if (!g_enable_searched || area != g_enable_area_serial) {
            g_enable_searched = true;
            g_enable_area_serial = area;
            g_enable_info = __system_property_find(kEnableProperty);
        }
    }
    if (g_enable_info != nullptr) {
        const uint32_t serial = __system_property_serial(g_enable_info);
        if (!g_enable_read || serial != g_enable_serial) {
            bool value = false;
            __system_property_read_callback(g_enable_info, read_flag, &value);
            g_enable_value = value;
            g_enable_serial = serial;
            g_enable_read = true;
        }
    }
    const bool value = g_enable_value;
    pthread_mutex_unlock(&g_enable_lock);
    return value;
}

void use_identity(const char* reason, const char* detail) {
    __android_log_print(ANDROID_LOG_WARN, kTag, "%s %s: %s; colour layer is identity", kConfigPath,
                        reason, detail);
    camcolor::config_default(&g_config.config);
}

void load_file_locked() {
    const int fd = open(kConfigPath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        use_identity("open", strerror(errno));
        return;
    }
    size_t length = 0;
    bool failed = false;
    while (length < sizeof(g_config_text)) {
        const ssize_t n = read(fd, g_config_text + length, sizeof(g_config_text) - length);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            failed = true;
            break;
        }
        if (n == 0) {
            break;
        }
        length += static_cast<size_t>(n);
    }
    close(fd);
    if (failed) {
        use_identity("read", strerror(errno));
        return;
    }
    if (length > kMaxConfigBytes) {
        use_identity("size", "larger than 64 KiB");
        return;
    }
    char error[160];
    camcolor::Config parsed;
    if (!camcolor::config_parse(g_config_text, length, &parsed, error, sizeof(error))) {
        use_identity("rejected", error);
        return;
    }
    g_config.config = parsed;
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "%s loaded: ccm mask %#x %s, ggm mask %#x %s, hsbc %s, log %d", kConfigPath,
                        parsed.ccm.mask, camcolor::ccm_is_identity(parsed.ccm) ? "identity" : "active",
                        parsed.ggm.mask, camcolor::ggm_is_identity(parsed.ggm) ? "identity" : "active",
                        camcolor::hsbc_is_identity(parsed.hsbc) ? "identity" : "active",
                        parsed.log ? 1 : 0);
}

void reload_locked() {
    if (!g_config.initialized) {
        camcolor::config_default(&g_config.config);
        g_config.initialized = true;
    }
    const int64_t now = now_ns();
    if (g_config.checked && now - g_config.last_check_ns < kReloadIntervalNs) {
        return;
    }
    g_config.checked = true;
    g_config.last_check_ns = now;
    struct stat st;
    if (stat(kConfigPath, &st) != 0) {
        const int error = errno;
        if (g_config.have_signature || g_config.last_errno != error) {
            __android_log_print(ANDROID_LOG_INFO, kTag, "%s: %s; colour layer is identity",
                                kConfigPath, strerror(error));
        }
        g_config.have_signature = false;
        g_config.last_errno = error;
        camcolor::config_default(&g_config.config);
        return;
    }
    const int64_t mtime = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000 + st.st_mtim.tv_nsec;
    if (g_config.have_signature && st.st_dev == g_config.dev && st.st_ino == g_config.ino &&
        st.st_size == g_config.size && mtime == g_config.mtime_ns) {
        return;
    }
    g_config.have_signature = true;
    g_config.last_errno = 0;
    g_config.dev = st.st_dev;
    g_config.ino = st.st_ino;
    g_config.size = st.st_size;
    g_config.mtime_ns = mtime;
    load_file_locked();
}

void current_config(camcolor::Config* out) {
    pthread_mutex_lock(&g_config_lock);
    reload_locked();
    *out = g_config.config;
    pthread_mutex_unlock(&g_config_lock);
}

uint32_t profile_of(const void* info) {
    uint32_t profile;
    memcpy(&profile, static_cast<const char*>(info) + kProfileOffset, sizeof(profile));
    return profile;
}

void log_ccm(unsigned index, uint32_t profile, const uint32_t* before, const uint32_t* after) {
    int32_t a[9];
    int32_t b[9];
    camcolor::ccm_decode(before, a);
    camcolor::ccm_decode(after, b);
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "ccm index %u profile %u: [%d %d %d|%d %d %d|%d %d %d] -> "
                        "[%d %d %d|%d %d %d|%d %d %d]",
                        index, profile, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], b[0], b[1],
                        b[2], b[3], b[4], b[5], b[6], b[7], b[8]);
}

void log_ggm(unsigned index, uint32_t profile, const uint32_t* before, const uint32_t* after) {
    const size_t points[5] = {0, 48, 96, 144, 191};
    unsigned g[10];
    for (size_t i = 0; i < 5; ++i) {
        g[2 * i] = (before[points[i]] >> 10) & 0x3ff;
        g[2 * i + 1] = (after[points[i]] >> 10) & 0x3ff;
    }
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "ggm index %u profile %u: g[0] %u->%u g[48] %u->%u g[96] %u->%u "
                        "g[144] %u->%u g[191] %u->%u",
                        index, profile, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9]);
}

void log_hsbc(const uint32_t* before, const uint32_t* after) {
    int32_t a[12];
    int32_t b[12];
    camcolor::g2cx_decode(before, a);
    camcolor::g2cx_decode(after, b);
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "g2cx: [%d %d %d %+d|%d %d %d %+d|%d %d %d %+d] -> "
                        "[%d %d %d %+d|%d %d %d %+d|%d %d %d %+d]",
                        a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], b[0],
                        b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11]);
}

void hook_refine_ccm(void* self, unsigned char index, const void* info, void* data) {
    if (!color_enabled()) {
        g_real_ccm(self, index, info, data);
        return;
    }
    uint32_t before[camcolor::kCcmWords];
    memcpy(before, data, sizeof(before));
    g_real_ccm(self, index, info, data);
    uint32_t regs[camcolor::kCcmWords];
    memcpy(regs, data, sizeof(regs));
    if (memcmp(before, regs, sizeof(regs)) != 0) {
        return;
    }
    camcolor::Config config;
    current_config(&config);
    if (index >= 32 || (config.ccm.mask & (1u << index)) == 0 || camcolor::ccm_is_identity(config.ccm)) {
        return;
    }
    camcolor::ccm_apply(config.ccm, regs);
    memcpy(data, regs, sizeof(regs));
    if (config.log && should_log(&g_log_ccm)) {
        log_ccm(index, profile_of(info), before, regs);
    }
}

void hook_refine_ggm(void* self, unsigned char index, const void* info, void* data) {
    if (!color_enabled()) {
        g_real_ggm(self, index, info, data);
        return;
    }
    uint32_t before[camcolor::kGgmWords];
    memcpy(before, data, sizeof(before));
    g_real_ggm(self, index, info, data);
    uint32_t regs[camcolor::kGgmWords];
    memcpy(regs, data, sizeof(regs));
    if (memcmp(before, regs, sizeof(regs)) != 0) {
        return;
    }
    camcolor::Config config;
    current_config(&config);
    if (index >= 32 || (config.ggm.mask & (1u << index)) == 0 || camcolor::ggm_is_identity(config.ggm)) {
        return;
    }
    camcolor::ggm_apply(config.ggm, regs);
    memcpy(data, regs, sizeof(regs));
    if (config.log && should_log(&g_log_ggm)) {
        log_ggm(index, profile_of(info), before, regs);
    }
}

void hook_user_setting_hsbc(void* self, const void* level, void* data) {
    g_real_hsbc(self, level, data);
    if (!color_enabled()) {
        return;
    }
    camcolor::Config config;
    current_config(&config);
    if (camcolor::hsbc_is_identity(config.hsbc)) {
        return;
    }
    uint32_t before[camcolor::kG2cxWords];
    memcpy(before, data, sizeof(before));
    uint32_t regs[camcolor::kG2cxWords];
    memcpy(regs, before, sizeof(regs));
    camcolor::hsbc_apply(config.hsbc, regs);
    memcpy(data, regs, sizeof(regs));
    if (config.log && should_log(&g_log_hsbc)) {
        log_hsbc(before, regs);
    }
}

void prepare_hooks() {
    pthread_once(&g_load_once, load_real);
    if (g_real == nullptr || g_real_vtable == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "no vtable in %s; colour hooks disabled",
                            kRealLibrary);
        return;
    }
    void* const* vtable = static_cast<void* const*>(g_real_vtable);
    struct Expected {
        size_t slot;
        const char* name;
    };
    const Expected expected[] = {
            {kSlotRefineCamInfo, kRefineCamInfo}, {kSlotRefineCcm, kRefineCcm},
            {kSlotRefineGgm, kRefineGgm},         {kSlotRefineColor, kRefineColor},
            {kSlotUserSettingHsbc, kUserSettingHsbc}, {kSlotMapZoom, kMapZoom},
    };
    if (vtable[0] != nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "unexpected vtable offset; colour hooks disabled");
        return;
    }
    for (const Expected& entry : expected) {
        void* address = dlsym(g_real, entry.name);
        if (address == nullptr || vtable[kAddressPoint + entry.slot] != address) {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "vtable slot %zu is not %s; colour hooks disabled",
                                entry.slot, entry.name);
            return;
        }
    }
    memcpy(g_hook_table, vtable, sizeof(g_hook_table));
    g_hook_table[kAddressPoint + kSlotRefineCcm] = reinterpret_cast<void*>(hook_refine_ccm);
    g_hook_table[kAddressPoint + kSlotRefineGgm] = reinterpret_cast<void*>(hook_refine_ggm);
    g_hook_table[kAddressPoint + kSlotUserSettingHsbc] = reinterpret_cast<void*>(hook_user_setting_hsbc);
    g_real_vptr = const_cast<void**>(vtable) + kAddressPoint;
    g_hook_vptr = g_hook_table + kAddressPoint;
    __atomic_store_n(&g_hooks_ready, true, __ATOMIC_RELEASE);
    __android_log_print(ANDROID_LOG_INFO, kTag, "colour hooks ready");
}

void attach(void* object) {
    void** vptr = static_cast<void**>(object);
    void* expected = g_real_vptr;
    if (__atomic_compare_exchange_n(vptr, &expected, g_hook_vptr, false, __ATOMIC_RELEASE,
                                    __ATOMIC_RELAXED)) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "colour hooks attached to %p", object);
        return;
    }
    if (expected != g_hook_vptr && should_log(&g_log_vptr)) {
        __android_log_print(ANDROID_LOG_WARN, kTag, "object %p has vtable %p; left alone", object,
                            expected);
    }
}

void detach(void* object) {
    if (!__atomic_load_n(&g_hooks_ready, __ATOMIC_ACQUIRE)) {
        return;
    }
    void** vptr = static_cast<void**>(object);
    void* expected = g_hook_vptr;
    if (__atomic_compare_exchange_n(vptr, &expected, g_real_vptr, false, __ATOMIC_RELEASE,
                                    __ATOMIC_RELAXED)) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "colour hooks detached from %p", object);
    }
}

void session(void* object) {
    if (!color_enabled()) {
        detach(object);
        return;
    }
    pthread_once(&g_hooks_once, prepare_hooks);
    if (!__atomic_load_n(&g_hooks_ready, __ATOMIC_ACQUIRE)) {
        return;
    }
    pthread_mutex_lock(&g_config_lock);
    reload_locked();
    pthread_mutex_unlock(&g_config_lock);
    attach(object);
}

}

extern "C" void* camcolor_resolve(size_t index) {
    pthread_once(&g_load_once, load_real);
    void* address = index < kSymbolCount ? __atomic_load_n(&camcolor_slots[index], __ATOMIC_ACQUIRE)
                                         : nullptr;
    if (address == nullptr) {
        __android_log_assert(nullptr, kTag, "%s: %s is unavailable", kRealLibrary,
                             index < kSymbolCount ? kSymbolNames[index] : "?");
    }
    return address;
}

namespace NSIspTuning {

class CAMCOLOR_EXPORT IspTuningCustom {
  public:
    static IspTuningCustom* createInstance(unsigned int sensor_dev, unsigned int sensor_id,
                                           int sensor_index);
};

}

NSIspTuning::IspTuningCustom* NSIspTuning::IspTuningCustom::createInstance(unsigned int sensor_dev,
                                                                          unsigned int sensor_id,
                                                                          int sensor_index) {
    using Create = IspTuningCustom* (*)(unsigned int, unsigned int, int);
    const Create create = reinterpret_cast<Create>(camcolor_resolve(0));
    IspTuningCustom* object = create(sensor_dev, sensor_id, sensor_index);
    if (object != nullptr) {
        session(object);
    }
    return object;
}
