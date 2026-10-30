/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use crate::types::*;
use mindone_radio_at::engine::{Command, CommandId, Error, Event};
use mindone_radio_at::Channels;
use mindone_radio_state::{model::{Model, Radio, SimCard, Source, Update}, observe};
use std::collections::{BTreeSet, VecDeque};

#[derive(Clone, Debug, Eq, PartialEq)]
enum Operation { Baseband, BasebandRaw, Imei, Identity, Svn(bool, String), Activity, Card, Slots, Power(bool) }

struct Pending {
    channel: u8, id: CommandId, request: Request, operation: Operation,
    serials: Vec<i32>, deadline: u64, attempts: u8, raw: Vec<String>,
}

pub struct Modem {
    pending: Vec<Pending>, responses: VecDeque<Response>,
    serials: BTreeSet<i32>, updates: VecDeque<(Source, Update)>,
    pub tty_mode: i32, pub broadcast_config: Vec<GsmBroadcastSmsConfigInfo>,
    pub radio_capability: Option<RadioCapability>, pub phone_capability: Option<PhoneCapability>,
    pub imei_type: ImeiType, pub reduced_feature_set1: bool,
}

fn engine_error(error: Error) -> RadioError {
    match error {
        Error::Failed | Error::ResyncFailed => RadioError::RadioNotAvailable,
        Error::Timeout => RadioError::GenericFailure,
        _ => RadioError::InternalErr,
    }
}

pub fn modem_error(line: &str) -> RadioError {
    match line {
        "OK" => RadioError::None,
        "ERROR" | "NO CARRIER" => RadioError::GenericFailure,
        value if value.starts_with("+CME ERROR:") || value.starts_with("+CMS ERROR:") => RadioError::ModemErr,
        _ => RadioError::InvalidResponse,
    }
}

fn fields(body: &str) -> Result<Vec<String>, RadioError> {
    let mut fields = Vec::new();
    let mut current = String::new();
    let mut quoted = false;
    for c in body.chars() {
        match c {
            '"' => quoted = !quoted,
            ',' if !quoted => { fields.push(current.trim().into()); current.clear(); }
            _ => current.push(c),
        }
    }
    if quoted { return Err(RadioError::InvalidResponse); }
    fields.push(current.trim().into());
    Ok(fields)
}

fn one(lines: &[String], prefix: &str) -> Result<String, RadioError> {
    if lines.len() != 1 { return Err(RadioError::InvalidResponse); }
    let body = lines[0].strip_prefix(prefix).ok_or(RadioError::InvalidResponse)?;
    let values = fields(body)?;
    if values.len() != 1 || values[0].is_empty() { return Err(RadioError::InvalidResponse); }
    Ok(values[0].clone())
}

pub fn activity(line: &str) -> Result<ActivityStatsInfo, RadioError> {
    let values = fields(line.strip_prefix("+ERFTX:").ok_or(RadioError::InvalidResponse)?)?
        .iter().map(|v| v.parse::<i32>().map_err(|_| RadioError::InvalidResponse)).collect::<Result<Vec<_>, _>>()?;
    if values.len() < 5 || values[0] != 11 || !(0..=5).contains(&values[1]) {
        return Err(RadioError::InvalidResponse);
    }
    let count = values[1] as usize;
    if values.len() != count + 5 || values[2..].iter().any(|v| *v < 0) { return Err(RadioError::InvalidResponse); }
    let mut tx = [0; 5];
    tx[..count].copy_from_slice(&values[2..2 + count]);
    Ok(ActivityStatsInfo { sleep_mode_time_ms: values[count + 3], idle_mode_time_ms: values[count + 4],
        tech_specific_info: vec![ActivityStatsTechSpecificInfo { rat: 0, frequency_range: 0,
            txm_mode_time_ms: tx, rx_mode_time_ms: values[count + 2] }] })
}

pub fn slots(line: &str) -> Result<Vec<SimSlotStatus>, RadioError> {
    let values = fields(line.strip_prefix("+ESLOTSINFO:").ok_or(RadioError::InvalidResponse)?)?;
    let count = values[0].parse::<usize>().map_err(|_| RadioError::InvalidResponse)?;
    if count == 0 || count > 8 || values.len() != 1 + 6 * count { return Err(RadioError::InvalidResponse); }
    values[1..].chunks_exact(6).map(|v| {
        if v[0].contains("+CME ERROR:14") || v[0].contains("+CME ERROR: 14") { return Err(RadioError::SimBusy); }
        let logical_slot_id = v[1].parse().map_err(|_| RadioError::InvalidResponse)?;
        let port_active = match v[2].as_str() { "0" => false, "1" => true, _ => return Err(RadioError::InvalidResponse) };
        let card_state = if v[0].contains("+CPIN: CARD_RESTRICTED") { CardState::Restricted }
            else if !v[4].is_empty() || !v[5].is_empty() { CardState::Present } else { CardState::Absent };
        Ok(SimSlotStatus { card_state, atr: v[3].clone(), eid: v[5].to_ascii_uppercase(),
            port_info: vec![SimPortInfo { icc_id: v[4].clone(), logical_slot_id, port_active }], supported_mep_mode: 0 })
    }).collect()
}

impl Modem {
    pub fn new() -> Self {
        Self { pending: Vec::new(),
            responses: VecDeque::new(), serials: BTreeSet::new(), updates: VecDeque::new(), tty_mode: 0,
            broadcast_config: Vec::new(), radio_capability: None, phone_capability: None,
            imei_type: ImeiType::Invalid, reduced_feature_set1: true }
    }

    pub fn next_deadline(&self) -> Option<u64> { self.pending.iter().map(|pending| pending.deadline).min() }

    pub fn pop_update(&mut self) -> Option<(Source, Update)> { self.updates.pop_front() }

    pub fn pop_response(&mut self) -> Option<Response> {
        let response = self.responses.pop_front()?;
        self.serials.remove(&response.serial);
        Some(response)
    }

    fn respond(&mut self, serial: i32, result: Result<Value, RadioError>) {
        let (error, value) = match result { Ok(value) => (RadioError::None, Some(value)), Err(error) => (error, None) };
        self.responses.push_back(Response { serial, error, value });
    }

    pub fn reset(&mut self) {
        let serials: Vec<_> = self.pending.drain(..).flat_map(|pending| pending.serials).collect();
        for serial in serials { self.respond(serial, Err(RadioError::RadioNotAvailable)); }
        self.updates.clear();
    }

    pub fn submit(&mut self, serial: i32, request: Request, deadline: u64, channels: &mut Channels, model: &Model) {
        if !self.serials.insert(serial) { return; }
        if model.radio.value == Radio::Unavailable {
            self.respond(serial, Err(RadioError::RadioNotAvailable)); return;
        }
        let operation = match &request {
            Request::SetRadioPower { on, for_emergency_call: false, preferred_for_emergency_call: false } => Some(Operation::Power(*on)),
            Request::GetBasebandVersion => Some(Operation::Baseband),
            Request::GetImei => Some(Operation::Imei),
            Request::GetDeviceIdentity => Some(Operation::Identity),
            Request::GetModemActivityInfo => Some(Operation::Activity),
            Request::GetIccCardStatus if model.sim.value.card != SimCard::Absent => Some(Operation::Card),
            Request::GetSimSlotsStatus => Some(Operation::Slots),
            _ => None,
        };
        if let Some(operation) = operation {
            if !matches!(operation, Operation::Power(_)) {
                if let Some(pending) = self.pending.iter_mut().find(|pending| pending.request == request) {
                    pending.serials.push(serial); return;
                }
            }
            self.start(request, operation, vec![serial], deadline, 0, channels);
            return;
        }
        let result = match request {
            Request::SendDeviceState(DeviceStateType::LowDataExpected, _) => Ok(Value::Void),
            Request::GetRadioCapability => self.radio_capability.clone().map(Value::Capability).ok_or(RadioError::RequestNotSupported),
            Request::GetPhoneCapability => self.phone_capability.clone().map(Value::Phone).ok_or(RadioError::RequestNotSupported),
            Request::GetHalDeviceCapabilities => Ok(Value::ReducedFeatureSet1(self.reduced_feature_set1)),
            Request::GetIccCardStatus => Ok(Value::Card(CardStatus::absent())),
            Request::GetCurrentCalls => Ok(Value::CurrentCalls(Vec::new())),
            Request::SetTtyMode(mode) if (0..=3).contains(&mode) => { self.tty_mode = mode; Ok(Value::Void) }
            Request::SetTtyMode(_) => Err(RadioError::InvalidArguments),
            Request::SetGsmBroadcastConfig(config) => {
                self.broadcast_config = config;
                Err(RadioError::GenericFailure)
            }
            Request::SetDataProfile { profile_count: 0 } => Ok(Value::Void),
            Request::SetNullCipherAndIntegrityEnabled(_) | Request::SetSecurityAlgorithmsUpdatedEnabled(_)
                | Request::SetCellularIdentifierDisclosedEnabled(_) => Err(RadioError::ModemErr),
            Request::SetPreferredDataModem(0) => Ok(Value::Void),
            Request::SetPreferredDataModem(_) => Err(RadioError::InvalidArguments),
            Request::GetVoiceRadioTechnology => Ok(Value::VoiceRadioTechnology(1)),
            _ => Err(RadioError::RequestNotSupported),
        };
        self.respond(serial, result);
    }

    fn start(&mut self, request: Request, operation: Operation, serials: Vec<i32>, deadline: u64, attempts: u8, channels: &mut Channels) {
        let (channel, text, prefix) = match &operation {
            Operation::Baseband => (1, "AT+CGMR", "+CGMR:"),
            Operation::BasebandRaw => (1, "AT+CGMR", ""),
            Operation::Imei | Operation::Identity => (1, "AT+CGSN", ""),
            Operation::Svn(_, _) => (1, "AT+EGMR=0,9", "+EGMR:"),
            Operation::Activity => (1, "AT+ERFTX=11", "+ERFTX:"),
            Operation::Card => (6, "AT+CPIN?", "+CPIN:"),
            Operation::Slots => (6, "AT+ESLOTSINFO?", "+ESLOTSINFO:"),
            Operation::Power(true) => (8, "AT+EFUN=1", ""),
            Operation::Power(false) => (8, "AT+EFUN=0", ""),
        };
        let result = Command::new(text, (!prefix.is_empty()).then_some(prefix), 32, deadline)
            .and_then(|command| channels.get_mut(channel).ok_or(Error::Configuration)?.enqueue(command));
        match result {
            Ok(id) => self.pending.push(Pending { channel, id, request, operation, serials, deadline, attempts, raw: Vec::new() }),
            Err(error) => { for serial in serials { self.respond(serial, Err(engine_error(error))); } }
        }
    }

    pub fn on_event(&mut self, channel: u8, event: &Event, _now: u64, channels: &mut Channels, _model: &Model) {
        match event {
            Event::Urc { line, registered } => {
                if !*registered && !line.starts_with('+') {
                    if let Some(pending) = self.pending.iter_mut().find(|p| p.channel == channel) {
                        if matches!(pending.operation, Operation::BasebandRaw | Operation::Imei | Operation::Identity)
                            && pending.raw.len() < 33 { pending.raw.push(line.clone()); }
                    }
                }
            }
            Event::Completed(response) => {
                let Some(position) = self.pending.iter().position(|p| p.channel == channel && p.id == response.id) else { return; };
                let pending = self.pending.remove(position);
                let mut lines = response.intermediates.clone();
                lines.extend(pending.raw.iter().cloned());
                if matches!(pending.operation, Operation::BasebandRaw | Operation::Imei | Operation::Identity | Operation::Power(_)) {
                    lines.retain(|line| !["+ESIMS:", "+EUSIM:", "+ECPIN:", "+CPIN:"].iter().any(|prefix| line.starts_with(prefix)));
                }
                let error = match &response.result { Ok(ref line) => modem_error(line), Err(error) => engine_error(*error) };
                let final_line = response.result.as_ref().map(String::as_str).unwrap_or("");
                let cme = final_line.strip_prefix("+CME ERROR:").and_then(|v| v.trim().parse::<i32>().ok());
                if error == RadioError::None {
                    for line in &lines {
                        if let Some(update) = observe::line(line) { self.updates.push_back((Source::Query, update)); }
                    }
                }
                if let Some(update) = observe::line(final_line) { self.updates.push_back((Source::Query, update)); }
                if error == RadioError::None && pending.operation == Operation::Baseband && lines.is_empty() {
                    self.start(pending.request, Operation::BasebandRaw, pending.serials, pending.deadline, pending.attempts, channels); return;
                }
                let result = if pending.operation == Operation::Card && cme == Some(10) {
                    Ok(Value::Card(CardStatus::absent()))
                } else if error != RadioError::None {
                    Err(match (&pending.operation, cme) {
                        (Operation::Power(_), Some(54)) => RadioError::OperationNotAllowed,
                        (Operation::Card, Some(14)) => RadioError::SimBusy,
                        (Operation::Card, Some(13 | 15)) => RadioError::SimErr,
                        _ if error == RadioError::RadioNotAvailable => error,
                        (Operation::Power(_), _) => RadioError::OemError1,
                        (Operation::Baseband | Operation::BasebandRaw | Operation::Activity, _) => RadioError::GenericFailure,
                        (Operation::Imei | Operation::Identity | Operation::Svn(_, _), _) => RadioError::EmptyRecord,
                        (Operation::Slots, _) => RadioError::RequestNotSupported,
                        _ => error,
                    })
                } else { match &pending.operation {
                    Operation::Baseband => one(&lines, "+CGMR:").map(Value::Baseband),
                    Operation::BasebandRaw => one(&lines, "").map(Value::Baseband),
                    Operation::Imei | Operation::Identity => {
                        match one(&lines, "") {
                            Ok(imei) if imei.len() == 15 && imei.bytes().all(|v| v.is_ascii_digit()) => {
                                let identity = pending.operation == Operation::Identity;
                                self.start(pending.request, Operation::Svn(identity, imei), pending.serials, pending.deadline, pending.attempts, channels); return;
                            }
                            _ => Err(RadioError::InvalidResponse),
                        }
                    }
                    Operation::Svn(identity, imei) => one(&lines, "+EGMR:").and_then(|svn| {
                        if svn.len() != 2 || !svn.bytes().all(|v| v.is_ascii_digit()) { return Err(RadioError::InvalidResponse); }
                        Ok(if *identity { Value::Identity(DeviceIdentity { imei: imei.clone(), imeisv: svn, esn: String::new(), meid: String::new() }) }
                            else { Value::Imei(ImeiInfo { kind: self.imei_type, imei: imei.clone(), svn }) })
                    }),
                    Operation::Activity if lines.len() == 1 => activity(&lines[0]).map(Value::Activity),
                    Operation::Slots if lines.len() == 1 => slots(&lines[0]).map(Value::Slots),
                    Operation::Card => Err(RadioError::RequestNotSupported),
                    Operation::Power(on) => {
                        self.updates.push_back((Source::PowerResponse, Update::Radio(if *on { Radio::On } else { Radio::Off })));
                        Ok(Value::Void)
                    }
                    _ => Err(RadioError::InvalidResponse),
                }};
                if pending.operation == Operation::Slots && result == Err(RadioError::SimBusy) {
                    if pending.attempts < 29 {
                        self.start(pending.request, pending.operation, pending.serials, pending.deadline, pending.attempts + 1, channels); return;
                    }
                    for serial in pending.serials { self.respond(serial, Err(RadioError::SimErr)); }
                } else {
                    for serial in pending.serials { self.respond(serial, result.clone()); }
                }
            }
            Event::Error(Error::Failed | Error::ResyncFailed) => { self.reset(); }
            _ => (),
        }
    }
}
