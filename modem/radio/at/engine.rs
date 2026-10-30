/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use crate::line::{Framer, Record, LINE_BYTES};
use std::collections::VecDeque;
use std::sync::atomic::{AtomicU64, Ordering};

pub const QUEUE_COMMANDS: usize = 64;
pub const TRANSACTION_BYTES: usize = 65536;
pub const SILENCE_MS: u64 = 300;
pub const RESYNC_TIMEOUT_MS: u64 = 5000;
pub const RESYNC_ATTEMPTS: u8 = 3;
pub const PREFIXES: usize = 64;
pub const EXTRA_FINALS: usize = 64;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    Configuration,
    QueueFull,
    ClockRegression,
    ClockOverflow,
    IdExhausted,
    StaleProgress,
    InvalidProgress,
    UnexpectedFinal,
    Timeout,
    LineTooLarge,
    TransactionTooLarge,
    TooManyLines,
    ResyncFailed,
    Failed,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Quarantine {
    Silence,
    Writing,
    Reading,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Phase {
    Idle,
    Writing,
    Reading,
    Quarantine(Quarantine),
    Failed,
}

pub struct Command {
    text: String,
    expected_prefix: Option<String>,
    extra_finals: Vec<String>,
    max_lines: usize,
    deadline_ms: u64,
}

fn valid_text(text: &str) -> bool {
    !text.is_empty() && text.len() <= LINE_BYTES && text.bytes().all(|byte| (0x20..=0x7e).contains(&byte))
}

impl Command {
    pub fn new(text: &str, expected_prefix: Option<&str>, max_lines: usize, deadline_ms: u64) -> Result<Self, Error> {
        if !valid_text(text) || !text.starts_with("AT") || max_lines > TRANSACTION_BYTES
            || expected_prefix.is_some_and(|prefix| !valid_text(prefix)) {
            return Err(Error::Configuration);
        }
        Ok(Self {
            text: text.into(), expected_prefix: expected_prefix.map(String::from),
            extra_finals: Vec::new(), max_lines, deadline_ms,
        })
    }

    pub fn with_extra_finals(mut self, finals: &[&str]) -> Result<Self, Error> {
        if finals.len() > EXTRA_FINALS || finals.iter().any(|line| !valid_text(line) || *line == self.text) {
            return Err(Error::Configuration);
        }
        self.extra_finals = finals.iter().map(|line| String::from(*line)).collect();
        Ok(self)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CommandId(u64);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct WriteToken(u64);

pub struct WriteRequest<'a> {
    pub token: WriteToken,
    pub bytes: &'a [u8],
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Response {
    pub id: CommandId,
    pub intermediates: Vec<String>,
    pub result: Result<String, Error>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Event {
    Completed(Response),
    Urc { line: String, registered: bool },
    Error(Error),
    Resynchronized,
}

struct Pending {
    id: CommandId,
    command: Command,
    intermediates: Vec<String>,
    received: usize,
}

static NEXT_WRITE: AtomicU64 = AtomicU64::new(0);

pub struct Engine {
    phase: Phase,
    last_now: u64,
    next_id: u64,
    queue: VecDeque<Pending>,
    active: Option<Pending>,
    prefixes: Vec<String>,
    late_finals: Vec<String>,
    framer: Framer,
    transmit: Vec<u8>,
    accepted: usize,
    token: Option<WriteToken>,
    quiet_since: u64,
    resync_deadline: u64,
    resync_attempts: u8,
    resync_intermediate: bool,
}

fn standard_final(line: &str) -> bool {
    matches!(line, "OK" | "ERROR" | "NO CARRIER") || ["+CME ERROR:", "+CMS ERROR:"].iter().any(|prefix| {
        line.strip_prefix(prefix).is_some_and(|value| {
            let value = value.trim();
            !value.is_empty() && value.bytes().all(|byte| byte.is_ascii_digit())
        })
    })
}

impl Engine {
    pub fn new(now_ms: u64, urc_prefixes: &[&str]) -> Result<Self, Error> {
        if urc_prefixes.len() > PREFIXES || urc_prefixes.iter().any(|prefix| !valid_text(prefix)) {
            return Err(Error::Configuration);
        }
        Ok(Self {
            phase: Phase::Idle, last_now: now_ms, next_id: 0,
            queue: VecDeque::new(), active: None,
            prefixes: urc_prefixes.iter().map(|prefix| String::from(*prefix)).collect(),
            late_finals: Vec::new(), framer: Framer::new(), transmit: Vec::new(),
            accepted: 0, token: None, quiet_since: now_ms, resync_deadline: 0, resync_attempts: 0,
            resync_intermediate: false,
        })
    }

    pub fn phase(&self) -> Phase { self.phase }
    pub fn queued(&self) -> usize { self.queue.len() }
    pub fn is_quarantined(&self) -> bool { matches!(self.phase, Phase::Quarantine(_)) }

    pub fn enqueue(&mut self, command: Command) -> Result<CommandId, Error> {
        if self.phase == Phase::Failed { return Err(Error::Failed); }
        if self.queue.len() == QUEUE_COMMANDS { return Err(Error::QueueFull); }
        self.next_id = self.next_id.checked_add(1).ok_or(Error::IdExhausted)?;
        let id = CommandId(self.next_id);
        self.queue.push_back(Pending { id, command, intermediates: Vec::new(), received: 0 });
        Ok(id)
    }

    pub fn pending_write(&self) -> Option<WriteRequest<'_>> {
        self.token.map(|token| WriteRequest { token, bytes: &self.transmit[self.accepted..] })
    }

    fn complete(pending: Pending, result: Result<String, Error>, emit: &mut impl FnMut(Event)) {
        emit(Event::Completed(Response { id: pending.id, intermediates: pending.intermediates, result }));
    }

    fn clear_write(&mut self) {
        self.token = None;
        self.transmit.clear();
        self.accepted = 0;
    }

    fn fail(&mut self, error: Error, emit: &mut impl FnMut(Event)) -> Result<(), Error> {
        self.phase = Phase::Failed;
        self.clear_write();
        self.framer.clear();
        if let Some(pending) = self.active.take() { Self::complete(pending, Err(error), emit); }
        while let Some(pending) = self.queue.pop_front() { Self::complete(pending, Err(error), emit); }
        emit(Event::Error(error));
        Err(error)
    }

    fn clock(&mut self, now_ms: u64, emit: &mut impl FnMut(Event)) -> Result<(), Error> {
        if self.phase == Phase::Failed { return Err(Error::Failed); }
        if now_ms < self.last_now { return self.fail(Error::ClockRegression, emit); }
        self.last_now = now_ms;
        Ok(())
    }

    fn quarantine(&mut self, now_ms: u64, error: Error, emit: &mut impl FnMut(Event)) {
        if let Some(pending) = self.active.take() {
            self.late_finals = pending.command.extra_finals.clone();
            Self::complete(pending, Err(error), emit);
        }
        self.phase = Phase::Quarantine(Quarantine::Silence);
        self.clear_write();
        self.quiet_since = now_ms;
        self.resync_attempts = 0;
        self.resync_intermediate = false;
        emit(Event::Error(error));
    }

    fn expire(&mut self, now_ms: u64, emit: &mut impl FnMut(Event)) -> Result<(), Error> {
        if self.active.as_ref().is_some_and(|pending| now_ms >= pending.command.deadline_ms) {
            self.quarantine(now_ms, Error::Timeout, emit);
        }
        if matches!(self.phase, Phase::Quarantine(Quarantine::Writing | Quarantine::Reading))
            && now_ms >= self.resync_deadline {
            return self.fail(Error::ResyncFailed, emit);
        }
        let count = self.queue.len();
        for _ in 0..count {
            let pending = self.queue.pop_front().unwrap();
            if now_ms >= pending.command.deadline_ms { Self::complete(pending, Err(Error::Timeout), emit); }
            else { self.queue.push_back(pending); }
        }
        Ok(())
    }

    fn write(&mut self, bytes: Vec<u8>, emit: &mut impl FnMut(Event)) -> Result<(), Error> {
        let mut current = NEXT_WRITE.load(Ordering::Relaxed);
        let token = loop {
            let Some(next) = current.checked_add(1) else { return self.fail(Error::IdExhausted, emit); };
            match NEXT_WRITE.compare_exchange_weak(current, next, Ordering::Relaxed, Ordering::Relaxed) {
                Ok(_) => break WriteToken(next),
                Err(value) => current = value,
            }
        };
        self.transmit = bytes;
        self.accepted = 0;
        self.token = Some(token);
        Ok(())
    }

    pub fn tick(&mut self, now_ms: u64, mut emit: impl FnMut(Event)) -> Result<(), Error> {
        self.clock(now_ms, &mut emit)?;
        self.expire(now_ms, &mut emit)?;
        if self.phase == Phase::Quarantine(Quarantine::Silence) && now_ms - self.quiet_since >= SILENCE_MS {
            if self.resync_attempts == RESYNC_ATTEMPTS { return self.fail(Error::ResyncFailed, &mut emit); }
            self.resync_deadline = match now_ms.checked_add(RESYNC_TIMEOUT_MS) {
                Some(deadline) => deadline,
                None => return self.fail(Error::ClockOverflow, &mut emit),
            };
            self.resync_attempts += 1;
            self.resync_intermediate = false;
            self.framer.clear();
            self.phase = Phase::Quarantine(Quarantine::Writing);
            self.write(b"AT+CMEE?\r".to_vec(), &mut emit)?;
        } else if self.phase == Phase::Idle && self.framer.buffered_bytes() == 0 {
            if let Some(pending) = self.queue.pop_front() {
                let mut bytes = pending.command.text.as_bytes().to_vec();
                bytes.push(b'\r');
                self.active = Some(pending);
                self.phase = Phase::Writing;
                self.write(bytes, &mut emit)?;
            }
        }
        Ok(())
    }

    pub fn accept_written(&mut self, now_ms: u64, token: WriteToken, bytes: usize, mut emit: impl FnMut(Event)) -> Result<(), Error> {
        self.clock(now_ms, &mut emit)?;
        let timed_out = self.token == Some(token)
            && self.active.as_ref().is_some_and(|pending| now_ms >= pending.command.deadline_ms);
        self.expire(now_ms, &mut emit)?;
        if timed_out { return Err(Error::Timeout); }
        if self.token != Some(token) { return self.fail(Error::StaleProgress, &mut emit); }
        if bytes == 0 || bytes > self.transmit.len() - self.accepted {
            return self.fail(Error::InvalidProgress, &mut emit);
        }
        self.accepted += bytes;
        if self.accepted == self.transmit.len() {
            self.phase = match self.phase {
                Phase::Writing => Phase::Reading,
                Phase::Quarantine(Quarantine::Writing) => Phase::Quarantine(Quarantine::Reading),
                _ => return self.fail(Error::InvalidProgress, &mut emit),
            };
            self.clear_write();
        }
        Ok(())
    }

    fn urc(&self, line: String, emit: &mut impl FnMut(Event)) {
        let registered = self.prefixes.iter().any(|prefix| line.starts_with(prefix));
        emit(Event::Urc { line, registered });
    }

    fn record(&mut self, now_ms: u64, line: String, emit: &mut impl FnMut(Event)) -> Result<(), Error> {
        if self.is_quarantined() {
            let terminal = standard_final(&line) || self.late_finals.contains(&line)
                || self.queue.iter().any(|pending| pending.command.extra_finals.contains(&line));
            if terminal {
                if self.phase == Phase::Quarantine(Quarantine::Reading) && self.resync_intermediate && line == "OK" {
                    self.phase = Phase::Idle;
                    self.late_finals.clear();
                    self.resync_intermediate = false;
                    emit(Event::Resynchronized);
                } else {
                    self.phase = Phase::Quarantine(Quarantine::Silence);
                    self.clear_write();
                    self.quiet_since = now_ms;
                    self.resync_intermediate = false;
                }
            } else if self.phase == Phase::Quarantine(Quarantine::Reading)
                && line.strip_prefix("+CMEE:").is_some_and(|value| {
                    let value = value.trim();
                    !value.is_empty() && value.bytes().all(|byte| byte.is_ascii_digit())
                }) {
                self.resync_intermediate = true;
            } else if line != "AT+CMEE?" { self.urc(line, emit); }
            return Ok(());
        }
        if let Some(pending) = self.active.as_ref() {
            if line == pending.command.text { return Ok(()); }
            let terminal = standard_final(&line) || pending.command.extra_finals.contains(&line);
            let intermediate = pending.command.expected_prefix.as_ref().is_some_and(|prefix| line.starts_with(prefix));
            if terminal || intermediate {
                if self.phase != Phase::Reading {
                    if terminal { self.quarantine(now_ms, Error::UnexpectedFinal, emit); } else { self.urc(line, emit); }
                    return Ok(());
                }
                if terminal {
                    let pending = self.active.take().unwrap();
                    self.phase = Phase::Idle;
                    Self::complete(pending, Ok(line), emit);
                } else {
                    let pending = self.active.as_mut().unwrap();
                    if pending.intermediates.len() == pending.command.max_lines {
                        self.quarantine(now_ms, Error::TooManyLines, emit);
                    } else { pending.intermediates.push(line); }
                }
                return Ok(());
            }
        }
        if self.active.is_none() && (standard_final(&line)
            || self.queue.iter().any(|pending| pending.command.extra_finals.contains(&line))) {
            self.quarantine(now_ms, Error::UnexpectedFinal, emit);
            return Ok(());
        }
        self.urc(line, emit);
        Ok(())
    }

    pub fn feed(&mut self, now_ms: u64, input: &[u8], mut emit: impl FnMut(Event)) -> Result<(), Error> {
        self.clock(now_ms, &mut emit)?;
        self.expire(now_ms, &mut emit)?;
        for &byte in input {
            if self.is_quarantined() { self.quiet_since = now_ms; }
            if let Some(pending) = self.active.as_mut() {
                pending.received += 1;
                if pending.received > TRANSACTION_BYTES {
                    self.quarantine(now_ms, Error::TransactionTooLarge, &mut emit);
                }
            }
            match self.framer.push(byte) {
                Some(Record::Line(line)) => self.record(now_ms, line, &mut emit)?,
                Some(Record::TooLarge) => self.quarantine(now_ms, Error::LineTooLarge, &mut emit),
                None => (),
            }
        }
        Ok(())
    }
}
