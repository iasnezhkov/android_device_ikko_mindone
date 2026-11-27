/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*vsim_notif_rsp_fn)(void* ctx, int32_t serial, int32_t radio_error);
typedef void (*vsim_op_rsp_fn)(void* ctx, int32_t serial, int32_t radio_error);
typedef void (*vsim_event_ind_fn)(void* ctx, int32_t tx_id, int32_t event_id, int32_t result,
                                  int32_t data_len, const char* hex_data);
typedef void (*vsim_status_ind_fn)(void* ctx, int32_t sim_inserted);
typedef void (*vsim_ril_state_fn)(void* ctx, int32_t connected);

typedef struct vsim_binder_callbacks {
    void* ctx;
    vsim_notif_rsp_fn on_notif_rsp;
    vsim_op_rsp_fn on_op_rsp;
    vsim_event_ind_fn on_event_ind;
    vsim_status_ind_fn on_status_ind;
    vsim_ril_state_fn on_ril_state;
} vsim_binder_callbacks_t;

int vsim_binder_init(const vsim_binder_callbacks_t* callbacks);
int vsim_binder_connect(void);
int vsim_binder_send_notification(int32_t serial, int32_t tx_id, int32_t event_id,
                                  int32_t sim_type);
int vsim_binder_send_operation(int32_t serial, int32_t tx_id, int32_t event_id, int32_t result,
                               const uint8_t* data, size_t data_len);
void vsim_binder_shutdown(void);

#ifdef __cplusplus
}
#endif
