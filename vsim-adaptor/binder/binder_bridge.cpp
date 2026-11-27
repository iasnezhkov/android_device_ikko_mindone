/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vsim-adaptor"

#include "binder_bridge.h"

#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_parcel.h>
#include <android/binder_process.h>
#include <android/binder_stability.h>
#include <android/log.h>

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr char kServiceName[] =
        "vendor.mediatek.hardware.mtkradioex.sim.IMtkRadioExSim/slot1";
constexpr char kSimDescriptor[] =
        "vendor.mediatek.hardware.mtkradioex.sim.IMtkRadioExSim";
constexpr char kRespDescriptor[] =
        "vendor.mediatek.hardware.mtkradioex.sim.IMtkRadioExSimResponse";
constexpr char kIndDescriptor[] =
        "vendor.mediatek.hardware.mtkradioex.sim.IMtkRadioExSimIndication";
constexpr char kInterfaceHash[] = "dd10c80aa3478fddc0a46cab8597191943a29052";
constexpr int32_t kInterfaceVersion = 2;

constexpr uint32_t kTxSendVsimNotification = 24;
constexpr uint32_t kTxSendVsimOperation = 25;
constexpr uint32_t kTxResponseAckMtk = 36;
constexpr uint32_t kTxSetResponseFunctionsMtk = 37;

constexpr uint32_t kTxRespVsimNotification = 25;
constexpr uint32_t kTxRespVsimOperation = 26;

constexpr uint32_t kTxIndVirtualSimStatusChanged = 11;
constexpr uint32_t kTxIndVsimEvent = 12;

constexpr uint32_t kTxGetInterfaceVersion = FIRST_CALL_TRANSACTION + 16777214;
constexpr uint32_t kTxGetInterfaceHash = FIRST_CALL_TRANSACTION + 16777213;

constexpr int32_t kSolicitedAckExp = 1;
constexpr int32_t kUnsolicitedAckExp = 1;

struct BridgeState {
    std::mutex mutex;
    vsim_binder_callbacks_t cb{};
    AIBinder* remote = nullptr;
    AIBinder* resp_binder = nullptr;
    AIBinder* ind_binder = nullptr;
    AIBinder_Class* sim_class = nullptr;
    AIBinder_Class* resp_class = nullptr;
    AIBinder_Class* ind_class = nullptr;
    AIBinder_DeathRecipient* death_recipient = nullptr;
    bool initialized = false;
};

BridgeState g_state;

void* onCreate(void* args) { return args; }
void onDestroy(void*) {}
binder_status_t onRemoteStubTransact(AIBinder*, transaction_code_t, const AParcel*, AParcel*) {
    return STATUS_UNKNOWN_TRANSACTION;
}

bool readRadioResponseInfo(const AParcel* in, int32_t* type, int32_t* serial, int32_t* error) {
    int32_t non_null = 0;
    if (AParcel_readInt32(in, &non_null) != STATUS_OK || non_null == 0) return false;
    int32_t start = AParcel_getDataPosition(in);
    int32_t size = 0;
    if (AParcel_readInt32(in, &size) != STATUS_OK || size < 4) return false;
    if (AParcel_getDataPosition(in) - start < size) {
        if (AParcel_readInt32(in, type) != STATUS_OK) return false;
    }
    if (AParcel_getDataPosition(in) - start < size) {
        if (AParcel_readInt32(in, serial) != STATUS_OK) return false;
    }
    if (AParcel_getDataPosition(in) - start < size) {
        if (AParcel_readInt32(in, error) != STATUS_OK) return false;
    }
    if (AParcel_setDataPosition(in, start + size) != STATUS_OK) return false;
    return true;
}

bool readStringHelper(const AParcel* in, std::string* out) {
    auto allocator = [](void* stringData, int32_t length, char** buffer) -> bool {
        auto* s = static_cast<std::string*>(stringData);
        if (length < 0) {
            s->clear();
            *buffer = nullptr;
            return true;
        }
        s->resize(static_cast<size_t>(length));
        *buffer = s->data();
        return true;
    };
    if (AParcel_readString(in, out, allocator) != STATUS_OK) return false;
    if (!out->empty() && out->back() == '\0') out->pop_back();
    return true;
}

bool readVsimOperationEvent(const AParcel* in, int32_t* tx_id, int32_t* event_id, int32_t* result,
                            int32_t* data_len, std::string* data) {
    int32_t non_null = 0;
    if (AParcel_readInt32(in, &non_null) != STATUS_OK || non_null == 0) return false;
    int32_t start = AParcel_getDataPosition(in);
    int32_t size = 0;
    if (AParcel_readInt32(in, &size) != STATUS_OK || size < 4) return false;
    if (AParcel_getDataPosition(in) - start < size) {
        if (AParcel_readInt32(in, tx_id) != STATUS_OK) return false;
    }
    if (AParcel_getDataPosition(in) - start < size) {
        if (AParcel_readInt32(in, event_id) != STATUS_OK) return false;
    }
    if (AParcel_getDataPosition(in) - start < size) {
        if (AParcel_readInt32(in, result) != STATUS_OK) return false;
    }
    if (AParcel_getDataPosition(in) - start < size) {
        if (AParcel_readInt32(in, data_len) != STATUS_OK) return false;
    }
    if (AParcel_getDataPosition(in) - start < size) {
        if (!readStringHelper(in, data)) return false;
    }
    if (AParcel_setDataPosition(in, start + size) != STATUS_OK) return false;
    return true;
}

AIBinder* acquireRemote() {
    std::lock_guard<std::mutex> lock(g_state.mutex);
    if (!g_state.remote) return nullptr;
    AIBinder_incStrong(g_state.remote);
    return g_state.remote;
}

void sendAckMtk() {
    AIBinder* remote = acquireRemote();
    if (!remote) return;
    AParcel* in = nullptr;
    AParcel* out = nullptr;
    if (AIBinder_prepareTransaction(remote, &in) == STATUS_OK) {
        AIBinder_transact(remote, kTxResponseAckMtk, &in, &out, FLAG_ONEWAY);
    }
    if (in) AParcel_delete(in);
    if (out) AParcel_delete(out);
    AIBinder_decStrong(remote);
}

binder_status_t handleCommonMeta(transaction_code_t code, AParcel* out) {
    if (code == kTxGetInterfaceVersion) {
        if (AParcel_writeInt32(out, 0) != STATUS_OK) return STATUS_BAD_VALUE;
        return AParcel_writeInt32(out, kInterfaceVersion);
    }
    if (code == kTxGetInterfaceHash) {
        if (AParcel_writeInt32(out, 0) != STATUS_OK) return STATUS_BAD_VALUE;
        return AParcel_writeString(out, kInterfaceHash, static_cast<int32_t>(strlen(kInterfaceHash)));
    }
    return STATUS_UNKNOWN_TRANSACTION;
}

binder_status_t onRespTransact(AIBinder*, transaction_code_t code, const AParcel* in, AParcel* out) {
    if (code == kTxGetInterfaceVersion || code == kTxGetInterfaceHash) {
        return handleCommonMeta(code, out);
    }
    int32_t type = 0, serial = 0, error = 0;
    if (code == kTxRespVsimNotification || code == kTxRespVsimOperation) {
        if (!readRadioResponseInfo(in, &type, &serial, &error)) return STATUS_BAD_VALUE;
        if (type == kSolicitedAckExp) sendAckMtk();
        vsim_binder_callbacks_t cb;
        {
            std::lock_guard<std::mutex> lock(g_state.mutex);
            cb = g_state.cb;
        }
        if (code == kTxRespVsimNotification && cb.on_notif_rsp) {
            cb.on_notif_rsp(cb.ctx, serial, error);
        } else if (code == kTxRespVsimOperation && cb.on_op_rsp) {
            cb.on_op_rsp(cb.ctx, serial, error);
        }
        return STATUS_OK;
    }
    if (readRadioResponseInfo(in, &type, &serial, &error) && type == kSolicitedAckExp) {
        sendAckMtk();
    }
    return STATUS_OK;
}

binder_status_t onIndTransact(AIBinder*, transaction_code_t code, const AParcel* in, AParcel* out) {
    if (code == kTxGetInterfaceVersion || code == kTxGetInterfaceHash) {
        return handleCommonMeta(code, out);
    }
    int32_t ind_type = 0;
    if (AParcel_readInt32(in, &ind_type) != STATUS_OK) return STATUS_BAD_VALUE;
    if (ind_type == kUnsolicitedAckExp) sendAckMtk();

    vsim_binder_callbacks_t cb;
    {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        cb = g_state.cb;
    }
    if (code == kTxIndVirtualSimStatusChanged) {
        int32_t sim_inserted = 0;
        if (AParcel_readInt32(in, &sim_inserted) != STATUS_OK) return STATUS_BAD_VALUE;
        if (cb.on_status_ind) cb.on_status_ind(cb.ctx, sim_inserted);
        return STATUS_OK;
    }
    if (code == kTxIndVsimEvent) {
        int32_t tx_id = 0, event_id = 0, result = 0, data_len = 0;
        std::string hex_data;
        if (!readVsimOperationEvent(in, &tx_id, &event_id, &result, &data_len, &hex_data)) {
            return STATUS_BAD_VALUE;
        }
        if (cb.on_event_ind) {
            cb.on_event_ind(cb.ctx, tx_id, event_id, result, data_len, hex_data.c_str());
        }
        return STATUS_OK;
    }
    return STATUS_OK;
}

void onBinderDied(void*) {
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "IMtkRadioExSim/slot1 died");
    vsim_binder_callbacks_t cb;
    {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        if (g_state.remote) {
            AIBinder_decStrong(g_state.remote);
            g_state.remote = nullptr;
        }
        cb = g_state.cb;
    }
    if (cb.on_ril_state) cb.on_ril_state(cb.ctx, 0);
}

bool setResponseFunctionsLocked(AIBinder* remote, AIBinder* resp, AIBinder* ind) {
    AParcel* in = nullptr;
    AParcel* out = nullptr;
    binder_status_t st = AIBinder_prepareTransaction(remote, &in);
    if (st == STATUS_OK) st = AParcel_writeStrongBinder(in, resp);
    if (st == STATUS_OK) st = AParcel_writeStrongBinder(in, ind);
    if (st == STATUS_OK) {
        st = AIBinder_transact(remote, kTxSetResponseFunctionsMtk, &in, &out, FLAG_ONEWAY);
    }
    if (in) AParcel_delete(in);
    if (out) AParcel_delete(out);
    if (st != STATUS_OK) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "setResponseFunctionsMtk failed: %d", st);
        return false;
    }
    return true;
}

}

extern "C" {

int vsim_binder_init(const vsim_binder_callbacks_t* callbacks) {
    if (!callbacks) return -1;
    std::lock_guard<std::mutex> lock(g_state.mutex);
    if (g_state.initialized) return 0;
    g_state.cb = *callbacks;

    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();

    g_state.sim_class = AIBinder_Class_define(kSimDescriptor, onCreate, onDestroy,
                                              onRemoteStubTransact);
    g_state.resp_class = AIBinder_Class_define(kRespDescriptor, onCreate, onDestroy, onRespTransact);
    g_state.ind_class = AIBinder_Class_define(kIndDescriptor, onCreate, onDestroy, onIndTransact);
    if (!g_state.sim_class || !g_state.resp_class || !g_state.ind_class) return -1;

    g_state.resp_binder = AIBinder_new(g_state.resp_class, nullptr);
    g_state.ind_binder = AIBinder_new(g_state.ind_class, nullptr);
    if (!g_state.resp_binder || !g_state.ind_binder) return -1;

    AIBinder_markVintfStability(g_state.resp_binder);
    AIBinder_markVintfStability(g_state.ind_binder);

    g_state.death_recipient = AIBinder_DeathRecipient_new(onBinderDied);
    if (!g_state.death_recipient) return -1;

    g_state.initialized = true;
    return 0;
}

int vsim_binder_connect(void) {
    AIBinder* candidate = AServiceManager_checkService(kServiceName);
    if (!candidate) return -1;

    AIBinder_Class* sim_class = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        sim_class = g_state.sim_class;
    }
    if (!sim_class || !AIBinder_associateClass(candidate, sim_class)) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "failed to associate IMtkRadioExSim class");
        AIBinder_decStrong(candidate);
        return -1;
    }

    vsim_binder_callbacks_t cb;
    {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        if (!g_state.initialized) {
            AIBinder_decStrong(candidate);
            return -1;
        }
        if (g_state.remote) {
            if (AIBinder_isAlive(g_state.remote)) {
                AIBinder_decStrong(candidate);
                return 0;
            }
            AIBinder_decStrong(g_state.remote);
            g_state.remote = nullptr;
        }
        AIBinder_linkToDeath(candidate, g_state.death_recipient, nullptr);
        if (!setResponseFunctionsLocked(candidate, g_state.resp_binder, g_state.ind_binder)) {
            AIBinder_unlinkToDeath(candidate, g_state.death_recipient, nullptr);
            AIBinder_decStrong(candidate);
            return -1;
        }
        g_state.remote = candidate;
        cb = g_state.cb;
    }
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "connected to %s", kServiceName);
    if (cb.on_ril_state) cb.on_ril_state(cb.ctx, 1);
    return 0;
}

int vsim_binder_send_notification(int32_t serial, int32_t tx_id, int32_t event_id,
                                  int32_t sim_type) {
    AIBinder* remote = acquireRemote();
    if (!remote) return -1;
    AParcel* in = nullptr;
    AParcel* out = nullptr;
    binder_status_t st = AIBinder_prepareTransaction(remote, &in);
    if (st == STATUS_OK) st = AParcel_writeInt32(in, serial);
    if (st == STATUS_OK) st = AParcel_writeInt32(in, tx_id);
    if (st == STATUS_OK) st = AParcel_writeInt32(in, event_id);
    if (st == STATUS_OK) st = AParcel_writeInt32(in, sim_type);
    if (st == STATUS_OK) st = AParcel_writeInt32(in, 0);
    if (st == STATUS_OK) {
        st = AIBinder_transact(remote, kTxSendVsimNotification, &in, &out, FLAG_ONEWAY);
    }
    if (in) AParcel_delete(in);
    if (out) AParcel_delete(out);
    AIBinder_decStrong(remote);
    if (st != STATUS_OK) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "sendVsimNotification(serial=%d, event=%d) failed: %d",
                            serial, event_id, st);
        return -1;
    }
    return 0;
}

int vsim_binder_send_operation(int32_t serial, int32_t tx_id, int32_t event_id, int32_t result,
                               const uint8_t* data, size_t data_len) {
    AIBinder* remote = acquireRemote();
    if (!remote) return -1;
    AParcel* in = nullptr;
    AParcel* out = nullptr;
    binder_status_t st = AIBinder_prepareTransaction(remote, &in);
    if (st == STATUS_OK) st = AParcel_writeInt32(in, serial);
    if (st == STATUS_OK) st = AParcel_writeInt32(in, tx_id);
    if (st == STATUS_OK) st = AParcel_writeInt32(in, event_id);
    if (st == STATUS_OK) st = AParcel_writeInt32(in, result);
    if (st == STATUS_OK) st = AParcel_writeInt32(in, static_cast<int32_t>(data_len));
    if (st == STATUS_OK) {
        const auto* bytes = reinterpret_cast<const int8_t*>(data);
        int32_t len = static_cast<int32_t>(data_len);
        st = AParcel_writeByteArray(in, bytes ? bytes : reinterpret_cast<const int8_t*>(""), len);
    }
    if (st == STATUS_OK) st = AParcel_writeInt32(in, 0);
    if (st == STATUS_OK) {
        st = AIBinder_transact(remote, kTxSendVsimOperation, &in, &out, FLAG_ONEWAY);
    }
    if (in) AParcel_delete(in);
    if (out) AParcel_delete(out);
    AIBinder_decStrong(remote);
    if (st != STATUS_OK) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "sendVsimOperation(serial=%d, event=%d) failed: %d",
                            serial, event_id, st);
        return -1;
    }
    return 0;
}

void vsim_binder_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_state.mutex);
    if (g_state.remote) {
        if (g_state.death_recipient) {
            AIBinder_unlinkToDeath(g_state.remote, g_state.death_recipient, nullptr);
        }
        AIBinder_decStrong(g_state.remote);
        g_state.remote = nullptr;
    }
    if (g_state.resp_binder) {
        AIBinder_decStrong(g_state.resp_binder);
        g_state.resp_binder = nullptr;
    }
    if (g_state.ind_binder) {
        AIBinder_decStrong(g_state.ind_binder);
        g_state.ind_binder = nullptr;
    }
    if (g_state.death_recipient) {
        AIBinder_DeathRecipient_delete(g_state.death_recipient);
        g_state.death_recipient = nullptr;
    }
    g_state.initialized = false;
}

}
