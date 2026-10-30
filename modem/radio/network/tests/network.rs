/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#![forbid(unsafe_code)]

use mindone_radio_at::Channels;
use mindone_radio_at::engine::{Event, Response as AtResponse};
use mindone_radio_network::{service::{modem_error, Network}, types::*};
use mindone_radio_state::model::{Event as StateEvent, Model, Radio, Source, Update};

struct Harness { network: Network, model: Model, now: u64, channels: Channels, updates: Vec<(Source, Update)> }

impl Harness {
    fn new() -> Self {
        let mut model = Model::new(1);
        model.apply(StateEvent { generation: 1, observed_ms: 0, source: Source::Transport, update: Update::Radio(Radio::On) });
        Self { network: Network::new(), model, now: 0, channels: Channels::new(0, &[1, 3, 6], &["+EREG:", "+EGREG:", "+ECSQ:"]).unwrap(), updates: Vec::new() }
    }
    fn request(&mut self, serial: i32, request: Request) { self.network.submit(serial, request, 1000, &mut self.channels, &self.model); }
    fn events(&mut self, channel: u8, events: Vec<Event>) {
        for event in events {
            if let Event::Urc { line, .. } = &event {
                if let Some(update) = mindone_radio_state::observe::line(line) {
                    self.model.apply(StateEvent { generation: 1, observed_ms: self.now, source: Source::Urc, update });
                }
            }
            self.network.on_event(channel, &event, self.now, &mut self.channels, &self.model);
            while let Some((source, update)) = self.network.pop_update() {
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
        let result = self.network.pop_response().unwrap();
        assert_eq!(result.serial, serial);
        result
    }
    fn value(&mut self, serial: i32) -> Value {
        let response = self.response(serial);
        assert_eq!(response.error, RadioError::None);
        response.value.unwrap()
    }
    fn empty(&mut self) { assert_eq!(self.network.pop_response(), None); }
}

#[test]
fn aidl_verified_radio_error_numbers() {
    assert_eq!([RadioError::None as i32, RadioError::RadioNotAvailable as i32, RadioError::GenericFailure as i32,
        RadioError::RequestNotSupported as i32, RadioError::InternalErr as i32, RadioError::ModemErr as i32,
        RadioError::InvalidArguments as i32, RadioError::InvalidResponse as i32], [0, 1, 2, 6, 38, 40, 44, 66]);
}

#[test]
fn c53_indication_driven_idle_poll_uses_only_eops_transaction() {
    let mut h = Harness::new();
    for (iteration, cs, ps) in [(0, 0, 0), (1, 2, 4), (2, 0, 2)] {
        h.now += 10;
        h.receive(6, &format!("\r\n+EREG: {cs}\r\n+EGREG: {ps}\r\n"));
        let serial = iteration * 4;
        h.request(serial, Request::GetOperator);
        h.request(serial + 1, Request::GetNetworkSelectionMode);
        h.request(serial + 2, Request::GetDataRegistrationState);
        h.request(serial + 3, Request::GetVoiceRegistrationState);
        for (serial, state) in [(serial + 2, ps), (serial + 3, cs)] {
            let Value::Registration(reg) = h.value(serial) else { panic!() };
            assert_eq!(reg.reg_state as u32, state);
            assert_eq!(reg.rat, Rat::Unknown);
            assert_eq!(reg.cell_identity, None);
            assert_eq!(reg.reason_for_denial, 0);
        }
        assert_eq!(h.write(6), "AT+EOPS?\r");
        h.receive(6, "\r\n+EOPS: 0,2,\"\",1\r\nOK\r\n");
        assert_eq!(h.value(serial), Value::Operator(OperatorInfo::default()));
        assert_eq!(h.value(serial + 1), Value::Selection(SelectionMode::Automatic));
        for channel in [1, 3, 6] { assert_eq!(h.channels.get_mut(channel).unwrap().queued(), 0); }
        h.empty();
    }
}

#[test]
fn concurrent_operator_reads_coalesce_and_each_serial_completes_once() {
    let mut h = Harness::new();
    h.receive(6, "+EGREG: 1\r\n");
    for serial in 0..20 { h.request(serial, Request::GetOperator); }
    assert_eq!(h.channels.get_mut(6).unwrap().queued(), 1);
    assert_eq!(h.write(6), "AT+EOPS?\r");
    let events = h.feed(6, "+EOPS: 0,2,\"00101\",1\r\nOK\r\n");
    let Event::Completed(response) = &events[0] else { panic!() };
    let duplicate = AtResponse { id: response.id, intermediates: response.intermediates.clone(), result: response.result.clone() };
    h.events(6, events);
    h.network.on_event(6, &Event::Completed(duplicate), h.now, &mut h.channels, &h.model);
    for serial in 0..20 {
        assert_eq!(h.value(serial), Value::Operator(OperatorInfo { numeric: "00101".into(), ..Default::default() }));
    }
    h.empty();
}

#[test]
fn timeout_completes_every_waiter_and_late_ok_cannot_duplicate() {
    let mut h = Harness::new();
    h.request(1, Request::GetOperator);
    h.request(2, Request::GetOperator);
    h.request(3, Request::GetNetworkSelectionMode);
    h.write(6);
    h.now = 1000;
    let mut events = Vec::new();
    h.channels.get_mut(6).unwrap().tick(h.now, |event| events.push(event)).unwrap();
    h.events(6, events);
    for serial in 1..=3 { assert_eq!(h.response(serial).error, RadioError::GenericFailure); }
    h.receive(6, "+EOPS: 0,2,\"\",1\r\nOK\r\n");
    h.empty();
}

#[test]
fn modem_errors_are_explicit_and_vendor_numbers_are_never_cast() {
    assert_eq!(modem_error("ERROR"), RadioError::GenericFailure);
    for code in [1, 6, 10, 13, 14, 40, 44, 100, 9999] {
        assert_eq!(modem_error(&format!("+CME ERROR: {code}")), RadioError::ModemErr);
    }
    assert_eq!(modem_error("+CMS ERROR: 500"), RadioError::ModemErr);
    assert_eq!(modem_error("strange"), RadioError::InvalidResponse);
    let mut h = Harness::new();
    for (serial, final_line, error) in [(1, "ERROR", RadioError::GenericFailure), (2, "+CME ERROR: 44", RadioError::ModemErr)] {
        h.request(serial, Request::GetOperator);
        h.write(6);
        h.receive(6, &format!("{final_line}\r\n"));
        assert_eq!(h.response(serial).error, error);
    }
    h.empty();
}

#[test]
fn transport_loss_drains_reads_selection_controls_and_deferred_filters_once() {
    let mut h = Harness::new();
    h.request(1, Request::GetOperator);
    h.request(2, Request::GetNetworkSelectionMode);
    h.request(3, Request::GetSignalStrength);
    h.request(4, Request::SetIndicationFilter(3));
    h.request(5, Request::SetIndicationFilter(0));
    h.network.reset();
    h.model.radio.value = Radio::Unavailable;
    let mut serials = Vec::new();
    while let Some(response) = h.network.pop_response() {
        assert_eq!(response.error, RadioError::RadioNotAvailable);
        serials.push(response.serial);
    }
    serials.sort(); assert_eq!(serials, vec![1, 2, 3, 4, 5]);
    h.network.reset();
    h.model.radio.value = Radio::Unavailable;
    h.empty();
    h.request(6, Request::GetVoiceRegistrationState);
    assert_eq!(h.response(6).error, RadioError::RadioNotAvailable);
    h.channels = Channels::new(h.now, &[1, 3, 6], &["+EREG:", "+EGREG:", "+ECSQ:"]).unwrap();
    h.model.radio.value = Radio::On;
    h.request(7, Request::GetOperator);
    assert_eq!(h.write(6), "AT+EOPS?\r");
}

#[test]
fn unknown_registration_and_cells_do_not_invent_service() {
    let mut h = Harness::new();
    h.request(1, Request::GetDataRegistrationState);
    let Value::Registration(reg) = h.value(1) else { panic!() };
    assert_eq!(reg.reg_state, RegState::Unknown);
    h.request(2, Request::GetCellInfoList);
    assert_eq!(h.value(2), Value::Cells(vec![]));
    h.request(3, Request::GetImsRegistrationState);
    assert_eq!(h.response(3).error, RadioError::RequestNotSupported);
}

#[test]
fn operator_modes_two_and_four_remain_explicitly_unverified() {
    for (mode, expected) in [(0, SelectionMode::Automatic), (1, SelectionMode::Manual),
        (2, SelectionMode::DeregisteredUnverified), (4, SelectionMode::ManualAutomaticUnverified)] {
        let mut h = Harness::new();
        h.receive(6, "+EGREG: 1\r\n");
        h.request(1, Request::GetOperator); h.write(6);
        h.receive(6, &format!("+EOPS: {mode},2,\"00101\",1\r\nOK\r\n"));
        let Value::Operator(info) = h.value(1) else { panic!() };
        assert_eq!(info.numeric, if mode == 2 { "" } else { "00101" });
        assert!(info.alpha_long.is_empty() && info.alpha_short.is_empty());
        h.request(2, Request::GetNetworkSelectionMode);
        assert_eq!(h.value(2), Value::Selection(expected));
    }
}

#[test]
fn malformed_success_is_invalid_response_and_does_not_update_selection() {
    for transcript in ["OK\r\n", "+EOPS: invalid\r\nOK\r\n"] {
        let mut h = Harness::new();
        h.request(1, Request::GetOperator); h.request(2, Request::GetNetworkSelectionMode); h.write(6);
        h.receive(6, transcript);
        assert_ne!(h.response(1).error, RadioError::None);
        assert_ne!(h.response(2).error, RadioError::None);
        h.empty();
    }
}

#[test]
fn signal_multiline_query_coalesces_and_converts_gsm_and_lte() {
    let mut h = Harness::new();
    h.request(1, Request::GetSignalStrength); h.request(2, Request::GetSignalStrength);
    assert_eq!(h.write(1), "AT+ECSQ\r");
    h.receive(1, "+ECSQ: 0,3,-360,0,0,0,0,1,0,0\r\n+ECSQ: 0,99,0,0,0,-40,-400,4096,40,3\r\nOK\r\n");
    let expected = Value::Signal(SignalStrength {
        gsm: Some(GsmSignal { signal_strength: Some(11), bit_error_rate: Some(3) }),
        lte: Some(LteSignal { rsrp: Some(100), rsrq: Some(10), rssnr: Some(100) }), wcdma: None,
    });
    assert_eq!(h.value(1), expected); assert_eq!(h.value(2), expected); h.empty();
}

#[test]
fn malformed_signal_is_atomic_and_absent_groups_remain_unknown() {
    let mut h = Harness::new();
    h.request(1, Request::GetSignalStrength); h.write(1);
    h.receive(1, "+ECSQ: 0,99,0,0,0,-40,-400,4096,40,3\r\n+ECSQ: bad\r\nOK\r\n");
    assert_eq!(h.response(1).error, RadioError::InvalidResponse);
    assert!(h.model.signals.is_empty());
    h.request(2, Request::GetSignalStrength); h.write(1);
    h.receive(1, "+ECSQ: 0,99,32767,0,0,32767,32767,4096,32767,0\r\nOK\r\n");
    let Value::Signal(signal) = h.value(2) else { panic!() };
    assert_eq!(signal.gsm, None); assert_eq!(signal.wcdma, None);
    assert_eq!(signal.lte, Some(LteSignal { rsrp: None, rsrq: None, rssnr: None }));
}

fn criteria() -> SignalCriteria {
    SignalCriteria { access_network: 3, measurement: 3, enabled: true, hysteresis_ms: 1000,
        hysteresis_db: 2, thresholds: vec![-120, -110, -100, -90, -80] }
}

#[test]
fn criteria_store_and_variable_threshold_command_on_channel_one() {
    let mut h = Harness::new();
    let criteria = criteria();
    h.request(1, Request::SetSignalStrengthReportingCriteria(vec![criteria.clone()]));
    assert_eq!(h.network.signal_criteria, vec![criteria]);
    assert_eq!(h.write(1), "AT+ECSQ=4, 1, 4, 0, 1000, 2, 5, -120, -110, -100, -90, -80\r");
    h.receive(1, "OK\r\n"); assert_eq!(h.value(1), Value::Void); h.empty();
}

#[test]
fn unsupported_and_invalid_criteria_have_one_response_and_no_command() {
    let mut h = Harness::new();
    let mut criteria = criteria(); criteria.access_network = 99;
    h.request(1, Request::SetSignalStrengthReportingCriteria(vec![criteria]));
    assert_eq!(h.response(1).error, RadioError::RequestNotSupported);
    let mut criteria = self::criteria(); criteria.thresholds = vec![0, 0];
    h.request(2, Request::SetSignalStrengthReportingCriteria(vec![criteria]));
    assert_eq!(h.response(2).error, RadioError::InvalidArguments);
    assert_eq!(h.channels.get_mut(1).unwrap().queued(), 0); h.empty();
}

#[test]
fn criteria_command_failure_is_reported_once_after_all_controls() {
    let mut h = Harness::new();
    h.request(1, Request::SetSignalStrengthReportingCriteria(vec![criteria(), criteria()]));
    h.write(1); h.receive(1, "ERROR\r\n"); h.empty();
    h.write(1); h.receive(1, "OK\r\n");
    assert_eq!(h.response(1).error, RadioError::GenericFailure); h.empty();
}

#[test]
fn link_criteria_stored_but_no_matching_modem_control_supported() {
    let mut h = Harness::new();
    let criteria = LinkCriteria { access_network: 3, hysteresis_ms: 1000, hysteresis_dl_kbps: 50,
        hysteresis_ul_kbps: 50, thresholds_dl_kbps: vec![100, 1000], thresholds_ul_kbps: vec![100, 1000] };
    h.request(1, Request::SetLinkCapacityReportingCriteria(criteria.clone()));
    assert_eq!(h.network.link_criteria, Some(criteria));
    assert_eq!(h.response(1).error, RadioError::RequestNotSupported); h.empty();
}

#[test]
fn filter_delegates_channels_and_fallback_to_state_policy() {
    let mut h = Harness::new();
    h.request(1, Request::SetIndicationFilter(0));
    for (channel, command, result) in [(1, "AT+ECSQ=0\r", "OK\r\n"),
        (1, "AT+EREG=9\r", "ERROR\r\n"), (1, "AT+EREG=5\r", "OK\r\n"),
        (1, "AT+EGREG=9\r", "OK\r\n"), (3, "AT+ESCREENSTATE=0\r", "OK\r\n")] {
        assert_eq!(h.write(channel), command); h.receive(channel, result);
    }
    assert_eq!(h.value(1), Value::Void);
    assert_eq!(h.network.policy().applied(mindone_radio_state::policy::Control::Cs), Some(false));
    h.request(2, Request::SetIndicationFilter(0));
    assert_eq!(h.value(2), Value::Void); h.empty();
}

#[test]
fn concurrent_filters_preserve_each_serial_and_latest_desired_policy() {
    let mut h = Harness::new();
    h.request(1, Request::SetIndicationFilter(3));
    h.request(2, Request::SetIndicationFilter(0));
    for enabled in [true, false] {
        for (channel, text) in [(1, format!("AT+ECSQ={}\r", u8::from(enabled))),
            (1, format!("AT+EREG={}\r", if enabled {3} else {9})),
            (1, format!("AT+EGREG={}\r", if enabled {3} else {9})),
            (3, format!("AT+ESCREENSTATE={}\r", u8::from(enabled)))] {
            assert_eq!(h.write(channel), text); h.receive(channel, "OK\r\n");
        }
        if enabled {
            assert_eq!(h.write(1), "AT+ECSQ\r");
            h.receive(1, "+ECSQ: 0,0,-400,0,0,0,0,1,0,0\r\nOK\r\n");
        }
    }
    assert_eq!(h.network.indication_filter(), 0);
    assert_eq!(h.value(1), Value::Void); assert_eq!(h.value(2), Value::Void); h.empty();
}

#[test]
fn configured_unverified_rat_mapping_enables_bounded_serving_cell_only() {
    let mut h = Harness::new();
    h.receive(6, "+EGREG: 1,\"1234\",\"123456\",4096\r\n");
    h.request(1, Request::GetCellInfoList); assert_eq!(h.value(1), Value::Cells(vec![]));
    h.network.registration_rat_mapping_unverified.insert(4096, Rat::Lte);
    h.request(2, Request::GetCellInfoList);
    let Value::Cells(cells) = h.value(2) else { panic!() };
    assert_eq!(cells.len(), 1);
    assert_eq!(cells[0].identity, CellIdentity::Lte { tac: 0x1234, ci: 0x123456, plmn: String::new() });
    h.receive(6, "+EGREG: 2,\"1234\",\"123456\",4096\r\n");
    h.request(3, Request::GetCellInfoList); assert_eq!(h.value(3), Value::Cells(vec![]));
}

#[test]
fn no_sim_operator_is_empty_even_with_a_numeric_plmn() {
    let mut h = Harness::new();
    h.receive(6, "+EREG: 2\r\n+EGREG: 4\r\n");
    h.request(1, Request::GetOperator); h.write(6);
    h.receive(6, "+EOPS: 0,2,\"00101\",1\r\nOK\r\n");
    assert_eq!(h.value(1), Value::Operator(OperatorInfo::default()));
}

#[test]
fn queued_identical_filters_complete_without_recursive_processing() {
    let mut h = Harness::new();
    for serial in 0..10000 { h.request(serial, Request::SetIndicationFilter(0)); }
    for channel in [1, 1, 1, 3] { h.write(channel); h.receive(channel, "OK\r\n"); }
    for serial in 0..10000 { assert_eq!(h.value(serial), Value::Void); }
    h.empty();
}

#[test]
fn enabling_signal_filter_refreshes_and_restore_reapplies_without_responses() {
    let mut h = Harness::new();
    assert_eq!(h.network.indication_filter(), 0);
    h.network.restore(&mut h.channels, h.now + 5000);
    assert_eq!(h.channels.get(1).unwrap().queued(), 0);
    h.request(7, Request::SetIndicationFilter(1));
    assert_eq!(h.network.indication_filter(), 1);
    for restoring in [false, true] {
        for (channel, command) in [(1, "AT+ECSQ=1\r"), (1, "AT+EREG=9\r"),
            (1, "AT+EGREG=9\r"), (3, "AT+ESCREENSTATE=1\r")] {
            assert_eq!(h.write(channel), command);
            h.receive(channel, "OK\r\n");
        }
        if !restoring { assert_eq!(h.value(7), Value::Void); }
        h.empty();
        assert_eq!(h.write(1), "AT+ECSQ\r");
        h.receive(1, "+ECSQ: 0,3,-400,0,0,0,0,1,0,0\r\nOK\r\n");
        assert_eq!(h.updates.last(), Some(&(Source::Query, Update::Signal(
            mindone_radio_state::model::SignalRat::GsmCsq,
            mindone_radio_state::model::Signal::Gsm(mindone_radio_state::parse::Csq { rssi: Some(6), ber: Some(3) })) )));
        h.empty();
        if !restoring {
            h.network.reset();
            h.channels = Channels::new(0, &[1, 3, 6], &["+EREG:", "+EGREG:", "+ECSQ:"]).unwrap();
            h.network.restore(&mut h.channels, h.now + 5000);
        }
    }
}

#[test]
fn service_ignores_urcs_and_query_updates_do_not_mutate_model() {
    let mut h = Harness::new();
    let event = Event::Urc { line: "+EREG: 1".into(), registered: true };
    h.network.on_event(1, &event, 0, &mut h.channels, &h.model);
    assert_eq!(h.network.pop_update(), None);
    h.request(1, Request::GetSignalStrength);
    h.write(1);
    for event in h.feed(1, "+ECSQ: 0,3,-400,0,0,0,0,1,0,0\r\nOK\r\n") {
        h.network.on_event(1, &event, 0, &mut h.channels, &h.model);
    }
    assert!(h.model.signals.is_empty());
    assert!(matches!(h.network.pop_update(), Some((Source::Query, Update::Signal(_, _)))));
    assert!(matches!(h.value(1), Value::Signal(SignalStrength { gsm: Some(_), .. })));
}
