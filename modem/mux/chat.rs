/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use crate::transport::{Deadline, Fault};
use crate::{next_epoch, valid_limit};
use core::sync::atomic::AtomicU64;

pub const CHAT_BYTES: usize = 4096;
pub const COMMAND_BYTES: usize = 128;
pub const COMMAND_TIMEOUT_MS: u64 = 5000;
pub const READY_WAIT_MS: u64 = 20000;
pub const USER_READY_WAIT_MS: u64 = 10000;
pub const SETTLE_MS: u64 = 1000;
pub const TOKEN_SETTLE_MS: u64 = 20;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    Configuration,
    InvalidPin,
    ClockOverflow,
    ClockRegression,
    EpochExhausted,
    Failed,
    UnexpectedState,
    StaleProgress,
    InvalidProgress,
    NoProgress,
    LateProgress,
    Timeout,
    Rejected,
    InputTooLarge,
    RecordTooLarge,
    InvalidRecord,
    EndOfStream,
    DeviceReset,
    IoFailure,
    Cancelled,
}

pub struct Pin {
    digits: [u8; 10],
    length: usize,
}

impl Pin {
    pub fn from_c_integer(value: i32) -> Option<Self> {
        if value < 0 { return None; }
        let mut digits = [b'0'; 10];
        let mut value = value as u32;
        let mut length = 0;
        while value != 0 || length < 4 {
            digits[9 - length] = b'0' + (value % 10) as u8;
            value /= 10;
            length += 1;
        }
        digits.copy_within(10 - length..10, 0);
        Some(Self { digits, length })
    }

    pub fn new(input: &[u8]) -> Result<Self, Error> {
        if !(4..=8).contains(&input.len()) || !input.iter().all(u8::is_ascii_digit) {
            return Err(Error::InvalidPin);
        }
        let mut digits = [0; 10];
        digits[..input.len()].copy_from_slice(input);
        Ok(Self { digits, length: input.len() })
    }
}

pub struct Config {
    pub frame_limit: usize,
    pub baud_index: i32,
    pub user_build: bool,
    pub pin: Option<Pin>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Command {
    At,
    AtRetry,
    Reset,
    EchoOff,
    Pin,
    Cmux,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Phase {
    WaitingReady,
    Writing,
    Reading,
    Settling,
    Ready,
    Failed,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct WriteToken(u64);

pub struct WriteRequest<'a> {
    pub token: WriteToken,
    pub bytes: &'a [u8],
}

pub struct Feed<'a> {
    pub consumed: usize,
    pub mux_tail: Option<&'a [u8]>,
}

#[derive(Clone, Copy)]
enum Record {
    Other,
    Ready,
    Ok,
    Rejected,
    CmuxReady,
}

static NEXT_WRITE: AtomicU64 = AtomicU64::new(0);

pub struct Controller {
    phase: Phase,
    command: Option<Command>,
    deadline: Deadline,
    last_now: u64,
    pin: Option<Pin>,
    frame_limit: usize,
    baud_index: i32,
    indication_seen: bool,
    cmux_ready: bool,
    token: Option<WriteToken>,
    transmit: [u8; COMMAND_BYTES],
    transmit_length: usize,
    accepted: usize,
    record: [u8; CHAT_BYTES],
    record_length: usize,
    line_end: bool,
    token_until: Option<u64>,
    error: Option<Error>,
}

fn line_end_bytes(input: &[u8]) -> usize {
    input.iter().take_while(|byte| matches!(byte, b'\r' | b'\n')).count()
}

fn token_boundary(token: &[u8], next: u8) -> bool {
    !(0x20..=0x7e).contains(&next) || (token == b"+CMUX: READY" && matches!(next, b'O' | b'E'))
}

impl Controller {
    pub fn new(config: Config, now_ms: u64) -> Result<Self, Error> {
        valid_limit(config.frame_limit).map_err(|_| Error::Configuration)?;
        let wait = if config.user_build { USER_READY_WAIT_MS } else { READY_WAIT_MS };
        let deadline = Deadline::after(now_ms, wait).map_err(|_| Error::ClockOverflow)?;
        Ok(Self {
            phase: Phase::WaitingReady, command: None, deadline, last_now: now_ms,
            pin: config.pin, frame_limit: config.frame_limit, baud_index: config.baud_index,
            indication_seen: false, cmux_ready: false, token: None,
            transmit: [0; COMMAND_BYTES], transmit_length: 0, accepted: 0,
            record: [0; CHAT_BYTES], record_length: 0, line_end: false, token_until: None, error: None,
        })
    }

    pub fn phase(&self) -> Phase { self.phase }
    pub fn command(&self) -> Option<Command> { self.command }
    pub fn error(&self) -> Option<Error> { self.error }
    pub fn indication_seen(&self) -> bool { self.indication_seen }
    pub fn cmux_ready_seen(&self) -> bool { self.cmux_ready }
    pub fn buffered_bytes(&self) -> usize { self.record_length }
    pub fn token_pending(&self) -> bool { self.token_until.is_some() }

    pub fn expires_at(&self) -> Option<u64> {
        let at = self.token_until.map_or(self.deadline.expires_at(), |until| until.min(self.deadline.expires_at()));
        (!matches!(self.phase, Phase::Ready | Phase::Failed)).then_some(at)
    }

    pub fn pending_write(&self) -> Option<WriteRequest<'_>> {
        (self.phase == Phase::Writing).then(|| WriteRequest {
            token: self.token.unwrap(), bytes: &self.transmit[self.accepted..self.transmit_length],
        })
    }

    fn fail<T>(&mut self, error: Error) -> Result<T, Error> {
        self.phase = Phase::Failed;
        self.error = Some(error);
        self.token = None;
        self.transmit.fill(0);
        self.transmit_length = 0;
        self.accepted = 0;
        self.record.fill(0);
        self.record_length = 0;
        self.token_until = None;
        self.pin = None;
        Err(error)
    }

    fn clock(&mut self, now_ms: u64) -> Result<(), Error> {
        if self.phase == Phase::Failed { return Err(Error::Failed); }
        if now_ms < self.last_now { return self.fail(Error::ClockRegression); }
        self.last_now = now_ms;
        Ok(())
    }

    fn append(&mut self, value: &[u8]) -> Result<(), Error> {
        if value.len() > COMMAND_BYTES - self.transmit_length {
            return self.fail(Error::Configuration);
        }
        self.transmit[self.transmit_length..self.transmit_length + value.len()].copy_from_slice(value);
        self.transmit_length += value.len();
        Ok(())
    }

    fn decimal(&mut self, mut value: usize) -> Result<(), Error> {
        let mut digits = [0; 10];
        let mut start = digits.len();
        loop {
            start -= 1;
            digits[start] = b'0' + (value % 10) as u8;
            value /= 10;
            if value == 0 { break; }
        }
        self.append(&digits[start..])
    }

    fn start(&mut self, command: Command, now_ms: u64) -> Result<(), Error> {
        let deadline = match Deadline::after(now_ms, COMMAND_TIMEOUT_MS) {
            Ok(value) => value,
            Err(_) => return self.fail(Error::ClockOverflow),
        };
        let token = match next_epoch(&NEXT_WRITE) {
            Some(value) => WriteToken(value),
            None => return self.fail(Error::EpochExhausted),
        };
        self.transmit.fill(0);
        self.transmit_length = 0;
        self.accepted = 0;
        self.record_length = 0;
        match command {
            Command::At | Command::AtRetry => self.append(b"AT")?,
            Command::Reset => self.append(b"ATZ")?,
            Command::EchoOff => self.append(b"ATE0")?,
            Command::Pin => {
                let Some(pin) = self.pin.take() else { return self.fail(Error::Configuration); };
                self.append(b"AT+CPIN=")?;
                self.append(&pin.digits[..pin.length])?;
            }
            Command::Cmux => {
                self.append(b"AT+CMUX=0,0,")?;
                if self.baud_index < 0 { self.append(b"-")?; }
                self.decimal(self.baud_index.unsigned_abs() as usize)?;
                self.append(b",")?;
                self.decimal(self.frame_limit)?;
            }
        }
        self.append(b"\r")?;
        self.deadline = deadline;
        self.command = Some(command);
        self.token = Some(token);
        self.phase = Phase::Writing;
        Ok(())
    }

    pub fn tick(&mut self, now_ms: u64) -> Result<(), Error> {
        self.clock(now_ms)?;
        if let Some(until) = self.token_until {
            if now_ms < until { return Ok(()); }
            self.token_until = None;
            self.record(now_ms)?;
            if matches!(self.phase, Phase::Settling | Phase::Ready) { self.line_end = true; }
            return Ok(());
        }
        if self.deadline.remaining(now_ms).is_some() { return Ok(()); }
        match self.phase {
            Phase::WaitingReady => self.start(Command::At, now_ms),
            Phase::Reading if self.command == Some(Command::At) => self.start(Command::AtRetry, now_ms),
            Phase::Reading | Phase::Writing => self.fail(Error::Timeout),
            Phase::Settling => { self.phase = Phase::Ready; Ok(()) }
            Phase::Ready => Ok(()),
            Phase::Failed => Err(Error::Failed),
        }
    }

    pub fn accept_written(&mut self, now_ms: u64, token: WriteToken, bytes: usize) -> Result<(), Error> {
        self.clock(now_ms)?;
        if self.phase != Phase::Writing { return self.fail(Error::UnexpectedState); }
        if Some(token) != self.token { return self.fail(Error::StaleProgress); }
        if bytes == 0 { return self.fail(Error::NoProgress); }
        if bytes > self.transmit_length - self.accepted { return self.fail(Error::InvalidProgress); }
        if self.deadline.remaining(now_ms).is_none() { return self.fail(Error::LateProgress); }
        self.accepted += bytes;
        if self.accepted == self.transmit_length {
            self.phase = Phase::Reading;
            self.token = None;
            self.transmit.fill(0);
            self.transmit_length = 0;
            self.accepted = 0;
            self.record_length = 0;
        }
        Ok(())
    }

    pub fn write_fault(&mut self, now_ms: u64, token: WriteToken, fault: Fault) -> Result<(), Error> {
        self.clock(now_ms)?;
        if self.phase != Phase::Writing { return self.fail(Error::UnexpectedState); }
        if Some(token) != self.token { return self.fail(Error::StaleProgress); }
        match fault {
            Fault::Interrupted | Fault::WouldBlock | Fault::Busy => self.tick(now_ms),
            Fault::EndOfStream => self.fail(Error::EndOfStream),
            Fault::DeviceReset => self.fail(Error::DeviceReset),
            Fault::Failure => self.fail(Error::IoFailure),
        }
    }

    pub fn read_fault(&mut self, now_ms: u64, fault: Fault) -> Result<(), Error> {
        self.clock(now_ms)?;
        match fault {
            Fault::Interrupted | Fault::WouldBlock | Fault::Busy => self.tick(now_ms),
            Fault::EndOfStream => self.fail(Error::EndOfStream),
            Fault::DeviceReset => self.fail(Error::DeviceReset),
            Fault::Failure => self.fail(Error::IoFailure),
        }
    }

    pub fn cancel(&mut self) -> Result<(), Error> { self.fail(Error::Cancelled) }

    fn record(&mut self, now_ms: u64) -> Result<(), Error> {
        let record = match &self.record[..self.record_length] {
            b"OK" => Record::Ok,
            b"ERROR" => Record::Rejected,
            b"+EIND: 128" => Record::Ready,
            b"+CMUX: READY" => Record::CmuxReady,
            _ => Record::Other,
        };
        self.record_length = 0;
        if self.phase == Phase::WaitingReady && matches!(record, Record::Ready) {
            self.indication_seen = true;
            return self.start(Command::At, now_ms);
        }
        if self.phase != Phase::Reading { return Ok(()); }
        match record {
            Record::CmuxReady if self.command == Some(Command::Cmux) => { self.cmux_ready = true; Ok(()) }
            Record::Rejected if self.command == Some(Command::At) => self.start(Command::AtRetry, now_ms),
            Record::Rejected => self.fail(Error::Rejected),
            Record::Ok => match self.command {
                Some(Command::At | Command::AtRetry) => self.start(Command::Reset, now_ms),
                Some(Command::Reset) => self.start(Command::EchoOff, now_ms),
                Some(Command::EchoOff) => self.start(if self.pin.is_some() { Command::Pin } else { Command::Cmux }, now_ms),
                Some(Command::Pin) => self.start(Command::Cmux, now_ms),
                Some(Command::Cmux) => {
                    if self.cmux_ready {
                        self.phase = Phase::Ready;
                    } else {
                        self.deadline = match Deadline::after(now_ms, SETTLE_MS) {
                            Ok(value) => value,
                            Err(_) => return self.fail(Error::ClockOverflow),
                        };
                        self.phase = Phase::Settling;
                    }
                    Ok(())
                }
                None => self.fail(Error::UnexpectedState),
            },
            _ => Ok(()),
        }
    }

    fn unframed_cmux_result(&self) -> bool {
        self.phase == Phase::Reading && self.command == Some(Command::Cmux)
            && matches!(&self.record[..self.record_length], b"OK" | b"ERROR" | b"+CMUX: READY")
    }

    fn hand_off<'a>(&mut self, input: &'a [u8], start: usize) -> Feed<'a> {
        let rest = &input[start..];
        let skipped = if self.line_end { line_end_bytes(rest) } else { 0 };
        if skipped < rest.len() { self.line_end = false; }
        let consumed = start + skipped;
        Feed { consumed, mux_tail: Some(&input[consumed..]) }
    }

    fn complete(&mut self, now_ms: u64) -> Result<bool, Error> {
        self.record(now_ms)?;
        let done = matches!(self.phase, Phase::Settling | Phase::Ready);
        if done { self.line_end = true; }
        Ok(done)
    }

    pub fn feed<'a>(&mut self, now_ms: u64, input: &'a [u8]) -> Result<Feed<'a>, Error> {
        self.clock(now_ms)?;
        if let (Some(_), Some(&first)) = (self.token_until, input.first()) {
            self.token_until = None;
            if token_boundary(&self.record[..self.record_length], first) && self.complete(now_ms)? {
                return Ok(self.hand_off(input, 0));
            }
        }
        self.tick(now_ms)?;
        if matches!(self.phase, Phase::Settling | Phase::Ready) { return Ok(self.hand_off(input, 0)); }
        if input.len() > CHAT_BYTES { return self.fail(Error::InputTooLarge); }
        for (index, byte) in input.iter().copied().enumerate() {
            let complete = if byte == b'\r' || byte == b'\n' {
                self.record_length != 0
            } else if (0x20..=0x7e).contains(&byte) {
                if self.record_length == CHAT_BYTES { return self.fail(Error::RecordTooLarge); }
                self.record[self.record_length] = byte;
                self.record_length += 1;
                if !self.unframed_cmux_result() {
                    false
                } else if let Some(&next) = input.get(index + 1) {
                    token_boundary(&self.record[..self.record_length], next)
                } else {
                    match now_ms.checked_add(TOKEN_SETTLE_MS) {
                        Some(until) => self.token_until = Some(until),
                        None => return self.fail(Error::ClockOverflow),
                    }
                    false
                }
            } else {
                false
            };
            if complete && self.complete(now_ms)? { return Ok(self.hand_off(input, index + 1)); }
        }
        Ok(Feed { consumed: input.len(), mux_tail: None })
    }
}
