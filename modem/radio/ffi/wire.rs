/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use std::mem::{align_of, offset_of, size_of};
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrString {
    pub data: *const u8,
    pub len: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrCellIdentity {
    pub kind: u32,
    pub area: u32,
    pub cell: u32,
    pub plmn: MrString,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrGsmSignal {
    pub signal_strength_present: u8,
    pub signal_strength: u8,
    pub bit_error_rate_present: u8,
    pub bit_error_rate: u8,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrLteSignal {
    pub rsrp_present: u8,
    pub rsrp: i32,
    pub rsrq_present: u8,
    pub rsrq: i32,
    pub rssnr_present: u8,
    pub rssnr: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrWcdmaSignal {
    pub signal_strength_present: u8,
    pub signal_strength: u8,
    pub rscp_present: u8,
    pub rscp: i32,
    pub ecno_present: u8,
    pub ecno: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrSignalStrength {
    pub gsm_present: u8,
    pub gsm: MrGsmSignal,
    pub lte_present: u8,
    pub lte: MrLteSignal,
    pub wcdma_present: u8,
    pub wcdma: MrWcdmaSignal,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrRegStateResult {
    pub reg_state: i32,
    pub rat: i32,
    pub cell_identity_present: u8,
    pub cell_identity: MrCellIdentity,
    pub reason_for_denial: i32,
    pub registered_plmn: MrString,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrOperatorInfo {
    pub alpha_long: MrString,
    pub alpha_short: MrString,
    pub numeric: MrString,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrCellInfo {
    pub registered: u8,
    pub identity: MrCellIdentity,
    pub signal: MrSignalStrength,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrSignalCriteria {
    pub access_network: i32,
    pub measurement: i32,
    pub enabled: u8,
    pub hysteresis_ms: i32,
    pub hysteresis_db: i32,
    pub thresholds_data: *const i32,
    pub thresholds_len: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrLinkCriteria {
    pub access_network: i32,
    pub hysteresis_ms: i32,
    pub hysteresis_dl_kbps: i32,
    pub hysteresis_ul_kbps: i32,
    pub thresholds_dl_kbps_data: *const i32,
    pub thresholds_dl_kbps_len: u32,
    pub thresholds_ul_kbps_data: *const i32,
    pub thresholds_ul_kbps_len: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrImeiInfo {
    pub kind: i32,
    pub imei: MrString,
    pub svn: MrString,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrDeviceIdentity {
    pub imei: MrString,
    pub imeisv: MrString,
    pub esn: MrString,
    pub meid: MrString,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrActivityStatsTechSpecificInfo {
    pub rat: i32,
    pub frequency_range: i32,
    pub txm_mode_time_ms: [i32; 5],
    pub rx_mode_time_ms: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrActivityStatsInfo {
    pub sleep_mode_time_ms: i32,
    pub idle_mode_time_ms: i32,
    pub tech_specific_info_data: *const MrActivityStatsTechSpecificInfo,
    pub tech_specific_info_len: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrRadioCapability {
    pub session: i32,
    pub phase: i32,
    pub raf: i32,
    pub logical_modem_uuid: MrString,
    pub status: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrSlotPortMapping {
    pub physical_slot_id: i32,
    pub port_id: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrAppStatus {
    pub app_type: i32,
    pub app_state: i32,
    pub perso_substate: i32,
    pub aid_ptr: MrString,
    pub app_label_ptr: MrString,
    pub pin1_replaced: u8,
    pub pin1: i32,
    pub pin2: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrCardStatus {
    pub card_state: i32,
    pub universal_pin_state: i32,
    pub gsm_umts_subscription_app_index: i32,
    pub cdma_subscription_app_index: i32,
    pub ims_subscription_app_index: i32,
    pub applications_data: *const MrAppStatus,
    pub applications_len: u32,
    pub atr: MrString,
    pub iccid: MrString,
    pub eid: MrString,
    pub slot_map: MrSlotPortMapping,
    pub supported_mep_mode: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrSimPortInfo {
    pub icc_id: MrString,
    pub logical_slot_id: i32,
    pub port_active: u8,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrSimSlotStatus {
    pub card_state: i32,
    pub atr: MrString,
    pub eid: MrString,
    pub port_info_data: *const MrSimPortInfo,
    pub port_info_len: u32,
    pub supported_mep_mode: i32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrPhoneCapability {
    pub max_active_data: i8,
    pub max_active_internet_data: i8,
    pub is_internet_lingering_supported: u8,
    pub logical_modem_ids_data: *const i8,
    pub logical_modem_ids_len: u32,
    pub max_active_voice: i8,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrGsmBroadcastSmsConfigInfo {
    pub from_service_id: i32,
    pub to_service_id: i32,
    pub from_code_scheme: i32,
    pub to_code_scheme: i32,
    pub selected: u8,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrUusInfo {
    pub uus_type: i32,
    pub uus_dcs: i32,
    pub uus_data: MrString,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrCall {
    pub state: i32,
    pub index: i32,
    pub toa: i32,
    pub is_mpty: u8,
    pub is_mt: u8,
    pub als: i8,
    pub is_voice: u8,
    pub is_voice_privacy: u8,
    pub number: MrString,
    pub number_presentation: i32,
    pub name: MrString,
    pub name_presentation: i32,
    pub uus_info_data: *const MrUusInfo,
    pub uus_info_len: u32,
    pub audio_quality: i32,
    pub forwarded_number: MrString,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrRequest {
    pub kind: u32,
    pub boolean: u8,
    pub mask: u32,
    pub integer: i32,
    pub small_integer: i8,
    pub profile_count: u64,
    pub power_on: u8,
    pub for_emergency_call: u8,
    pub preferred_for_emergency_call: u8,
    pub device_state_type: i32,
    pub signal_criteria_data: *const MrSignalCriteria,
    pub signal_criteria_len: u32,
    pub link_criteria: MrLinkCriteria,
    pub broadcast_config_data: *const MrGsmBroadcastSmsConfigInfo,
    pub broadcast_config_len: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrValue {
    pub kind: u32,
    pub boolean: u8,
    pub integer: i32,
    pub text: MrString,
    pub registration: MrRegStateResult,
    pub operator_info: MrOperatorInfo,
    pub signal: MrSignalStrength,
    pub imei: MrImeiInfo,
    pub identity: MrDeviceIdentity,
    pub activity: MrActivityStatsInfo,
    pub capability: MrRadioCapability,
    pub card: MrCardStatus,
    pub phone: MrPhoneCapability,
    pub selection: i32,
    pub cells_data: *const MrCellInfo,
    pub cells_len: u32,
    pub slots_data: *const MrSimSlotStatus,
    pub slots_len: u32,
    pub calls_data: *const MrCall,
    pub calls_len: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrIndication {
    pub kind: u32,
    pub signal: MrSignalStrength,
    pub radio_state: i32,
    pub generation: u64,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrOutput {
    pub kind: u32,
    pub invocation_id: u64,
    pub error: i32,
    pub value_present: u8,
    pub value: MrValue,
    pub indication: MrIndication,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MrLayout {
    pub name: MrString,
    pub size: u64,
    pub alignment: u64,
    pub offsets_data: *const u64,
    pub offsets_len: u32,
}
pub const MR_OK: i32 = 0;
pub const MR_INVALID_ARGUMENT: i32 = 1;
pub const MR_FULL: i32 = 2;
pub const MR_STOPPED: i32 = 3;
pub const MR_ID_EXHAUSTED: i32 = 4;
pub const MR_IO_ERROR: i32 = 5;
pub const MR_PANIC: i32 = 6;
pub const MR_INVALID_HANDLE: i32 = 7;
pub const MR_TIMEOUT: i32 = 8;
pub const MR_REQUEST_NETWORK_GET_VOICE_REGISTRATION_STATE: i32 = 1;
pub const MR_REQUEST_NETWORK_GET_DATA_REGISTRATION_STATE: i32 = 2;
pub const MR_REQUEST_NETWORK_GET_OPERATOR: i32 = 3;
pub const MR_REQUEST_NETWORK_GET_NETWORK_SELECTION_MODE: i32 = 4;
pub const MR_REQUEST_NETWORK_GET_SIGNAL_STRENGTH: i32 = 5;
pub const MR_REQUEST_NETWORK_GET_CELL_INFO_LIST: i32 = 6;
pub const MR_REQUEST_NETWORK_GET_IMS_REGISTRATION_STATE: i32 = 7;
pub const MR_REQUEST_NETWORK_SET_INDICATION_FILTER: i32 = 8;
pub const MR_REQUEST_NETWORK_SET_SIGNAL_STRENGTH_REPORTING_CRITERIA: i32 = 9;
pub const MR_REQUEST_NETWORK_SET_LINK_CAPACITY_REPORTING_CRITERIA: i32 = 10;
pub const MR_REQUEST_MODEM_SET_RADIO_POWER: i32 = 101;
pub const MR_REQUEST_MODEM_GET_BASEBAND_VERSION: i32 = 102;
pub const MR_REQUEST_MODEM_GET_IMEI: i32 = 103;
pub const MR_REQUEST_MODEM_GET_DEVICE_IDENTITY: i32 = 104;
pub const MR_REQUEST_MODEM_GET_MODEM_ACTIVITY_INFO: i32 = 105;
pub const MR_REQUEST_MODEM_GET_MODEM_STACK_STATUS: i32 = 106;
pub const MR_REQUEST_MODEM_ENABLE_MODEM: i32 = 107;
pub const MR_REQUEST_MODEM_SEND_DEVICE_STATE: i32 = 108;
pub const MR_REQUEST_MODEM_GET_RADIO_CAPABILITY: i32 = 109;
pub const MR_REQUEST_MODEM_GET_ICC_CARD_STATUS: i32 = 110;
pub const MR_REQUEST_MODEM_GET_UICC_APPLICATIONS_ENABLEMENT: i32 = 111;
pub const MR_REQUEST_MODEM_ARE_UICC_APPLICATIONS_ENABLED: i32 = 112;
pub const MR_REQUEST_MODEM_ENABLE_UICC_APPLICATIONS: i32 = 113;
pub const MR_REQUEST_MODEM_SET_CARRIER_INFO_FOR_IMSI_ENCRYPTION: i32 = 114;
pub const MR_REQUEST_MODEM_GET_HAL_DEVICE_CAPABILITIES: i32 = 115;
pub const MR_REQUEST_MODEM_GET_SIM_SLOTS_STATUS: i32 = 116;
pub const MR_REQUEST_MODEM_GET_PHONE_CAPABILITY: i32 = 117;
pub const MR_REQUEST_MODEM_GET_NUM_OF_LIVE_MODEMS: i32 = 118;
pub const MR_REQUEST_MODEM_SET_NUM_OF_LIVE_MODEMS: i32 = 119;
pub const MR_REQUEST_MODEM_SET_PREFERRED_DATA_MODEM: i32 = 120;
pub const MR_REQUEST_MODEM_GET_CURRENT_CALLS: i32 = 121;
pub const MR_REQUEST_MODEM_SET_TTY_MODE: i32 = 122;
pub const MR_REQUEST_MODEM_SET_GSM_BROADCAST_CONFIG: i32 = 123;
pub const MR_REQUEST_MODEM_SET_DATA_PROFILE: i32 = 124;
pub const MR_REQUEST_MODEM_SET_NULL_CIPHER_AND_INTEGRITY_ENABLED: i32 = 125;
pub const MR_REQUEST_MODEM_SET_SECURITY_ALGORITHMS_UPDATED_ENABLED: i32 = 126;
pub const MR_REQUEST_MODEM_SET_CELLULAR_IDENTIFIER_DISCLOSED_ENABLED: i32 = 127;
pub const MR_REQUEST_MODEM_IS_SATELLITE_ENABLED_FOR_CARRIER: i32 = 128;
pub const MR_REQUEST_MODEM_GET_BARRING_INFO: i32 = 129;
pub const MR_REQUEST_MODEM_GET_VOICE_RADIO_TECHNOLOGY: i32 = 130;
pub const MR_VALUE_NETWORK_VOID: i32 = 1;
pub const MR_VALUE_NETWORK_REGISTRATION: i32 = 2;
pub const MR_VALUE_NETWORK_OPERATOR: i32 = 3;
pub const MR_VALUE_NETWORK_SELECTION: i32 = 4;
pub const MR_VALUE_NETWORK_SIGNAL: i32 = 5;
pub const MR_VALUE_NETWORK_CELLS: i32 = 6;
pub const MR_VALUE_MODEM_VOID: i32 = 101;
pub const MR_VALUE_MODEM_BASEBAND: i32 = 102;
pub const MR_VALUE_MODEM_IMEI: i32 = 103;
pub const MR_VALUE_MODEM_IDENTITY: i32 = 104;
pub const MR_VALUE_MODEM_ACTIVITY: i32 = 105;
pub const MR_VALUE_MODEM_CAPABILITY: i32 = 106;
pub const MR_VALUE_MODEM_CARD: i32 = 107;
pub const MR_VALUE_MODEM_SLOTS: i32 = 108;
pub const MR_VALUE_MODEM_PHONE: i32 = 109;
pub const MR_VALUE_MODEM_REDUCED_FEATURE_SET1: i32 = 110;
pub const MR_VALUE_MODEM_CURRENT_CALLS: i32 = 111;
pub const MR_VALUE_MODEM_VOICE_RADIO_TECHNOLOGY: i32 = 112;
pub const MR_OUTPUT_RESPONSE: i32 = 1;
pub const MR_OUTPUT_INDICATION: i32 = 2;
pub const MR_INDICATION_NETWORK_STATE_CHANGED: i32 = 1;
pub const MR_INDICATION_CURRENT_SIGNAL_STRENGTH: i32 = 2;
pub const MR_INDICATION_RADIO_STATE_CHANGED: i32 = 3;
pub const MR_INDICATION_SIM_STATUS_CHANGED: i32 = 4;
pub const MR_INDICATION_MODEM_RESET: i32 = 5;
pub const MR_REG_STATE_NOT_REGISTERED: i32 = 0;
pub const MR_REG_STATE_HOME: i32 = 1;
pub const MR_REG_STATE_SEARCHING: i32 = 2;
pub const MR_REG_STATE_DENIED: i32 = 3;
pub const MR_REG_STATE_UNKNOWN: i32 = 4;
pub const MR_REG_STATE_ROAMING: i32 = 5;
pub const MR_RAT_UNKNOWN: i32 = 0;
pub const MR_RAT_GSM: i32 = 16;
pub const MR_RAT_UMTS: i32 = 3;
pub const MR_RAT_LTE: i32 = 14;
pub const MR_SELECTION_MODE_AUTOMATIC: i32 = 0;
pub const MR_SELECTION_MODE_MANUAL: i32 = 1;
pub const MR_SELECTION_MODE_DEREGISTERED_UNVERIFIED: i32 = 2;
pub const MR_SELECTION_MODE_MANUAL_AUTOMATIC_UNVERIFIED: i32 = 3;
pub const MR_DEVICE_STATE_TYPE_POWER_SAVE_MODE: i32 = 0;
pub const MR_DEVICE_STATE_TYPE_CHARGING_STATE: i32 = 1;
pub const MR_DEVICE_STATE_TYPE_LOW_DATA_EXPECTED: i32 = 2;
pub const MR_IMEI_TYPE_INVALID: i32 = 0;
pub const MR_IMEI_TYPE_PRIMARY: i32 = 1;
pub const MR_IMEI_TYPE_SECONDARY: i32 = 2;
pub const MR_CARD_STATE_ABSENT: i32 = 0;
pub const MR_CARD_STATE_PRESENT: i32 = 1;
pub const MR_CARD_STATE_ERROR: i32 = 2;
pub const MR_CARD_STATE_RESTRICTED: i32 = 3;
pub const MR_RADIO_UNAVAILABLE: i32 = 0;
pub const MR_RADIO_OFF: i32 = 1;
pub const MR_RADIO_ON: i32 = 2;
pub const MR_RADIO_ERROR_NONE: i32 = 0;
pub const MR_RADIO_ERROR_RADIO_NOT_AVAILABLE: i32 = 1;
pub const MR_RADIO_ERROR_GENERIC_FAILURE: i32 = 2;
pub const MR_RADIO_ERROR_REQUEST_NOT_SUPPORTED: i32 = 6;
pub const MR_RADIO_ERROR_INTERNAL_ERR: i32 = 38;
pub const MR_RADIO_ERROR_MODEM_ERR: i32 = 40;
pub const MR_RADIO_ERROR_SIM_ERR: i32 = 43;
pub const MR_RADIO_ERROR_INVALID_ARGUMENTS: i32 = 44;
pub const MR_RADIO_ERROR_SIM_BUSY: i32 = 51;
pub const MR_RADIO_ERROR_OPERATION_NOT_ALLOWED: i32 = 54;
pub const MR_RADIO_ERROR_EMPTY_RECORD: i32 = 55;
pub const MR_RADIO_ERROR_INVALID_RESPONSE: i32 = 66;
pub const MR_RADIO_ERROR_OEM_ERROR1: i32 = 501;
pub const MR_CELL_GSM: i32 = 1;
pub const MR_CELL_WCDMA: i32 = 2;
pub const MR_CELL_LTE: i32 = 3;
pub const MR_ABI_VERSION: i32 = 2;
pub const MR_MAX_INPUT_BYTES: i32 = 4194304;
pub const MR_MAX_ARRAY_ELEMENTS: i32 = 65536;
pub const MR_WAIT_FOREVER: u32 = u32::MAX;
pub(crate) fn layouts() -> Vec<MrLayout> {
    vec![
        MrLayout {
            name: MrString {
                data: b"mr_string".as_ptr(),
                len: 9,
            },
            size: size_of::<MrString>() as u64,
            alignment: align_of::<MrString>() as u64,
            offsets_data: &[
                offset_of!(MrString, data) as u64,
                offset_of!(MrString, len) as u64,
            ] as *const u64,
            offsets_len: 2,
        },
        MrLayout {
            name: MrString {
                data: b"mr_cell_identity".as_ptr(),
                len: 16,
            },
            size: size_of::<MrCellIdentity>() as u64,
            alignment: align_of::<MrCellIdentity>() as u64,
            offsets_data: &[
                offset_of!(MrCellIdentity, kind) as u64,
                offset_of!(MrCellIdentity, area) as u64,
                offset_of!(MrCellIdentity, cell) as u64,
                offset_of!(MrCellIdentity, plmn) as u64,
            ] as *const u64,
            offsets_len: 4,
        },
        MrLayout {
            name: MrString {
                data: b"mr_gsm_signal".as_ptr(),
                len: 13,
            },
            size: size_of::<MrGsmSignal>() as u64,
            alignment: align_of::<MrGsmSignal>() as u64,
            offsets_data: &[
                offset_of!(MrGsmSignal, signal_strength_present) as u64,
                offset_of!(MrGsmSignal, signal_strength) as u64,
                offset_of!(MrGsmSignal, bit_error_rate_present) as u64,
                offset_of!(MrGsmSignal, bit_error_rate) as u64,
            ] as *const u64,
            offsets_len: 4,
        },
        MrLayout {
            name: MrString {
                data: b"mr_lte_signal".as_ptr(),
                len: 13,
            },
            size: size_of::<MrLteSignal>() as u64,
            alignment: align_of::<MrLteSignal>() as u64,
            offsets_data: &[
                offset_of!(MrLteSignal, rsrp_present) as u64,
                offset_of!(MrLteSignal, rsrp) as u64,
                offset_of!(MrLteSignal, rsrq_present) as u64,
                offset_of!(MrLteSignal, rsrq) as u64,
                offset_of!(MrLteSignal, rssnr_present) as u64,
                offset_of!(MrLteSignal, rssnr) as u64,
            ] as *const u64,
            offsets_len: 6,
        },
        MrLayout {
            name: MrString {
                data: b"mr_wcdma_signal".as_ptr(),
                len: 15,
            },
            size: size_of::<MrWcdmaSignal>() as u64,
            alignment: align_of::<MrWcdmaSignal>() as u64,
            offsets_data: &[
                offset_of!(MrWcdmaSignal, signal_strength_present) as u64,
                offset_of!(MrWcdmaSignal, signal_strength) as u64,
                offset_of!(MrWcdmaSignal, rscp_present) as u64,
                offset_of!(MrWcdmaSignal, rscp) as u64,
                offset_of!(MrWcdmaSignal, ecno_present) as u64,
                offset_of!(MrWcdmaSignal, ecno) as u64,
            ] as *const u64,
            offsets_len: 6,
        },
        MrLayout {
            name: MrString {
                data: b"mr_signal_strength".as_ptr(),
                len: 18,
            },
            size: size_of::<MrSignalStrength>() as u64,
            alignment: align_of::<MrSignalStrength>() as u64,
            offsets_data: &[
                offset_of!(MrSignalStrength, gsm_present) as u64,
                offset_of!(MrSignalStrength, gsm) as u64,
                offset_of!(MrSignalStrength, lte_present) as u64,
                offset_of!(MrSignalStrength, lte) as u64,
                offset_of!(MrSignalStrength, wcdma_present) as u64,
                offset_of!(MrSignalStrength, wcdma) as u64,
            ] as *const u64,
            offsets_len: 6,
        },
        MrLayout {
            name: MrString {
                data: b"mr_reg_state_result".as_ptr(),
                len: 19,
            },
            size: size_of::<MrRegStateResult>() as u64,
            alignment: align_of::<MrRegStateResult>() as u64,
            offsets_data: &[
                offset_of!(MrRegStateResult, reg_state) as u64,
                offset_of!(MrRegStateResult, rat) as u64,
                offset_of!(MrRegStateResult, cell_identity_present) as u64,
                offset_of!(MrRegStateResult, cell_identity) as u64,
                offset_of!(MrRegStateResult, reason_for_denial) as u64,
                offset_of!(MrRegStateResult, registered_plmn) as u64,
            ] as *const u64,
            offsets_len: 6,
        },
        MrLayout {
            name: MrString {
                data: b"mr_operator_info".as_ptr(),
                len: 16,
            },
            size: size_of::<MrOperatorInfo>() as u64,
            alignment: align_of::<MrOperatorInfo>() as u64,
            offsets_data: &[
                offset_of!(MrOperatorInfo, alpha_long) as u64,
                offset_of!(MrOperatorInfo, alpha_short) as u64,
                offset_of!(MrOperatorInfo, numeric) as u64,
            ] as *const u64,
            offsets_len: 3,
        },
        MrLayout {
            name: MrString {
                data: b"mr_cell_info".as_ptr(),
                len: 12,
            },
            size: size_of::<MrCellInfo>() as u64,
            alignment: align_of::<MrCellInfo>() as u64,
            offsets_data: &[
                offset_of!(MrCellInfo, registered) as u64,
                offset_of!(MrCellInfo, identity) as u64,
                offset_of!(MrCellInfo, signal) as u64,
            ] as *const u64,
            offsets_len: 3,
        },
        MrLayout {
            name: MrString {
                data: b"mr_signal_criteria".as_ptr(),
                len: 18,
            },
            size: size_of::<MrSignalCriteria>() as u64,
            alignment: align_of::<MrSignalCriteria>() as u64,
            offsets_data: &[
                offset_of!(MrSignalCriteria, access_network) as u64,
                offset_of!(MrSignalCriteria, measurement) as u64,
                offset_of!(MrSignalCriteria, enabled) as u64,
                offset_of!(MrSignalCriteria, hysteresis_ms) as u64,
                offset_of!(MrSignalCriteria, hysteresis_db) as u64,
                offset_of!(MrSignalCriteria, thresholds_data) as u64,
                offset_of!(MrSignalCriteria, thresholds_len) as u64,
            ] as *const u64,
            offsets_len: 7,
        },
        MrLayout {
            name: MrString {
                data: b"mr_link_criteria".as_ptr(),
                len: 16,
            },
            size: size_of::<MrLinkCriteria>() as u64,
            alignment: align_of::<MrLinkCriteria>() as u64,
            offsets_data: &[
                offset_of!(MrLinkCriteria, access_network) as u64,
                offset_of!(MrLinkCriteria, hysteresis_ms) as u64,
                offset_of!(MrLinkCriteria, hysteresis_dl_kbps) as u64,
                offset_of!(MrLinkCriteria, hysteresis_ul_kbps) as u64,
                offset_of!(MrLinkCriteria, thresholds_dl_kbps_data) as u64,
                offset_of!(MrLinkCriteria, thresholds_dl_kbps_len) as u64,
                offset_of!(MrLinkCriteria, thresholds_ul_kbps_data) as u64,
                offset_of!(MrLinkCriteria, thresholds_ul_kbps_len) as u64,
            ] as *const u64,
            offsets_len: 8,
        },
        MrLayout {
            name: MrString {
                data: b"mr_imei_info".as_ptr(),
                len: 12,
            },
            size: size_of::<MrImeiInfo>() as u64,
            alignment: align_of::<MrImeiInfo>() as u64,
            offsets_data: &[
                offset_of!(MrImeiInfo, kind) as u64,
                offset_of!(MrImeiInfo, imei) as u64,
                offset_of!(MrImeiInfo, svn) as u64,
            ] as *const u64,
            offsets_len: 3,
        },
        MrLayout {
            name: MrString {
                data: b"mr_device_identity".as_ptr(),
                len: 18,
            },
            size: size_of::<MrDeviceIdentity>() as u64,
            alignment: align_of::<MrDeviceIdentity>() as u64,
            offsets_data: &[
                offset_of!(MrDeviceIdentity, imei) as u64,
                offset_of!(MrDeviceIdentity, imeisv) as u64,
                offset_of!(MrDeviceIdentity, esn) as u64,
                offset_of!(MrDeviceIdentity, meid) as u64,
            ] as *const u64,
            offsets_len: 4,
        },
        MrLayout {
            name: MrString {
                data: b"mr_activity_stats_tech_specific_info".as_ptr(),
                len: 36,
            },
            size: size_of::<MrActivityStatsTechSpecificInfo>() as u64,
            alignment: align_of::<MrActivityStatsTechSpecificInfo>() as u64,
            offsets_data: &[
                offset_of!(MrActivityStatsTechSpecificInfo, rat) as u64,
                offset_of!(MrActivityStatsTechSpecificInfo, frequency_range) as u64,
                offset_of!(MrActivityStatsTechSpecificInfo, txm_mode_time_ms) as u64,
                offset_of!(MrActivityStatsTechSpecificInfo, rx_mode_time_ms) as u64,
            ] as *const u64,
            offsets_len: 4,
        },
        MrLayout {
            name: MrString {
                data: b"mr_activity_stats_info".as_ptr(),
                len: 22,
            },
            size: size_of::<MrActivityStatsInfo>() as u64,
            alignment: align_of::<MrActivityStatsInfo>() as u64,
            offsets_data: &[
                offset_of!(MrActivityStatsInfo, sleep_mode_time_ms) as u64,
                offset_of!(MrActivityStatsInfo, idle_mode_time_ms) as u64,
                offset_of!(MrActivityStatsInfo, tech_specific_info_data) as u64,
                offset_of!(MrActivityStatsInfo, tech_specific_info_len) as u64,
            ] as *const u64,
            offsets_len: 4,
        },
        MrLayout {
            name: MrString {
                data: b"mr_radio_capability".as_ptr(),
                len: 19,
            },
            size: size_of::<MrRadioCapability>() as u64,
            alignment: align_of::<MrRadioCapability>() as u64,
            offsets_data: &[
                offset_of!(MrRadioCapability, session) as u64,
                offset_of!(MrRadioCapability, phase) as u64,
                offset_of!(MrRadioCapability, raf) as u64,
                offset_of!(MrRadioCapability, logical_modem_uuid) as u64,
                offset_of!(MrRadioCapability, status) as u64,
            ] as *const u64,
            offsets_len: 5,
        },
        MrLayout {
            name: MrString {
                data: b"mr_slot_port_mapping".as_ptr(),
                len: 20,
            },
            size: size_of::<MrSlotPortMapping>() as u64,
            alignment: align_of::<MrSlotPortMapping>() as u64,
            offsets_data: &[
                offset_of!(MrSlotPortMapping, physical_slot_id) as u64,
                offset_of!(MrSlotPortMapping, port_id) as u64,
            ] as *const u64,
            offsets_len: 2,
        },
        MrLayout {
            name: MrString {
                data: b"mr_app_status".as_ptr(),
                len: 13,
            },
            size: size_of::<MrAppStatus>() as u64,
            alignment: align_of::<MrAppStatus>() as u64,
            offsets_data: &[
                offset_of!(MrAppStatus, app_type) as u64,
                offset_of!(MrAppStatus, app_state) as u64,
                offset_of!(MrAppStatus, perso_substate) as u64,
                offset_of!(MrAppStatus, aid_ptr) as u64,
                offset_of!(MrAppStatus, app_label_ptr) as u64,
                offset_of!(MrAppStatus, pin1_replaced) as u64,
                offset_of!(MrAppStatus, pin1) as u64,
                offset_of!(MrAppStatus, pin2) as u64,
            ] as *const u64,
            offsets_len: 8,
        },
        MrLayout {
            name: MrString {
                data: b"mr_card_status".as_ptr(),
                len: 14,
            },
            size: size_of::<MrCardStatus>() as u64,
            alignment: align_of::<MrCardStatus>() as u64,
            offsets_data: &[
                offset_of!(MrCardStatus, card_state) as u64,
                offset_of!(MrCardStatus, universal_pin_state) as u64,
                offset_of!(MrCardStatus, gsm_umts_subscription_app_index) as u64,
                offset_of!(MrCardStatus, cdma_subscription_app_index) as u64,
                offset_of!(MrCardStatus, ims_subscription_app_index) as u64,
                offset_of!(MrCardStatus, applications_data) as u64,
                offset_of!(MrCardStatus, applications_len) as u64,
                offset_of!(MrCardStatus, atr) as u64,
                offset_of!(MrCardStatus, iccid) as u64,
                offset_of!(MrCardStatus, eid) as u64,
                offset_of!(MrCardStatus, slot_map) as u64,
                offset_of!(MrCardStatus, supported_mep_mode) as u64,
            ] as *const u64,
            offsets_len: 12,
        },
        MrLayout {
            name: MrString {
                data: b"mr_sim_port_info".as_ptr(),
                len: 16,
            },
            size: size_of::<MrSimPortInfo>() as u64,
            alignment: align_of::<MrSimPortInfo>() as u64,
            offsets_data: &[
                offset_of!(MrSimPortInfo, icc_id) as u64,
                offset_of!(MrSimPortInfo, logical_slot_id) as u64,
                offset_of!(MrSimPortInfo, port_active) as u64,
            ] as *const u64,
            offsets_len: 3,
        },
        MrLayout {
            name: MrString {
                data: b"mr_sim_slot_status".as_ptr(),
                len: 18,
            },
            size: size_of::<MrSimSlotStatus>() as u64,
            alignment: align_of::<MrSimSlotStatus>() as u64,
            offsets_data: &[
                offset_of!(MrSimSlotStatus, card_state) as u64,
                offset_of!(MrSimSlotStatus, atr) as u64,
                offset_of!(MrSimSlotStatus, eid) as u64,
                offset_of!(MrSimSlotStatus, port_info_data) as u64,
                offset_of!(MrSimSlotStatus, port_info_len) as u64,
                offset_of!(MrSimSlotStatus, supported_mep_mode) as u64,
            ] as *const u64,
            offsets_len: 6,
        },
        MrLayout {
            name: MrString {
                data: b"mr_phone_capability".as_ptr(),
                len: 19,
            },
            size: size_of::<MrPhoneCapability>() as u64,
            alignment: align_of::<MrPhoneCapability>() as u64,
            offsets_data: &[
                offset_of!(MrPhoneCapability, max_active_data) as u64,
                offset_of!(MrPhoneCapability, max_active_internet_data) as u64,
                offset_of!(MrPhoneCapability, is_internet_lingering_supported) as u64,
                offset_of!(MrPhoneCapability, logical_modem_ids_data) as u64,
                offset_of!(MrPhoneCapability, logical_modem_ids_len) as u64,
                offset_of!(MrPhoneCapability, max_active_voice) as u64,
            ] as *const u64,
            offsets_len: 6,
        },
        MrLayout {
            name: MrString {
                data: b"mr_gsm_broadcast_sms_config_info".as_ptr(),
                len: 32,
            },
            size: size_of::<MrGsmBroadcastSmsConfigInfo>() as u64,
            alignment: align_of::<MrGsmBroadcastSmsConfigInfo>() as u64,
            offsets_data: &[
                offset_of!(MrGsmBroadcastSmsConfigInfo, from_service_id) as u64,
                offset_of!(MrGsmBroadcastSmsConfigInfo, to_service_id) as u64,
                offset_of!(MrGsmBroadcastSmsConfigInfo, from_code_scheme) as u64,
                offset_of!(MrGsmBroadcastSmsConfigInfo, to_code_scheme) as u64,
                offset_of!(MrGsmBroadcastSmsConfigInfo, selected) as u64,
            ] as *const u64,
            offsets_len: 5,
        },
        MrLayout {
            name: MrString {
                data: b"mr_uus_info".as_ptr(),
                len: 11,
            },
            size: size_of::<MrUusInfo>() as u64,
            alignment: align_of::<MrUusInfo>() as u64,
            offsets_data: &[
                offset_of!(MrUusInfo, uus_type) as u64,
                offset_of!(MrUusInfo, uus_dcs) as u64,
                offset_of!(MrUusInfo, uus_data) as u64,
            ] as *const u64,
            offsets_len: 3,
        },
        MrLayout {
            name: MrString {
                data: b"mr_call".as_ptr(),
                len: 7,
            },
            size: size_of::<MrCall>() as u64,
            alignment: align_of::<MrCall>() as u64,
            offsets_data: &[
                offset_of!(MrCall, state) as u64,
                offset_of!(MrCall, index) as u64,
                offset_of!(MrCall, toa) as u64,
                offset_of!(MrCall, is_mpty) as u64,
                offset_of!(MrCall, is_mt) as u64,
                offset_of!(MrCall, als) as u64,
                offset_of!(MrCall, is_voice) as u64,
                offset_of!(MrCall, is_voice_privacy) as u64,
                offset_of!(MrCall, number) as u64,
                offset_of!(MrCall, number_presentation) as u64,
                offset_of!(MrCall, name) as u64,
                offset_of!(MrCall, name_presentation) as u64,
                offset_of!(MrCall, uus_info_data) as u64,
                offset_of!(MrCall, uus_info_len) as u64,
                offset_of!(MrCall, audio_quality) as u64,
                offset_of!(MrCall, forwarded_number) as u64,
            ] as *const u64,
            offsets_len: 16,
        },
        MrLayout {
            name: MrString {
                data: b"mr_request".as_ptr(),
                len: 10,
            },
            size: size_of::<MrRequest>() as u64,
            alignment: align_of::<MrRequest>() as u64,
            offsets_data: &[
                offset_of!(MrRequest, kind) as u64,
                offset_of!(MrRequest, boolean) as u64,
                offset_of!(MrRequest, mask) as u64,
                offset_of!(MrRequest, integer) as u64,
                offset_of!(MrRequest, small_integer) as u64,
                offset_of!(MrRequest, profile_count) as u64,
                offset_of!(MrRequest, power_on) as u64,
                offset_of!(MrRequest, for_emergency_call) as u64,
                offset_of!(MrRequest, preferred_for_emergency_call) as u64,
                offset_of!(MrRequest, device_state_type) as u64,
                offset_of!(MrRequest, signal_criteria_data) as u64,
                offset_of!(MrRequest, signal_criteria_len) as u64,
                offset_of!(MrRequest, link_criteria) as u64,
                offset_of!(MrRequest, broadcast_config_data) as u64,
                offset_of!(MrRequest, broadcast_config_len) as u64,
            ] as *const u64,
            offsets_len: 15,
        },
        MrLayout {
            name: MrString {
                data: b"mr_value".as_ptr(),
                len: 8,
            },
            size: size_of::<MrValue>() as u64,
            alignment: align_of::<MrValue>() as u64,
            offsets_data: &[
                offset_of!(MrValue, kind) as u64,
                offset_of!(MrValue, boolean) as u64,
                offset_of!(MrValue, integer) as u64,
                offset_of!(MrValue, text) as u64,
                offset_of!(MrValue, registration) as u64,
                offset_of!(MrValue, operator_info) as u64,
                offset_of!(MrValue, signal) as u64,
                offset_of!(MrValue, imei) as u64,
                offset_of!(MrValue, identity) as u64,
                offset_of!(MrValue, activity) as u64,
                offset_of!(MrValue, capability) as u64,
                offset_of!(MrValue, card) as u64,
                offset_of!(MrValue, phone) as u64,
                offset_of!(MrValue, selection) as u64,
                offset_of!(MrValue, cells_data) as u64,
                offset_of!(MrValue, cells_len) as u64,
                offset_of!(MrValue, slots_data) as u64,
                offset_of!(MrValue, slots_len) as u64,
                offset_of!(MrValue, calls_data) as u64,
                offset_of!(MrValue, calls_len) as u64,
            ] as *const u64,
            offsets_len: 20,
        },
        MrLayout {
            name: MrString {
                data: b"mr_indication".as_ptr(),
                len: 13,
            },
            size: size_of::<MrIndication>() as u64,
            alignment: align_of::<MrIndication>() as u64,
            offsets_data: &[
                offset_of!(MrIndication, kind) as u64,
                offset_of!(MrIndication, signal) as u64,
                offset_of!(MrIndication, radio_state) as u64,
                offset_of!(MrIndication, generation) as u64,
            ] as *const u64,
            offsets_len: 4,
        },
        MrLayout {
            name: MrString {
                data: b"mr_output".as_ptr(),
                len: 9,
            },
            size: size_of::<MrOutput>() as u64,
            alignment: align_of::<MrOutput>() as u64,
            offsets_data: &[
                offset_of!(MrOutput, kind) as u64,
                offset_of!(MrOutput, invocation_id) as u64,
                offset_of!(MrOutput, error) as u64,
                offset_of!(MrOutput, value_present) as u64,
                offset_of!(MrOutput, value) as u64,
                offset_of!(MrOutput, indication) as u64,
            ] as *const u64,
            offsets_len: 6,
        },
        MrLayout {
            name: MrString {
                data: b"mr_layout".as_ptr(),
                len: 9,
            },
            size: size_of::<MrLayout>() as u64,
            alignment: align_of::<MrLayout>() as u64,
            offsets_data: &[
                offset_of!(MrLayout, name) as u64,
                offset_of!(MrLayout, size) as u64,
                offset_of!(MrLayout, alignment) as u64,
                offset_of!(MrLayout, offsets_data) as u64,
                offset_of!(MrLayout, offsets_len) as u64,
            ] as *const u64,
            offsets_len: 5,
        },
    ]
}
