/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use crate::{valid_limit, Frame, POLL_FINAL, UI, UIH};

pub const SETUP_TIMEOUT_MS: u64 = 30000;
pub const TRANSMIT_RETRY_MS: u64 = 10;
const SABM: u8 = 0x2f;
const UA: u8 = 0x63;
const DM: u8 = 0x0f;
const DISC: u8 = 0x43;
const MSC: u8 = 0xe1;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Phase {
    Idle,
    Setup,
    Running,
    Closing,
    PeerClosing,
    Failed,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    Configuration,
    NotStarted,
    AlreadyStarted,
    Failed,
    ClockOverflow,
    SetupTimeout,
    EndpointUnavailable,
    EffectRejected,
    OversizedPayload,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ChannelConfig {
    pub channel: u8,
    pub atci: bool,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Channel {
    pub configured: bool,
    pub atci: bool,
    pub endpoint_present: bool,
    pub opening: bool,
    pub open: bool,
    pub disconnecting: bool,
    pub refused: bool,
    pub refusal_reported: bool,
    pub peer_paused: bool,
    pub local_paused: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Action<'a> {
    Transmit(Frame<'a>),
    CloseEndpoint(u8),
    Ready { refused_channels: usize },
    AtciUnavailable,
}

pub trait Sink {
    fn open_endpoint(&mut self, channel: u8) -> bool;
    fn pause_endpoint(&mut self, _channel: u8) -> bool { true }
    fn apply(&mut self, action: Action<'_>) -> bool;
}

#[derive(Debug, Eq, PartialEq)]
pub enum Handoff<'a> {
    None,
    Control(&'a [u8]),
    Data { channel: u8, payload: &'a [u8] },
    EndpointMissing { channel: u8, bytes: usize },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ApplicationInput {
    Forward,
    ReportRefusal,
    Discard,
}

pub struct Session {
    channels: [Channel; 64],
    order: [u8; 63],
    count: usize,
    phase: Phase,
    limit: usize,
    unresolved: usize,
    setup_done: bool,
    ready_started: bool,
    poll_final_echo: bool,
    setup_deadline: u64,
    recovery: bool,
    retry_at: [u64; 64],
    attempts: [u8; 64],
    now: u64,
    escalation_after: u64,
}

impl Session {
    pub fn new(config: &[ChannelConfig], limit: usize) -> Result<Self, Error> {
        if valid_limit(limit).is_err() || config.len() > 63 {
            return Err(Error::Configuration);
        }
        let mut channels = [Channel::default(); 64];
        channels[0].configured = true;
        let mut order = [0; 63];
        for (index, item) in config.iter().enumerate() {
            if item.channel == 0 || item.channel >= 64 || channels[item.channel as usize].configured {
                return Err(Error::Configuration);
            }
            channels[item.channel as usize].configured = true;
            channels[item.channel as usize].atci = item.atci;
            order[index] = item.channel;
        }
        Ok(Self {
            channels,
            order,
            count: config.len(),
            phase: Phase::Idle,
            limit,
            unresolved: 0,
            setup_done: false,
            ready_started: false,
            poll_final_echo: false,
            setup_deadline: 0,
            recovery: false, retry_at: [0; 64], attempts: [0; 64], now: 0, escalation_after: 0,
        })
    }

    pub fn enable_recovery(&mut self, escalation_after: u64) {
        self.recovery = true;
        self.escalation_after = escalation_after;
    }
    pub fn recovery_enabled(&self) -> bool { self.recovery }
    pub fn recovery_deadline(&self) -> Option<u64> { self.retry_at.iter().copied().filter(|at| *at != 0).min() }
    pub fn recovery_cooldown(&self, now: u64) -> u64 { self.escalation_after.saturating_sub(now) }
    pub fn disconnect_pending(&self) -> bool { self.channels.iter().any(|channel| channel.disconnecting) }
    pub fn recovery_tick(&mut self, now: u64, sink: &mut impl Sink) -> Result<bool, Error> {
        self.now = now;
        for channel in 1..64 {
            if self.retry_at[channel] == 0 || now < self.retry_at[channel] { continue; }
            if self.attempts[channel] == 3 {
                self.channels[channel].opening = false;
                if now >= self.escalation_after {
                    self.escalation_after = now.checked_add(300000).ok_or(Error::ClockOverflow)?;
                    return Ok(true);
                }
                self.retry_at[channel] = self.escalation_after;
            } else {
                self.attempts[channel] += 1;
                self.channels[channel].opening = true;
                self.transmit(channel as u8, SABM | POLL_FINAL, &[], sink)?;
                self.retry_at[channel] = now.checked_add(1000).ok_or(Error::ClockOverflow)?;
            }
        }
        Ok(false)
    }
    fn recover_channel(&mut self, channel: u8, sink: &mut impl Sink) -> Result<(), Error> {
        let index = channel as usize;
        let state = &mut self.channels[index];
        state.peer_paused = false;
        state.local_paused = false;
        state.refused = false;
        state.opening = false;
        state.disconnecting = false;
        if !sink.pause_endpoint(channel) { return Err(Error::EffectRejected); }
        if self.retry_at[index] == 0 { self.retry_at[index] = self.now.checked_add(1000).ok_or(Error::ClockOverflow)?; }
        Ok(())
    }
    pub fn phase(&self) -> Phase {
        self.phase
    }

    pub fn channel(&self, channel: u8) -> Option<Channel> {
        self.channels.get(channel as usize).copied().filter(|value| value.configured)
    }

    pub fn unresolved(&self) -> usize {
        self.unresolved
    }

    pub fn setup_done(&self) -> bool {
        self.setup_done
    }

    pub fn ready_started(&self) -> bool {
        self.ready_started
    }

    pub fn poll_final_pending(&self) -> bool {
        self.poll_final_echo
    }

    pub fn setup_deadline(&self) -> u64 {
        self.setup_deadline
    }

    fn active(&self) -> Result<(), Error> {
        match self.phase {
            Phase::Idle => Err(Error::NotStarted),
            Phase::Failed => Err(Error::Failed),
            _ => Ok(()),
        }
    }

    fn apply(&mut self, action: Action<'_>, sink: &mut impl Sink) -> Result<(), Error> {
        if !sink.apply(action) {
            self.phase = Phase::Failed;
            return Err(Error::EffectRejected);
        }
        Ok(())
    }

    fn transmit(
        &mut self,
        channel: u8,
        mut control: u8,
        payload: &[u8],
        sink: &mut impl Sink,
    ) -> Result<(), Error> {
        if payload.len() > self.limit {
            self.phase = Phase::Failed;
            return Err(Error::OversizedPayload);
        }
        if matches!(control, UI | UIH)
            && self.poll_final_echo
            && payload.first().is_some_and(|value| value & !2 == MSC)
        {
            control |= POLL_FINAL;
            self.poll_final_echo = false;
        }
        self.apply(Action::Transmit(Frame { address: channel << 2 | 3, control, payload }), sink)
    }

    pub fn begin(&mut self, now_ms: u64, sink: &mut impl Sink) -> Result<(), Error> {
        if self.phase != Phase::Idle {
            return Err(Error::AlreadyStarted);
        }
        let deadline = now_ms.checked_add(SETUP_TIMEOUT_MS).ok_or(Error::ClockOverflow)?;
        self.phase = Phase::Setup;
        self.setup_done = false;
        self.unresolved = self.count + 1;
        self.setup_deadline = deadline;
        self.channels[0].opening = true;
        self.transmit(0, SABM | POLL_FINAL, &[], sink)?;
        for index in 0..self.count {
            let channel = self.order[index];
            if !sink.open_endpoint(channel) {
                self.phase = Phase::Failed;
                return Err(Error::EndpointUnavailable);
            }
            self.channels[channel as usize].endpoint_present = true;
            self.channels[channel as usize].opening = true;
            self.transmit(channel, SABM | POLL_FINAL, &[], sink)?;
        }
        Ok(())
    }

    fn resolve(&mut self, sink: &mut impl Sink) -> Result<(), Error> {
        if self.unresolved == 0 {
            return Ok(());
        }
        self.unresolved -= 1;
        if self.unresolved == 0 && !self.setup_done {
            self.setup_done = true;
            self.phase = Phase::Running;
            if !self.ready_started {
                let refused_channels = self.channels.iter().filter(|value| value.refused).count();
                self.apply(Action::Ready { refused_channels }, sink)?;
                self.ready_started = true;
            }
        }
        Ok(())
    }

    fn close_endpoint(&mut self, channel: u8, sink: &mut impl Sink) -> Result<(), Error> {
        self.apply(Action::CloseEndpoint(channel), sink)?;
        let state = &mut self.channels[channel as usize];
        state.endpoint_present = false;
        state.peer_paused = false;
        state.local_paused = false;
        Ok(())
    }

    pub fn receive<'a>(
        &'a mut self,
        frame: Frame<'a>,
        sink: &mut impl Sink,
    ) -> Result<Handoff<'a>, Error> {
        self.active()?;
        if matches!(self.phase, Phase::Closing | Phase::PeerClosing) {
            if self.recovery && self.phase == Phase::Closing && frame.control & !POLL_FINAL == UA {
                let state = &mut self.channels[frame.channel() as usize];
                if state.disconnecting { state.disconnecting = false; state.open = false; }
            }
            return Ok(Handoff::None);
        }
        if frame.payload.len() > self.limit {
            return Err(Error::OversizedPayload);
        }
        let channel = frame.channel();
        let index = channel as usize;
        let configured = self.channels[index].configured;
        match frame.control & !POLL_FINAL {
            UI | UIH => {
                self.poll_final_echo |= frame.control & POLL_FINAL != 0;
                if channel == 0 {
                    return Ok(Handoff::Control(frame.payload));
                }
                if configured {
                    return Ok(if self.channels[index].endpoint_present && (!self.recovery || self.channels[index].open) {
                        Handoff::Data { channel, payload: frame.payload }
                    } else {
                        Handoff::EndpointMissing { channel, bytes: frame.payload.len() }
                    });
                }
            }
            UA if configured => {
                let state = &mut self.channels[index];
                if state.open {
                    if state.disconnecting {
                        state.disconnecting = false;
                        state.open = false;
                    }
                } else if state.opening {
                    state.opening = false;
                    state.open = true;
                    self.retry_at[index] = 0;
                    if self.attempts[index] == 0 { self.resolve(sink)?; }
                } else {
                    state.disconnecting = false;
                }
            }
            DM if configured => {
                if self.recovery && channel != 0 && self.retry_at[index] != 0 {
                    self.channels[index].opening = false;
                    return Ok(Handoff::None);
                }
                if self.channels[index].open {
                    self.channels[index].open = false;
                    self.channels[index].disconnecting = false;
                    if channel == 0 {
                        self.phase = Phase::PeerClosing;
                    } else {
                        if self.recovery { self.recover_channel(channel, sink)?; }
                        else { self.close_endpoint(channel, sink)?; }
                    }
                } else if channel == 0 {
                    self.phase = Phase::Closing;
                } else if self.channels[index].opening {
                    self.channels[index].opening = false;
                    self.channels[index].refused = true;
                    if self.channels[index].atci {
                        self.apply(Action::AtciUnavailable, sink)?;
                    }
                    self.resolve(sink)?;
                }
            }
            SABM => {
                if !configured {
                    self.transmit(channel, DM | POLL_FINAL, &[], sink)?;
                    return Ok(Handoff::None);
                }
                if channel != 0 && !self.channels[index].endpoint_present {
                    if !sink.open_endpoint(channel) {
                        self.transmit(channel, DM | POLL_FINAL, &[], sink)?;
                        return Ok(Handoff::None);
                    }
                    self.channels[index].endpoint_present = true;
                    self.channels[index].local_paused = false;
                    self.channels[index].peer_paused = false;
                }
                self.retry_at[index] = 0;
                self.channels[index].open = true;
                self.channels[index].disconnecting = false;
                self.channels[index].refused = false;
                self.channels[index].refusal_reported = false;
                if self.channels[index].opening {
                    self.channels[index].opening = false;
                    if self.attempts[index] == 0 { self.resolve(sink)?; }
                }
                self.transmit(channel, UA | POLL_FINAL, &[], sink)?;
            }
            DISC => {
                if !configured || !self.channels[index].open {
                    self.transmit(channel, DM | POLL_FINAL, &[], sink)?;
                    return Ok(Handoff::None);
                }
                self.channels[index].open = false;
                self.transmit(channel, UA | POLL_FINAL, &[], sink)?;
                if channel == 0 {
                    self.phase = Phase::PeerClosing;
                } else {
                    if self.recovery { self.recover_channel(channel, sink)?; }
                    else { self.close_endpoint(channel, sink)?; }
                }
            }
            _ => {}
        }
        Ok(Handoff::None)
    }

    pub fn apply_peer_signals(&mut self, channel: u8, signals: u8) -> Result<(), Error> {
        self.active()?;
        if let Some(value) = self.channels.get_mut(channel as usize) {
            if value.configured && value.open {
                value.peer_paused = signals & 2 != 0;
            }
        }
        Ok(())
    }

    pub fn can_read_endpoint(&self, channel: u8, transmit_capacity: bool) -> bool {
        matches!(self.phase, Phase::Setup | Phase::Running) && self.setup_done && self.channel(channel).is_some_and(|value| {
            value.endpoint_present && (value.open || value.refused) && !value.peer_paused && transmit_capacity
        })
    }

    pub fn application_input(&mut self, channel: u8) -> Result<ApplicationInput, Error> {
        self.active()?;
        let value = self.channels.get_mut(channel as usize).filter(|value| value.configured)
            .ok_or(Error::Configuration)?;
        if value.refused {
            let result = if value.refusal_reported {
                ApplicationInput::Discard
            } else {
                ApplicationInput::ReportRefusal
            };
            value.refusal_reported = true;
            return Ok(result);
        }
        Ok(ApplicationInput::Forward)
    }

    fn send_local_signals(&mut self, channel: u8, paused: bool, sink: &mut impl Sink) -> Result<(), Error> {
        self.transmit(0, UIH | POLL_FINAL, &[MSC | 2, 5, channel << 2 | 3, 0x8d | if paused { 2 } else { 0 }], sink)
    }

    pub fn set_local_pause(&mut self, channel: u8, paused: bool, sink: &mut impl Sink) -> Result<(), Error> {
        self.active()?;
        let value = self.channels.get_mut(channel as usize).filter(|value| value.configured && value.endpoint_present)
            .ok_or(Error::EndpointUnavailable)?;
        if value.local_paused != paused {
            value.local_paused = paused;
            self.send_local_signals(channel, paused, sink)?;
        }
        Ok(())
    }

    pub fn recreate_endpoint(&mut self, channel: u8, sink: &mut impl Sink) -> Result<(), Error> {
        self.active()?;
        if channel == 0 || self.channel(channel).is_none() {
            return Err(Error::Configuration);
        }
        let was_paused = self.channels[channel as usize].local_paused;
        self.close_endpoint(channel, sink)?;
        if was_paused {
            self.send_local_signals(channel, false, sink)?;
        }
        if !sink.open_endpoint(channel) {
            self.phase = Phase::Failed;
            return Err(Error::EndpointUnavailable);
        }
        self.channels[channel as usize].endpoint_present = true;
        Ok(())
    }

    pub fn request_disconnect(&mut self, channel: u8, sink: &mut impl Sink) -> Result<(), Error> {
        self.active()?;
        let value = self.channels.get_mut(channel as usize).filter(|value| value.configured)
            .ok_or(Error::Configuration)?;
        if value.open {
            value.disconnecting = true;
            self.transmit(channel, DISC | POLL_FINAL, &[], sink)?;
        }
        Ok(())
    }

    pub fn send_control(&mut self, control: u8, payload: &[u8], sink: &mut impl Sink) -> Result<(), Error> {
        self.active()?;
        self.transmit(0, control, payload, sink)
    }

    pub fn peer_close(&mut self) -> Result<(), Error> {
        self.active()?;
        self.phase = Phase::PeerClosing;
        Ok(())
    }

    pub fn poll_timeout(&mut self, now_ms: u64, transmit_pending: bool) -> Result<Option<u64>, Error> {
        self.active()?;
        if matches!(self.phase, Phase::Closing | Phase::PeerClosing) {
            return Ok(None);
        }
        let retry = transmit_pending.then_some(TRANSMIT_RETRY_MS);
        if !self.setup_done {
            if now_ms >= self.setup_deadline {
                self.phase = Phase::Failed;
                return Err(Error::SetupTimeout);
            }
            let remaining = self.setup_deadline - now_ms;
            return Ok(Some(retry.map_or(remaining, |value| value.min(remaining))));
        }
        Ok(retry)
    }

    pub fn reset_after_transport_close(&mut self) {
        let control_peer_paused = self.channels[0].peer_paused;
        for value in &mut self.channels {
            *value = Channel { configured: value.configured, atci: value.atci, ..Channel::default() };
        }
        self.channels[0].peer_paused = control_peer_paused;
        self.phase = Phase::Idle;
        self.unresolved = 0;
        self.setup_done = false;
        self.poll_final_echo = false;
        self.setup_deadline = 0;
        self.retry_at = [0; 64];
        self.attempts = [0; 64];
    }
}
