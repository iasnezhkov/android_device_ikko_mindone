#![forbid(unsafe_code)]
/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */


use mindone_mux_core::chat;
use mindone_mux_platform::{wait_ready, Driver, PosixEndpoint, PtyFactory, MAX_WAIT_MS};
use mindone_mux_runtime::{Effects, Error, Event, Limits, Report, State};
use std::cell::RefCell;
use std::io;
use std::rc::Rc;
use std::time::Instant;

pub const RESTART_DELAY_MS: u64 = 5000;
pub const REPORT_WAIT_MS: u64 = 5000;

#[repr(u32)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Property { ReporterRunning, ModemException, MuxReady, RilReady, AtciUnavailable, ReportCase, Report, RilRestart }

#[repr(u32)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Value { Zero, One, Two }

pub trait Properties {
    fn set(&mut self, property: Property, value: Value) -> bool;
}

struct PublicationState<P> {
    properties: P,
    initialized: bool,
    ril_started: bool,
    rejected: bool,
    restart_ril: bool,
}

pub struct Publication<P>(Rc<RefCell<PublicationState<P>>>);

impl<P> Clone for Publication<P> {
    fn clone(&self) -> Self { Self(Rc::clone(&self.0)) }
}

impl<P: Properties> PublicationState<P> {
    fn set(&mut self, property: Property, value: Value) -> bool {
        if self.rejected { return false; }
        if !self.properties.set(property, value) { self.rejected = true; return false; }
        true
    }
}

impl<P: Properties> Publication<P> {
    pub fn new(properties: P) -> Self {
        Self(Rc::new(RefCell::new(PublicationState { properties, initialized: false, ril_started: false, rejected: false, restart_ril: false })))
    }
    pub fn initialize(&mut self) -> bool {
        let mut state = self.0.borrow_mut();
        if state.initialized { return !state.rejected; }
        if !state.set(Property::ReporterRunning, Value::Zero) || !state.set(Property::ModemException, Value::Zero) { return false; }
        state.initialized = true;
        true
    }
    pub fn rejected(&self) -> bool { self.0.borrow().rejected }
    fn recover(&mut self, stage: Value) -> bool {
        let mut state = self.0.borrow_mut();
        state.initialized && state.set(Property::ReportCase, stage) && state.set(Property::Report, Value::One)
    }
}

impl<P: Properties> Effects for Publication<P> {
    fn apply(&mut self, event: Event) -> bool {
        let mut state = self.0.borrow_mut();
        if !state.initialized || state.rejected { return false; }
        match event {
            Event::Ready { .. } => {
                if state.ril_started {
                    return if state.restart_ril { state.set(Property::RilRestart, Value::One) } else { true };
                }
                if !state.set(Property::MuxReady, Value::One) || !state.set(Property::RilReady, Value::One) { return false; }
                state.ril_started = true;
                true
            }
            Event::AtciUnavailable => state.set(Property::AtciUnavailable, Value::One),
            Event::ModemException => state.set(Property::ModemException, Value::One),
        }
    }
}

pub trait Opener {
    fn open(&mut self) -> io::Result<(PosixEndpoint, PtyFactory)>;
}

pub trait Clock {
    fn now_ms(&self) -> io::Result<u64>;
}

pub struct MonotonicClock(Instant);

impl Default for MonotonicClock {
    fn default() -> Self { Self(Instant::now()) }
}

impl Clock for MonotonicClock {
    fn now_ms(&self) -> io::Result<u64> {
        self.0.elapsed().as_millis().try_into().map_err(|_| io::Error::other("Service monotonic clock overflow"))
    }
}

#[derive(Clone, Copy)]
pub struct Config {
    pub frame_limit: usize,
    pub baud_index: u8,
    pub user_build: bool,
}

impl Config {
    fn chat(self) -> chat::Config {
        chat::Config { frame_limit: self.frame_limit, baud_index: i32::from(self.baud_index), user_build: self.user_build, pin: None }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Exit { Stopped, ModemException, RecoveryRequested, PropertyRejected, UnsupportedLocalClose, InternalFailure }

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Phase { Starting, Running, Restarting { deadline: u64 }, Recovering { deadline: u64 }, Finished(Exit) }

pub struct Service<P: Properties, O: Opener, C: Clock> {
    publication: Publication<P>,
    opener: O,
    clock: C,
    config: Config,
    driver: Option<Driver<Publication<P>>>,
    phase: Phase,
    last_now: u64,
    runtime_error: Option<Error>,
    daemon: Option<(i32, i32)>,
    escalation_after: u64,
}

impl<P: Properties, O: Opener, C: Clock> Service<P, O, C> {
    pub fn new(properties: P, opener: O, clock: C, config: Config) -> io::Result<Self> {
        chat::Controller::new(config.chat(), 0).map_err(|_| io::Error::new(io::ErrorKind::InvalidInput, "Invalid service chat configuration"))?;
        let last_now = clock.now_ms()?;
        Ok(Self { publication: Publication::new(properties), opener, clock, config, driver: None,
            phase: Phase::Starting, last_now, runtime_error: None, daemon: None, escalation_after: 0 })
    }
    pub fn daemon_mode(&mut self, baud: i32, pin: i32) {
        self.daemon = Some((baud, pin));
        self.publication.0.borrow_mut().restart_ril = true;
    }
    pub fn phase(&self) -> Phase { self.phase }
    pub fn runtime_error(&self) -> Option<Error> { self.runtime_error }
    pub fn runtime_state(&self) -> Option<State> { self.driver.as_ref().map(|driver| driver.runtime().state()) }
    fn close(&mut self) {
        if let Some(mut driver) = self.driver.take() { driver.stop(); }
    }
    fn finish(&mut self, exit: Exit) {
        self.close();
        self.phase = Phase::Finished(exit);
    }
    pub fn stop(&mut self) {
        if !matches!(self.phase, Phase::Finished(_)) { self.finish(Exit::Stopped); }
    }
    fn now(&mut self) -> Option<u64> {
        match self.clock.now_ms() {
            Ok(now) if now >= self.last_now => { self.last_now = now; Some(now) }
            _ => { self.finish(Exit::InternalFailure); None }
        }
    }
    fn recovery(&mut self, now: u64) {
        self.close();
        if now.checked_add(REPORT_WAIT_MS).is_none() { self.finish(Exit::InternalFailure); return; }
        if !self.publication.recover(Value::One) { self.finish(Exit::PropertyRejected); return; }
        let Some(now) = self.now() else { return; };
        let Some(deadline) = now.checked_add(REPORT_WAIT_MS) else { self.finish(Exit::InternalFailure); return; };
        self.phase = Phase::Recovering { deadline };
    }
    fn failed(&mut self, now: u64, error: Error) {
        self.runtime_error = Some(error);
        self.close();
        if self.publication.rejected() {
            self.finish(Exit::PropertyRejected);
        } else if error == Error::Chat(chat::Error::DeviceReset) {
            let exit = if self.publication.apply(Event::ModemException) { Exit::ModemException } else { Exit::PropertyRejected };
            self.finish(exit);
        } else if error == Error::UnsupportedLocalClose {
            self.finish(Exit::UnsupportedLocalClose);
        } else if matches!(error, Error::ClockRegression | Error::ClockOverflow | Error::Configuration | Error::InvalidProgress) {
            self.finish(Exit::InternalFailure);
        } else {
            self.recovery(now);
        }
    }
    fn open(&mut self, now: u64) {
        if !self.publication.initialize() { self.finish(Exit::PropertyRejected); return; }
        let (serial, factory) = match self.opener.open() {
            Ok(parts) => parts,
            Err(_) => { self.recovery(now); return; }
        };
        let mut chat = self.config.chat();
        if let Some((baud, pin)) = self.daemon { chat.baud_index = baud; chat.pin = chat::Pin::from_c_integer(pin); }
        match Driver::new(serial, factory, self.publication.clone(), chat) {
            Ok(mut driver) => {
                if self.daemon.is_some() { driver.enable_recovery(self.escalation_after.saturating_sub(now)); }
                self.driver = Some(driver); self.phase = Phase::Running;
            }
            Err(error) => self.failed(now, error),
        }
    }
    fn observed(&mut self, now: u64, report: Report) {
        match report.state {
            State::Failed => self.failed(now, report.error.unwrap_or(Error::Io)),
            State::Closed => {
                if let Some(driver) = &self.driver {
                    self.escalation_after = self.escalation_after.max(now.saturating_add(driver.recovery_cooldown()));
                }
                self.close();
                if let Some(deadline) = now.checked_add(RESTART_DELAY_MS) { self.phase = Phase::Restarting { deadline }; }
                else { self.finish(Exit::InternalFailure); }
            }
            _ => {}
        }
    }
    pub fn step(&mut self, maximum: u64, limits: Limits) -> Phase {
        if matches!(self.phase, Phase::Finished(_)) { return self.phase; }
        if maximum > MAX_WAIT_MS { self.finish(Exit::InternalFailure); return self.phase; }
        let Some(now) = self.now() else { return self.phase; };
        match self.phase {
            Phase::Starting => self.open(now),
            Phase::Running => {
                let result = self.driver.as_mut().expect("Running service has an owned driver").step(maximum, limits);
                let Some(now) = self.now() else { return self.phase; };
                match result {
                    Ok(report) => self.observed(now, report),
                    Err(_) => self.recovery(now),
                }
            }
            Phase::Restarting { deadline } | Phase::Recovering { deadline } => {
                if now < deadline && wait_ready(&[], now, Some(deadline), maximum).is_err() {
                    self.finish(Exit::InternalFailure); return self.phase;
                }
                let Some(now) = self.now() else { return self.phase; };
                if now >= deadline {
                    match self.phase {
                        Phase::Restarting { .. } => self.open(now),
                        Phase::Recovering { .. } => {
                            let exit = if self.publication.recover(Value::Two) { Exit::RecoveryRequested } else { Exit::PropertyRejected };
                            self.finish(exit);
                        }
                        _ => {}
                    }
                }
            }
            Phase::Finished(_) => {}
        }
        self.phase
    }
}

impl<P: Properties, O: Opener, C: Clock> Drop for Service<P, O, C> {
    fn drop(&mut self) { self.close(); }
}

#[cfg(test)]
#[path = "tests/service.rs"]
mod tests;
