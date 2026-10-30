/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#![forbid(unsafe_code)]

use mindone_radio_core::*;
use mindone_radio_at::engine::Error;
use mindone_radio_state::{model::{Radio, SimCard}, policy::{SIGNAL_STRENGTH, FULL_NETWORK_STATE, Control}};
use mindone_radio_network::types as n;
use mindone_radio_modem::types as m;
use std::collections::BTreeMap;

const SIGNAL: &str = "+ECSQ: 0,99,-400,0,0,-40,-400,4096,80,3";
const REGISTRATION: &str = "+EREG: 0,FFFF,FFFFFFF,0,0,0,0,0";

struct Fake {
    core: Core, now: u64, writes: Vec<(u8, String)>, output: Vec<Output>,
    egreg_fallback: bool, disable_errors: bool,
}

impl Fake {
    fn new() -> Self {
        Self { core: Core::new(0).unwrap(), now: 0, writes: Vec::new(), output: Vec::new(),
            egreg_fallback: false, disable_errors: false }
    }

    fn booted() -> Self {
        let mut fake = Self::new();
        fake.settle();
        assert!(fake.core.is_ready());
        fake.output.clear(); fake.writes.clear();
        fake
    }

    fn collect(&mut self) { self.output.extend(self.core.drain()); }

    fn urc(&mut self, channel: u8, line: &str) {
        self.core.on_readable(channel, format!("\r\n{line}\r\n").as_bytes(), self.now).unwrap();
        self.collect();
    }

    fn submit(&mut self, serial: i32, request: Request) {
        self.core.submit(request, serial, self.now);
        self.collect();
    }

    fn step(&mut self) -> bool {
        self.core.tick(self.now).unwrap();
        let writes = self.core.pending_writes();
        if writes.is_empty() { self.collect(); return false; }
        for (channel, token, bytes) in writes {
            assert!(CHANNELS.contains(&channel));
            let command = String::from_utf8(bytes.clone()).unwrap().trim_end_matches('\r').to_string();
            self.writes.push((channel, command.clone()));
            self.core.on_written(channel, token, bytes.len(), self.now).unwrap();
            let reply = match command.as_str() {
                "AT+ESIMS" => "+ESIMS: 0,0\r\nOK".into(),
                "AT+CPIN?" => "+CME ERROR: 10".into(),
                "AT+EOPS?" => "+EOPS: 0,2,\"000000\"\r\nOK".into(),
                "AT+ECSQ" => format!("{SIGNAL}\r\nOK"),
                "AT+CGMR" => "+CGMR: MOLY.TEST\r\nOK".into(),
                "AT+CGSN" => "490154203237518\r\nOK".into(),
                "AT+EGMR=0,9" => "+EGMR: 78\r\nOK".into(),
                "AT+ESLOTSINFO?" => "+ESLOTSINFO: 1,\"+CME ERROR: 10\",0,1,\"\",\"\",\"\"\r\nOK".into(),
                "AT+ERFTX=11" => "+ERFTX: 11,0,0,0,0\r\nOK".into(),
                "AT+EGREG=3,1" if self.egreg_fallback => "ERROR".into(),
                "AT+EREG=9" | "AT+EGREG=9" if self.disable_errors => "ERROR".into(),
                _ => "OK".into(),
            };
            self.core.on_readable(channel, format!("\r\n{reply}\r\n").as_bytes(), self.now).unwrap();
        }
        self.collect();
        true
    }

    fn settle(&mut self) {
        for _ in 0..256 { if !self.step() { return; } }
        panic!("fake modem did not settle");
    }

    fn responses(&self) -> Vec<(i32, RadioError, Option<Value>)> {
        self.output.iter().filter_map(|output| match output {
            Output::Response { serial, error, value } => Some((*serial, *error, value.clone())), _ => None,
        }).collect()
    }

    fn indications(&self, expected: Indication) -> usize {
        self.output.iter().filter(|output| **output == Output::Indication(expected.clone())).count()
    }
}

fn filter(value: u32) -> Request { Request::Network(n::Request::SetIndicationFilter(value)) }
fn power(on: bool) -> Request {
    Request::Modem(m::Request::SetRadioPower { on, for_emergency_call: false, preferred_for_emergency_call: false })
}

#[test]
fn boot_is_ordered_and_eind_is_not_a_ready_barrier() {
    let mut fake = Fake::new();
    for code in [1, 2, 32] { fake.urc(0, &format!("+EIND: {code}")); }
    assert!(!fake.core.is_ready());
    assert_eq!(fake.core.last_eind().unwrap().code, 32);
    fake.submit(7, Request::Modem(m::Request::GetBasebandVersion));
    assert!(fake.responses().is_empty());
    fake.egreg_fallback = true;
    fake.settle();
    let commands: Vec<_> = fake.writes.iter().map(|(channel, text)| (*channel, text.as_str())).collect();
    assert_eq!(&commands[..12], &[
        (8, "ATE0Q0V1"), (8, "AT+CMEE=1"), (8, "AT+CMER=1,0,0,2,0"),
        (8, "AT+ICCID=1"), (8, "AT+ESIMS=1"), (8, "AT+ESIMS"), (8, "AT+EFUN=0"),
        (1, "AT+EREG=3"), (1, "AT+EGREG=3,1"), (1, "AT+EGREG=3"), (1, "AT+PSBEARER=1"), (1, "AT+ECSQ=1"),
    ]);
    assert_eq!(fake.responses(), [(7, RadioError::None, Some(Value::Modem(m::Value::Baseband("MOLY.TEST".into()))))]);
    assert_eq!(fake.core.model().sim.value.card, SimCard::Absent);
    assert_eq!(fake.indications(Indication::RadioStateChanged(Radio::Off)), 1);
    assert!(fake.core.is_ready());
}

#[test]
fn recurring_registration_has_zero_repeat_indications_or_poll_writes() {
    let mut fake = Fake::booted();
    fake.urc(0, REGISTRATION);
    assert_eq!(fake.indications(Indication::NetworkStateChanged), 1);
    fake.output.clear();
    for index in 0..100 {
        fake.now += 30000 + (index % 11) * 1000;
        fake.urc([0, 1, 3, 6, 8][index as usize % 5], REGISTRATION);
    }
    assert_eq!(fake.indications(Indication::NetworkStateChanged), 0);
    assert!(fake.writes.is_empty());
    assert!(fake.core.pending_writes().is_empty());
    assert_eq!(fake.core.model().cs.observed_ms, fake.now);
}

#[test]
fn filters_send_commands_to_the_right_owners_and_fallback_only_after_error() {
    let mut fake = Fake::booted();
    fake.submit(1, filter(SIGNAL_STRENGTH | FULL_NETWORK_STATE));
    fake.settle();
    assert_eq!(fake.writes, [(1,"AT+ECSQ=1"), (1,"AT+EREG=3"), (1,"AT+EGREG=3"), (3,"AT+ESCREENSTATE=1"), (1,"AT+ECSQ")].map(|(channel,text)| (channel,text.into())));
    assert_eq!(fake.responses().len(), 1);
    assert!(fake.output.iter().any(|output| matches!(output, Output::Indication(Indication::CurrentSignalStrength(_)))));
    fake.writes.clear(); fake.output.clear();
    fake.submit(2, filter(SIGNAL_STRENGTH | FULL_NETWORK_STATE));
    fake.settle();
    assert!(fake.writes.is_empty());
    assert_eq!(fake.responses().len(), 1);
    fake.writes.clear(); fake.output.clear(); fake.disable_errors = true;
    fake.submit(3, filter(0));
    fake.settle();
    assert_eq!(fake.writes, [(1,"AT+ECSQ=0"), (1,"AT+EREG=9"), (1,"AT+EREG=5"), (1,"AT+EGREG=9"), (1,"AT+EGREG=5"), (3,"AT+ESCREENSTATE=0")].map(|(channel,text)| (channel,text.into())));
    assert_eq!(fake.core.policy().applied(Control::Cs), Some(false));
    assert_eq!(fake.responses()[0].1, RadioError::None);
}

#[test]
fn filter_off_keeps_essential_transitions_and_suppresses_cell_and_signal_noise() {
    let mut fake = Fake::booted();
    fake.submit(1, filter(0)); fake.settle(); fake.output.clear();
    fake.urc(0, "+EREG: 1,0001,0001,4096,0,0,0,0");
    assert_eq!(fake.indications(Indication::NetworkStateChanged), 1);
    fake.output.clear();
    fake.urc(0, "+EREG: 1,0001,0002,4096,0,0,0,0");
    fake.urc(0, SIGNAL);
    assert!(fake.output.is_empty());
    fake.urc(0, "+EREG: 3,0001,0002,4096,1,0,0,15");
    assert_eq!(fake.indications(Indication::NetworkStateChanged), 1);
    fake.submit(2, filter(FULL_NETWORK_STATE)); fake.settle(); fake.output.clear();
    fake.urc(0, "+EREG: 3,0001,0003,4096,1,0,0,15");
    assert_eq!(fake.indications(Indication::NetworkStateChanged), 1);
}

#[test]
fn same_serial_across_domains_and_coalesced_reads_get_one_reply_per_invocation() {
    let mut fake = Fake::booted();
    for request in [Request::Modem(m::Request::GetBasebandVersion), Request::Modem(m::Request::GetBasebandVersion),
        Request::Network(n::Request::GetSignalStrength), Request::Network(n::Request::GetSignalStrength),
        Request::Network(n::Request::GetOperator), power(true), Request::Modem(m::Request::GetSimSlotsStatus)] {
        fake.submit(42, request);
    }
    fake.settle();
    assert_eq!(fake.responses().len(), 7);
    assert!(fake.responses().iter().all(|response| response.0 == 42 && response.1 == RadioError::None));
    assert_eq!(fake.writes.iter().filter(|(_, text)| text == "AT+CGMR").count(), 1);
    assert_eq!(fake.writes.iter().filter(|(_, text)| text == "AT+ECSQ").count(), 1);
    assert_eq!(fake.indications(Indication::RadioStateChanged(Radio::On)), 1);
}

#[test]
fn reset_mid_request_fails_every_invocation_once_and_replays_policy_after_boot() {
    let mut fake = Fake::booted();
    fake.submit(1, filter(SIGNAL_STRENGTH)); fake.settle(); fake.output.clear(); fake.writes.clear();
    for request in [Request::Modem(m::Request::GetImei), Request::Network(n::Request::GetOperator), power(true)] { fake.submit(9, request); }
    let stale = fake.core.pending_writes();
    fake.core.on_reset(fake.now).unwrap(); fake.collect();
    assert_eq!(fake.responses().len(), 3);
    assert!(fake.responses().iter().all(|response| response.1 == RadioError::RadioNotAvailable));
    assert_eq!(fake.indications(Indication::ModemReset { generation: 1 }), 1);
    assert_eq!(fake.indications(Indication::RadioStateChanged(Radio::Unavailable)), 1);
    assert_eq!(fake.core.policy().applied(Control::Signal), None);
    fake.core.on_reset(fake.now).unwrap();
    fake.core.on_reconnect(fake.now).unwrap();
    for (channel, token, bytes) in stale { assert_eq!(fake.core.on_written(channel, token, bytes.len(), fake.now), Err(Error::StaleProgress)); }
    fake.submit(10, Request::Modem(m::Request::GetBasebandVersion)); fake.settle();
    assert_eq!(fake.responses().len(), 4);
    assert_eq!(fake.core.model().generation, 1);
    assert_eq!(fake.core.policy().applied(Control::Signal), Some(true));
    assert_eq!(fake.writes[0], (8, "ATE0Q0V1".into()));
    assert!(fake.writes.contains(&(3,"AT+ESCREENSTATE=1".into())));
}

#[test]
fn stalled_command_deadline_resets_instead_of_completing_a_later_request() {
    let mut fake = Fake::booted();
    fake.submit(1, Request::Modem(m::Request::GetImei));
    let (channel, token, bytes) = fake.core.pending_writes().remove(0);
    fake.core.on_written(channel, token, bytes.len(), 0).unwrap();
    assert_eq!(fake.core.next_deadline(), Some(COMMAND_TIMEOUT_MS));
    fake.now = COMMAND_TIMEOUT_MS;
    fake.core.tick(fake.now).unwrap();
    fake.core.on_readable(channel, b"\r\nOK\r\n", fake.now).unwrap(); fake.collect();
    assert_eq!(fake.responses(), [(1, RadioError::RadioNotAvailable, None)]);
    assert!(!fake.core.is_ready());
    assert_eq!(fake.core.next_deadline(), None);
}

#[test]
fn fragmented_urcs_during_numeric_identity_and_partial_writes_are_preserved() {
    let mut fake = Fake::booted();
    fake.submit(7, Request::Modem(m::Request::GetImei));
    let (channel, token, bytes) = fake.core.pending_writes().remove(0);
    fake.core.on_written(channel, token, 2, 0).unwrap();
    assert_eq!(fake.core.pending_writes()[0].2, bytes[2..]);
    fake.core.on_written(channel, token, bytes.len() - 2, 0).unwrap();
    for part in ["\r\n+ERE", "G: 0,FFFF,FFFFFFF,0,0,0,0,0\r\n490154203", "237518\r\n+ESIMS: 0,0\r\nOK\r\n"] {
        fake.core.on_readable(channel, part.as_bytes(), 0).unwrap();
    }
    fake.settle();
    assert_eq!(fake.responses().len(), 1);
    assert_eq!(fake.responses()[0].1, RadioError::None);
    assert_eq!(fake.indications(Indication::NetworkStateChanged), 1);
    assert_eq!(fake.core.model().sim.value.card, SimCard::Absent);
}

#[test]
fn sim_query_updates_model_and_all_channels_deduplicate_sim_urcs() {
    let mut fake = Fake::booted();
    fake.urc(0, "+CPIN: READY"); fake.output.clear();
    fake.submit(1, Request::Modem(m::Request::GetIccCardStatus)); fake.settle();
    assert_eq!(fake.core.model().sim.value.card, SimCard::Absent);
    assert_eq!(fake.indications(Indication::SimStatusChanged), 1);
    for channel in [0, 1, 3, 6, 8] { fake.urc(channel, "+ESIMS: 0,0"); }
    assert_eq!(fake.indications(Indication::SimStatusChanged), 1);
}

fn captured_request(name: &str, argument: &str) -> Request {
    let modem = match name {
        "RADIO_POWER" => return power(argument == "true"),
        "GET_HAL_DEVICE_CAPABILITIES" => m::Request::GetHalDeviceCapabilities,
        "SET_TTY_MODE" => m::Request::SetTtyMode(0), "GET_SIM_STATUS" => m::Request::GetIccCardStatus,
        "GET_SLOT_STATUS" => m::Request::GetSimSlotsStatus, "BASEBAND_VERSION" => m::Request::GetBasebandVersion,
        "DEVICE_IMEI" => m::Request::GetImei, "GET_RADIO_CAPABILITY" => m::Request::GetRadioCapability,
        "GET_UICC_APPLICATIONS_ENABLEMENT" => m::Request::GetUiccApplicationsEnablement,
        "SET_NULL_CIPHER_AND_INTEGRITY_ENABLED" => m::Request::SetNullCipherAndIntegrityEnabled(false),
        "SET_CELLULAR_IDENTIFIER_DISCLOSED_ENABLED" => m::Request::SetCellularIdentifierDisclosedEnabled(false),
        "SET_SECURITY_ALGORITHMS_UPDATED_ENABLED" => m::Request::SetSecurityAlgorithmsUpdatedEnabled(false),
        "SEND_DEVICE_STATE" => m::Request::SendDeviceState(match &argument[..1] {
            "0" => m::DeviceStateType::PowerSaveMode, "1" => m::DeviceStateType::ChargingState,
            "2" => m::DeviceStateType::LowDataExpected, _ => panic!(),
        }, argument.ends_with("true")),
        "GET_MODEM_STATUS" => m::Request::GetModemStackStatus, "GET_PHONE_CAPABILITY" => m::Request::GetPhoneCapability,
        "SET_PREFERRED_DATA_MODEM" => m::Request::SetPreferredDataModem(0),
        "GSM_SET_BROADCAST_CONFIG" => m::Request::SetGsmBroadcastConfig(Vec::new()),
        "GET_CURRENT_CALLS" => m::Request::GetCurrentCalls, "VOICE_RADIO_TECH" => m::Request::GetVoiceRadioTechnology,
        "GET_BARRING_INFO" => m::Request::GetBarringInfo, "IS_SATELLITE_ENABLED_FOR_CARRIER" => m::Request::IsSatelliteEnabledForCarrier,
        "SET_DATA_PROFILE" => m::Request::SetDataProfile { profile_count: 0 }, "GET_ACTIVITY_INFO" => m::Request::GetModemActivityInfo,
        _ => return Request::Network(match name {
            "SIGNAL_STRENGTH" => n::Request::GetSignalStrength,
            "OPERATOR" => n::Request::GetOperator, "QUERY_NETWORK_SELECTION_MODE" => n::Request::GetNetworkSelectionMode,
            "SET_UNSOLICITED_RESPONSE_FILTER" => n::Request::SetIndicationFilter(3),
            "GET_CELL_INFO_LIST" => n::Request::GetCellInfoList,
            "DATA_REGISTRATION_STATE" => n::Request::GetDataRegistrationState,
            "VOICE_REGISTRATION_STATE" => n::Request::GetVoiceRegistrationState,
            "SET_SIGNAL_STRENGTH_REPORTING_CRITERIA" => n::Request::SetSignalStrengthReportingCriteria(vec![n::SignalCriteria {
                access_network: 3, measurement: 3, enabled: true, hysteresis_ms: 0, hysteresis_db: 2, thresholds: vec![-120,-100,-80],
            }]),
            "SET_LINK_CAPACITY_REPORTING_CRITERIA" => n::Request::SetLinkCapacityReportingCriteria(n::LinkCriteria {
                hysteresis_ms: 0, hysteresis_dl_kbps: 0, hysteresis_ul_kbps: 0, thresholds_dl_kbps: Vec::new(),
                thresholds_ul_kbps: Vec::new(), access_network: 3,
            }),
            _ => panic!("unhandled capture request {name}"),
        }),
    };
    Request::Modem(modem)
}

#[test]
fn c53_no_sim_all_117_requests_have_exactly_one_matching_reply() {
    let mut fake = Fake::booted();
    fake.urc(0, REGISTRATION); fake.output.clear();
    let mut expected = BTreeMap::new();
    for (index, row) in include_str!("../../modem/tests/c53-boot.tsv").lines().enumerate() {
        let fields: Vec<_> = row.split('\t').collect();
        let serial = fields[1].parse::<i32>().unwrap();
        fake.now = index as u64 * 10;
        fake.submit(serial, captured_request(fields[2], fields[3])); fake.settle();
        expected.insert(serial, 1usize);
    }
    let mut actual = BTreeMap::new();
    for (serial, error, _) in fake.responses() {
        assert_ne!(error, RadioError::RadioNotAvailable);
        *actual.entry(serial).or_insert(0usize) += 1;
    }
    assert_eq!(actual, expected);
    assert_eq!(fake.responses().len(), 117);
    assert!(fake.writes.iter().any(|entry| entry.0 == 1));
    assert!(fake.writes.iter().any(|entry| entry.0 == 3));
    assert!(fake.writes.iter().any(|entry| entry.0 == 6));
    assert!(fake.writes.iter().any(|entry| entry.0 == 8));
}

#[test]
fn required_boot_failure_fails_waiting_requests_once() {
    let mut fake = Fake::new();
    fake.submit(1, Request::Modem(m::Request::GetImei));
    let (channel, token, bytes) = fake.core.pending_writes().remove(0);
    fake.core.on_written(channel, token, bytes.len(), 0).unwrap();
    fake.core.on_readable(channel, b"\r\nERROR\r\n", 0).unwrap(); fake.collect();
    assert_eq!(fake.responses(), [(1, RadioError::RadioNotAvailable, None)]);
    assert_eq!(fake.indications(Indication::ModemReset { generation: 1 }), 1);
    assert!(fake.core.pending_writes().is_empty());
    fake.submit(2, Request::Modem(m::Request::GetCurrentCalls));
    assert_eq!(fake.responses()[1], (2, RadioError::RadioNotAvailable, None));
}

#[test]
fn failed_filter_command_does_not_loop_or_claim_applied() {
    let mut fake = Fake::booted();
    fake.submit(1, filter(0));
    let (channel, token, bytes) = fake.core.pending_writes().remove(0);
    assert_eq!(bytes, b"AT+ECSQ=0\r");
    fake.core.on_written(channel, token, bytes.len(), 0).unwrap();
    fake.core.on_readable(channel, b"\r\nERROR\r\n", 0).unwrap();
    fake.settle();
    assert_eq!(fake.responses(), [(1, RadioError::ModemErr, None)]);
    assert_eq!(fake.core.policy().applied(Control::Signal), None);
    assert_eq!(fake.core.policy().failed.len(), 1);
    fake.submit(2, filter(0)); fake.settle();
    assert_eq!(fake.core.policy().applied(Control::Signal), Some(false));
    assert_eq!(fake.responses()[1].1, RadioError::None);
}

#[test]
fn queued_filter_changes_complete_in_order_with_signal_only_screen_hint() {
    let mut fake = Fake::booted();
    fake.submit(1, filter(3)); fake.submit(1, filter(0)); fake.submit(1, filter(SIGNAL_STRENGTH));
    fake.settle();
    assert_eq!(fake.responses().len(), 3);
    assert!(fake.responses().iter().all(|response| response.1 == RadioError::None));
    assert_eq!(fake.core.policy().applied(Control::Screen), Some(true));
    assert_eq!(fake.core.policy().applied(Control::Cs), Some(false));
    let hints: Vec<_> = fake.writes.iter().filter(|entry| entry.0 == 3).map(|entry| entry.1.as_str()).collect();
    assert_eq!(hints, ["AT+ESCREENSTATE=1", "AT+ESCREENSTATE=0", "AT+ESCREENSTATE=1"]);
}

#[test]
fn sim_errors_preserve_distinct_card_semantics() {
    for (line, state, error) in [("+CME ERROR: 13", SimCard::Error, RadioError::SimErr),
        ("+CME ERROR: 14", SimCard::Present, RadioError::SimBusy),
        ("+CME ERROR: 15", SimCard::Error, RadioError::SimErr),
        ("ERROR", SimCard::Present, RadioError::GenericFailure)] {
        let mut fake = Fake::booted();
        fake.urc(0, "+CPIN: READY"); fake.output.clear();
        fake.submit(1, Request::Modem(m::Request::GetIccCardStatus));
        let (channel, token, bytes) = fake.core.pending_writes().remove(0);
        fake.core.on_written(channel, token, bytes.len(), 0).unwrap();
        fake.core.on_readable(channel, format!("\r\n{line}\r\n").as_bytes(), 0).unwrap(); fake.collect();
        assert_eq!(fake.responses()[0].1, error);
        assert_eq!(fake.core.model().sim.value.card, state);
    }
}

#[test]
fn full_filter_deduplicates_signal_and_signal_changes_on_every_channel() {
    let mut fake = Fake::booted();
    fake.submit(1, filter(3)); fake.settle(); fake.output.clear();
    for channel in [0, 1, 3, 6, 8] { fake.urc(channel, SIGNAL); }
    assert!(fake.output.is_empty());
    fake.urc(8, "+ECSQ: 0,99,-400,0,0,-40,-404,4096,80,3");
    assert_eq!(fake.output.iter().filter(|output| matches!(output, Output::Indication(Indication::CurrentSignalStrength(_)))).count(), 1);
}

#[test]
fn read_only_notification_channel_and_clock_validation_do_not_corrupt_active_write() {
    let mut fake = Fake::booted();
    fake.now = 10;
    fake.submit(1, Request::Modem(m::Request::GetBasebandVersion));
    let (channel, token, bytes) = fake.core.pending_writes().remove(0);
    assert_eq!(fake.core.on_written(0, token, 1, 10), Err(Error::Configuration));
    assert_eq!(fake.core.on_readable(channel, b"\r\nOK\r\n", 9), Err(Error::ClockRegression));
    assert_eq!(fake.core.pending_writes()[0].2, bytes);
    fake.settle();
    assert_eq!(fake.responses().len(), 1);
    assert_eq!(fake.responses()[0].1, RadioError::None);
}

#[test]
fn trailing_bytes_from_failed_boot_do_not_enter_the_next_generation() {
    let mut fake = Fake::new();
    let (channel, token, bytes) = fake.core.pending_writes().remove(0);
    fake.core.on_written(channel, token, bytes.len(), 0).unwrap();
    fake.core.on_readable(channel, b"\r\nERROR\r\n+CPIN: READY\r\n", 0).unwrap();
    assert_eq!(fake.core.model().generation, 1);
    assert_eq!(fake.core.model().sim.value.card, SimCard::Unknown);
    assert_eq!(fake.core.model().radio.value, Radio::Unavailable);
}

#[test]
fn radio_power_completion_has_one_observation_path() {
    let mut fake = Fake::booted();
    fake.submit(1, power(true));
    let (channel, token, bytes) = fake.core.pending_writes().remove(0);
    assert_eq!((channel, bytes.as_slice()), (8, b"AT+EFUN=1\r".as_slice()));
    fake.core.on_written(channel, token, bytes.len(), fake.now).unwrap();
    fake.core.on_readable(channel, b"OK\r\n", fake.now).unwrap();
    fake.collect();
    assert_eq!(fake.responses(), [(1, RadioError::None, Some(Value::Modem(m::Value::Void)))]);
    assert_eq!(fake.indications(Indication::RadioStateChanged(Radio::On)), 1);
    assert_eq!(fake.core.model().radio.source, mindone_radio_state::model::Source::PowerResponse);
    fake.core.tick(fake.now).unwrap();
    fake.collect();
    assert_eq!(fake.indications(Indication::RadioStateChanged(Radio::On)), 1);
    assert!(fake.core.pending_writes().is_empty());
}

#[test]
fn removal_urc_answers_card_status_from_model_without_cpin_query() {
    let mut fake = Fake::booted();
    fake.urc(0, "+CPIN: READY");
    assert_eq!(fake.core.model().sim.value.card, SimCard::Present);
    fake.output.clear();
    fake.urc(6, "+ESIMS: 0,0");
    assert_eq!(fake.indications(Indication::SimStatusChanged), 1);
    fake.submit(1, Request::Modem(m::Request::GetIccCardStatus));
    assert_eq!(fake.responses(), [(1, RadioError::None, Some(Value::Modem(m::Value::Card(m::CardStatus::absent()))))]);
    assert!(fake.core.pending_writes().is_empty());
    fake.settle();
    assert!(fake.writes.is_empty());
}

#[test]
fn reconnect_reapplies_filter_then_refreshes_signal_once_without_internal_response() {
    let mut fake = Fake::booted();
    fake.submit(1, filter(SIGNAL_STRENGTH));
    fake.settle();
    fake.core.on_reset(fake.now).unwrap();
    fake.core.on_reconnect(fake.now).unwrap();
    fake.output.clear();
    fake.writes.clear();
    fake.settle();
    let expected: Vec<_> = [(1, "AT+ECSQ=1"), (1, "AT+EREG=9"), (1, "AT+EGREG=9"),
        (3, "AT+ESCREENSTATE=1"), (1, "AT+ECSQ")].into_iter().map(|(channel, text)| (channel, text.into())).collect();
    assert_eq!(&fake.writes[fake.writes.len() - expected.len()..], expected);
    assert_eq!(fake.writes.iter().filter(|(_, text)| text == "AT+ECSQ").count(), 1);
    assert_eq!(fake.core.policy().applied(Control::Signal), Some(true));
    assert!(fake.responses().is_empty());
    assert_eq!(fake.output.iter().filter(|output| matches!(output, Output::Indication(Indication::CurrentSignalStrength(_)))).count(), 1);
}

#[test]
fn urc_changes_are_counted_once_in_a_stable_generation() {
    use mindone_radio_state::model::{Knowledge, Source};
    let mut fake = Fake::booted();
    let generation = fake.core.model().generation;
    for state in [0, 1, 2] {
        fake.output.clear();
        for channel in [0, 1, 3, 6, 8] {
            fake.now += 1;
            fake.urc(channel, &format!("+EREG: {state}"));
            assert_eq!(fake.core.model().generation, generation);
            assert_eq!(fake.core.model().cs.source, Source::Urc);
            assert_eq!(fake.core.model().cs.observed_ms, fake.now);
            assert!(matches!(&fake.core.model().cs.value, Knowledge::Known(registration) if registration.state == state));
            assert_eq!(fake.indications(Indication::NetworkStateChanged), 1);
        }
    }
    assert!(fake.writes.is_empty());
}

#[test]
fn internal_restore_and_refresh_deadlines_are_visible_without_invocations() {
    let mut fake = Fake::booted();
    fake.submit(1, filter(SIGNAL_STRENGTH));
    fake.settle();
    assert_eq!(fake.core.next_deadline(), None);
    fake.core.on_reset(fake.now).unwrap();
    fake.core.on_reconnect(fake.now).unwrap();
    while !fake.core.is_ready() { assert!(fake.step()); }
    assert!(fake.responses().iter().all(|response| response.1 == RadioError::None));
    let deadline = fake.core.next_deadline().unwrap();
    assert!(deadline > fake.now);
    let writes = fake.core.pending_writes();
    assert!(writes.iter().any(|(_, _, bytes)| bytes == b"AT+ECSQ=1\r"));
    while !fake.core.pending_writes().iter().any(|(_, _, bytes)| bytes == b"AT+ECSQ\r") {
        assert!(fake.step());
    }
    assert_eq!(fake.core.next_deadline(), Some(deadline));
    fake.output.clear();
    fake.now = deadline;
    fake.core.tick(fake.now).unwrap();
    fake.collect();
    assert_eq!(fake.indications(Indication::ModemReset { generation: 2 }), 1);
    assert_eq!(fake.core.next_deadline(), None);
    assert!(!fake.core.is_ready());
    assert!(fake.responses().is_empty());
}
