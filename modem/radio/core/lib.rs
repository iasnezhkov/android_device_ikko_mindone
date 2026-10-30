/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#![forbid(unsafe_code)]
#![crate_name = "mindone_radio_core"]

use mindone_radio_at::{engine::{Command, CommandId, Error, Event, WriteToken}, line::{Framer, Record}, Channels};
use mindone_radio_state::{model::*, observe, parse, policy::{Policy, SIGNAL_STRENGTH, FULL_NETWORK_STATE}};
use mindone_radio_network::{service::Network, types as network};
use mindone_radio_modem::{service::Modem, types as modem};
use std::collections::{BTreeMap, VecDeque};

pub use modem::RadioError;
pub const CHANNELS: [u8; 4] = [1, 3, 6, 8];
pub const REQUEST_LIMIT: usize = 1024;
pub const COMMAND_TIMEOUT_MS: u64 = 5000;
pub const BOOT_TIMEOUT_MS: u64 = 30000;
const PREFIXES: &[&str] = &["+EREG:", "+EGREG:", "+ECSQ:", "+ESIMS:", "+EUSIM:", "+CPIN:", "+ECPIN:", "+EIND:", "+ESLOTSINFO:"];

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Request { Network(network::Request), Modem(modem::Request) }

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Value { Network(network::Value), Modem(modem::Value) }

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Indication {
    NetworkStateChanged, CurrentSignalStrength(network::SignalStrength), RadioStateChanged(Radio),
    SimStatusChanged, ModemReset { generation: u64 },
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Output {
    Response { serial: i32, error: RadioError, value: Option<Value> },
    Indication(Indication),
}

struct Invocation { serial: i32, deadline: u64, waiting: Option<Request> }
struct BootStep { channel: u8, text: &'static str, prefix: Option<&'static str>, required: bool }

pub struct Core {
    channels: Channels, notification: Framer,
    model: Model, network: Network, modem: Modem,
    available: bool, ready: bool, now: u64, next_id: i32,
    invocations: BTreeMap<i32, Invocation>, outputs: VecDeque<Output>,
    boot: VecDeque<BootStep>, boot_active: Option<(u8, CommandId, BootStep)>, boot_deadline: Option<u64>,
    internal_deadlines: Vec<(u8, CommandId, u64)>, last_eind: Option<parse::Eind>,
}

fn network_error(error: network::RadioError) -> RadioError {
    match error {
        network::RadioError::None => RadioError::None,
        network::RadioError::RadioNotAvailable => RadioError::RadioNotAvailable,
        network::RadioError::GenericFailure => RadioError::GenericFailure,
        network::RadioError::RequestNotSupported => RadioError::RequestNotSupported,
        network::RadioError::InternalErr => RadioError::InternalErr,
        network::RadioError::ModemErr => RadioError::ModemErr,
        network::RadioError::InvalidArguments => RadioError::InvalidArguments,
        network::RadioError::InvalidResponse => RadioError::InvalidResponse,
    }
}

impl Core {
    pub fn new(now: u64) -> Result<Self, Error> {
        let mut core = Self {
            channels: Channels::new(now, &CHANNELS, PREFIXES)?, notification: Framer::new(),
            model: Model::new(0), network: Network::new(), modem: Modem::new(),
            available: true, ready: false, now, next_id: 0, invocations: BTreeMap::new(), outputs: VecDeque::new(),
            boot: VecDeque::new(), boot_active: None, boot_deadline: None,
            internal_deadlines: Vec::new(), last_eind: None,
        };
        core.begin_boot(now)?;
        core.pump(now)?;
        Ok(core)
    }

    pub fn last_eind(&self) -> Option<&parse::Eind> { self.last_eind.as_ref() }
    pub fn model(&self) -> &Model { &self.model }
    pub fn policy(&self) -> &Policy { self.network.policy() }
    pub fn is_ready(&self) -> bool { self.ready }

    fn clock(&mut self, now: u64) -> Result<(), Error> {
        if now < self.now { return Err(Error::ClockRegression); }
        self.now = now;
        Ok(())
    }

    fn indicate(&mut self, indication: Indication) { self.outputs.push_back(Output::Indication(indication)); }

    fn apply(&mut self, source: Source, update: Update, now: u64) {
        let old = match &update {
            Update::Registration(Domain::Cs, _) => Some(self.model.cs.value.clone()),
            Update::Registration(Domain::Ps, _) => Some(self.model.ps.value.clone()), _ => None,
        };
        let essential = match (&update, old) {
            (Update::Registration(_, next), Some(Knowledge::Known(previous))) =>
                previous.state != next.state || previous.reject_cause != next.reject_cause
                    || previous.emergency_status_unverified != next.emergency_status_unverified,
            _ => true,
        };
        let changed = self.model.apply(mindone_radio_state::model::Event {
            generation: self.model.generation, observed_ms: now, source, update,
        });
        let filter = self.network.indication_filter();
        match changed {
            Some(Change::Registration(_)) if essential || filter & FULL_NETWORK_STATE != 0 =>
                self.indicate(Indication::NetworkStateChanged),
            Some(Change::Signal(_)) if filter & SIGNAL_STRENGTH != 0 =>
                self.indicate(Indication::CurrentSignalStrength(self.signal())),
            Some(Change::Radio) => self.indicate(Indication::RadioStateChanged(self.model.radio.value)),
            Some(Change::Sim) => self.indicate(Indication::SimStatusChanged),
            _ => (),
        }
    }

    fn signal(&self) -> network::SignalStrength {
        let mut signal = network::SignalStrength::default();
        let mut latest_lte = None;
        for observation in self.model.signals.values() {
            match &observation.value {
                Signal::Gsm(value) => signal.gsm = Some(network::GsmSignal { signal_strength: value.rssi, bit_error_rate: value.ber }),
                Signal::Lte { rsrp, rsrq, rssnr } if latest_lte.is_none_or(|time| observation.observed_ms >= time) => {
                    latest_lte = Some(observation.observed_ms);
                    signal.lte = Some(network::LteSignal { rsrp: *rsrp, rsrq: *rsrq, rssnr: *rssnr });
                }
                _ => (),
            }
        }
        signal
    }

    fn begin_boot(&mut self, now: u64) -> Result<(), Error> {
        self.boot_deadline = Some(now.checked_add(BOOT_TIMEOUT_MS).ok_or(Error::ClockOverflow)?);
        self.boot = [
            (8, "ATE0Q0V1", None, true), (8, "AT+CMEE=1", None, true),
            (8, "AT+CMER=1,0,0,2,0", None, true), (8, "AT+ICCID=1", None, false),
            (8, "AT+ESIMS=1", None, true), (8, "AT+ESIMS", Some("+ESIMS:"), true),
            (8, "AT+EFUN=0", None, true), (1, "AT+EREG=3", None, true),
            (1, "AT+EGREG=3,1", None, true), (1, "AT+PSBEARER=1", None, false),
            (1, "AT+ECSQ=1", None, true),
        ].into_iter().map(|(channel, text, prefix, required)| BootStep { channel, text, prefix, required }).collect();
        self.advance_boot(now)
    }

    fn advance_boot(&mut self, now: u64) -> Result<(), Error> {
        if let Some(step) = self.boot.pop_front() {
            let deadline = now.checked_add(COMMAND_TIMEOUT_MS).ok_or(Error::ClockOverflow)?;
            let command = Command::new(step.text, step.prefix, 32, deadline)?;
            let id = self.channels.get_mut(step.channel).ok_or(Error::Configuration)?.enqueue(command)?;
            self.internal_deadlines.push((step.channel, id, deadline));
            self.boot_active = Some((step.channel, id, step));
        } else {
            self.boot_active = None;
            self.boot_deadline = None;
            self.ready = true;
            self.apply(Source::Transport, Update::Radio(Radio::Off), now);
            self.network.restore(&mut self.channels, now.checked_add(COMMAND_TIMEOUT_MS).ok_or(Error::ClockOverflow)?);
            self.collect(now);
            let ids: Vec<_> = self.invocations.keys().copied().collect();
            for id in ids {
                if let Some(request) = self.invocations.get_mut(&id).and_then(|invocation| invocation.waiting.take()) {
                    self.execute(id, request, now);
                }
            }
        }
        Ok(())
    }

    pub fn submit(&mut self, request: Request, serial: i32, now: u64) {
        let error = if self.clock(now).is_err() { Some(RadioError::InvalidArguments) }
            else if !self.available { Some(RadioError::RadioNotAvailable) }
            else if self.invocations.len() >= REQUEST_LIMIT || self.next_id == i32::MAX { Some(RadioError::InternalErr) }
            else { None };
        if let Some(error) = error {
            self.outputs.push_back(Output::Response { serial, error, value: None }); return;
        }
        self.next_id += 1;
        let id = self.next_id;
        let Some(deadline) = now.checked_add(if self.ready { COMMAND_TIMEOUT_MS } else { BOOT_TIMEOUT_MS }) else {
            self.outputs.push_back(Output::Response { serial, error: RadioError::InternalErr, value: None }); return;
        };
        self.invocations.insert(id, Invocation { serial, deadline, waiting: None });
        if self.ready { self.execute(id, request, now); }
        else { self.invocations.get_mut(&id).unwrap().waiting = Some(request); }
        self.collect(now);
        if self.available { let _ = self.pump(now); }
    }

    fn execute(&mut self, id: i32, request: Request, now: u64) {
        let Some(deadline) = now.checked_add(COMMAND_TIMEOUT_MS) else {
            self.finish(id, RadioError::InternalErr, None); return;
        };
        self.invocations.get_mut(&id).unwrap().deadline = deadline;
        match request {
            Request::Network(request) => {
                self.network.submit(id, request, deadline, &mut self.channels, &self.model);
            }
            Request::Modem(request) => {
                self.modem.submit(id, request, deadline, &mut self.channels, &self.model);
            }
        }
        self.collect(now);
    }

    fn finish(&mut self, id: i32, error: RadioError, value: Option<Value>) {
        if let Some(invocation) = self.invocations.remove(&id) {
            self.outputs.push_back(Output::Response { serial: invocation.serial, error, value });
        }
    }

    fn collect(&mut self, now: u64) {
        while let Some((source, update)) = self.network.pop_update() { self.apply(source, update, now); }
        while let Some((source, update)) = self.modem.pop_update() { self.apply(source, update, now); }
        while let Some(response) = self.network.pop_response() {
            self.finish(response.serial, network_error(response.error), response.value.map(Value::Network));
        }
        while let Some(response) = self.modem.pop_response() {
            self.finish(response.serial, response.error, response.value.map(Value::Modem));
        }
    }

    fn dispatch(&mut self, channel: u8, event: Event, now: u64) -> Result<(), Error> {
        if !self.available { return Ok(()); }
        if let Event::Urc { line, .. } = &event {
            if let Ok(eind) = parse::eind(line) { self.last_eind = Some(eind); }
            if let Some(update) = observe::line(line) { self.apply(Source::Urc, update, now); }
        }
        let boot_completed = if let Event::Completed(response) = &event {
            self.internal_deadlines.retain(|entry| entry.0 != channel || entry.1 != response.id);
            if self.boot_active.as_ref().is_some_and(|active| active.0 == channel && active.1 == response.id) {
                for line in &response.intermediates {
                    if let Some(update) = observe::line(line) { self.apply(Source::Query, update, now); }
                }
                Some((self.boot_active.take().unwrap().2, response.result.as_deref() == Ok("OK"),
                    response.result.as_deref() == Ok("ERROR")))
            } else { None }
        } else { None };
        self.network.on_event(channel, &event, now, &mut self.channels, &self.model);
        self.collect(now);
        self.modem.on_event(channel, &event, now, &mut self.channels, &self.model);
        self.collect(now);
        if matches!(event, Event::Error(_)) { self.on_reset(now)?; return Ok(()); }
        if let Some((step, success, plain_error)) = boot_completed {
            if !success {
                if step.text == "AT+EGREG=3,1" && plain_error {
                    self.boot.push_front(BootStep { channel: 1, text: "AT+EGREG=3", prefix: None, required: true });
                } else if step.required { self.on_reset(now)?; return Ok(()); }
            }
            self.advance_boot(now)?;
        }
        Ok(())
    }

    fn pump(&mut self, now: u64) -> Result<(), Error> {
        if !self.available { return Ok(()); }
        for channel in CHANNELS {
            let mut events = Vec::new();
            let result = self.channels.get_mut(channel).ok_or(Error::Configuration)?.tick(now, |event| events.push(event));
            for event in events { self.dispatch(channel, event, now)?; }
            if result.is_err() && self.available { self.on_reset(now)?; }
            if !self.available { return Ok(()); }
        }
        Ok(())
    }

    pub fn pending_writes(&self) -> Vec<(u8, WriteToken, Vec<u8>)> {
        if !self.available { return Vec::new(); }
        self.channels.ids().filter_map(|channel|
            self.channels.get(channel)?.pending_write().map(|write| (channel, write.token, write.bytes.to_vec()))).collect()
    }

    pub fn on_readable(&mut self, channel: u8, bytes: &[u8], now: u64) -> Result<(), Error> {
        self.clock(now)?;
        if !self.available { return Ok(()); }
        self.expire(now)?;
        if !self.available { return Ok(()); }
        if channel == 0 {
            for byte in bytes {
                if let Some(Record::Line(line)) = self.notification.push(*byte) {
                    self.dispatch(0, Event::Urc { line, registered: true }, now)?;
                }
            }
        } else {
            let mut events = Vec::new();
            let result = self.channels.get_mut(channel).ok_or(Error::Configuration)?.feed(now, bytes, |event| events.push(event));
            for event in events { self.dispatch(channel, event, now)?; }
            if result.is_err() && self.available { self.on_reset(now)?; }
        }
        self.pump(now)
    }

    pub fn on_written(&mut self, channel: u8, token: WriteToken, n: usize, now: u64) -> Result<(), Error> {
        self.clock(now)?;
        let engine = self.channels.get(channel).ok_or(Error::Configuration)?;
        if !self.available || engine.pending_write().is_none_or(|write| write.token != token) {
            return Err(Error::StaleProgress);
        }
        self.expire(now)?;
        if !self.available { return Err(Error::Timeout); }
        let mut events = Vec::new();
        let result = self.channels.get_mut(channel).ok_or(Error::Configuration)?.accept_written(now, token, n, |event| events.push(event));
        for event in events { self.dispatch(channel, event, now)?; }
        if result.is_err() && self.available { self.on_reset(now)?; }
        result
    }

    pub fn next_deadline(&self) -> Option<u64> {
        if !self.available { return None; }
        self.invocations.values().map(|invocation| invocation.deadline)
            .chain(self.boot_deadline).chain(self.internal_deadlines.iter().map(|entry| entry.2))
            .chain(self.network.next_deadline()).chain(self.modem.next_deadline()).min()
    }

    fn expire(&mut self, now: u64) -> Result<(), Error> {
        if self.next_deadline().is_some_and(|deadline| now >= deadline) { self.on_reset(now)?; }
        Ok(())
    }

    pub fn tick(&mut self, now: u64) -> Result<(), Error> {
        self.clock(now)?;
        self.expire(now)?;
        self.pump(now)
    }

    pub fn on_reset(&mut self, now: u64) -> Result<(), Error> {
        self.clock(now)?;
        if !self.available { return Ok(()); }
        self.available = false;
        self.ready = false;
        let generation = self.model.generation.checked_add(1).ok_or(Error::IdExhausted)?;
        let ids: Vec<_> = self.invocations.keys().copied().collect();
        for id in ids { self.finish(id, RadioError::RadioNotAvailable, None); }
        self.model = Model::new(generation);
        self.network.reset();
        self.modem.reset();
        self.collect(now);
        self.boot.clear(); self.boot_active = None; self.boot_deadline = None;
        self.internal_deadlines.clear();
        self.notification.clear(); self.last_eind = None;
        self.indicate(Indication::ModemReset { generation });
        self.indicate(Indication::RadioStateChanged(Radio::Unavailable));
        Ok(())
    }

    pub fn on_reconnect(&mut self, now: u64) -> Result<(), Error> {
        self.clock(now)?;
        if self.available { return Err(Error::Configuration); }
        self.channels = Channels::new(now, &CHANNELS, PREFIXES)?;
        self.available = true;
        self.begin_boot(now)?;
        self.pump(now)
    }

    pub fn drain(&mut self) -> Vec<Output> { self.outputs.drain(..).collect() }
}
