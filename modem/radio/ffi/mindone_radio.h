/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#ifndef MINDONE_RADIO_H
#define MINDONE_RADIO_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct mr_string {
    const uint8_t *data;
    uint32_t len;
} mr_string;
typedef struct mr_cell_identity {
    uint32_t kind;
    uint32_t area;
    uint32_t cell;
    mr_string plmn;
} mr_cell_identity;
typedef struct mr_gsm_signal {
    uint8_t signal_strength_present;
    uint8_t signal_strength;
    uint8_t bit_error_rate_present;
    uint8_t bit_error_rate;
} mr_gsm_signal;
typedef struct mr_lte_signal {
    uint8_t rsrp_present;
    int32_t rsrp;
    uint8_t rsrq_present;
    int32_t rsrq;
    uint8_t rssnr_present;
    int32_t rssnr;
} mr_lte_signal;
typedef struct mr_wcdma_signal {
    uint8_t signal_strength_present;
    uint8_t signal_strength;
    uint8_t rscp_present;
    int32_t rscp;
    uint8_t ecno_present;
    int32_t ecno;
} mr_wcdma_signal;
typedef struct mr_signal_strength {
    uint8_t gsm_present;
    mr_gsm_signal gsm;
    uint8_t lte_present;
    mr_lte_signal lte;
    uint8_t wcdma_present;
    mr_wcdma_signal wcdma;
} mr_signal_strength;
typedef struct mr_reg_state_result {
    int32_t reg_state;
    int32_t rat;
    uint8_t cell_identity_present;
    mr_cell_identity cell_identity;
    int32_t reason_for_denial;
    mr_string registered_plmn;
} mr_reg_state_result;
typedef struct mr_operator_info {
    mr_string alpha_long;
    mr_string alpha_short;
    mr_string numeric;
} mr_operator_info;
typedef struct mr_cell_info {
    uint8_t registered;
    mr_cell_identity identity;
    mr_signal_strength signal;
} mr_cell_info;
typedef struct mr_signal_criteria {
    int32_t access_network;
    int32_t measurement;
    uint8_t enabled;
    int32_t hysteresis_ms;
    int32_t hysteresis_db;
    const int32_t *thresholds_data;
    uint32_t thresholds_len;
} mr_signal_criteria;
typedef struct mr_link_criteria {
    int32_t access_network;
    int32_t hysteresis_ms;
    int32_t hysteresis_dl_kbps;
    int32_t hysteresis_ul_kbps;
    const int32_t *thresholds_dl_kbps_data;
    uint32_t thresholds_dl_kbps_len;
    const int32_t *thresholds_ul_kbps_data;
    uint32_t thresholds_ul_kbps_len;
} mr_link_criteria;
typedef struct mr_imei_info {
    int32_t kind;
    mr_string imei;
    mr_string svn;
} mr_imei_info;
typedef struct mr_device_identity {
    mr_string imei;
    mr_string imeisv;
    mr_string esn;
    mr_string meid;
} mr_device_identity;
typedef struct mr_activity_stats_tech_specific_info {
    int32_t rat;
    int32_t frequency_range;
    int32_t txm_mode_time_ms[5];
    int32_t rx_mode_time_ms;
} mr_activity_stats_tech_specific_info;
typedef struct mr_activity_stats_info {
    int32_t sleep_mode_time_ms;
    int32_t idle_mode_time_ms;
    const mr_activity_stats_tech_specific_info *tech_specific_info_data;
    uint32_t tech_specific_info_len;
} mr_activity_stats_info;
typedef struct mr_radio_capability {
    int32_t session;
    int32_t phase;
    int32_t raf;
    mr_string logical_modem_uuid;
    int32_t status;
} mr_radio_capability;
typedef struct mr_slot_port_mapping {
    int32_t physical_slot_id;
    int32_t port_id;
} mr_slot_port_mapping;
typedef struct mr_app_status {
    int32_t app_type;
    int32_t app_state;
    int32_t perso_substate;
    mr_string aid_ptr;
    mr_string app_label_ptr;
    uint8_t pin1_replaced;
    int32_t pin1;
    int32_t pin2;
} mr_app_status;
typedef struct mr_card_status {
    int32_t card_state;
    int32_t universal_pin_state;
    int32_t gsm_umts_subscription_app_index;
    int32_t cdma_subscription_app_index;
    int32_t ims_subscription_app_index;
    const mr_app_status *applications_data;
    uint32_t applications_len;
    mr_string atr;
    mr_string iccid;
    mr_string eid;
    mr_slot_port_mapping slot_map;
    int32_t supported_mep_mode;
} mr_card_status;
typedef struct mr_sim_port_info {
    mr_string icc_id;
    int32_t logical_slot_id;
    uint8_t port_active;
} mr_sim_port_info;
typedef struct mr_sim_slot_status {
    int32_t card_state;
    mr_string atr;
    mr_string eid;
    const mr_sim_port_info *port_info_data;
    uint32_t port_info_len;
    int32_t supported_mep_mode;
} mr_sim_slot_status;
typedef struct mr_phone_capability {
    int8_t max_active_data;
    int8_t max_active_internet_data;
    uint8_t is_internet_lingering_supported;
    const int8_t *logical_modem_ids_data;
    uint32_t logical_modem_ids_len;
    int8_t max_active_voice;
} mr_phone_capability;
typedef struct mr_gsm_broadcast_sms_config_info {
    int32_t from_service_id;
    int32_t to_service_id;
    int32_t from_code_scheme;
    int32_t to_code_scheme;
    uint8_t selected;
} mr_gsm_broadcast_sms_config_info;
typedef struct mr_uus_info {
    int32_t uus_type;
    int32_t uus_dcs;
    mr_string uus_data;
} mr_uus_info;
typedef struct mr_call {
    int32_t state;
    int32_t index;
    int32_t toa;
    uint8_t is_mpty;
    uint8_t is_mt;
    int8_t als;
    uint8_t is_voice;
    uint8_t is_voice_privacy;
    mr_string number;
    int32_t number_presentation;
    mr_string name;
    int32_t name_presentation;
    const mr_uus_info *uus_info_data;
    uint32_t uus_info_len;
    int32_t audio_quality;
    mr_string forwarded_number;
} mr_call;
typedef struct mr_request {
    uint32_t kind;
    uint8_t boolean;
    uint32_t mask;
    int32_t integer;
    int8_t small_integer;
    uint64_t profile_count;
    uint8_t power_on;
    uint8_t for_emergency_call;
    uint8_t preferred_for_emergency_call;
    int32_t device_state_type;
    const mr_signal_criteria *signal_criteria_data;
    uint32_t signal_criteria_len;
    mr_link_criteria link_criteria;
    const mr_gsm_broadcast_sms_config_info *broadcast_config_data;
    uint32_t broadcast_config_len;
} mr_request;
typedef struct mr_value {
    uint32_t kind;
    uint8_t boolean;
    int32_t integer;
    mr_string text;
    mr_reg_state_result registration;
    mr_operator_info operator_info;
    mr_signal_strength signal;
    mr_imei_info imei;
    mr_device_identity identity;
    mr_activity_stats_info activity;
    mr_radio_capability capability;
    mr_card_status card;
    mr_phone_capability phone;
    int32_t selection;
    const mr_cell_info *cells_data;
    uint32_t cells_len;
    const mr_sim_slot_status *slots_data;
    uint32_t slots_len;
    const mr_call *calls_data;
    uint32_t calls_len;
} mr_value;
typedef struct mr_indication {
    uint32_t kind;
    mr_signal_strength signal;
    int32_t radio_state;
    uint64_t generation;
} mr_indication;
typedef struct mr_output {
    uint32_t kind;
    uint64_t invocation_id;
    int32_t error;
    uint8_t value_present;
    mr_value value;
    mr_indication indication;
} mr_output;
typedef struct mr_layout {
    mr_string name;
    uint64_t size;
    uint64_t alignment;
    const uint64_t *offsets_data;
    uint32_t offsets_len;
} mr_layout;
typedef uint64_t mr_runtime;
typedef uint64_t mr_batch;
#define MR_OK 0
#define MR_INVALID_ARGUMENT 1
#define MR_FULL 2
#define MR_STOPPED 3
#define MR_ID_EXHAUSTED 4
#define MR_IO_ERROR 5
#define MR_PANIC 6
#define MR_INVALID_HANDLE 7
#define MR_TIMEOUT 8
#define MR_REQUEST_NETWORK_GET_VOICE_REGISTRATION_STATE 1
#define MR_REQUEST_NETWORK_GET_DATA_REGISTRATION_STATE 2
#define MR_REQUEST_NETWORK_GET_OPERATOR 3
#define MR_REQUEST_NETWORK_GET_NETWORK_SELECTION_MODE 4
#define MR_REQUEST_NETWORK_GET_SIGNAL_STRENGTH 5
#define MR_REQUEST_NETWORK_GET_CELL_INFO_LIST 6
#define MR_REQUEST_NETWORK_GET_IMS_REGISTRATION_STATE 7
#define MR_REQUEST_NETWORK_SET_INDICATION_FILTER 8
#define MR_REQUEST_NETWORK_SET_SIGNAL_STRENGTH_REPORTING_CRITERIA 9
#define MR_REQUEST_NETWORK_SET_LINK_CAPACITY_REPORTING_CRITERIA 10
#define MR_REQUEST_MODEM_SET_RADIO_POWER 101
#define MR_REQUEST_MODEM_GET_BASEBAND_VERSION 102
#define MR_REQUEST_MODEM_GET_IMEI 103
#define MR_REQUEST_MODEM_GET_DEVICE_IDENTITY 104
#define MR_REQUEST_MODEM_GET_MODEM_ACTIVITY_INFO 105
#define MR_REQUEST_MODEM_GET_MODEM_STACK_STATUS 106
#define MR_REQUEST_MODEM_ENABLE_MODEM 107
#define MR_REQUEST_MODEM_SEND_DEVICE_STATE 108
#define MR_REQUEST_MODEM_GET_RADIO_CAPABILITY 109
#define MR_REQUEST_MODEM_GET_ICC_CARD_STATUS 110
#define MR_REQUEST_MODEM_GET_UICC_APPLICATIONS_ENABLEMENT 111
#define MR_REQUEST_MODEM_ARE_UICC_APPLICATIONS_ENABLED 112
#define MR_REQUEST_MODEM_ENABLE_UICC_APPLICATIONS 113
#define MR_REQUEST_MODEM_SET_CARRIER_INFO_FOR_IMSI_ENCRYPTION 114
#define MR_REQUEST_MODEM_GET_HAL_DEVICE_CAPABILITIES 115
#define MR_REQUEST_MODEM_GET_SIM_SLOTS_STATUS 116
#define MR_REQUEST_MODEM_GET_PHONE_CAPABILITY 117
#define MR_REQUEST_MODEM_GET_NUM_OF_LIVE_MODEMS 118
#define MR_REQUEST_MODEM_SET_NUM_OF_LIVE_MODEMS 119
#define MR_REQUEST_MODEM_SET_PREFERRED_DATA_MODEM 120
#define MR_REQUEST_MODEM_GET_CURRENT_CALLS 121
#define MR_REQUEST_MODEM_SET_TTY_MODE 122
#define MR_REQUEST_MODEM_SET_GSM_BROADCAST_CONFIG 123
#define MR_REQUEST_MODEM_SET_DATA_PROFILE 124
#define MR_REQUEST_MODEM_SET_NULL_CIPHER_AND_INTEGRITY_ENABLED 125
#define MR_REQUEST_MODEM_SET_SECURITY_ALGORITHMS_UPDATED_ENABLED 126
#define MR_REQUEST_MODEM_SET_CELLULAR_IDENTIFIER_DISCLOSED_ENABLED 127
#define MR_REQUEST_MODEM_IS_SATELLITE_ENABLED_FOR_CARRIER 128
#define MR_REQUEST_MODEM_GET_BARRING_INFO 129
#define MR_REQUEST_MODEM_GET_VOICE_RADIO_TECHNOLOGY 130
#define MR_VALUE_NETWORK_VOID 1
#define MR_VALUE_NETWORK_REGISTRATION 2
#define MR_VALUE_NETWORK_OPERATOR 3
#define MR_VALUE_NETWORK_SELECTION 4
#define MR_VALUE_NETWORK_SIGNAL 5
#define MR_VALUE_NETWORK_CELLS 6
#define MR_VALUE_MODEM_VOID 101
#define MR_VALUE_MODEM_BASEBAND 102
#define MR_VALUE_MODEM_IMEI 103
#define MR_VALUE_MODEM_IDENTITY 104
#define MR_VALUE_MODEM_ACTIVITY 105
#define MR_VALUE_MODEM_CAPABILITY 106
#define MR_VALUE_MODEM_CARD 107
#define MR_VALUE_MODEM_SLOTS 108
#define MR_VALUE_MODEM_PHONE 109
#define MR_VALUE_MODEM_REDUCED_FEATURE_SET1 110
#define MR_VALUE_MODEM_CURRENT_CALLS 111
#define MR_VALUE_MODEM_VOICE_RADIO_TECHNOLOGY 112
#define MR_OUTPUT_RESPONSE 1
#define MR_OUTPUT_INDICATION 2
#define MR_INDICATION_NETWORK_STATE_CHANGED 1
#define MR_INDICATION_CURRENT_SIGNAL_STRENGTH 2
#define MR_INDICATION_RADIO_STATE_CHANGED 3
#define MR_INDICATION_SIM_STATUS_CHANGED 4
#define MR_INDICATION_MODEM_RESET 5
#define MR_REG_STATE_NOT_REGISTERED 0
#define MR_REG_STATE_HOME 1
#define MR_REG_STATE_SEARCHING 2
#define MR_REG_STATE_DENIED 3
#define MR_REG_STATE_UNKNOWN 4
#define MR_REG_STATE_ROAMING 5
#define MR_RAT_UNKNOWN 0
#define MR_RAT_GSM 16
#define MR_RAT_UMTS 3
#define MR_RAT_LTE 14
#define MR_SELECTION_MODE_AUTOMATIC 0
#define MR_SELECTION_MODE_MANUAL 1
#define MR_SELECTION_MODE_DEREGISTERED_UNVERIFIED 2
#define MR_SELECTION_MODE_MANUAL_AUTOMATIC_UNVERIFIED 3
#define MR_DEVICE_STATE_TYPE_POWER_SAVE_MODE 0
#define MR_DEVICE_STATE_TYPE_CHARGING_STATE 1
#define MR_DEVICE_STATE_TYPE_LOW_DATA_EXPECTED 2
#define MR_IMEI_TYPE_INVALID 0
#define MR_IMEI_TYPE_PRIMARY 1
#define MR_IMEI_TYPE_SECONDARY 2
#define MR_CARD_STATE_ABSENT 0
#define MR_CARD_STATE_PRESENT 1
#define MR_CARD_STATE_ERROR 2
#define MR_CARD_STATE_RESTRICTED 3
#define MR_RADIO_UNAVAILABLE 0
#define MR_RADIO_OFF 1
#define MR_RADIO_ON 2
#define MR_RADIO_ERROR_NONE 0
#define MR_RADIO_ERROR_RADIO_NOT_AVAILABLE 1
#define MR_RADIO_ERROR_GENERIC_FAILURE 2
#define MR_RADIO_ERROR_REQUEST_NOT_SUPPORTED 6
#define MR_RADIO_ERROR_INTERNAL_ERR 38
#define MR_RADIO_ERROR_MODEM_ERR 40
#define MR_RADIO_ERROR_SIM_ERR 43
#define MR_RADIO_ERROR_INVALID_ARGUMENTS 44
#define MR_RADIO_ERROR_SIM_BUSY 51
#define MR_RADIO_ERROR_OPERATION_NOT_ALLOWED 54
#define MR_RADIO_ERROR_EMPTY_RECORD 55
#define MR_RADIO_ERROR_INVALID_RESPONSE 66
#define MR_RADIO_ERROR_OEM_ERROR1 501
#define MR_CELL_GSM 1
#define MR_CELL_WCDMA 2
#define MR_CELL_LTE 3
#define MR_ABI_VERSION 2
#define MR_MAX_INPUT_BYTES 4194304
#define MR_MAX_ARRAY_ELEMENTS 65536
#define MR_WAIT_FOREVER UINT32_MAX
uint32_t mr_abi_version(void);
int32_t mr_create(mr_runtime *out);
int32_t mr_submit(mr_runtime runtime, uint64_t invocation_id, const mr_request *request);
int32_t mr_next_batch(mr_runtime runtime, uint32_t timeout_ms, mr_batch *out);
int32_t mr_batch_count(mr_batch batch, uint32_t *out);
int32_t mr_batch_at(mr_batch batch, uint32_t index, const mr_output **out);
int32_t mr_batch_free(mr_batch *batch);
int32_t mr_stop(mr_runtime runtime);
int32_t mr_destroy(mr_runtime *runtime);
uint32_t mr_layout_count(void);
int32_t mr_layout_at(uint32_t index, mr_layout *out);
#ifdef __cplusplus
}
#endif
#endif
