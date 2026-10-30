/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use super::*;
use crate::convert::Codec;
use mindone_radio_modem::types as m;
use mindone_radio_network::types as n;
use mindone_radio_runtime::{
    core::{Indication, Request, Value},
    Delivery,
};
use mindone_radio_state::model as s;
fn round_trip<T: Codec + std::fmt::Debug + PartialEq>(value: T) {
    let mut arena = Arena::default();
    let wire = value.encode(&mut arena);
    let decoded = T::decode(wire, &Input::default()).unwrap();
    drop(arena);
    assert_eq!(value, decoded);
}
#[test]
fn enum_n_reg_state_not_registered() {
    round_trip(n::RegState::NotRegistered);
}
#[test]
fn enum_n_reg_state_home() {
    round_trip(n::RegState::Home);
}
#[test]
fn enum_n_reg_state_searching() {
    round_trip(n::RegState::Searching);
}
#[test]
fn enum_n_reg_state_denied() {
    round_trip(n::RegState::Denied);
}
#[test]
fn enum_n_reg_state_unknown() {
    round_trip(n::RegState::Unknown);
}
#[test]
fn enum_n_reg_state_roaming() {
    round_trip(n::RegState::Roaming);
}
#[test]
fn enum_n_rat_unknown() {
    round_trip(n::Rat::Unknown);
}
#[test]
fn enum_n_rat_gsm() {
    round_trip(n::Rat::Gsm);
}
#[test]
fn enum_n_rat_umts() {
    round_trip(n::Rat::Umts);
}
#[test]
fn enum_n_rat_lte() {
    round_trip(n::Rat::Lte);
}
#[test]
fn enum_n_selection_mode_automatic() {
    round_trip(n::SelectionMode::Automatic);
}
#[test]
fn enum_n_selection_mode_manual() {
    round_trip(n::SelectionMode::Manual);
}
#[test]
fn enum_n_selection_mode_deregistered_unverified() {
    round_trip(n::SelectionMode::DeregisteredUnverified);
}
#[test]
fn enum_n_selection_mode_manual_automatic_unverified() {
    round_trip(n::SelectionMode::ManualAutomaticUnverified);
}
#[test]
fn enum_m_device_state_type_power_save_mode() {
    round_trip(m::DeviceStateType::PowerSaveMode);
}
#[test]
fn enum_m_device_state_type_charging_state() {
    round_trip(m::DeviceStateType::ChargingState);
}
#[test]
fn enum_m_device_state_type_low_data_expected() {
    round_trip(m::DeviceStateType::LowDataExpected);
}
#[test]
fn enum_m_imei_type_invalid() {
    round_trip(m::ImeiType::Invalid);
}
#[test]
fn enum_m_imei_type_primary() {
    round_trip(m::ImeiType::Primary);
}
#[test]
fn enum_m_imei_type_secondary() {
    round_trip(m::ImeiType::Secondary);
}
#[test]
fn enum_m_card_state_absent() {
    round_trip(m::CardState::Absent);
}
#[test]
fn enum_m_card_state_present() {
    round_trip(m::CardState::Present);
}
#[test]
fn enum_m_card_state_error() {
    round_trip(m::CardState::Error);
}
#[test]
fn enum_m_card_state_restricted() {
    round_trip(m::CardState::Restricted);
}
#[test]
fn enum_s_radio_unavailable() {
    round_trip(s::Radio::Unavailable);
}
#[test]
fn enum_s_radio_off() {
    round_trip(s::Radio::Off);
}
#[test]
fn enum_s_radio_on() {
    round_trip(s::Radio::On);
}
#[test]
fn error_number_none() {
    let mut arena = Arena::default();
    assert_eq!(m::RadioError::None.encode(&mut arena), 0);
    assert_eq!(
        m::RadioError::None.encode(&mut arena),
        m::RadioError::None as i32
    );
}
#[test]
fn enum_m_radio_error_none() {
    round_trip(m::RadioError::None);
}
#[test]
fn error_number_radio_not_available() {
    let mut arena = Arena::default();
    assert_eq!(m::RadioError::RadioNotAvailable.encode(&mut arena), 1);
    assert_eq!(
        m::RadioError::RadioNotAvailable.encode(&mut arena),
        m::RadioError::RadioNotAvailable as i32
    );
}
#[test]
fn enum_m_radio_error_radio_not_available() {
    round_trip(m::RadioError::RadioNotAvailable);
}
#[test]
fn error_number_generic_failure() {
    let mut arena = Arena::default();
    assert_eq!(m::RadioError::GenericFailure.encode(&mut arena), 2);
    assert_eq!(
        m::RadioError::GenericFailure.encode(&mut arena),
        m::RadioError::GenericFailure as i32
    );
}
#[test]
fn enum_m_radio_error_generic_failure() {
    round_trip(m::RadioError::GenericFailure);
}
#[test]
fn error_number_request_not_supported() {
    let mut arena = Arena::default();
    assert_eq!(m::RadioError::RequestNotSupported.encode(&mut arena), 6);
    assert_eq!(
        m::RadioError::RequestNotSupported.encode(&mut arena),
        m::RadioError::RequestNotSupported as i32
    );
}
#[test]
fn enum_m_radio_error_request_not_supported() {
    round_trip(m::RadioError::RequestNotSupported);
}
#[test]
fn error_number_internal_err() {
    let mut arena = Arena::default();
    assert_eq!(m::RadioError::InternalErr.encode(&mut arena), 38);
    assert_eq!(
        m::RadioError::InternalErr.encode(&mut arena),
        m::RadioError::InternalErr as i32
    );
}
#[test]
fn enum_m_radio_error_internal_err() {
    round_trip(m::RadioError::InternalErr);
}
#[test]
fn error_number_modem_err() {
    let mut arena = Arena::default();
    assert_eq!(m::RadioError::ModemErr.encode(&mut arena), 40);
    assert_eq!(
        m::RadioError::ModemErr.encode(&mut arena),
        m::RadioError::ModemErr as i32
    );
}
#[test]
fn enum_m_radio_error_modem_err() {
    round_trip(m::RadioError::ModemErr);
}
#[test]
fn error_number_sim_err() {
    let mut arena = Arena::default();
    assert_eq!(m::RadioError::SimErr.encode(&mut arena), 43);
    assert_eq!(
        m::RadioError::SimErr.encode(&mut arena),
        m::RadioError::SimErr as i32
    );
}
#[test]
fn enum_m_radio_error_sim_err() {
    round_trip(m::RadioError::SimErr);
}
#[test]
fn error_number_invalid_arguments() {
    let mut arena = Arena::default();
    assert_eq!(m::RadioError::InvalidArguments.encode(&mut arena), 44);
    assert_eq!(
        m::RadioError::InvalidArguments.encode(&mut arena),
        m::RadioError::InvalidArguments as i32
    );
}
#[test]
fn enum_m_radio_error_invalid_arguments() {
    round_trip(m::RadioError::InvalidArguments);
}
#[test]
fn error_number_sim_busy() {
    let mut arena = Arena::default();
    assert_eq!(m::RadioError::SimBusy.encode(&mut arena), 51);
    assert_eq!(
        m::RadioError::SimBusy.encode(&mut arena),
        m::RadioError::SimBusy as i32
    );
}
#[test]
fn enum_m_radio_error_sim_busy() {
    round_trip(m::RadioError::SimBusy);
}
#[test]
fn error_number_operation_not_allowed() {
    let mut arena = Arena::default();
    assert_eq!(m::RadioError::OperationNotAllowed.encode(&mut arena), 54);
    assert_eq!(
        m::RadioError::OperationNotAllowed.encode(&mut arena),
        m::RadioError::OperationNotAllowed as i32
    );
}
#[test]
fn enum_m_radio_error_operation_not_allowed() {
    round_trip(m::RadioError::OperationNotAllowed);
}
#[test]
fn error_number_empty_record() {
    let mut arena = Arena::default();
    assert_eq!(m::RadioError::EmptyRecord.encode(&mut arena), 55);
    assert_eq!(
        m::RadioError::EmptyRecord.encode(&mut arena),
        m::RadioError::EmptyRecord as i32
    );
}
#[test]
fn enum_m_radio_error_empty_record() {
    round_trip(m::RadioError::EmptyRecord);
}
#[test]
fn error_number_invalid_response() {
    let mut arena = Arena::default();
    assert_eq!(m::RadioError::InvalidResponse.encode(&mut arena), 66);
    assert_eq!(
        m::RadioError::InvalidResponse.encode(&mut arena),
        m::RadioError::InvalidResponse as i32
    );
}
#[test]
fn enum_m_radio_error_invalid_response() {
    round_trip(m::RadioError::InvalidResponse);
}
#[test]
fn error_number_oem_error1() {
    let mut arena = Arena::default();
    assert_eq!(m::RadioError::OemError1.encode(&mut arena), 501);
    assert_eq!(
        m::RadioError::OemError1.encode(&mut arena),
        m::RadioError::OemError1 as i32
    );
}
#[test]
fn enum_m_radio_error_oem_error1() {
    round_trip(m::RadioError::OemError1);
}
#[test]
fn model_n_gsm_signal_1() {
    round_trip(n::GsmSignal {
        signal_strength: Some(32),
        bit_error_rate: Some(33),
    });
}
#[test]
fn model_n_gsm_signal_0() {
    round_trip(n::GsmSignal {
        signal_strength: None,
        bit_error_rate: None,
    });
}
#[test]
fn model_n_lte_signal_1() {
    round_trip(n::LteSignal {
        rsrp: Some(-32),
        rsrq: Some(33),
        rssnr: Some(-34),
    });
}
#[test]
fn model_n_lte_signal_0() {
    round_trip(n::LteSignal {
        rsrp: None,
        rsrq: None,
        rssnr: None,
    });
}
#[test]
fn model_n_wcdma_signal_1() {
    round_trip(n::WcdmaSignal {
        signal_strength: Some(32),
        rscp: Some(33),
        ecno: Some(-34),
    });
}
#[test]
fn model_n_wcdma_signal_0() {
    round_trip(n::WcdmaSignal {
        signal_strength: None,
        rscp: None,
        ecno: None,
    });
}
#[test]
fn model_n_signal_strength_1() {
    round_trip(n::SignalStrength {
        gsm: Some(n::GsmSignal {
            signal_strength: Some(89),
            bit_error_rate: Some(90),
        }),
        lte: Some(n::LteSignal {
            rsrp: Some(23),
            rsrq: Some(-24),
            rssnr: Some(25),
        }),
        wcdma: Some(n::WcdmaSignal {
            signal_strength: Some(54),
            rscp: Some(55),
            ecno: Some(-56),
        }),
    });
}
#[test]
fn model_n_signal_strength_0() {
    round_trip(n::SignalStrength {
        gsm: None,
        lte: None,
        wcdma: None,
    });
}
#[test]
fn model_n_reg_state_result_1() {
    round_trip(n::RegStateResult {
        reg_state: n::RegState::Home,
        rat: n::Rat::Unknown,
        cell_identity: Some(n::CellIdentity::Lte {
            tac: 23,
            ci: 42,
            plmn: "44010".into(),
        }),
        reason_for_denial: 35,
        registered_plmn: "field35\0日本語".to_owned(),
    });
}
#[test]
fn model_n_reg_state_result_0() {
    round_trip(n::RegStateResult {
        reg_state: n::RegState::Home,
        rat: n::Rat::Unknown,
        cell_identity: None,
        reason_for_denial: 35,
        registered_plmn: "field35\0日本語".to_owned(),
    });
}
#[test]
fn model_n_operator_info_1() {
    round_trip(n::OperatorInfo {
        alpha_long: "field31\0日本語".to_owned(),
        alpha_short: "field32\0日本語".to_owned(),
        numeric: "field33\0日本語".to_owned(),
    });
}
#[test]
fn model_n_operator_info_0() {
    round_trip(n::OperatorInfo {
        alpha_long: "field31\0日本語".to_owned(),
        alpha_short: "field32\0日本語".to_owned(),
        numeric: "field33\0日本語".to_owned(),
    });
}
#[test]
fn model_n_cell_info_1() {
    round_trip(n::CellInfo {
        registered: true,
        identity: n::CellIdentity::Lte {
            tac: 23,
            ci: 42,
            plmn: "44010".into(),
        },
        signal: n::SignalStrength {
            gsm: Some(n::GsmSignal {
                signal_strength: Some(92),
                bit_error_rate: Some(93),
            }),
            lte: Some(n::LteSignal {
                rsrp: Some(26),
                rsrq: Some(-27),
                rssnr: Some(28),
            }),
            wcdma: Some(n::WcdmaSignal {
                signal_strength: Some(57),
                rscp: Some(58),
                ecno: Some(-59),
            }),
        },
    });
}
#[test]
fn model_n_cell_info_0() {
    round_trip(n::CellInfo {
        registered: true,
        identity: n::CellIdentity::Lte {
            tac: 23,
            ci: 42,
            plmn: "44010".into(),
        },
        signal: n::SignalStrength {
            gsm: None,
            lte: None,
            wcdma: None,
        },
    });
}
#[test]
fn model_n_signal_criteria_1() {
    round_trip(n::SignalCriteria {
        access_network: -32,
        measurement: 33,
        enabled: true,
        hysteresis_ms: 35,
        hysteresis_db: -36,
        thresholds: vec![37, -38],
    });
}
#[test]
fn model_n_signal_criteria_0() {
    round_trip(n::SignalCriteria {
        access_network: -32,
        measurement: 33,
        enabled: true,
        hysteresis_ms: 35,
        hysteresis_db: -36,
        thresholds: vec![37, -38],
    });
}
#[test]
fn model_n_link_criteria_1() {
    round_trip(n::LinkCriteria {
        access_network: -32,
        hysteresis_ms: 33,
        hysteresis_dl_kbps: -34,
        hysteresis_ul_kbps: 35,
        thresholds_dl_kbps: vec![-36, 37],
        thresholds_ul_kbps: vec![37, -38],
    });
}
#[test]
fn model_n_link_criteria_0() {
    round_trip(n::LinkCriteria {
        access_network: -32,
        hysteresis_ms: 33,
        hysteresis_dl_kbps: -34,
        hysteresis_ul_kbps: 35,
        thresholds_dl_kbps: vec![-36, 37],
        thresholds_ul_kbps: vec![37, -38],
    });
}
#[test]
fn model_m_imei_info_1() {
    round_trip(m::ImeiInfo {
        kind: m::ImeiType::Primary,
        imei: "field32\0日本語".to_owned(),
        svn: "field33\0日本語".to_owned(),
    });
}
#[test]
fn model_m_imei_info_0() {
    round_trip(m::ImeiInfo {
        kind: m::ImeiType::Primary,
        imei: "field32\0日本語".to_owned(),
        svn: "field33\0日本語".to_owned(),
    });
}
#[test]
fn model_m_device_identity_1() {
    round_trip(m::DeviceIdentity {
        imei: "field31\0日本語".to_owned(),
        imeisv: "field32\0日本語".to_owned(),
        esn: "field33\0日本語".to_owned(),
        meid: "field34\0日本語".to_owned(),
    });
}
#[test]
fn model_m_device_identity_0() {
    round_trip(m::DeviceIdentity {
        imei: "field31\0日本語".to_owned(),
        imeisv: "field32\0日本語".to_owned(),
        esn: "field33\0日本語".to_owned(),
        meid: "field34\0日本語".to_owned(),
    });
}
#[test]
fn model_m_activity_stats_tech_specific_info_1() {
    round_trip(m::ActivityStatsTechSpecificInfo {
        rat: -32,
        frequency_range: 33,
        txm_mode_time_ms: [1, -2, 3, -4, 5],
        rx_mode_time_ms: 35,
    });
}
#[test]
fn model_m_activity_stats_tech_specific_info_0() {
    round_trip(m::ActivityStatsTechSpecificInfo {
        rat: -32,
        frequency_range: 33,
        txm_mode_time_ms: [1, -2, 3, -4, 5],
        rx_mode_time_ms: 35,
    });
}
#[test]
fn model_m_activity_stats_info_1() {
    round_trip(m::ActivityStatsInfo {
        sleep_mode_time_ms: -32,
        idle_mode_time_ms: 33,
        tech_specific_info: vec![
            m::ActivityStatsTechSpecificInfo {
                rat: -54,
                frequency_range: 55,
                txm_mode_time_ms: [1, -2, 3, -4, 5],
                rx_mode_time_ms: 57,
            },
            m::ActivityStatsTechSpecificInfo {
                rat: 85,
                frequency_range: -86,
                txm_mode_time_ms: [1, -2, 3, -4, 5],
                rx_mode_time_ms: -88,
            },
        ],
    });
}
#[test]
fn model_m_activity_stats_info_0() {
    round_trip(m::ActivityStatsInfo {
        sleep_mode_time_ms: -32,
        idle_mode_time_ms: 33,
        tech_specific_info: vec![
            m::ActivityStatsTechSpecificInfo {
                rat: -54,
                frequency_range: 55,
                txm_mode_time_ms: [1, -2, 3, -4, 5],
                rx_mode_time_ms: 57,
            },
            m::ActivityStatsTechSpecificInfo {
                rat: 85,
                frequency_range: -86,
                txm_mode_time_ms: [1, -2, 3, -4, 5],
                rx_mode_time_ms: -88,
            },
        ],
    });
}
#[test]
fn model_m_radio_capability_1() {
    round_trip(m::RadioCapability {
        session: -32,
        phase: 33,
        raf: -34,
        logical_modem_uuid: "field34\0日本語".to_owned(),
        status: -36,
    });
}
#[test]
fn model_m_radio_capability_0() {
    round_trip(m::RadioCapability {
        session: -32,
        phase: 33,
        raf: -34,
        logical_modem_uuid: "field34\0日本語".to_owned(),
        status: -36,
    });
}
#[test]
fn model_m_slot_port_mapping_1() {
    round_trip(m::SlotPortMapping {
        physical_slot_id: -32,
        port_id: 33,
    });
}
#[test]
fn model_m_slot_port_mapping_0() {
    round_trip(m::SlotPortMapping {
        physical_slot_id: -32,
        port_id: 33,
    });
}
#[test]
fn model_m_app_status_1() {
    round_trip(m::AppStatus {
        app_type: -32,
        app_state: 33,
        perso_substate: -34,
        aid_ptr: "field34\0日本語".to_owned(),
        app_label_ptr: "field35\0日本語".to_owned(),
        pin1_replaced: false,
        pin1: -38,
        pin2: 39,
    });
}
#[test]
fn model_m_app_status_0() {
    round_trip(m::AppStatus {
        app_type: -32,
        app_state: 33,
        perso_substate: -34,
        aid_ptr: "field34\0日本語".to_owned(),
        app_label_ptr: "field35\0日本語".to_owned(),
        pin1_replaced: false,
        pin1: -38,
        pin2: 39,
    });
}
#[test]
fn model_m_card_status_1() {
    round_trip(m::CardStatus {
        card_state: m::CardState::Restricted,
        universal_pin_state: 33,
        gsm_umts_subscription_app_index: -34,
        cdma_subscription_app_index: 35,
        ims_subscription_app_index: -36,
        applications: vec![
            m::AppStatus {
                app_type: 50,
                app_state: -51,
                perso_substate: 52,
                aid_ptr: "field1119\0日本語".to_owned(),
                app_label_ptr: "field1120\0日本語".to_owned(),
                pin1_replaced: true,
                pin1: 56,
                pin2: -57,
            },
            m::AppStatus {
                app_type: -81,
                app_state: 82,
                perso_substate: -83,
                aid_ptr: "field1150\0日本語".to_owned(),
                app_label_ptr: "field1151\0日本語".to_owned(),
                pin1_replaced: false,
                pin1: -87,
                pin2: 88,
            },
        ],
        atr: "field37\0日本語".to_owned(),
        iccid: "field38\0日本語".to_owned(),
        eid: "field39\0日本語".to_owned(),
        slot_map: m::SlotPortMapping {
            physical_slot_id: 77,
            port_id: -78,
        },
        supported_mep_mode: -42,
    });
}
#[test]
fn model_m_card_status_0() {
    round_trip(m::CardStatus {
        card_state: m::CardState::Restricted,
        universal_pin_state: 33,
        gsm_umts_subscription_app_index: -34,
        cdma_subscription_app_index: 35,
        ims_subscription_app_index: -36,
        applications: vec![
            m::AppStatus {
                app_type: 50,
                app_state: -51,
                perso_substate: 52,
                aid_ptr: "field1119\0日本語".to_owned(),
                app_label_ptr: "field1120\0日本語".to_owned(),
                pin1_replaced: true,
                pin1: 56,
                pin2: -57,
            },
            m::AppStatus {
                app_type: -81,
                app_state: 82,
                perso_substate: -83,
                aid_ptr: "field1150\0日本語".to_owned(),
                app_label_ptr: "field1151\0日本語".to_owned(),
                pin1_replaced: false,
                pin1: -87,
                pin2: 88,
            },
        ],
        atr: "field37\0日本語".to_owned(),
        iccid: "field38\0日本語".to_owned(),
        eid: "field39\0日本語".to_owned(),
        slot_map: m::SlotPortMapping {
            physical_slot_id: 77,
            port_id: -78,
        },
        supported_mep_mode: -42,
    });
}
#[test]
fn model_m_sim_port_info_1() {
    round_trip(m::SimPortInfo {
        icc_id: "field31\0日本語".to_owned(),
        logical_slot_id: 33,
        port_active: true,
    });
}
#[test]
fn model_m_sim_port_info_0() {
    round_trip(m::SimPortInfo {
        icc_id: "field31\0日本語".to_owned(),
        logical_slot_id: 33,
        port_active: true,
    });
}
#[test]
fn model_m_sim_slot_status_1() {
    round_trip(m::SimSlotStatus {
        card_state: m::CardState::Restricted,
        atr: "field32\0日本語".to_owned(),
        eid: "field33\0日本語".to_owned(),
        port_info: vec![
            m::SimPortInfo {
                icc_id: "field1054\0日本語".to_owned(),
                logical_slot_id: -86,
                port_active: false,
            },
            m::SimPortInfo {
                icc_id: "field1085\0日本語".to_owned(),
                logical_slot_id: 20,
                port_active: true,
            },
        ],
        supported_mep_mode: -36,
    });
}
#[test]
fn model_m_sim_slot_status_0() {
    round_trip(m::SimSlotStatus {
        card_state: m::CardState::Restricted,
        atr: "field32\0日本語".to_owned(),
        eid: "field33\0日本語".to_owned(),
        port_info: vec![
            m::SimPortInfo {
                icc_id: "field1054\0日本語".to_owned(),
                logical_slot_id: -86,
                port_active: false,
            },
            m::SimPortInfo {
                icc_id: "field1085\0日本語".to_owned(),
                logical_slot_id: 20,
                port_active: true,
            },
        ],
        supported_mep_mode: -36,
    });
}
#[test]
fn model_m_phone_capability_1() {
    round_trip(m::PhoneCapability {
        max_active_data: -32,
        max_active_internet_data: 33,
        is_internet_lingering_supported: true,
        logical_modem_ids: vec![35, -36],
        max_active_voice: -36,
    });
}
#[test]
fn model_m_phone_capability_0() {
    round_trip(m::PhoneCapability {
        max_active_data: -32,
        max_active_internet_data: 33,
        is_internet_lingering_supported: true,
        logical_modem_ids: vec![35, -36],
        max_active_voice: -36,
    });
}
#[test]
fn model_m_gsm_broadcast_sms_config_info_1() {
    round_trip(m::GsmBroadcastSmsConfigInfo {
        from_service_id: -32,
        to_service_id: 33,
        from_code_scheme: -34,
        to_code_scheme: 35,
        selected: true,
    });
}
#[test]
fn model_m_gsm_broadcast_sms_config_info_0() {
    round_trip(m::GsmBroadcastSmsConfigInfo {
        from_service_id: -32,
        to_service_id: 33,
        from_code_scheme: -34,
        to_code_scheme: 35,
        selected: true,
    });
}
#[test]
fn model_m_uus_info_1() {
    round_trip(m::UusInfo {
        uus_type: -32,
        uus_dcs: 33,
        uus_data: "field33\0日本語".to_owned(),
    });
}
#[test]
fn model_m_uus_info_0() {
    round_trip(m::UusInfo {
        uus_type: -32,
        uus_dcs: 33,
        uus_data: "field33\0日本語".to_owned(),
    });
}
#[test]
fn model_m_call_1() {
    round_trip(m::Call {
        state: -32,
        index: 33,
        toa: -34,
        is_mpty: false,
        is_mt: true,
        als: 37,
        is_voice: true,
        is_voice_privacy: false,
        number: "field39\0日本語".to_owned(),
        number_presentation: 41,
        name: "field41\0日本語".to_owned(),
        name_presentation: 43,
        uus_info: vec![
            m::UusInfo {
                uus_type: -73,
                uus_dcs: 74,
                uus_data: "field1335\0日本語".to_owned(),
            },
            m::UusInfo {
                uus_type: 7,
                uus_dcs: -8,
                uus_data: "field1366\0日本語".to_owned(),
            },
        ],
        audio_quality: 45,
        forwarded_number: "field45\0日本語".to_owned(),
    });
}
#[test]
fn model_m_call_0() {
    round_trip(m::Call {
        state: -32,
        index: 33,
        toa: -34,
        is_mpty: false,
        is_mt: true,
        als: 37,
        is_voice: true,
        is_voice_privacy: false,
        number: "field39\0日本語".to_owned(),
        number_presentation: 41,
        name: "field41\0日本語".to_owned(),
        name_presentation: 43,
        uus_info: vec![
            m::UusInfo {
                uus_type: -73,
                uus_dcs: 74,
                uus_data: "field1335\0日本語".to_owned(),
            },
            m::UusInfo {
                uus_type: 7,
                uus_dcs: -8,
                uus_data: "field1366\0日本語".to_owned(),
            },
        ],
        audio_quality: 45,
        forwarded_number: "field45\0日本語".to_owned(),
    });
}
#[test]
fn request_network_get_voice_registration_state() {
    round_trip(Request::Network(n::Request::GetVoiceRegistrationState));
}
#[test]
fn request_network_get_data_registration_state() {
    round_trip(Request::Network(n::Request::GetDataRegistrationState));
}
#[test]
fn request_network_get_operator() {
    round_trip(Request::Network(n::Request::GetOperator));
}
#[test]
fn request_network_get_network_selection_mode() {
    round_trip(Request::Network(n::Request::GetNetworkSelectionMode));
}
#[test]
fn request_network_get_signal_strength() {
    round_trip(Request::Network(n::Request::GetSignalStrength));
}
#[test]
fn request_network_get_cell_info_list() {
    round_trip(Request::Network(n::Request::GetCellInfoList));
}
#[test]
fn request_network_get_ims_registration_state() {
    round_trip(Request::Network(n::Request::GetImsRegistrationState));
}
#[test]
fn request_network_set_indication_filter() {
    round_trip(Request::Network(n::Request::SetIndicationFilter(2)));
}
#[test]
fn request_network_set_signal_strength_reporting_criteria() {
    round_trip(Request::Network(
        n::Request::SetSignalStrengthReportingCriteria(vec![
            n::SignalCriteria {
                access_network: -32,
                measurement: 33,
                enabled: true,
                hysteresis_ms: 35,
                hysteresis_db: -36,
                thresholds: vec![37, -38],
            },
            n::SignalCriteria {
                access_network: 63,
                measurement: -64,
                enabled: false,
                hysteresis_ms: -66,
                hysteresis_db: 67,
                thresholds: vec![-68, 69],
            },
        ]),
    ));
}
#[test]
fn request_network_set_link_capacity_reporting_criteria() {
    round_trip(Request::Network(
        n::Request::SetLinkCapacityReportingCriteria(n::LinkCriteria {
            access_network: -32,
            hysteresis_ms: 33,
            hysteresis_dl_kbps: -34,
            hysteresis_ul_kbps: 35,
            thresholds_dl_kbps: vec![-36, 37],
            thresholds_ul_kbps: vec![37, -38],
        }),
    ));
}
#[test]
fn request_modem_set_radio_power() {
    round_trip(Request::Modem(m::Request::SetRadioPower {
        on: true,
        for_emergency_call: false,
        preferred_for_emergency_call: true,
    }));
}
#[test]
fn request_modem_get_baseband_version() {
    round_trip(Request::Modem(m::Request::GetBasebandVersion));
}
#[test]
fn request_modem_get_imei() {
    round_trip(Request::Modem(m::Request::GetImei));
}
#[test]
fn request_modem_get_device_identity() {
    round_trip(Request::Modem(m::Request::GetDeviceIdentity));
}
#[test]
fn request_modem_get_modem_activity_info() {
    round_trip(Request::Modem(m::Request::GetModemActivityInfo));
}
#[test]
fn request_modem_get_modem_stack_status() {
    round_trip(Request::Modem(m::Request::GetModemStackStatus));
}
#[test]
fn request_modem_enable_modem() {
    round_trip(Request::Modem(m::Request::EnableModem(true)));
}
#[test]
fn request_modem_send_device_state() {
    round_trip(Request::Modem(m::Request::SendDeviceState(
        m::DeviceStateType::ChargingState,
        true,
    )));
}
#[test]
fn request_modem_get_radio_capability() {
    round_trip(Request::Modem(m::Request::GetRadioCapability));
}
#[test]
fn request_modem_get_icc_card_status() {
    round_trip(Request::Modem(m::Request::GetIccCardStatus));
}
#[test]
fn request_modem_get_uicc_applications_enablement() {
    round_trip(Request::Modem(m::Request::GetUiccApplicationsEnablement));
}
#[test]
fn request_modem_are_uicc_applications_enabled() {
    round_trip(Request::Modem(m::Request::AreUiccApplicationsEnabled));
}
#[test]
fn request_modem_enable_uicc_applications() {
    round_trip(Request::Modem(m::Request::EnableUiccApplications(true)));
}
#[test]
fn request_modem_set_carrier_info_for_imsi_encryption() {
    round_trip(Request::Modem(m::Request::SetCarrierInfoForImsiEncryption));
}
#[test]
fn request_modem_get_hal_device_capabilities() {
    round_trip(Request::Modem(m::Request::GetHalDeviceCapabilities));
}
#[test]
fn request_modem_get_sim_slots_status() {
    round_trip(Request::Modem(m::Request::GetSimSlotsStatus));
}
#[test]
fn request_modem_get_phone_capability() {
    round_trip(Request::Modem(m::Request::GetPhoneCapability));
}
#[test]
fn request_modem_get_num_of_live_modems() {
    round_trip(Request::Modem(m::Request::GetNumOfLiveModems));
}
#[test]
fn request_modem_set_num_of_live_modems() {
    round_trip(Request::Modem(m::Request::SetNumOfLiveModems(-2)));
}
#[test]
fn request_modem_set_preferred_data_modem() {
    round_trip(Request::Modem(m::Request::SetPreferredDataModem(-2)));
}
#[test]
fn request_modem_get_current_calls() {
    round_trip(Request::Modem(m::Request::GetCurrentCalls));
}
#[test]
fn request_modem_set_tty_mode() {
    round_trip(Request::Modem(m::Request::SetTtyMode(-2)));
}
#[test]
fn request_modem_set_gsm_broadcast_config() {
    round_trip(Request::Modem(m::Request::SetGsmBroadcastConfig(vec![
        m::GsmBroadcastSmsConfigInfo {
            from_service_id: -32,
            to_service_id: 33,
            from_code_scheme: -34,
            to_code_scheme: 35,
            selected: true,
        },
        m::GsmBroadcastSmsConfigInfo {
            from_service_id: 63,
            to_service_id: -64,
            from_code_scheme: 65,
            to_code_scheme: -66,
            selected: false,
        },
    ])));
}
#[test]
fn request_modem_set_data_profile() {
    round_trip(Request::Modem(m::Request::SetDataProfile {
        profile_count: 42,
    }));
}
#[test]
fn request_modem_set_null_cipher_and_integrity_enabled() {
    round_trip(Request::Modem(
        m::Request::SetNullCipherAndIntegrityEnabled(true),
    ));
}
#[test]
fn request_modem_set_security_algorithms_updated_enabled() {
    round_trip(Request::Modem(
        m::Request::SetSecurityAlgorithmsUpdatedEnabled(true),
    ));
}
#[test]
fn request_modem_set_cellular_identifier_disclosed_enabled() {
    round_trip(Request::Modem(
        m::Request::SetCellularIdentifierDisclosedEnabled(true),
    ));
}
#[test]
fn request_modem_is_satellite_enabled_for_carrier() {
    round_trip(Request::Modem(m::Request::IsSatelliteEnabledForCarrier));
}
#[test]
fn request_modem_get_barring_info() {
    round_trip(Request::Modem(m::Request::GetBarringInfo));
}
#[test]
fn request_modem_get_voice_radio_technology() {
    round_trip(Request::Modem(m::Request::GetVoiceRadioTechnology));
}
#[test]
fn value_network_void() {
    round_trip(Value::Network(n::Value::Void));
}
#[test]
fn value_network_registration() {
    round_trip(Value::Network(n::Value::Registration(n::RegStateResult {
        reg_state: n::RegState::Home,
        rat: n::Rat::Unknown,
        cell_identity: Some(n::CellIdentity::Lte {
            tac: 23,
            ci: 42,
            plmn: "44010".into(),
        }),
        reason_for_denial: 35,
        registered_plmn: "field35\0日本語".to_owned(),
    })));
}
#[test]
fn value_network_operator() {
    round_trip(Value::Network(n::Value::Operator(n::OperatorInfo {
        alpha_long: "field31\0日本語".to_owned(),
        alpha_short: "field32\0日本語".to_owned(),
        numeric: "field33\0日本語".to_owned(),
    })));
}
#[test]
fn value_network_selection() {
    round_trip(Value::Network(n::Value::Selection(
        n::SelectionMode::Manual,
    )));
}
#[test]
fn value_network_signal() {
    round_trip(Value::Network(n::Value::Signal(n::SignalStrength {
        gsm: Some(n::GsmSignal {
            signal_strength: Some(89),
            bit_error_rate: Some(90),
        }),
        lte: Some(n::LteSignal {
            rsrp: Some(23),
            rsrq: Some(-24),
            rssnr: Some(25),
        }),
        wcdma: Some(n::WcdmaSignal {
            signal_strength: Some(54),
            rscp: Some(55),
            ecno: Some(-56),
        }),
    })));
}
#[test]
fn value_network_cells() {
    round_trip(Value::Network(n::Value::Cells(vec![
        n::CellInfo {
            registered: true,
            identity: n::CellIdentity::Lte {
                tac: 23,
                ci: 42,
                plmn: "44010".into(),
            },
            signal: n::SignalStrength {
                gsm: Some(n::GsmSignal {
                    signal_strength: Some(92),
                    bit_error_rate: Some(93),
                }),
                lte: Some(n::LteSignal {
                    rsrp: Some(26),
                    rsrq: Some(-27),
                    rssnr: Some(28),
                }),
                wcdma: Some(n::WcdmaSignal {
                    signal_strength: Some(57),
                    rscp: Some(58),
                    ecno: Some(-59),
                }),
            },
        },
        n::CellInfo {
            registered: false,
            identity: n::CellIdentity::Lte {
                tac: 23,
                ci: 42,
                plmn: "44010".into(),
            },
            signal: n::SignalStrength {
                gsm: Some(n::GsmSignal {
                    signal_strength: Some(7),
                    bit_error_rate: Some(8),
                }),
                lte: Some(n::LteSignal {
                    rsrp: Some(-38),
                    rsrq: Some(39),
                    rssnr: Some(-40),
                }),
                wcdma: Some(n::WcdmaSignal {
                    signal_strength: Some(69),
                    rscp: Some(-70),
                    ecno: Some(71),
                }),
            },
        },
    ])));
}
#[test]
fn value_modem_void() {
    round_trip(Value::Modem(m::Value::Void));
}
#[test]
fn value_modem_baseband() {
    round_trip(Value::Modem(m::Value::Baseband(
        "field1\0日本語".to_owned(),
    )));
}
#[test]
fn value_modem_imei() {
    round_trip(Value::Modem(m::Value::Imei(m::ImeiInfo {
        kind: m::ImeiType::Primary,
        imei: "field32\0日本語".to_owned(),
        svn: "field33\0日本語".to_owned(),
    })));
}
#[test]
fn value_modem_identity() {
    round_trip(Value::Modem(m::Value::Identity(m::DeviceIdentity {
        imei: "field31\0日本語".to_owned(),
        imeisv: "field32\0日本語".to_owned(),
        esn: "field33\0日本語".to_owned(),
        meid: "field34\0日本語".to_owned(),
    })));
}
#[test]
fn value_modem_activity() {
    round_trip(Value::Modem(m::Value::Activity(m::ActivityStatsInfo {
        sleep_mode_time_ms: -32,
        idle_mode_time_ms: 33,
        tech_specific_info: vec![
            m::ActivityStatsTechSpecificInfo {
                rat: -54,
                frequency_range: 55,
                txm_mode_time_ms: [1, -2, 3, -4, 5],
                rx_mode_time_ms: 57,
            },
            m::ActivityStatsTechSpecificInfo {
                rat: 85,
                frequency_range: -86,
                txm_mode_time_ms: [1, -2, 3, -4, 5],
                rx_mode_time_ms: -88,
            },
        ],
    })));
}
#[test]
fn value_modem_capability() {
    round_trip(Value::Modem(m::Value::Capability(m::RadioCapability {
        session: -32,
        phase: 33,
        raf: -34,
        logical_modem_uuid: "field34\0日本語".to_owned(),
        status: -36,
    })));
}
#[test]
fn value_modem_card() {
    round_trip(Value::Modem(m::Value::Card(m::CardStatus {
        card_state: m::CardState::Restricted,
        universal_pin_state: 33,
        gsm_umts_subscription_app_index: -34,
        cdma_subscription_app_index: 35,
        ims_subscription_app_index: -36,
        applications: vec![
            m::AppStatus {
                app_type: 50,
                app_state: -51,
                perso_substate: 52,
                aid_ptr: "field1119\0日本語".to_owned(),
                app_label_ptr: "field1120\0日本語".to_owned(),
                pin1_replaced: true,
                pin1: 56,
                pin2: -57,
            },
            m::AppStatus {
                app_type: -81,
                app_state: 82,
                perso_substate: -83,
                aid_ptr: "field1150\0日本語".to_owned(),
                app_label_ptr: "field1151\0日本語".to_owned(),
                pin1_replaced: false,
                pin1: -87,
                pin2: 88,
            },
        ],
        atr: "field37\0日本語".to_owned(),
        iccid: "field38\0日本語".to_owned(),
        eid: "field39\0日本語".to_owned(),
        slot_map: m::SlotPortMapping {
            physical_slot_id: 77,
            port_id: -78,
        },
        supported_mep_mode: -42,
    })));
}
#[test]
fn value_modem_slots() {
    round_trip(Value::Modem(m::Value::Slots(vec![
        m::SimSlotStatus {
            card_state: m::CardState::Restricted,
            atr: "field32\0日本語".to_owned(),
            eid: "field33\0日本語".to_owned(),
            port_info: vec![
                m::SimPortInfo {
                    icc_id: "field1054\0日本語".to_owned(),
                    logical_slot_id: -86,
                    port_active: false,
                },
                m::SimPortInfo {
                    icc_id: "field1085\0日本語".to_owned(),
                    logical_slot_id: 20,
                    port_active: true,
                },
            ],
            supported_mep_mode: -36,
        },
        m::SimSlotStatus {
            card_state: m::CardState::Error,
            atr: "field63\0日本語".to_owned(),
            eid: "field64\0日本語".to_owned(),
            port_info: vec![
                m::SimPortInfo {
                    icc_id: "field2015\0日本語".to_owned(),
                    logical_slot_id: 77,
                    port_active: true,
                },
                m::SimPortInfo {
                    icc_id: "field2046\0日本語".to_owned(),
                    logical_slot_id: -11,
                    port_active: false,
                },
            ],
            supported_mep_mode: 67,
        },
    ])));
}
#[test]
fn value_modem_phone() {
    round_trip(Value::Modem(m::Value::Phone(m::PhoneCapability {
        max_active_data: -32,
        max_active_internet_data: 33,
        is_internet_lingering_supported: true,
        logical_modem_ids: vec![35, -36],
        max_active_voice: -36,
    })));
}
#[test]
fn value_modem_reduced_feature_set1() {
    round_trip(Value::Modem(m::Value::ReducedFeatureSet1(true)));
}
#[test]
fn value_modem_current_calls() {
    round_trip(Value::Modem(m::Value::CurrentCalls(vec![
        m::Call {
            state: -32,
            index: 33,
            toa: -34,
            is_mpty: false,
            is_mt: true,
            als: 37,
            is_voice: true,
            is_voice_privacy: false,
            number: "field39\0日本語".to_owned(),
            number_presentation: 41,
            name: "field41\0日本語".to_owned(),
            name_presentation: 43,
            uus_info: vec![
                m::UusInfo {
                    uus_type: -73,
                    uus_dcs: 74,
                    uus_data: "field1335\0日本語".to_owned(),
                },
                m::UusInfo {
                    uus_type: 7,
                    uus_dcs: -8,
                    uus_data: "field1366\0日本語".to_owned(),
                },
            ],
            audio_quality: 45,
            forwarded_number: "field45\0日本語".to_owned(),
        },
        m::Call {
            state: 63,
            index: -64,
            toa: 65,
            is_mpty: true,
            is_mt: false,
            als: -68,
            is_voice: false,
            is_voice_privacy: true,
            number: "field70\0日本語".to_owned(),
            number_presentation: -72,
            name: "field72\0日本語".to_owned(),
            name_presentation: -74,
            uus_info: vec![
                m::UusInfo {
                    uus_type: 64,
                    uus_dcs: -65,
                    uus_data: "field2296\0日本語".to_owned(),
                },
                m::UusInfo {
                    uus_type: -95,
                    uus_dcs: 96,
                    uus_data: "field2327\0日本語".to_owned(),
                },
            ],
            audio_quality: -76,
            forwarded_number: "field76\0日本語".to_owned(),
        },
    ])));
}
#[test]
fn value_modem_voice_radio_technology() {
    round_trip(Value::Modem(m::Value::VoiceRadioTechnology(-2)));
}
#[test]
fn indication_0() {
    round_trip(Indication::NetworkStateChanged);
}
#[test]
fn indication_1() {
    round_trip(Indication::CurrentSignalStrength(n::SignalStrength {
        gsm: Some(n::GsmSignal {
            signal_strength: Some(89),
            bit_error_rate: Some(90),
        }),
        lte: Some(n::LteSignal {
            rsrp: Some(23),
            rsrq: Some(-24),
            rssnr: Some(25),
        }),
        wcdma: Some(n::WcdmaSignal {
            signal_strength: Some(54),
            rscp: Some(55),
            ecno: Some(-56),
        }),
    }));
}
#[test]
fn indication_2() {
    round_trip(Indication::RadioStateChanged(s::Radio::Unavailable));
}
#[test]
fn indication_3() {
    round_trip(Indication::RadioStateChanged(s::Radio::Off));
}
#[test]
fn indication_4() {
    round_trip(Indication::RadioStateChanged(s::Radio::On));
}
#[test]
fn indication_5() {
    round_trip(Indication::SimStatusChanged);
}
#[test]
fn indication_6() {
    round_trip(Indication::ModemReset {
        generation: u64::MAX,
    });
}
#[test]
fn cell_identity_0() {
    round_trip(n::CellIdentity::Gsm {
        lac: 1,
        cid: 2,
        plmn: "".into(),
    });
}
#[test]
fn cell_identity_1() {
    round_trip(n::CellIdentity::Wcdma {
        lac: 3,
        cid: 4,
        plmn: "44010".into(),
    });
}
#[test]
fn cell_identity_2() {
    round_trip(n::CellIdentity::Lte {
        tac: 23,
        ci: 42,
        plmn: "44010".into(),
    });
}
#[test]
fn delivery_round_trips() {
    round_trip(Delivery::Response {
        invocation: u64::MAX,
        error: m::RadioError::OemError1,
        value: None,
    });
    round_trip(Delivery::Response {
        invocation: 7,
        error: m::RadioError::None,
        value: Some(Value::Network(n::Value::Void)),
    });
    round_trip(Delivery::Indication(Indication::NetworkStateChanged));
}
