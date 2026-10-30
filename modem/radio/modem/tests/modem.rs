/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#![forbid(unsafe_code)]

use mindone_radio_at::Channels;
use mindone_radio_at::engine::{Event};
use mindone_radio_modem::{service::{activity, modem_error, slots, Modem}, types::*};
use mindone_radio_state::model::{Event as StateEvent, Model, Radio, Source, Update};

struct Harness { modem: Modem, model: Model, now: u64, channels: Channels, updates: Vec<(Source, Update)> }

impl Harness {
    fn new() -> Self {
        let mut model = Model::new(1);
        model.apply(StateEvent { generation: 1, observed_ms: 0, source: Source::Transport, update: Update::Radio(Radio::On) });
        Self { modem: Modem::new(), model, now: 0, channels: Channels::new(0, &[1, 6, 8], &["+ESIMS:", "+EUSIM:", "+CPIN:", "+ECPIN:", "+ESLOTSINFO:"]).unwrap(), updates: Vec::new() }
    }
    fn request(&mut self, serial: i32, request: Request) { self.modem.submit(serial, request, 1000, &mut self.channels, &self.model); }
    fn events(&mut self, channel: u8, events: Vec<Event>) {
        for event in events {
            if let Event::Urc { line, .. } = &event {
                if let Some(update) = mindone_radio_state::observe::line(line) {
                    self.model.apply(StateEvent { generation: 1, observed_ms: self.now, source: Source::Urc, update });
                }
            }
            self.modem.on_event(channel, &event, self.now, &mut self.channels, &self.model);
            while let Some((source, update)) = self.modem.pop_update() {
                self.updates.push((source, update.clone()));
                self.model.apply(StateEvent { generation: 1, observed_ms: self.now, source, update });
            }
        }
    }
    fn write(&mut self, channel: u8) -> String {
        let mut events = Vec::new();
        let engine = self.channels.get_mut(channel).unwrap();
        engine.tick(self.now, |event| events.push(event)).unwrap();
        let write = engine.pending_write().unwrap();
        let text = String::from_utf8(write.bytes.to_vec()).unwrap();
        let (token, len) = (write.token, write.bytes.len());
        engine.accept_written(self.now, token, len, |event| events.push(event)).unwrap();
        self.events(channel, events);
        text
    }
    fn feed(&mut self, channel: u8, bytes: &str) -> Vec<Event> {
        let mut events = Vec::new();
        self.channels.get_mut(channel).unwrap().feed(self.now, bytes.as_bytes(), |event| events.push(event)).unwrap();
        events
    }
    fn receive(&mut self, channel: u8, bytes: &str) {
        let events = self.feed(channel, bytes);
        self.events(channel, events);
    }
    fn response(&mut self, serial: i32) -> Response {
        let result = self.modem.pop_response().unwrap();
        assert_eq!(result.serial, serial);
        result
    }
    fn value(&mut self, serial: i32) -> Value {
        let response = self.response(serial);
        assert_eq!(response.error, RadioError::None);
        response.value.unwrap()
    }
    fn empty(&mut self) { assert_eq!(self.modem.pop_response(), None); }
}

fn power(on: bool) -> Request {
    Request::SetRadioPower { on, for_emergency_call: false, preferred_for_emergency_call: false }
}

fn configured(h: &mut Harness) {
    h.modem.imei_type = ImeiType::Primary;
    h.modem.radio_capability = Some(RadioCapability { session: -1, phase: 3, raf: 0,
        logical_modem_uuid: "modem_sys1_ps0".into(), status: 1 });
    h.modem.phone_capability = Some(PhoneCapability { max_active_data: 1, max_active_internet_data: 1,
        is_internet_lingering_supported: false, logical_modem_ids: vec![0], max_active_voice: -1 });
}

#[test]
fn radio_power_off_on_transcript() {
    let mut h = Harness::new();
    for (serial, on, wire, state) in [(4, false, "AT+EFUN=0\r", Radio::Off), (27, true, "AT+EFUN=1\r", Radio::On)] {
        h.request(serial, power(on));
        assert_eq!(h.write(8), wire);
        h.receive(8, "\r\nOK\r\n");
        assert_eq!(h.value(serial), Value::Void);
        assert_eq!(h.model.radio.value, state);
    }
    h.empty();
}

#[test]
fn failed_power_does_not_change_state() {
    let mut h = Harness::new();
    for (serial, line, error) in [(1, "+CME ERROR: 54", RadioError::OperationNotAllowed),
        (2, "ERROR", RadioError::OemError1), (3, "+CME ERROR: 42", RadioError::OemError1)] {
        h.request(serial, power(false));
        h.write(8);
        h.receive(8, &format!("\r\n{line}\r\n"));
        assert_eq!(h.response(serial).error, error);
        assert_eq!(h.model.radio.value, Radio::On);
    }
}

#[test]
fn emergency_power_requires_unimplemented_ccci_coordination() {
    let mut h = Harness::new();
    h.request(1, Request::SetRadioPower { on: true, for_emergency_call: true, preferred_for_emergency_call: false });
    assert_eq!(h.response(1).error, RadioError::RequestNotSupported);
    assert_eq!(h.channels.get_mut(8).unwrap().queued(), 0);
}

#[test]
fn baseband_prefixed_and_coalesced() {
    let mut h = Harness::new();
    h.request(18, Request::GetBasebandVersion);
    h.request(19, Request::GetBasebandVersion);
    assert_eq!(h.write(1), "AT+CGMR\r");
    h.receive(1, "\r\n+CGMR: \"MOLY.TEST\"\r\nOK\r\n");
    for serial in [18, 19] { assert_eq!(h.value(serial), Value::Baseband("MOLY.TEST".into())); }
    h.empty();
    assert_eq!(h.channels.get_mut(1).unwrap().queued(), 0);
}

#[test]
fn baseband_raw_fallback_preserves_sim_urc() {
    let mut h = Harness::new();
    h.request(1, Request::GetBasebandVersion);
    h.write(1);
    h.receive(1, "\r\nOK\r\n");
    h.empty();
    assert_eq!(h.write(1), "AT+CGMR\r");
    h.receive(1, "\r\n+ESIMS: 0,0\r\n  MOLY.RAW  \r\nOK\r\n");
    assert_eq!(h.value(1), Value::Baseband("MOLY.RAW".into()));
    h.request(2, Request::GetIccCardStatus);
    assert_eq!(h.value(2), Value::Card(CardStatus::absent()));
}

#[test]
fn imei_and_svn_are_read_from_modem() {
    let mut h = Harness::new();
    configured(&mut h);
    h.request(19, Request::GetImei);
    h.request(20, Request::GetImei);
    assert_eq!(h.write(1), "AT+CGSN\r");
    h.receive(1, "\r\n490154203237518\r\nOK\r\n");
    h.empty();
    assert_eq!(h.write(1), "AT+EGMR=0,9\r");
    h.receive(1, "\r\n+EGMR: \"78\"\r\nOK\r\n");
    for serial in [19, 20] {
        assert_eq!(h.value(serial), Value::Imei(ImeiInfo { kind: ImeiType::Primary,
            imei: "490154203237518".into(), svn: "78".into() }));
    }
    h.empty();
}

#[test]
fn device_identity_non_c2k_transcript() {
    let mut h = Harness::new();
    h.request(1, Request::GetDeviceIdentity);
    h.write(1);
    h.receive(1, "\r\n490154203237518\r\nOK\r\n");
    h.write(1);
    h.receive(1, "\r\n+EGMR: 78\r\nOK\r\n");
    assert_eq!(h.value(1), Value::Identity(DeviceIdentity { imei: "490154203237518".into(),
        imeisv: "78".into(), esn: String::new(), meid: String::new() }));
}

#[test]
fn malformed_identity_is_never_invented() {
    for input in ["", "123", "49015420323751X", "490154203237518\r\n123"] {
        let mut h = Harness::new();
        h.request(1, Request::GetImei);
        h.write(1);
        h.receive(1, &format!("\r\n{input}\r\nOK\r\n"));
        assert_eq!(h.response(1).error, RadioError::InvalidResponse);
        h.empty();
    }
}

#[test]
fn identity_error_uses_explicit_legacy_empty_record() {
    let mut h = Harness::new();
    h.request(1, Request::GetDeviceIdentity);
    h.write(1);
    h.receive(1, "\r\nERROR\r\n");
    assert_eq!(h.response(1).error, RadioError::EmptyRecord);
}

#[test]
fn activity_transcript_zero_fills_missing_tx_buckets() {
    let mut h = Harness::new();
    h.request(1, Request::GetModemActivityInfo);
    h.request(2, Request::GetModemActivityInfo);
    assert_eq!(h.write(1), "AT+ERFTX=11\r");
    h.receive(1, "\r\n+ERFTX: 11,2,10,20,30,40,50\r\nOK\r\n");
    let expected = Value::Activity(ActivityStatsInfo { sleep_mode_time_ms: 40, idle_mode_time_ms: 50,
        tech_specific_info: vec![ActivityStatsTechSpecificInfo { rat: 0, frequency_range: 0,
            txm_mode_time_ms: [10,20,0,0,0], rx_mode_time_ms: 30 }] });
    assert_eq!(h.value(1), expected);
    assert_eq!(h.value(2), expected);
}

#[test]
fn activity_rejects_bad_tag_count_negative_and_overflow() {
    for line in ["+ERFTX: 12,0,1,2,3", "+ERFTX: 11,6,1,2,3,4,5,6,7,8,9",
        "+ERFTX: 11,-1,1,2,3", "+ERFTX: 11,1,-1,2,3,4", "+ERFTX: 11,0,2147483648,2,3",
        "+ERFTX: 11,2,1,2,3", "+ERFTX: 11,0,1,2,3,4"] {
        assert_eq!(activity(line), Err(RadioError::InvalidResponse));
    }
    assert!(activity("+ERFTX: 11,0,1,2,3").is_ok());
}

#[test]
fn unavailable_modem_control_is_not_faked() {
    let mut h = Harness::new();
    for (serial, request) in [Request::GetModemStackStatus, Request::EnableModem(true), Request::EnableModem(false)].into_iter().enumerate() {
        h.request(serial as i32, request);
        assert_eq!(h.response(serial as i32).error, RadioError::RequestNotSupported);
    }
}

#[test]
fn device_state_matches_donor_for_both_boolean_values() {
    let mut h = Harness::new();
    for (kind, error) in [(DeviceStateType::LowDataExpected, RadioError::None),
        (DeviceStateType::PowerSaveMode, RadioError::RequestNotSupported),
        (DeviceStateType::ChargingState, RadioError::RequestNotSupported)] {
        for value in [true, false] {
            h.request(1, Request::SendDeviceState(kind, value));
            assert_eq!(h.response(1).error, error);
        }
    }
}

#[test]
fn capability_requires_explicit_configuration() {
    let mut h = Harness::new();
    h.request(1, Request::GetRadioCapability);
    assert_eq!(h.response(1).error, RadioError::RequestNotSupported);
    configured(&mut h);
    h.request(2, Request::GetRadioCapability);
    assert_eq!(h.value(2), Value::Capability(h.modem.radio_capability.clone().unwrap()));
}

#[test]
fn sim_absent_cme10_and_identical_status_reads_coalesce() {
    let mut h = Harness::new();
    for serial in [13, 14, 15, 17] { h.request(serial, Request::GetIccCardStatus); }
    assert_eq!(h.write(6), "AT+CPIN?\r");
    h.receive(6, "\r\n+CME ERROR:10\r\n");
    for serial in [13, 14, 15, 17] { assert_eq!(h.value(serial), Value::Card(CardStatus::absent())); }
    h.request(18, Request::GetIccCardStatus);
    assert_eq!(h.value(18), Value::Card(CardStatus::absent()));
    assert_eq!(h.channels.get_mut(6).unwrap().queued(), 0);
    h.empty();
}

#[test]
fn sim_status_is_driven_by_model() {
    let mut h = Harness::new();
    h.receive(6, "\r\n+ESIMS: 0,0\r\n");
    h.request(1, Request::GetIccCardStatus);
    assert_eq!(h.value(1), Value::Card(CardStatus::absent()));
    h.receive(6, "\r\n+EUSIM: 1\r\n");
    h.model.sim.value.card = mindone_radio_state::model::SimCard::Unknown;
    h.request(2, Request::GetIccCardStatus);
    assert_eq!(h.write(6), "AT+CPIN?\r");
    h.receive(6, "\r\n+CPIN: READY\r\nOK\r\n");
    assert_eq!(h.response(2).error, RadioError::RequestNotSupported);
}

#[test]
fn sim_errors_busy_and_ready_never_become_absent() {
    for (line, error) in [("+CME ERROR: 13", RadioError::SimErr), ("+CME ERROR: 14", RadioError::SimBusy),
        ("+CME ERROR: 15", RadioError::SimErr), ("ERROR", RadioError::GenericFailure),
        ("+CPIN: READY\r\nOK", RadioError::RequestNotSupported),
        ("+CPIN: SIM PIN\r\nOK", RadioError::RequestNotSupported)] {
        let mut h = Harness::new();
        h.request(1, Request::GetIccCardStatus);
        h.write(6);
        h.receive(6, &format!("\r\n{line}\r\n"));
        assert_eq!(h.response(1).error, error);
        h.request(2, Request::GetIccCardStatus);
        assert_eq!(h.write(6), "AT+CPIN?\r");
    }
}

#[test]
fn sim_unsupported_requests_are_explicit() {
    let mut h = Harness::new();
    for request in [Request::GetUiccApplicationsEnablement, Request::AreUiccApplicationsEnabled,
        Request::EnableUiccApplications(true), Request::SetCarrierInfoForImsiEncryption] {
        h.request(1, request);
        assert_eq!(h.response(1).error, RadioError::RequestNotSupported);
    }
}

#[test]
fn slot_status_uses_six_string_fields_and_iccid_evidence() {
    let mut h = Harness::new();
    h.request(16, Request::GetSimSlotsStatus);
    h.request(17, Request::GetSimSlotsStatus);
    assert_eq!(h.write(6), "AT+ESLOTSINFO?\r");
    h.receive(6, "\r\n+ESLOTSINFO: 2,\"+CME ERROR: 10\",0,1,\"\",\"\",\"\",\"+CPIN: READY\",1,0,\"3B00\",\"8900\",\"ab12\"\r\nOK\r\n");
    for serial in [16, 17] {
        let Value::Slots(slots) = h.value(serial) else { panic!() };
        assert_eq!(slots[0].card_state, CardState::Absent);
        assert_eq!(slots[1].card_state, CardState::Present);
        assert_eq!(slots[1].eid, "AB12");
        assert_eq!(slots[0].port_info[0].logical_slot_id, 0);
        assert!(!slots[1].port_info[0].port_active);
    }
}

#[test]
fn malformed_slots_and_restricted_card() {
    for line in ["+ESLOTSINFO: 0", "+ESLOTSINFO: 9", "+ESLOTSINFO: 1,0,0,1,\"\",\"\"",
        "+ESLOTSINFO: 1,0,0,2,\"\",\"\",\"\"", "+ESLOTSINFO: 1,\"unterminated"] {
        assert_eq!(slots(line), Err(RadioError::InvalidResponse));
    }
    assert_eq!(slots("+ESLOTSINFO: 1,\"+CPIN: CARD_RESTRICTED\",0,1,\"\",\"\",\"\"").unwrap()[0].card_state, CardState::Restricted);
}

#[test]
fn slot_busy_retries_thirty_times_then_sim_error() {
    let mut h = Harness::new();
    h.request(1, Request::GetSimSlotsStatus);
    for attempt in 0..30 {
        assert_eq!(h.write(6), "AT+ESLOTSINFO?\r");
        h.receive(6, "\r\n+ESLOTSINFO: 1,\"+CME ERROR:14\",0,1,\"\",\"\",\"\"\r\nOK\r\n");
        if attempt != 29 { h.empty(); }
    }
    assert_eq!(h.response(1).error, RadioError::SimErr);
    h.empty();
}

#[test]
fn radio_config_local_queries_and_preferred_data_modem() {
    let mut h = Harness::new();
    h.request(10, Request::GetHalDeviceCapabilities);
    assert_eq!(h.value(10), Value::ReducedFeatureSet1(true));
    h.request(54, Request::GetPhoneCapability);
    assert_eq!(h.response(54).error, RadioError::RequestNotSupported);
    configured(&mut h);
    h.request(54, Request::GetPhoneCapability);
    assert_eq!(h.value(54), Value::Phone(h.modem.phone_capability.clone().unwrap()));
    h.request(55, Request::SetPreferredDataModem(0));
    assert_eq!(h.value(55), Value::Void);
    h.request(63, Request::SetPreferredDataModem(1));
    assert_eq!(h.response(63).error, RadioError::InvalidArguments);
    for request in [Request::GetNumOfLiveModems, Request::SetNumOfLiveModems(1)] {
        h.request(1, request);
        assert_eq!(h.response(1).error, RadioError::RequestNotSupported);
    }
}

#[test]
fn current_calls_empty_and_tty_stored() {
    let mut h = Harness::new();
    h.request(59, Request::GetCurrentCalls);
    assert_eq!(h.value(59), Value::CurrentCalls(Vec::new()));
    for mode in 0..=3 {
        h.request(12, Request::SetTtyMode(mode));
        assert_eq!(h.value(12), Value::Void);
        assert_eq!(h.modem.tty_mode, mode);
    }
    h.request(12, Request::SetTtyMode(4));
    assert_eq!(h.response(12).error, RadioError::InvalidArguments);
    assert_eq!(h.modem.tty_mode, 3);
}

#[test]
fn broadcast_config_stored_with_captured_generic_failure() {
    let mut h = Harness::new();
    let config = vec![GsmBroadcastSmsConfigInfo { from_service_id: 0, to_service_id: 10,
        from_code_scheme: 0, to_code_scheme: 255, selected: true }];
    h.request(56, Request::SetGsmBroadcastConfig(config.clone()));
    assert_eq!(h.response(56).error, RadioError::GenericFailure);
    assert_eq!(h.modem.broadcast_config, config);
}

#[test]
fn remaining_boot_responses_match_capture() {
    let mut h = Harness::new();
    for request in [Request::SetNullCipherAndIntegrityEnabled(false), Request::SetSecurityAlgorithmsUpdatedEnabled(false),
        Request::SetCellularIdentifierDisclosedEnabled(false)] {
        h.request(1, request);
        assert_eq!(h.response(1).error, RadioError::ModemErr);
    }
    for request in [Request::IsSatelliteEnabledForCarrier, Request::GetBarringInfo] {
        h.request(1, request);
        assert_eq!(h.response(1).error, RadioError::RequestNotSupported);
    }
    h.request(74, Request::SetDataProfile { profile_count: 0 });
    assert_eq!(h.value(74), Value::Void);
    h.request(60, Request::GetVoiceRadioTechnology);
    assert_eq!(h.value(60), Value::VoiceRadioTechnology(1));
}

#[test]
fn transport_down_overrides_every_request_and_drains_reads_once() {
    let mut h = Harness::new();
    h.request(1, Request::GetImei);
    h.request(2, Request::GetImei);
    h.request(3, Request::GetSimSlotsStatus);
    h.modem.reset();
    h.model.radio.value = Radio::Unavailable;
    for serial in 1..=3 { assert_eq!(h.response(serial).error, RadioError::RadioNotAvailable); }
    for request in [Request::GetCurrentCalls, Request::GetHalDeviceCapabilities, Request::GetBarringInfo,
        Request::SendDeviceState(DeviceStateType::LowDataExpected, true), power(true)] {
        h.request(4, request);
        assert_eq!(h.response(4).error, RadioError::RadioNotAvailable);
    }
    h.channels = Channels::new(h.now, &[1, 6, 8], &["+ESIMS:", "+EUSIM:", "+CPIN:", "+ECPIN:", "+ESLOTSINFO:"]).unwrap();
    h.model.radio.value = Radio::On;
    h.request(5, Request::GetImei);
    assert_eq!(h.write(1), "AT+CGSN\r");
    h.empty();
}

#[test]
fn unavailable_model_overrides_local_handlers() {
    let mut h = Harness::new();
    h.model.apply(StateEvent { generation: 1, observed_ms: 0, source: Source::Transport, update: Update::Radio(Radio::Unavailable) });
    h.request(1, Request::SetTtyMode(0));
    assert_eq!(h.response(1).error, RadioError::RadioNotAvailable);
}

#[test]
fn duplicate_serial_and_replayed_completion_produce_one_response() {
    let mut h = Harness::new();
    h.request(1, Request::GetBasebandVersion);
    h.request(1, Request::GetBasebandVersion);
    h.write(1);
    let mut events = h.feed(1, "\r\n+CGMR: TEST\r\nOK\r\n");
    let Event::Completed(response) = events.pop().unwrap() else { panic!() };
    let replay = Event::Completed(mindone_radio_at::engine::Response { id: response.id,
        intermediates: response.intermediates.clone(), result: response.result.clone() });
    h.events(1, vec![Event::Completed(response), replay]);
    assert_eq!(h.value(1), Value::Baseband("TEST".into()));
    h.empty();
}

#[test]
fn timeout_and_fatal_engine_error_are_explicit() {
    let mut h = Harness::new();
    h.request(1, Request::GetModemActivityInfo);
    h.write(1);
    h.now = 1000;
    let mut events = Vec::new();
    h.channels.get_mut(1).unwrap().tick(h.now, |e| events.push(e)).unwrap();
    h.events(1, events);
    assert_eq!(h.response(1).error, RadioError::GenericFailure);
    h.request(2, Request::GetSimSlotsStatus);
    h.events(1, vec![Event::Error(mindone_radio_at::engine::Error::Failed)]);
    assert_eq!(h.response(2).error, RadioError::RadioNotAvailable);
    h.empty();
}

#[test]
fn final_errors_are_not_integer_casts() {
    assert_eq!(modem_error("OK"), RadioError::None);
    assert_eq!(modem_error("ERROR"), RadioError::GenericFailure);
    assert_eq!(modem_error("+CME ERROR: 501"), RadioError::ModemErr);
    assert_eq!(modem_error("+CMS ERROR: 42"), RadioError::ModemErr);
    assert_eq!(modem_error("unexpected"), RadioError::InvalidResponse);
}

#[test]
fn c53_boot_request_order_with_modem_confirmed_absence() {
    let mut h = Harness::new();
    configured(&mut h);
    let mut previous_line = 0;
    let mut handled = 0;
    let mut card_observed_absent = false;
    let mut network = 0;
    for row in include_str!("c53-boot.tsv").lines() {
        let columns: Vec<_> = row.split('\t').collect();
        let line: usize = columns[0].parse().unwrap();
        assert!(line > previous_line);
        previous_line = line;
        let serial: i32 = columns[1].parse().unwrap();
        let request = match columns[2] {
            "RADIO_POWER" => power(columns[3] == "true"),
            "GET_HAL_DEVICE_CAPABILITIES" => Request::GetHalDeviceCapabilities,
            "SET_TTY_MODE" => Request::SetTtyMode(0),
            "GET_SIM_STATUS" => Request::GetIccCardStatus,
            "GET_SLOT_STATUS" => Request::GetSimSlotsStatus,
            "BASEBAND_VERSION" => Request::GetBasebandVersion,
            "DEVICE_IMEI" => Request::GetImei,
            "GET_RADIO_CAPABILITY" => Request::GetRadioCapability,
            "GET_UICC_APPLICATIONS_ENABLEMENT" => Request::GetUiccApplicationsEnablement,
            "SET_NULL_CIPHER_AND_INTEGRITY_ENABLED" => Request::SetNullCipherAndIntegrityEnabled(false),
            "SET_CELLULAR_IDENTIFIER_DISCLOSED_ENABLED" => Request::SetCellularIdentifierDisclosedEnabled(false),
            "SET_SECURITY_ALGORITHMS_UPDATED_ENABLED" => Request::SetSecurityAlgorithmsUpdatedEnabled(false),
            "SEND_DEVICE_STATE" => Request::SendDeviceState(match &columns[3][..1] {
                "0" => DeviceStateType::PowerSaveMode, "1" => DeviceStateType::ChargingState,
                "2" => DeviceStateType::LowDataExpected, _ => panic!(),
            }, columns[3].ends_with("true")),
            "GET_MODEM_STATUS" => Request::GetModemStackStatus,
            "GET_PHONE_CAPABILITY" => Request::GetPhoneCapability,
            "SET_PREFERRED_DATA_MODEM" => Request::SetPreferredDataModem(0),
            "GSM_SET_BROADCAST_CONFIG" => Request::SetGsmBroadcastConfig(Vec::new()),
            "GET_CURRENT_CALLS" => Request::GetCurrentCalls,
            "VOICE_RADIO_TECH" => Request::GetVoiceRadioTechnology,
            "GET_BARRING_INFO" => Request::GetBarringInfo,
            "IS_SATELLITE_ENABLED_FOR_CARRIER" => Request::IsSatelliteEnabledForCarrier,
            "SET_DATA_PROFILE" => Request::SetDataProfile { profile_count: 0 },
            "GET_ACTIVITY_INFO" => Request::GetModemActivityInfo,
            "SIGNAL_STRENGTH" | "SET_SIGNAL_STRENGTH_REPORTING_CRITERIA" | "OPERATOR"
                | "QUERY_NETWORK_SELECTION_MODE" | "SET_UNSOLICITED_RESPONSE_FILTER"
                | "SET_LINK_CAPACITY_REPORTING_CRITERIA" | "GET_CELL_INFO_LIST"
                | "DATA_REGISTRATION_STATE" | "VOICE_REGISTRATION_STATE" => { network += 1; continue; }
            value => panic!("unhandled capture request {value}"),
        };
        let expected_error = match request {
            Request::GetUiccApplicationsEnablement | Request::GetModemStackStatus | Request::GetBarringInfo
                | Request::IsSatelliteEnabledForCarrier
                | Request::SendDeviceState(DeviceStateType::PowerSaveMode | DeviceStateType::ChargingState, _) => RadioError::RequestNotSupported,
            Request::SetNullCipherAndIntegrityEnabled(_) | Request::SetCellularIdentifierDisclosedEnabled(_)
                | Request::SetSecurityAlgorithmsUpdatedEnabled(_) => RadioError::ModemErr,
            Request::SetGsmBroadcastConfig(_) => RadioError::GenericFailure,
            _ => RadioError::None,
        };
        h.request(serial, request.clone());
        match request {
            Request::SetRadioPower { on, .. } => { assert_eq!(h.write(8), if on { "AT+EFUN=1\r" } else { "AT+EFUN=0\r" }); h.receive(8, "\r\nOK\r\n"); }
            Request::GetIccCardStatus if !card_observed_absent => {
                assert_eq!(h.write(6), "AT+CPIN?\r"); h.receive(6, "\r\n+CME ERROR: 10\r\n");
                card_observed_absent = true;
            }
            Request::GetSimSlotsStatus => {
                assert_eq!(h.write(6), "AT+ESLOTSINFO?\r");
                h.receive(6, "\r\n+ESLOTSINFO: 1,\"+CME ERROR: 10\",0,1,\"\",\"\",\"\"\r\nOK\r\n");
            }
            Request::GetBasebandVersion => { assert_eq!(h.write(1), "AT+CGMR\r"); h.receive(1, "\r\n+CGMR: TEST\r\nOK\r\n"); }
            Request::GetImei => {
                assert_eq!(h.write(1), "AT+CGSN\r"); h.receive(1, "\r\n490154203237518\r\nOK\r\n");
                assert_eq!(h.write(1), "AT+EGMR=0,9\r"); h.receive(1, "\r\n+EGMR: 78\r\nOK\r\n");
            }
            Request::GetModemActivityInfo => { assert_eq!(h.write(1), "AT+ERFTX=11\r"); h.receive(1, "\r\n+ERFTX: 11,0,0,0,0\r\nOK\r\n"); }
            _ => (),
        }
        assert_eq!(h.response(serial).error, expected_error, "capture line {line}");
        h.empty();
        handled += 1;
    }
    assert_eq!((handled, network), (40, 77));
}

#[test]
fn nonempty_profiles_are_not_silently_accepted() {
    let mut h = Harness::new();
    h.request(1, Request::SetDataProfile { profile_count: 1 });
    assert_eq!(h.response(1).error, RadioError::RequestNotSupported);
}

#[test]
fn read_final_failures_match_donor_mapping() {
    for (request, channel, expected) in [
        (Request::GetBasebandVersion, 1, RadioError::GenericFailure),
        (Request::GetModemActivityInfo, 1, RadioError::GenericFailure),
        (Request::GetImei, 1, RadioError::EmptyRecord),
        (Request::GetDeviceIdentity, 1, RadioError::EmptyRecord),
        (Request::GetSimSlotsStatus, 6, RadioError::RequestNotSupported),
    ] {
        let mut h = Harness::new();
        h.request(1, request.clone());
        h.request(2, request);
        h.write(channel);
        h.receive(channel, "\r\n+CME ERROR: 42\r\n");
        for serial in [1, 2] { assert_eq!(h.response(serial).error, expected); }
        h.empty();
    }
}

#[test]
fn svn_failure_and_missing_baseband_return_no_identity() {
    let mut h = Harness::new();
    h.request(1, Request::GetImei);
    h.write(1);
    h.receive(1, "\r\n490154203237518\r\nOK\r\n");
    h.write(1);
    h.receive(1, "\r\nERROR\r\n");
    assert_eq!(h.response(1).error, RadioError::EmptyRecord);
    h.request(2, Request::GetBasebandVersion);
    h.write(1);
    h.receive(1, "\r\nOK\r\n");
    h.write(1);
    h.receive(1, "\r\nOK\r\n");
    assert_eq!(h.response(2).error, RadioError::InvalidResponse);
}

#[test]
fn queue_full_is_internal_error_and_disconnect_completes_queued_power() {
    let mut h = Harness::new();
    for serial in 0..65 { h.request(serial, power(false)); }
    assert_eq!(h.response(64).error, RadioError::InternalErr);
    h.modem.reset();
    h.model.radio.value = Radio::Unavailable;
    for serial in 0..64 { assert_eq!(h.response(serial).error, RadioError::RadioNotAvailable); }
    h.empty();
}


#[test]
fn command_updates_are_queued_with_sources_without_writing_model() {
    let mut h = Harness::new();
    for on in [false, true] {
        h.request(1, power(on));
        h.write(8);
        for event in h.feed(8, "OK\r\n") {
            h.modem.on_event(8, &event, 0, &mut h.channels, &h.model);
        }
        assert_eq!(h.model.radio.value, Radio::On);
        assert_eq!(h.modem.pop_update(), Some((Source::PowerResponse, Update::Radio(if on { Radio::On } else { Radio::Off }))));
        assert_eq!(h.modem.pop_update(), None);
        assert_eq!(h.value(1), Value::Void);
    }
    for (wire, report, error) in [
        ("+CPIN: READY\r\nOK\r\n", mindone_radio_state::parse::SimReport::Cpin(mindone_radio_state::parse::SimApp::Ready), RadioError::RequestNotSupported),
        ("+CME ERROR: 10\r\n", mindone_radio_state::parse::SimReport::Absent, RadioError::None),
        ("+CME ERROR: 13\r\n", mindone_radio_state::parse::SimReport::Failure, RadioError::SimErr),
        ("+CME ERROR: 14\r\n", mindone_radio_state::parse::SimReport::Busy, RadioError::SimBusy),
        ("+CME ERROR: 15\r\n", mindone_radio_state::parse::SimReport::Failure, RadioError::SimErr),
    ] {
        h.request(2, Request::GetIccCardStatus);
        assert_eq!(h.write(6), "AT+CPIN?\r");
        for event in h.feed(6, wire) {
            h.modem.on_event(6, &event, 0, &mut h.channels, &h.model);
        }
        assert_eq!(h.model.sim.value.card, mindone_radio_state::model::SimCard::Unknown);
        assert_eq!(h.modem.pop_update(), Some((Source::Query, Update::Sim(report))));
        assert_eq!(h.modem.pop_update(), None);
        assert_eq!(h.response(2).error, error);
    }
}

#[test]
fn modem_urcs_do_not_create_query_updates_or_private_absence() {
    let mut h = Harness::new();
    h.modem.on_event(6, &Event::Urc { line: "+ESIMS: 0,0".into(), registered: true }, 0, &mut h.channels, &h.model);
    assert_eq!(h.modem.pop_update(), None);
    h.request(1, Request::GetIccCardStatus);
    assert_eq!(h.write(6), "AT+CPIN?\r");
}
