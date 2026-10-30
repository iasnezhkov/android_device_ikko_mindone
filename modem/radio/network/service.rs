/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use crate::types::*;
use mindone_radio_at::engine::{Command, CommandId, Error, Event};
use mindone_radio_at::Channels;
use mindone_radio_state::{model::*, observe, parse, policy::{Input, Policy, Step}};
use std::collections::{BTreeMap, VecDeque};

#[derive(Clone, Copy, Eq, PartialEq)]
enum Read { Operator, Signal }

struct Pending { channel: u8, id: CommandId, kind: Read, serials: Vec<i32>, deadline: u64 }
struct ControlJob {
    serial: Option<i32>, refresh: bool, deadline: u64, steps: VecDeque<(u8, String, Option<Step>)>,
    active: Option<(u8, CommandId, Option<Step>)>, error: RadioError,
}

pub struct Network {
    pending: Vec<Pending>, responses: VecDeque<Response>,
    selection: Option<SelectionMode>, selection_waiters: Vec<i32>, policy: Policy,
    controls: VecDeque<ControlJob>, filters: VecDeque<(Option<i32>, u32, u64)>, updates: VecDeque<(Source, Update)>,
    pub registration_rat_mapping_unverified: BTreeMap<u32, Rat>,
    pub signal_criteria: Vec<SignalCriteria>, pub link_criteria: Option<LinkCriteria>,
}

pub fn modem_error(final_line: &str) -> RadioError {
    match final_line {
        "OK" => RadioError::None,
        "ERROR" | "NO CARRIER" => RadioError::GenericFailure,
        line if line.starts_with("+CME ERROR:") || line.starts_with("+CMS ERROR:") => RadioError::ModemErr,
        _ => RadioError::InvalidResponse,
    }
}

fn engine_error(error: Error) -> RadioError {
    match error {
        Error::Failed | Error::ResyncFailed => RadioError::RadioNotAvailable,
        Error::Timeout => RadioError::GenericFailure,
        Error::QueueFull => RadioError::InternalErr,
        _ => RadioError::InternalErr,
    }
}

fn registration(model: &Model, domain: Domain, mappings: &BTreeMap<u32, Rat>) -> RegStateResult {
    let value = match domain { Domain::Cs => &model.cs.value, Domain::Ps => &model.ps.value };
    let Knowledge::Known(reg) = value else {
        return RegStateResult { reg_state: RegState::Unknown, rat: Rat::Unknown,
            cell_identity: None, reason_for_denial: 0, registered_plmn: String::new() };
    };
    let reg_state = match reg.state {
        0 => RegState::NotRegistered, 1 => RegState::Home, 2 => RegState::Searching,
        3 => RegState::Denied, 5 => RegState::Roaming, _ => RegState::Unknown,
    };
    let registered = matches!(reg_state, RegState::Home | RegState::Roaming);
    let rat = if registered { reg.rat_mtk_unverified.and_then(|act| mappings.get(&act)).copied().unwrap_or(Rat::Unknown) }
        else { Rat::Unknown };
    let plmn = reg.plmn.clone().unwrap_or_default();
    let cell_identity = reg.area.zip(reg.cell).and_then(|(area, cell)| {
        let cid = u32::try_from(cell).ok()?;
        match rat {
            Rat::Gsm if area <= 0xfffe && cid <= 0xffff => Some(CellIdentity::Gsm { lac: area, cid, plmn: plmn.clone() }),
            Rat::Umts if area <= 0xfffe && cid < 0xfffffff => Some(CellIdentity::Wcdma { lac: area, cid, plmn: plmn.clone() }),
            Rat::Lte if area <= 0xfffe && cid < 0xfffffff => Some(CellIdentity::Lte { tac: area, ci: cid, plmn: plmn.clone() }),
            _ => None,
        }
    });
    RegStateResult { reg_state, rat, cell_identity,
        reason_for_denial: reg.reject_cause.unwrap_or(0), registered_plmn: reg.plmn.clone().unwrap_or_default() }
}

fn signal(model: &Model) -> SignalStrength {
    let mut result = SignalStrength::default();
    let mut latest_lte = None;
    for observation in model.signals.values() {
        match &observation.value {
            Signal::Gsm(csq) => result.gsm = Some(GsmSignal { signal_strength: csq.rssi, bit_error_rate: csq.ber }),
            Signal::Lte { rsrp, rsrq, rssnr } if latest_lte.is_none_or(|time| observation.observed_ms >= time) => {
                latest_lte = Some(observation.observed_ms);
                result.lte = Some(LteSignal { rsrp: *rsrp, rsrq: *rsrq, rssnr: *rssnr });
            }
            _ => (),
        }
    }
    result
}

fn operator(line: &str) -> Result<(SelectionMode, OperatorInfo), RadioError> {
    let body = line.strip_prefix("+EOPS:").ok_or(RadioError::InvalidResponse)?;
    let fields: Vec<_> = body.split(',').map(str::trim).collect();
    let mode = match fields.first().copied() {
        Some("0") => SelectionMode::Automatic, Some("1") => SelectionMode::Manual,
        Some("2") => SelectionMode::DeregisteredUnverified, Some("4") => SelectionMode::ManualAutomaticUnverified,
        _ => return Err(RadioError::GenericFailure),
    };
    let mut info = OperatorInfo::default();
    if mode != SelectionMode::DeregisteredUnverified && fields.get(1) == Some(&"2") {
        if let Some(plmn) = fields.get(2).and_then(|s| s.strip_prefix('"')).and_then(|s| s.strip_suffix('"')) {
            if plmn != "000000" && matches!(plmn.len(), 5 | 6) && plmn.bytes().all(|b| b.is_ascii_digit()) { info.numeric = plmn.into(); }
        }
    }
    Ok((mode, info))
}

fn criteria_command(criteria: &SignalCriteria) -> Result<String, RadioError> {
    if criteria.hysteresis_ms < 0 || criteria.hysteresis_db < 0
        || criteria.thresholds.windows(2).any(|v| v[0] >= v[1]) {
        return Err(RadioError::InvalidArguments);
    }
    let (rat, measurement) = match (criteria.access_network, criteria.measurement) {
        (1, 1) => (1, 0), (2, 2) => (2, 0), (2, 9) => (2, 1),
        (3, n @ 3..=5) => (4, n - 3), (4, 1) => (16, 0), (6, n @ 6..=8) => (128, n - 6),
        _ => return Err(RadioError::RequestNotSupported),
    };
    let mut command = format!("AT+ECSQ=4, {}, {}, {}, {}, {}, {}", u8::from(criteria.enabled), rat,
        measurement, criteria.hysteresis_ms, criteria.hysteresis_db, criteria.thresholds.len());
    for threshold in &criteria.thresholds { command.push_str(&format!(", {threshold}")); }
    Ok(command)
}

impl Network {
    pub fn new() -> Self {
        Self { pending: Vec::new(),
            responses: VecDeque::new(), selection: None, selection_waiters: Vec::new(), policy: Policy::default(),
            controls: VecDeque::new(), filters: VecDeque::new(), updates: VecDeque::new(), registration_rat_mapping_unverified: BTreeMap::new(), signal_criteria: Vec::new(), link_criteria: None }
    }

    pub fn next_deadline(&self) -> Option<u64> {
        self.pending.iter().map(|pending| pending.deadline)
            .chain(self.controls.iter().map(|job| job.deadline))
            .chain(self.filters.iter().map(|job| job.2)).min()
    }

    pub fn pop_update(&mut self) -> Option<(Source, Update)> { self.updates.pop_front() }
    pub fn indication_filter(&self) -> u32 { self.policy.desired.map_or(0, |input| input.filter) }

    pub fn restore(&mut self, channels: &mut Channels, deadline: u64) {
        if let Some(input) = self.policy.desired {
            self.filters.push_back((None, input.filter, deadline));
            self.start_control(channels);
        }
    }

    pub fn policy(&self) -> &Policy { &self.policy }
    pub fn pop_response(&mut self) -> Option<Response> { self.responses.pop_front() }

    fn respond(&mut self, serial: i32, result: Result<Value, RadioError>) {
        let (error, value) = match result { Ok(value) => (RadioError::None, Some(value)), Err(error) => (error, None) };
        self.responses.push_back(Response { serial, error, value });
    }

    pub fn reset(&mut self) {
        let serials: Vec<_> = self.pending.drain(..).flat_map(|p| p.serials).chain(self.selection_waiters.drain(..))
            .chain(self.controls.drain(..).filter_map(|job| job.serial)).chain(self.filters.drain(..).filter_map(|job| job.0)).collect();
        for serial in serials { self.respond(serial, Err(RadioError::RadioNotAvailable)); }
        self.selection = None;
        self.updates.clear();
        self.policy.reconnect();
    }

    pub fn submit(&mut self, serial: i32, request: Request, deadline: u64, channels: &mut Channels, model: &Model) {
        if model.radio.value == Radio::Unavailable {
            self.respond(serial, Err(RadioError::RadioNotAvailable)); return;
        }
        match request {
            Request::GetVoiceRegistrationState => self.respond(serial, Ok(Value::Registration(registration(model, Domain::Cs, &self.registration_rat_mapping_unverified)))),
            Request::GetDataRegistrationState => self.respond(serial, Ok(Value::Registration(registration(model, Domain::Ps, &self.registration_rat_mapping_unverified)))),
            Request::GetCellInfoList => {
                let mut cells = Vec::new();
                for domain in [Domain::Ps, Domain::Cs] {
                    let registration = registration(model, domain, &self.registration_rat_mapping_unverified);
                    if let Some(identity) = registration.cell_identity {
                        if !cells.iter().any(|cell: &CellInfo| cell.identity == identity) {
                            cells.push(CellInfo { registered: true, identity, signal: signal(model) });
                        }
                    }
                }
                self.respond(serial, Ok(Value::Cells(cells)));
            }
            Request::GetImsRegistrationState => self.respond(serial, Err(RadioError::RequestNotSupported)),
            Request::GetNetworkSelectionMode => {
                if self.pending.iter().any(|p| p.kind == Read::Operator) { self.selection_waiters.push(serial); }
                else { self.respond(serial, self.selection.map(Value::Selection).ok_or(RadioError::InvalidResponse)); }
            }
            Request::GetOperator => self.read(Some(serial), Read::Operator, deadline, channels),
            Request::GetSignalStrength => self.read(Some(serial), Read::Signal, deadline, channels),
            Request::SetIndicationFilter(filter) => {
                self.policy.desired = Some(Input { filter });
                self.filters.push_back((Some(serial), filter, deadline)); self.start_control(channels);
            }
            Request::SetSignalStrengthReportingCriteria(criteria) => {
                let commands = criteria.iter().map(criteria_command).collect::<Result<Vec<_>, _>>();
                self.signal_criteria = criteria;
                match commands {
                    Err(error) => self.respond(serial, Err(error)),
                    Ok(commands) => {
                        self.controls.push_back(ControlJob { serial: Some(serial), refresh: false, deadline, active: None, error: RadioError::None,
                            steps: commands.into_iter().map(|command| (1, command, None)).collect() });
                        self.start_control(channels);
                    }
                }
            }
            Request::SetLinkCapacityReportingCriteria(criteria) => {
                let invalid = criteria.hysteresis_ms < 0 || criteria.hysteresis_dl_kbps < 0 || criteria.hysteresis_ul_kbps < 0
                    || [&criteria.thresholds_dl_kbps, &criteria.thresholds_ul_kbps].iter().any(|v|
                        v.iter().any(|n| *n < 0) || v.windows(2).any(|v| v[0] >= v[1]));
                self.link_criteria = Some(criteria);
                self.respond(serial, Err(if invalid { RadioError::InvalidArguments } else { RadioError::RequestNotSupported }));
            }
        }
    }

    fn read(&mut self, serial: Option<i32>, kind: Read, deadline: u64, channels: &mut Channels) {
        if let Some(pending) = self.pending.iter_mut().find(|p| p.kind == kind) { pending.serials.extend(serial); return; }
        let (channel, text, prefix) = match kind {
            Read::Operator => (6, "AT+EOPS?", "+EOPS:"), Read::Signal => (1, "AT+ECSQ", "+ECSQ:"),
        };
        let result = Command::new(text, Some(prefix), 32, deadline)
            .and_then(|command| channels.get_mut(channel).ok_or(Error::Configuration)?.enqueue(command));
        match result {
            Ok(id) => self.pending.push(Pending { channel, id, kind, serials: serial.into_iter().collect(), deadline }),
            Err(error) => { if let Some(serial) = serial { self.respond(serial, Err(engine_error(error))); } }
        }
    }

    fn start_control(&mut self, channels: &mut Channels) {
        loop {
            if self.controls.is_empty() {
                let Some((serial, filter, deadline)) = self.filters.pop_front() else { return; };
                let desired = self.policy.desired;
                let result = self.policy.reconcile(Input { filter });
                self.policy.desired = desired;
                match result {
                    Ok(steps) => self.controls.push_back(ControlJob { serial,
                        refresh: steps.iter().any(|step| step.control == mindone_radio_state::policy::Control::Signal && step.enabled),
                        deadline, active: None, error: RadioError::None,
                        steps: steps.into_iter().map(|step| (step.channel, step.command.clone(), Some(step))).collect() }),
                    Err(_) => { if let Some(serial) = serial { self.respond(serial, Err(RadioError::InternalErr)); } continue; }
                }
            }
            let job = self.controls.front_mut().unwrap();
            if job.active.is_some() { return; }
            let Some((channel, text, step)) = job.steps.pop_front() else {
                let job = self.controls.pop_front().unwrap();
                if let Some(serial) = job.serial {
                    self.respond(serial, if job.error == RadioError::None { Ok(Value::Void) } else { Err(job.error) });
                }
                if job.error == RadioError::None && job.refresh {
                    self.read(None, Read::Signal, job.deadline, channels);
                }
                continue;
            };
            let deadline = job.deadline;
            match Command::new(&text, None, 0, deadline).and_then(|command| channels.get_mut(channel).ok_or(Error::Configuration)?.enqueue(command)) {
                Ok(id) => self.controls.front_mut().unwrap().active = Some((channel, id, step)),
                Err(error) => {
                    let job = self.controls.front_mut().unwrap();
                    job.error = engine_error(error);
                    if let Some(step) = step {
                        if let Ok(Some(fallback)) = self.policy.complete(step.id, false) {
                            job.steps.push_front((fallback.channel, fallback.command.clone(), Some(fallback)));
                        }
                    }
                }
            }
        }
    }

    pub fn on_event(&mut self, channel: u8, event: &Event, _now: u64, channels: &mut Channels, model: &Model) {
        match event {
            Event::Completed(response) => {
                let error = match &response.result { Ok(ref line) => modem_error(line), Err(error) => engine_error(*error) };
                if let Some(position) = self.pending.iter().position(|p| p.channel == channel && p.id == response.id) {
                    let pending = self.pending.remove(position);
                    let result = if error != RadioError::None { Err(error) } else { match pending.kind {
                        Read::Operator => {
                            if response.intermediates.len() != 1 { Err(RadioError::InvalidResponse) }
                            else { operator(&response.intermediates[0]).map(|(mode, info)| {
                                self.selection = Some(mode);
                                let registered = [&model.cs.value, &model.ps.value].iter().any(|value|
                                    matches!(value, Knowledge::Known(reg) if matches!(reg.state, 1 | 5)));
                                Value::Operator(if registered { info } else { OperatorInfo::default() })
                            }) }
                        }
                        Read::Signal => {
                            if response.intermediates.is_empty() || response.intermediates.iter().any(|line| parse::ecsq(line).is_err()) {
                                Err(RadioError::InvalidResponse)
                            } else {
                                let mut value = signal(model);
                                for line in &response.intermediates {
                                    if let Some(update) = observe::line(line) {
                                        if let Update::Signal(_, measurement) = &update {
                                            match measurement {
                                                Signal::Gsm(csq) => value.gsm = Some(GsmSignal { signal_strength: csq.rssi, bit_error_rate: csq.ber }),
                                                Signal::Lte { rsrp, rsrq, rssnr } => value.lte = Some(LteSignal { rsrp: *rsrp, rsrq: *rsrq, rssnr: *rssnr }),
                                                _ => (),
                                            }
                                        }
                                        self.updates.push_back((Source::Query, update));
                                    }
                                }
                                Ok(Value::Signal(value))
                            }
                        }
                    }};
                    for serial in pending.serials { self.respond(serial, result.clone()); }
                    if pending.kind == Read::Operator {
                        let selection = if let Err(error) = result { Err(error) }
                            else { self.selection.map(Value::Selection).ok_or(RadioError::InvalidResponse) };
                        for serial in std::mem::take(&mut self.selection_waiters) { self.respond(serial, selection.clone()); }
                    }
                } else if self.controls.front().and_then(|job| job.active.as_ref()).is_some_and(|active| active.0 == channel && active.1 == response.id) {
                    let job = self.controls.front_mut().unwrap();
                    let (_, _, step) = job.active.take().unwrap();
                    if let Some(step) = step {
                        match self.policy.complete(step.id, error == RadioError::None) {
                            Ok(Some(fallback)) => job.steps.push_front((fallback.channel, fallback.command.clone(), Some(fallback))),
                            Ok(None) => { if error != RadioError::None { job.error = if error == RadioError::GenericFailure { RadioError::ModemErr } else { error }; } }
                            Err(_) => job.error = RadioError::InternalErr,
                        }
                    } else if error != RadioError::None { job.error = error; }
                    self.start_control(channels);
                }
            }
            Event::Error(Error::Failed | Error::ResyncFailed) => { self.reset(); }
            _ => (),
        }
    }

}
