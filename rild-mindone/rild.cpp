/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "rild-mindone"

#include <dlfcn.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <cutils/properties.h>
#include <log/log.h>

#include <vector>

extern "C" {
struct RIL_RadioFunctions;
using RIL_Token = void*;
using RIL_TimedCallback = void (*)(void*);

void RIL_onRequestComplete(RIL_Token token, int error, void* response, size_t length);
void RIL_onUnsolicitedResponse(int response, const void* data, size_t length, int socket_id);
void RIL_requestTimedCallback(RIL_TimedCallback callback, void* parameter,
                              const struct timeval* relative_time);
void RIL_onRequestAck(RIL_Token token);

struct RIL_Env {
    decltype(&RIL_onRequestComplete) OnRequestComplete;
    decltype(&RIL_onUnsolicitedResponse) OnUnsolicitedResponse;
    decltype(&RIL_requestTimedCallback) RequestTimedCallback;
    decltype(&RIL_onRequestAck) OnRequestAck;
};

const RIL_RadioFunctions* RIL_Init(const RIL_Env* env, int argc, char** argv);
const RIL_RadioFunctions* RIL_SAP_Init(const RIL_Env* env, int argc, char** argv);
void RIL_startEventLoop();
void RIL_initReadAndWriteLockOwnerListMutex();
void RIL_register(const RIL_RadioFunctions* functions);
void RIL_register_socket(decltype(&RIL_SAP_Init) initialize, int socket_type, int argc,
                         char** argv);
void rilc_thread_pool();
}

static_assert(sizeof(RIL_Env) == 32);
static_assert(offsetof(RIL_Env, OnRequestComplete) == 0);
static_assert(offsetof(RIL_Env, OnUnsolicitedResponse) == 8);
static_assert(offsetof(RIL_Env, RequestTimedCallback) == 16);
static_assert(offsetof(RIL_Env, OnRequestAck) == 24);

template <typename T>
static T LoadSymbol(void* library, const char* name) {
    dlerror();
    auto symbol = reinterpret_cast<T>(dlsym(library, name));
    const char* error = dlerror();
    if (error != nullptr || symbol == nullptr) {
        ALOGE("Cannot resolve %s: %s", name, error != nullptr ? error : "null symbol");
        return nullptr;
    }
    return symbol;
}

int main(int argc, char** argv) {
    const char* library_path = "libmtk-ril.so";
    char default_client_id[] = "0";
    char* client_id = default_client_id;
    int argument_start = argc;
    bool explicit_arguments = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--") == 0) {
            argument_start = i + 1;
            explicit_arguments = true;
            break;
        }
        if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            library_path = argv[++i];
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            client_id = argv[++i];
        } else {
            ALOGE("Usage: %s [-l library] [-c client_id] [-- library_arguments]", argv[0]);
            return EXIT_FAILURE;
        }
    }

    std::vector<char*> ril_arguments{argv[0]};
    char property_arguments[PROPERTY_VALUE_MAX] = {};
    if (explicit_arguments) {
        for (int i = argument_start; i < argc; ++i) {
            ril_arguments.push_back(argv[i]);
        }
    } else {
        property_get("vendor.rild.libargs", property_arguments, "");
        char* state = nullptr;
        for (char* argument = strtok_r(property_arguments, " ", &state); argument != nullptr;
             argument = strtok_r(nullptr, " ", &state)) {
            ril_arguments.push_back(argument);
        }
    }
    char client_option[] = "-c";
    ril_arguments.push_back(client_option);
    ril_arguments.push_back(client_id);
    const int ril_argc = static_cast<int>(ril_arguments.size());
    ril_arguments.push_back(nullptr);

    void* fusion = dlopen("librilfusion.so", RTLD_NOW | RTLD_GLOBAL);
    if (fusion == nullptr) {
        ALOGE("Cannot load librilfusion.so: %s", dlerror());
        return EXIT_FAILURE;
    }
    auto start_event_loop = LoadSymbol<decltype(&RIL_startEventLoop)>(fusion, "RIL_startEventLoop");
    auto initialize_locks = LoadSymbol<decltype(&RIL_initReadAndWriteLockOwnerListMutex)>(
            fusion, "RIL_initReadAndWriteLockOwnerListMutex");
    auto register_radio = LoadSymbol<decltype(&RIL_register)>(fusion, "RIL_register");
    auto thread_pool = LoadSymbol<decltype(&rilc_thread_pool)>(fusion, "rilc_thread_pool");
    static const RIL_Env env = {
            LoadSymbol<decltype(&RIL_onRequestComplete)>(fusion, "RIL_onRequestComplete"),
            LoadSymbol<decltype(&RIL_onUnsolicitedResponse)>(fusion, "RIL_onUnsolicitedResponse"),
            LoadSymbol<decltype(&RIL_requestTimedCallback)>(fusion, "RIL_requestTimedCallback"),
            LoadSymbol<decltype(&RIL_onRequestAck)>(fusion, "RIL_onRequestAck"),
    };
    if (start_event_loop == nullptr || initialize_locks == nullptr || register_radio == nullptr ||
        thread_pool == nullptr || env.OnRequestComplete == nullptr ||
        env.OnUnsolicitedResponse == nullptr || env.RequestTimedCallback == nullptr ||
        env.OnRequestAck == nullptr) {
        return EXIT_FAILURE;
    }

    void* implementation = dlopen(library_path, RTLD_NOW);
    if (implementation == nullptr) {
        ALOGE("Cannot load %s: %s", library_path, dlerror());
        return EXIT_FAILURE;
    }
    start_event_loop();
    auto initialize = LoadSymbol<decltype(&RIL_Init)>(implementation, "RIL_Init");
    if (initialize == nullptr) {
        return EXIT_FAILURE;
    }
    dlerror();
    auto initialize_sap = reinterpret_cast<decltype(&RIL_SAP_Init)>(
            dlsym(implementation, "RIL_SAP_Init"));
    const char* sap_error = dlerror();
    if (sap_error != nullptr || initialize_sap == nullptr) {
        ALOGW("Optional RIL_SAP_Init unavailable in %s", library_path);
        initialize_sap = nullptr;
    }
    initialize_locks();
    const RIL_RadioFunctions* functions = initialize(&env, ril_argc, ril_arguments.data());
    if (functions == nullptr) {
        ALOGE("RIL_Init returned null");
        return EXIT_FAILURE;
    }
    register_radio(functions);
    if (initialize_sap != nullptr) {
        auto register_socket =
                LoadSymbol<decltype(&RIL_register_socket)>(fusion, "RIL_register_socket");
        if (register_socket == nullptr) {
            return EXIT_FAILURE;
        }
        register_socket(initialize_sap, 1, ril_argc, ril_arguments.data());
    }
    thread_pool();
    ALOGE("rilc_thread_pool returned unexpectedly");
    return EXIT_FAILURE;
}
