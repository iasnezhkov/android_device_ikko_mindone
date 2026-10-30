#![forbid(unsafe_code)]
/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */


use mindone_mux_core::chat::{self, Controller};
use mindone_mux_core::mcc::{self, Effect};
use mindone_mux_core::session::{self, Action, ApplicationInput, ChannelConfig, Handoff, Session, Sink};
use mindone_mux_core::transport::{self, EndpointId, Fault, IoResult, PendingQueue, PollEpochs, PollToken, Stop, TransmitQueue, TurnBudget};
use mindone_mux_core::{Decoder, Frame, MAX_PAYLOAD};
use std::io::{self, Read, Write};
use std::os::fd::{AsFd, AsRawFd};
use std::os::unix::net::UnixStream;

pub const MAX_CHANNELS: usize = 46;
pub const READ_BYTES: usize = 4096;
pub const CLOSE_FLUSH_MS: u64 = 500;
pub const RETRY_MS: u64 = 10;
const MAX_PENDING: usize = transport::PENDING_BYTES + MAX_PAYLOAD;

pub trait Endpoint: AsFd {
    fn prepare(&self) -> io::Result<()>;
    fn read(&mut self, output: &mut [u8]) -> IoResult;
    fn write(&mut self, input: &[u8]) -> IoResult;
}

fn io_result(result: io::Result<usize>) -> IoResult {
    match result {
        Ok(bytes) => IoResult::Transferred(bytes),
        Err(error) => IoResult::Fault(match error.kind() {
            io::ErrorKind::Interrupted => Fault::Interrupted,
            io::ErrorKind::WouldBlock => Fault::WouldBlock,
            _ => Fault::Failure,
        }),
    }
}

impl Endpoint for UnixStream {
    fn prepare(&self) -> io::Result<()> { self.set_nonblocking(true) }
    fn read(&mut self, output: &mut [u8]) -> IoResult { io_result(Read::read(self, output)) }
    fn write(&mut self, input: &[u8]) -> IoResult { io_result(Write::write(self, input)) }
}

pub trait Factory<E: Endpoint> {
    fn open(&mut self, channel: u8) -> io::Result<E>;
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Event {
    Ready { refused_channels: usize },
    AtciUnavailable,
    ModemException,
}

pub trait Effects {
    fn apply(&mut self, event: Event) -> bool;
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum State { Startup, Multiplexing, PeerClosing, Closed, Failed }

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    Configuration, Io, Capacity, InvalidProgress, ClockRegression, ClockOverflow,
    Chat(chat::Error), ChatRead(chat::Error), Session(session::Error), Mcc(mcc::Error), EffectRejected,
    UnsupportedLocalClose,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Interest {
    pub token: PollToken,
    pub readable: bool,
    pub writable: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Ready {
    pub token: PollToken,
    pub readable: bool,
    pub writable: bool,
    pub hangup: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Limits {
    pub calls: usize,
    pub bytes: usize,
}

impl Default for Limits {
    fn default() -> Self { Self { calls: transport::MAX_TURN_CALLS, bytes: transport::MAX_TURN_BYTES } }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Report {
    pub state: State,
    pub error: Option<Error>,
    pub calls: usize,
    pub bytes: usize,
    pub frames: usize,
    pub discarded: usize,
    pub stale_events: usize,
}

fn io_turn(budget: &mut TurnBudget, requested: usize, operation: impl FnOnce(usize) -> IoResult) -> Result<Option<IoResult>, Error> {
    if requested == 0 { return Ok(None); }
    let Some(permit) = budget.permit() else { return Ok(None); };
    let offered = requested.min(permit.max_bytes());
    let result = operation(offered);
    match result {
        IoResult::Transferred(accepted) => {
            if accepted > offered { return Err(Error::InvalidProgress); }
            permit.complete(accepted).map_err(|_| Error::InvalidProgress)?;
        }
        IoResult::Fault(_) => permit.complete(0).map_err(|_| Error::InvalidProgress)?,
    }
    Ok(Some(result))
}

struct Slot<E> {
    endpoint: E,
    token: PollToken,
    pending: Box<PendingQueue<MAX_PENDING>>,
}

struct Context<'a, E, F, P> {
    tx: &'a mut TransmitQueue,
    slots: &'a mut [Option<Slot<E>>; 64],
    epochs: &'a mut PollEpochs,
    factory: &'a mut F,
    effects: &'a mut P,
    frame_limit: usize,
    discarded: &'a mut usize,
}

impl<E: Endpoint, F: Factory<E>, P: Effects> Sink for Context<'_, E, F, P> {
    fn open_endpoint(&mut self, channel: u8) -> bool {
        if channel == 0 || channel >= 64 || self.slots[channel as usize].is_some() { return false; }
        let Ok(endpoint) = self.factory.open(channel) else { return false; };
        if endpoint.prepare().is_err() { return false; }
        let Ok(pending) = PendingQueue::new(self.frame_limit) else { return false; };
        let Ok(token) = self.epochs.replace(EndpointId::Channel(channel), endpoint.as_fd().as_raw_fd()) else { return false; };
        self.slots[channel as usize] = Some(Slot { endpoint, token, pending: Box::new(pending) });
        true
    }
    fn pause_endpoint(&mut self, channel: u8) -> bool {
        if let Some(slot) = &mut self.slots[channel as usize] { *self.discarded += slot.pending.clear(); }
        true
    }
    fn apply(&mut self, action: Action<'_>) -> bool {
        match action {
            Action::Transmit(frame) => self.tx.admit_frame(frame).is_ok(),
            Action::CloseEndpoint(channel) => {
                if self.epochs.invalidate(EndpointId::Channel(channel)).is_err() { return false; }
                if let Some(mut slot) = self.slots[channel as usize].take() { *self.discarded += slot.pending.clear(); }
                true
            }
            Action::Ready { refused_channels } => self.effects.apply(Event::Ready { refused_channels }),
            Action::AtciUnavailable => self.effects.apply(Event::AtciUnavailable),
        }
    }
}

struct Writer<'a, E> { endpoint: &'a mut E, pty: bool }

impl<E: Endpoint> transport::ByteWriter for Writer<'_, E> {
    fn write(&mut self, input: &[u8]) -> IoResult {
        match self.endpoint.write(input) {
            IoResult::Fault(Fault::Busy | Fault::DeviceReset | Fault::EndOfStream) if self.pty => IoResult::Fault(Fault::Failure),
            value => value,
        }
    }
}

enum Delivery { Buffered, Done, Recreate }

impl<E: Endpoint, F: Factory<E>, P: Effects> Context<'_, E, F, P> {
    fn deliver(&mut self, channel: u8, input: &[u8], budget: &mut TurnBudget) -> Result<Delivery, Error> {
        let Some(slot) = self.slots[channel as usize].as_mut() else {
            *self.discarded += input.len(); return Ok(Delivery::Done);
        };
        if !slot.pending.pending().is_empty() {
            if slot.pending.buffer_remainder(input).is_err() { *self.discarded += input.len(); }
            return Ok(Delivery::Buffered);
        }
        let mut offset = 0;
        while offset < input.len() {
            let Some(result) = io_turn(budget, input.len() - offset, |offered| slot.endpoint.write(&input[offset..offset + offered]))? else { break; };
            match result {
                IoResult::Transferred(0) | IoResult::Fault(Fault::WouldBlock) => break,
                IoResult::Transferred(bytes) => { offset += bytes; }
                IoResult::Fault(Fault::Interrupted) => {}
                IoResult::Fault(_) => { *self.discarded += input.len() - offset; return Ok(Delivery::Recreate); }
            }
        }
        if offset == input.len() { return Ok(Delivery::Done); }
        slot.pending.buffer_remainder(&input[offset..]).map_err(|_| Error::Capacity)?;
        Ok(Delivery::Buffered)
    }
}

pub struct Runtime<E: Endpoint, F: Factory<E>, P: Effects> {
    serial: Option<E>,
    serial_token: Option<PollToken>,
    slots: [Option<Slot<E>>; 64],
    factory: F,
    effects: P,
    epochs: PollEpochs,
    chat: Controller,
    session: Session,
    tx: TransmitQueue,
    decoder: Decoder<{ transport::RX_BYTES }>,
    held_tail: Vec<u8>,
    frame_limit: usize,
    state: State,
    error: Option<Error>,
    last_now: u64,
    retry_at: u64,
    close_at: Option<u64>,
    local_close_at: Option<u64>,
    cursor: u8,
    write_first: bool,
    write_wakeup: bool,
    pollout_turn: bool,
}

impl<E: Endpoint, F: Factory<E>, P: Effects> Runtime<E, F, P> {
    pub fn new(serial: E, factory: F, effects: P, channels: &[ChannelConfig], config: chat::Config, now: u64) -> Result<Self, Error> {
        if channels.len() > MAX_CHANNELS { return Err(Error::Configuration); }
        let frame_limit = config.frame_limit;
        let session = Session::new(channels, frame_limit).map_err(Error::Session)?;
        let chat = Controller::new(config, now).map_err(Error::Chat)?;
        serial.prepare().map_err(|_| Error::Io)?;
        let mut epochs = PollEpochs::default();
        let serial_token = Some(epochs.replace(EndpointId::Serial, serial.as_fd().as_raw_fd()).map_err(|_| Error::Configuration)?);
        Ok(Self {
            serial: Some(serial), serial_token, slots: std::array::from_fn(|_| None), factory, effects, epochs,
            chat, session, tx: TransmitQueue::new(frame_limit).map_err(|_| Error::Configuration)?,
            decoder: Decoder::new(frame_limit).map_err(|_| Error::Configuration)?, held_tail: Vec::new(),
            frame_limit, state: State::Startup, error: None, last_now: now, retry_at: now, close_at: None, local_close_at: None, cursor: 0, write_first: false,
            write_wakeup: false, pollout_turn: false,
        })
    }

    pub fn enable_recovery(&mut self, escalation_after: u64) { self.session.enable_recovery(escalation_after); }
    pub fn recovery_cooldown(&self) -> u64 { self.session.recovery_cooldown(self.last_now) }
    pub fn state(&self) -> State { self.state }
    pub fn error(&self) -> Option<Error> { self.error }
    pub fn effects(&self) -> &P { &self.effects }
    pub fn factory(&self) -> &F { &self.factory }
    pub fn pending_tx(&self) -> usize { self.tx.pending().len() }
    pub fn held_tail_bytes(&self) -> usize { self.held_tail.len() }
    pub fn buffered_rx_bytes(&self) -> usize { self.decoder.buffered().len() }
    pub fn next_deadline(&self) -> Option<u64> {
        let deadline = match self.state {
            State::Startup => self.chat.expires_at(),
            State::Multiplexing if self.local_close_at.is_some() => self.local_close_at,
            State::Multiplexing => {
                let setup = (!self.session.setup_done()).then_some(self.session.setup_deadline());
                [setup, self.session.recovery_deadline(), self.local_close_at].into_iter().flatten().min()
            },
            State::PeerClosing => self.close_at,
            State::Closed | State::Failed => return None,
        };
        let retry_pending = match self.state {
            State::Startup => self.chat.pending_write().is_some(),
            State::Multiplexing | State::PeerClosing => !self.tx.pending().is_empty(),
            _ => false,
        };
        if retry_pending {
            let retry = self.retry_at.max(self.last_now);
            Some(deadline.map_or(retry, |end| end.min(retry)))
        } else { deadline }
    }
    pub fn interests(&self) -> Vec<Interest> {
        let mut result = Vec::new();
        if let Some(token) = self.serial_token {
            let (readable, writable) = match self.state {
                State::Startup => (self.chat.phase() != chat::Phase::Writing && self.held_tail.len() < transport::RX_BYTES,
                    self.chat.pending_write().is_some() && self.write_wakeup && self.last_now < self.retry_at),
                State::Multiplexing => (true, !self.tx.pending().is_empty() && self.write_wakeup && self.last_now < self.retry_at),
                State::PeerClosing => (false, !self.tx.pending().is_empty() && self.write_wakeup && self.last_now < self.retry_at),
                _ => (false, false),
            };
            if readable || writable { result.push(Interest { token, readable, writable }); }
        }
        if self.state == State::Multiplexing && self.local_close_at.is_none() {
            for channel in 1..64 {
                if self.session.channel(channel as u8).is_some_and(|state| !state.open && !state.refused && self.session.recovery_enabled()) { continue; }
                if let Some(slot) = &self.slots[channel] {
                    let token = slot.token;
                    if self.epochs.is_current(token) {
                        result.push(Interest { token,
                            readable: self.session.can_read_endpoint(channel as u8, self.tx.can_accept_pty_read()),
                            writable: !slot.pending.pending().is_empty() });
                    }
                }
            }
        }
        result
    }

    fn close_all(&mut self, discarded: &mut usize) {
        self.epochs.reset(); self.serial_token = None;
        for slot in &mut self.slots {
            if let Some(mut old) = slot.take() { *discarded += old.pending.clear(); }
        }
        self.serial = None;
        *discarded += self.tx.clear() + self.decoder.reset() + self.held_tail.len();
        self.held_tail.clear();
        self.session.reset_after_transport_close();
    }

    fn fail(&mut self, error: Error, discarded: &mut usize) {
        self.error = Some(error); self.state = State::Failed;
        self.close_all(discarded);
    }

    pub fn stop(&mut self) {
        let mut discarded = 0;
        self.close_all(&mut discarded);
        self.state = State::Closed;
    }

    fn session_do(&mut self, discarded: &mut usize, operation: impl FnOnce(&mut Session, &mut Context<'_, E, F, P>) -> Result<(), Error>) -> Result<(), Error> {
        let mut context = Context { tx: &mut self.tx, slots: &mut self.slots, epochs: &mut self.epochs,
            factory: &mut self.factory, effects: &mut self.effects, frame_limit: self.frame_limit, discarded };
        operation(&mut self.session, &mut context)
    }

    fn begin_mux(&mut self, now: u64, budget: &mut TurnBudget, report: &mut Report) -> Result<(), Error> {
        self.session_do(&mut report.discarded, |session, context| session.begin(now, context).map_err(Error::Session))?;
        let descriptor = self.serial.as_ref().ok_or(Error::Io)?.as_fd().as_raw_fd();
        self.epochs.invalidate(EndpointId::Serial).map_err(|_| Error::Configuration)?;
        self.serial_token = Some(self.epochs.replace(EndpointId::Serial, descriptor).map_err(|_| Error::Configuration)?);
        self.state = State::Multiplexing;
        self.decoder.push(&self.held_tail).map_err(|_| Error::Capacity)?;
        self.held_tail.clear();
        self.frames(now, budget, report)
    }

    fn peer_close(&mut self, now: u64) -> Result<(), Error> {
        self.session.peer_close().map_err(Error::Session)?;
        self.close_at = Some(now.checked_add(CLOSE_FLUSH_MS).ok_or(Error::ClockOverflow)?);
        self.state = State::PeerClosing;
        Ok(())
    }

    fn dispatch(frame: Frame<'_>, session: &mut Session, context: &mut Context<'_, E, F, P>, budget: &mut TurnBudget) -> Result<bool, Error> {
        let mut output = [0; MAX_PAYLOAD];
        match session.receive(frame, context).map_err(Error::Session)? {
            Handoff::Control(input) => {
                let prepared = mcc::prepare(input, context.frame_limit, &mut output).map_err(Error::Mcc)?;
                if let Some(reply) = prepared.reply {
                    session.send_control(reply.control, reply.payload, context).map_err(Error::Session)?;
                }
                match prepared.effect {
                    Effect::PeerClosing => return Ok(true),
                    Effect::PeerSignals { channel, signals } => session.apply_peer_signals(channel, signals).map_err(Error::Session)?,
                    Effect::None => {}
                }
            }
            Handoff::Data { channel, payload } => match context.deliver(channel, payload, budget)? {
                Delivery::Buffered => session.set_local_pause(channel, true, context).map_err(Error::Session)?,
                Delivery::Recreate => session.recreate_endpoint(channel, context).map_err(Error::Session)?,
                Delivery::Done => {}
            },
            Handoff::EndpointMissing { bytes, .. } => *context.discarded += bytes,
            Handoff::None => {}
        }
        match session.phase() {
            session::Phase::PeerClosing => Ok(true),
            session::Phase::Closing if session.recovery_enabled() => Ok(false),
            session::Phase::Closing => Err(Error::UnsupportedLocalClose),
            _ => Ok(false),
        }
    }

    fn frames(&mut self, now: u64, budget: &mut TurnBudget, report: &mut Report) -> Result<(), Error> {
        while let Some(frame) = self.decoder.next_frame() {
            report.frames += 1;
            if self.local_close_at.is_some() && frame.control & !mindone_mux_core::POLL_FINAL != 0x63 { continue; }
            let mut context = Context { tx: &mut self.tx, slots: &mut self.slots, epochs: &mut self.epochs,
                factory: &mut self.factory, effects: &mut self.effects, frame_limit: self.frame_limit, discarded: &mut report.discarded };
            let closing = Self::dispatch(frame, &mut self.session, &mut context, budget)?;
            if closing { self.peer_close(now)?; break; }
            if self.session.phase() == session::Phase::Closing && self.session.recovery_enabled() && self.local_close_at.is_none() {
                let mut context = Context { tx: &mut self.tx, slots: &mut self.slots, epochs: &mut self.epochs,
                    factory: &mut self.factory, effects: &mut self.effects, frame_limit: self.frame_limit, discarded: &mut report.discarded };
                for channel in 1..64 {
                    if self.session.channel(channel).is_some_and(|state| state.open) {
                        self.session.request_disconnect(channel, &mut context).map_err(Error::Session)?;
                    }
                }
                self.local_close_at = Some(now.checked_add(2000).ok_or(Error::ClockOverflow)?);
                break;
            }
        }
        Ok(())
    }

    fn read_serial(&mut self, now: u64, budget: &mut TurnBudget, report: &mut Report) -> Result<(), Error> {
        if self.state == State::Startup && self.chat.phase() == chat::Phase::Writing { return Ok(()); }
        let room = if self.state == State::Startup {
            (transport::RX_BYTES - self.held_tail.len()).min(READ_BYTES)
        } else { self.decoder.available_capacity().min(READ_BYTES) };
        let mut input = [0; READ_BYTES];
        let serial = self.serial.as_mut().ok_or(Error::Io)?;
        let Some(result) = io_turn(budget, room, |offered| serial.read(&mut input[..offered]))? else { return Ok(()); };
        match result {
            IoResult::Transferred(0) => Err(Error::Io),
            IoResult::Transferred(bytes) => {
                if self.state == State::Startup {
                    let result = self.chat.feed(now, &input[..bytes]).map_err(Error::Chat)?;
                    if let Some(tail) = result.mux_tail {
                        if tail.len() > transport::RX_BYTES - self.held_tail.len() { return Err(Error::Capacity); }
                        self.held_tail.extend_from_slice(tail);
                    }
                    if self.chat.phase() == chat::Phase::Ready { self.begin_mux(now, budget, report)?; }
                    Ok(())
                } else {
                    self.decoder.push(&input[..bytes]).map_err(|_| Error::Capacity)?;
                    self.frames(now, budget, report)
                }
            }
            IoResult::Fault(Fault::Interrupted | Fault::WouldBlock) => Ok(()),
            IoResult::Fault(fault) if self.state == State::Startup => self.chat.read_fault(now,
                if fault == Fault::Busy { Fault::Failure } else { fault }).map_err(|error|
                    if fault == Fault::DeviceReset { Error::ChatRead(error) } else { Error::Chat(error) }),
            IoResult::Fault(_) => Err(Error::Io),
        }
    }

    fn write_startup(&mut self, now: u64, budget: &mut TurnBudget) -> Result<(), Error> {
        if now < self.retry_at { return Ok(()); }
        for _ in 0..8 {
            let Some(request) = self.chat.pending_write() else { break; };
            let token = request.token;
            let serial = self.serial.as_mut().ok_or(Error::Io)?;
            let Some(result) = io_turn(budget, request.bytes.len(), |offered| serial.write(&request.bytes[..offered]))? else { break; };
            match result {
                IoResult::Transferred(bytes) => {
                    self.chat.accept_written(now, token, bytes).map_err(Error::Chat)?;
                }
                IoResult::Fault(fault) => {
                    self.chat.write_fault(now, token, fault).map_err(Error::Chat)?;
                    if fault != Fault::Interrupted {
                        self.retry_at = now.checked_add(RETRY_MS).ok_or(Error::ClockOverflow)?;
                        self.write_wakeup = !self.pollout_turn;
                        break;
                    }
                }
            }
        }
        Ok(())
    }

    fn flush_tx(&mut self, now: u64, budget: &mut TurnBudget, report: &mut Report) -> Result<(), Error> {
        if self.tx.pending().is_empty() || now < self.retry_at { return Ok(()); }
        let mut writer = Writer { endpoint: self.serial.as_mut().ok_or(Error::Io)?, pty: false };
        let result = self.tx.drain(&mut writer, budget);
        report.discarded += result.discarded;
        match result.stop {
            Stop::Complete | Stop::Yield => Ok(()),
            Stop::Blocked => {
                self.retry_at = now.checked_add(RETRY_MS).ok_or(Error::ClockOverflow)?;
                self.write_wakeup = !self.pollout_turn;
                Ok(())
            }
            Stop::DeviceReset => {
                if self.effects.apply(Event::ModemException) { Ok(()) } else { Err(Error::EffectRejected) }
            }
            _ => Err(Error::Io),
        }
    }

    fn recreate(&mut self, channel: u8, report: &mut Report) -> Result<(), Error> {
        self.session_do(&mut report.discarded, |session, context| session.recreate_endpoint(channel, context).map_err(Error::Session))
    }

    fn drain_channel(&mut self, channel: u8, budget: &mut TurnBudget, report: &mut Report) -> Result<(), Error> {
        let Some(slot) = self.slots[channel as usize].as_mut() else { return Ok(()); };
        let mut writer = Writer { endpoint: &mut slot.endpoint, pty: true };
        let result = slot.pending.drain(&mut writer, budget);
        match result.stop {
            Stop::Complete => {
                if self.session.channel(channel).is_some_and(|value| value.local_paused) {
                    self.session_do(&mut report.discarded, |session, context| session.set_local_pause(channel, false, context).map_err(Error::Session))?;
                }
                Ok(())
            }
            Stop::Yield | Stop::Blocked => Ok(()),
            _ => self.recreate(channel, report),
        }
    }

    fn read_channel(&mut self, channel: u8, budget: &mut TurnBudget, report: &mut Report) -> Result<(), Error> {
        if !self.session.can_read_endpoint(channel, self.tx.can_accept_pty_read()) { return Ok(()); }
        let mut input = [0; transport::PTY_READ_BYTES];
        let slot = self.slots[channel as usize].as_mut().ok_or(Error::Io)?;
        let Some(result) = io_turn(budget, transport::PTY_READ_BYTES, |offered| slot.endpoint.read(&mut input[..offered]))? else { return Ok(()); };
        match result {
            IoResult::Transferred(0) => self.recreate(channel, report),
            IoResult::Transferred(bytes) => {
                match self.session.application_input(channel).map_err(Error::Session)? {
                    ApplicationInput::Forward => self.tx.admit_pty_input(channel, &input[..bytes]).map_err(|_| Error::Capacity),
                    _ => { report.discarded += bytes; Ok(()) }
                }
            }
            IoResult::Fault(Fault::Interrupted | Fault::WouldBlock) => Ok(()),
            IoResult::Fault(_) => self.recreate(channel, report),
        }
    }

    fn run_turn(&mut self, now: u64, events: &[Ready], budget: &mut TurnBudget, call_limit: usize, report: &mut Report) -> Result<(), Error> {
        if now < self.last_now { return Err(Error::ClockRegression); }
        if events.len() > MAX_CHANNELS + 1 { return Err(Error::Configuration); }
        self.last_now = now;
        self.pollout_turn = false;
        if self.state == State::PeerClosing && self.close_at.is_some_and(|end| now >= end) {
            self.close_all(&mut report.discarded);
            self.state = State::Closed;
            return Ok(());
        }
        if self.state == State::Startup {
            let serial_readable = events.iter().any(|event| event.readable && self.epochs.is_current(event.token)
                && event.token.endpoint() == EndpointId::Serial);
            if !(self.chat.token_pending() && serial_readable) { self.chat.tick(now).map_err(Error::Chat)?; }
            if self.chat.phase() == chat::Phase::Ready { self.begin_mux(now, budget, report)?; }
        }
        if self.state == State::Multiplexing { self.session.poll_timeout(now, !self.tx.pending().is_empty()).map_err(Error::Session)?; }
        if self.state == State::Multiplexing && self.local_close_at.is_none() {
            let mut context = Context { tx: &mut self.tx, slots: &mut self.slots, epochs: &mut self.epochs,
                factory: &mut self.factory, effects: &mut self.effects, frame_limit: self.frame_limit, discarded: &mut report.discarded };
            if self.session.recovery_tick(now, &mut context).map_err(Error::Session)? {
                for channel in 1..64 {
                    if self.session.channel(channel).is_some_and(|state| state.open) {
                        self.session.request_disconnect(channel, &mut context).map_err(Error::Session)?;
                    }
                }
                self.local_close_at = Some(now.checked_add(2000).ok_or(Error::ClockOverflow)?);
            }
        }
        if self.local_close_at.is_some_and(|end| now >= end || !self.session.disconnect_pending()) {
            self.tx.admit_frame(Frame::command(0, mindone_mux_core::UIH, &[0xc3, 1]).map_err(|_| Error::Configuration)?).map_err(|_| Error::Capacity)?;
            self.local_close_at = None;
            self.peer_close(now)?;
        }

        let mut ready: [Option<Ready>; 64] = [None; 64];
        for event in events {
            if !self.epochs.is_current(event.token) { report.stale_events += 1; continue; }
            let node = match event.token.endpoint() { EndpointId::Serial => 0, EndpointId::Channel(channel) => usize::from(channel) };
            if let Some(current) = &mut ready[node] {
                current.readable |= event.readable; current.writable |= event.writable; current.hangup |= event.hangup;
            } else { ready[node] = Some(*event); }
        }
        for offset in 0..64 {
            let node = (usize::from(self.cursor) + offset) % 64;
            let Some(event) = ready[node] else { continue; };
            if !self.epochs.is_current(event.token) { report.stale_events += 1; continue; }
            if node == 0 {
                if event.writable { self.retry_at = now; self.pollout_turn = true; }
                let early_write = event.readable && self.write_first;
                if event.readable { self.write_first = !self.write_first; }
                if early_write {
                    match self.state {
                        State::Startup => self.write_startup(now, budget)?,
                        State::Multiplexing | State::PeerClosing => { self.flush_tx(now, budget, report)?; }
                        _ => {}
                    }
                }
                if self.epochs.is_current(event.token) && event.readable && matches!(self.state, State::Startup | State::Multiplexing) {
                    self.read_serial(now, budget, report)?;
                }
                if !self.epochs.is_current(event.token) { report.stale_events += 1; continue; }
                if event.writable && !early_write {
                    match self.state {
                        State::Startup => self.write_startup(now, budget)?,
                        State::Multiplexing | State::PeerClosing => { self.flush_tx(now, budget, report)?; }
                        _ => {}
                    }
                }
                if event.hangup && !event.readable { return Err(Error::Io); }
            } else if self.state == State::Multiplexing && self.local_close_at.is_none() {
                if self.session.recovery_enabled() && self.session.channel(node as u8).is_some_and(|state| !state.open && !state.refused) { continue; }
                if event.writable { self.drain_channel(node as u8, budget, report)?; }
                if !self.epochs.is_current(event.token) { report.stale_events += 1; continue; }
                if event.readable { self.read_channel(node as u8, budget, report)?; }
                else if event.hangup { self.recreate(node as u8, report)?; }
            }
            if budget.calls() == call_limit || budget.available_bytes() == 0 { self.cursor = ((node + 1) % 64) as u8; break; }
        }
        match self.state {
            State::Startup => self.write_startup(now, budget)?,
            State::Multiplexing | State::PeerClosing => self.flush_tx(now, budget, report)?,
            _ => {}
        }
        if self.state == State::PeerClosing && (self.tx.pending().is_empty() || self.close_at.is_some_and(|end| now >= end)) {
            self.close_all(&mut report.discarded); self.state = State::Closed;
        }
        Ok(())
    }

    pub fn drive(&mut self, now: u64, events: &[Ready], limits: Limits) -> Report {
        let mut report = Report { state: self.state, error: self.error, calls: 0, bytes: 0, frames: 0, discarded: 0, stale_events: 0 };
        if matches!(self.state, State::Closed | State::Failed) { return report; }
        match TurnBudget::new(limits.calls, limits.bytes) {
            Err(_) => self.fail(Error::Configuration, &mut report.discarded),
            Ok(mut budget) => {
                if let Err(error) = self.run_turn(now, events, &mut budget, limits.calls, &mut report) { self.fail(error, &mut report.discarded); }
                report.calls = budget.calls(); report.bytes = budget.bytes();
            }
        }
        report.state = self.state; report.error = self.error;
        report
    }
}
