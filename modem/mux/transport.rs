/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use crate::{encode, next_epoch, valid_limit, Decoder, Frame, MAX_OVERHEAD, UIH};
use core::sync::atomic::AtomicU64;

pub const TX_BYTES: usize = 65536;
pub const RX_BYTES: usize = 65536;
pub const PENDING_BYTES: usize = 4096;
pub const PTY_READ_BYTES: usize = 1024;
pub const MAX_TURN_CALLS: usize = 64;
pub const MAX_TURN_BYTES: usize = RX_BYTES;
pub const AT_TIMEOUT_MS: u64 = 5000;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    Configuration,
    Capacity,
    OversizedPayload,
    Channel,
    InvalidProgress,
    ClockOverflow,
    Descriptor,
    EpochExhausted,
}

pub struct ByteQueue<const CAPACITY: usize> {
    bytes: [u8; CAPACITY],
    start: usize,
    end: usize,
    limit: usize,
}

impl<const CAPACITY: usize> ByteQueue<CAPACITY> {
    pub fn new(limit: usize) -> Result<Self, Error> {
        if limit == 0 || limit > CAPACITY {
            return Err(Error::Configuration);
        }
        Ok(Self { bytes: [0; CAPACITY], start: 0, end: 0, limit })
    }

    pub fn pending(&self) -> &[u8] {
        &self.bytes[self.start..self.end]
    }

    pub fn available(&self) -> usize {
        self.limit - self.pending().len()
    }

    fn compact(&mut self) {
        self.bytes.copy_within(self.start..self.end, 0);
        self.end -= self.start;
        self.start = 0;
    }

    pub fn append(&mut self, input: &[u8]) -> Result<(), Error> {
        if input.len() > self.available() {
            return Err(Error::Capacity);
        }
        self.compact();
        self.bytes[self.end..self.end + input.len()].copy_from_slice(input);
        self.end += input.len();
        Ok(())
    }

    pub fn accept_written(&mut self, bytes: usize) -> Result<(), Error> {
        if bytes > self.pending().len() {
            return Err(Error::InvalidProgress);
        }
        self.start += bytes;
        if self.start == self.end {
            self.start = 0;
            self.end = 0;
        }
        Ok(())
    }

    pub fn clear(&mut self) -> usize {
        let discarded = self.pending().len();
        self.start = 0;
        self.end = 0;
        discarded
    }
}

pub struct TransmitQueue {
    bytes: ByteQueue<TX_BYTES>,
    frame_limit: usize,
}

fn wire_length(payload: usize) -> usize {
    payload + if payload < 128 { 6 } else { MAX_OVERHEAD }
}

impl TransmitQueue {
    pub fn new(frame_limit: usize) -> Result<Self, Error> {
        valid_limit(frame_limit).map_err(|_| Error::Configuration)?;
        Ok(Self { bytes: ByteQueue::new(TX_BYTES)?, frame_limit })
    }

    pub fn pending(&self) -> &[u8] {
        self.bytes.pending()
    }

    pub fn available(&self) -> usize {
        self.bytes.available()
    }

    pub fn clear(&mut self) -> usize {
        self.bytes.clear()
    }

    pub fn admit_frame(&mut self, frame: Frame<'_>) -> Result<(), Error> {
        if frame.payload.len() > self.frame_limit {
            return Err(Error::OversizedPayload);
        }
        if wire_length(frame.payload.len()) > self.available() {
            return Err(Error::Capacity);
        }
        self.bytes.compact();
        let end = self.bytes.end;
        let length = encode(frame, self.frame_limit, &mut self.bytes.bytes[end..self.bytes.limit])
            .map_err(|_| Error::Configuration)?;
        self.bytes.end += length;
        Ok(())
    }

    pub fn can_accept_pty_read(&self) -> bool {
        let reservation = PTY_READ_BYTES + (PTY_READ_BYTES / self.frame_limit + 1) * MAX_OVERHEAD;
        self.available() >= reservation
    }

    pub fn admit_pty_input(&mut self, channel: u8, input: &[u8]) -> Result<(), Error> {
        if channel == 0 || channel >= 64 {
            return Err(Error::Channel);
        }
        if input.len() > PTY_READ_BYTES {
            return Err(Error::OversizedPayload);
        }
        let required: usize = input.chunks(self.frame_limit).map(|chunk| wire_length(chunk.len())).sum();
        if required > self.available() {
            return Err(Error::Capacity);
        }
        for chunk in input.chunks(self.frame_limit) {
            self.admit_frame(Frame::command(channel, UIH, chunk).map_err(|_| Error::Channel)?)?;
        }
        Ok(())
    }

    pub fn drain(&mut self, writer: &mut impl ByteWriter, budget: &mut TurnBudget) -> TurnReport {
        drain(&mut self.bytes, writer, budget)
    }
}

pub struct PendingQueue<const CAPACITY: usize> {
    bytes: ByteQueue<CAPACITY>,
    frame_limit: usize,
}

impl<const CAPACITY: usize> PendingQueue<CAPACITY> {
    pub fn new(frame_limit: usize) -> Result<Self, Error> {
        valid_limit(frame_limit).map_err(|_| Error::Configuration)?;
        Ok(Self { bytes: ByteQueue::new(PENDING_BYTES + frame_limit)?, frame_limit })
    }

    pub fn pending(&self) -> &[u8] {
        self.bytes.pending()
    }

    pub fn buffer_remainder(&mut self, input: &[u8]) -> Result<(), Error> {
        if input.len() > self.frame_limit {
            return Err(Error::OversizedPayload);
        }
        if !self.pending().is_empty() && self.pending().len() + input.len() > PENDING_BYTES {
            return Err(Error::Capacity);
        }
        self.bytes.append(input)
    }

    pub fn clear(&mut self) -> usize {
        self.bytes.clear()
    }

    pub fn drain(&mut self, writer: &mut impl ByteWriter, budget: &mut TurnBudget) -> TurnReport {
        drain(&mut self.bytes, writer, budget)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Fault {
    Interrupted,
    WouldBlock,
    Busy,
    EndOfStream,
    DeviceReset,
    Failure,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum IoResult {
    Transferred(usize),
    Fault(Fault),
}

pub trait ByteWriter {
    fn write(&mut self, input: &[u8]) -> IoResult;
}

pub trait ByteReader {
    fn read(&mut self, output: &mut [u8]) -> IoResult;
}

pub trait FrameSink {
    fn frame(&mut self, frame: Frame<'_>) -> bool;
}

#[derive(Debug, Eq, PartialEq)]
pub struct TurnBudget {
    call_limit: usize,
    byte_limit: usize,
    calls: usize,
    bytes: usize,
}

impl TurnBudget {
    pub fn new(calls: usize, bytes: usize) -> Result<Self, Error> {
        if calls == 0 || calls > MAX_TURN_CALLS || bytes == 0 || bytes > MAX_TURN_BYTES {
            return Err(Error::Configuration);
        }
        Ok(Self { call_limit: calls, byte_limit: bytes, calls: 0, bytes: 0 })
    }

    pub fn calls(&self) -> usize {
        self.calls
    }

    pub fn bytes(&self) -> usize {
        self.bytes
    }

    pub fn available_bytes(&self) -> usize {
        self.byte_limit - self.bytes
    }

    pub fn permit(&mut self) -> Option<IoPermit<'_>> {
        if !self.take_call() {
            return None;
        }
        let max_bytes = self.available_bytes();
        self.bytes += max_bytes;
        Some(IoPermit { budget: self, max_bytes })
    }

    fn take_call(&mut self) -> bool {
        if self.calls == self.call_limit || self.bytes == self.byte_limit {
            return false;
        }
        self.calls += 1;
        true
    }
}

pub struct IoPermit<'a> {
    budget: &'a mut TurnBudget,
    max_bytes: usize,
}

impl IoPermit<'_> {
    pub fn max_bytes(&self) -> usize {
        self.max_bytes
    }

    pub fn complete(self, accepted: usize) -> Result<(), Error> {
        if accepted > self.max_bytes {
            return Err(Error::InvalidProgress);
        }
        self.budget.bytes -= self.max_bytes - accepted;
        Ok(())
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Stop {
    Complete,
    Yield,
    Blocked,
    EndOfStream,
    DeviceReset,
    Failure,
    NoProgress,
    InvalidProgress,
    Rejected,
    InvalidBuffer,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TurnReport {
    pub stop: Stop,
    pub calls: usize,
    pub bytes: usize,
    pub pending: usize,
    pub discarded: usize,
    pub frames: usize,
}

fn report(stop: Stop, before: (usize, usize), after: &TurnBudget, pending: usize, discarded: usize, frames: usize) -> TurnReport {
    TurnReport { stop, calls: after.calls - before.0, bytes: after.bytes - before.1, pending, discarded, frames }
}

fn drain<const CAPACITY: usize>(queue: &mut ByteQueue<CAPACITY>, writer: &mut impl ByteWriter, budget: &mut TurnBudget) -> TurnReport {
    let before = (budget.calls, budget.bytes);
    let stop = loop {
        if queue.pending().is_empty() {
            break Stop::Complete;
        }
        if !budget.take_call() {
            break Stop::Yield;
        }
        let offered = queue.pending().len().min(budget.available_bytes());
        match writer.write(&queue.pending()[..offered]) {
            IoResult::Transferred(0) => break Stop::NoProgress,
            IoResult::Transferred(bytes) => {
                if bytes > offered {
                    break Stop::InvalidProgress;
                }
                if queue.accept_written(bytes).is_err() {
                    break Stop::InvalidProgress;
                }
                budget.bytes += bytes;
            }
            IoResult::Fault(Fault::Interrupted) => {}
            IoResult::Fault(Fault::WouldBlock | Fault::Busy) => break Stop::Blocked,
            IoResult::Fault(Fault::DeviceReset) => {
                let discarded = queue.clear();
                return report(Stop::DeviceReset, before, budget, 0, discarded, 0);
            }
            IoResult::Fault(Fault::EndOfStream) => break Stop::EndOfStream,
            IoResult::Fault(Fault::Failure) => break Stop::Failure,
        }
    };
    report(stop, before, budget, queue.pending().len(), 0, 0)
}

pub struct Receiver {
    decoder: Decoder<RX_BYTES>,
}

impl Receiver {
    pub fn new(frame_limit: usize) -> Result<Self, Error> {
        Ok(Self { decoder: Decoder::new(frame_limit).map_err(|_| Error::Configuration)? })
    }

    pub fn pending(&self) -> &[u8] {
        self.decoder.buffered()
    }

    pub fn reset(&mut self) -> usize {
        self.decoder.reset()
    }

    pub fn read_turn(&mut self, reader: &mut impl ByteReader, scratch: &mut [u8], sink: &mut impl FrameSink, budget: &mut TurnBudget) -> TurnReport {
        let before = (budget.calls, budget.bytes);
        let mut frames = 0;
        if scratch.is_empty() || scratch.len() > RX_BYTES {
            return report(Stop::InvalidBuffer, before, budget, self.pending().len(), 0, 0);
        }
        let stop = loop {
            if !budget.take_call() {
                break Stop::Yield;
            }
            let offered = scratch.len().min(self.decoder.available_capacity()).min(budget.available_bytes());
            if offered == 0 {
                break Stop::InvalidBuffer;
            }
            match reader.read(&mut scratch[..offered]) {
                IoResult::Transferred(0) | IoResult::Fault(Fault::EndOfStream) => break Stop::EndOfStream,
                IoResult::Transferred(bytes) => {
                    if bytes > offered {
                        break Stop::InvalidProgress;
                    }
                    budget.bytes += bytes;
                    if self.decoder.push(&scratch[..bytes]).is_err() {
                        break Stop::InvalidProgress;
                    }
                    let mut accepted = true;
                    while let Some(frame) = self.decoder.next_frame() {
                        if !sink.frame(frame) {
                            accepted = false;
                            break;
                        }
                        frames += 1;
                    }
                    if !accepted {
                        break Stop::Rejected;
                    }
                }
                IoResult::Fault(Fault::Interrupted) => {}
                IoResult::Fault(Fault::WouldBlock | Fault::Busy) => break Stop::Blocked,
                IoResult::Fault(Fault::DeviceReset) => break Stop::DeviceReset,
                IoResult::Fault(Fault::Failure) => break Stop::Failure,
            }
        };
        let discarded = if matches!(stop, Stop::Yield | Stop::Blocked) { 0 } else { self.reset() };
        report(stop, before, budget, self.pending().len(), discarded, frames)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Deadline {
    expires_at: u64,
}

impl Deadline {
    pub fn after(now_ms: u64, duration_ms: u64) -> Result<Self, Error> {
        Ok(Self { expires_at: now_ms.checked_add(duration_ms).ok_or(Error::ClockOverflow)? })
    }

    pub fn at_command(now_ms: u64) -> Result<Self, Error> {
        Self::after(now_ms, AT_TIMEOUT_MS)
    }

    pub fn expires_at(&self) -> u64 {
        self.expires_at
    }

    pub fn remaining(&self, now_ms: u64) -> Option<u64> {
        self.expires_at.checked_sub(now_ms).filter(|value| *value != 0)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EndpointId {
    Serial,
    Channel(u8),
}

impl EndpointId {
    fn slot(self) -> Result<usize, Error> {
        match self {
            Self::Serial => Ok(0),
            Self::Channel(channel) if channel > 0 && channel < 64 => Ok(channel as usize),
            _ => Err(Error::Channel),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PollToken {
    endpoint: EndpointId,
    descriptor: i32,
    generation: u64,
}

impl PollToken {
    pub fn endpoint(&self) -> EndpointId {
        self.endpoint
    }

    pub fn descriptor(&self) -> i32 {
        self.descriptor
    }

    pub fn generation(&self) -> u64 {
        self.generation
    }
}

static NEXT_GENERATION: AtomicU64 = AtomicU64::new(0);

pub struct PollEpochs {
    entries: [Option<PollToken>; 64],
}

impl Default for PollEpochs {
    fn default() -> Self {
        Self { entries: [None; 64] }
    }
}

impl PollEpochs {
    pub fn replace(&mut self, endpoint: EndpointId, descriptor: i32) -> Result<PollToken, Error> {
        let slot = endpoint.slot()?;
        if descriptor < 0 {
            return Err(Error::Descriptor);
        }
        self.entries[slot] = None;
        let generation = next_epoch(&NEXT_GENERATION).ok_or(Error::EpochExhausted)?;
        let token = PollToken { endpoint, descriptor, generation };
        self.entries[slot] = Some(token);
        Ok(token)
    }

    pub fn invalidate(&mut self, endpoint: EndpointId) -> Result<(), Error> {
        self.entries[endpoint.slot()?] = None;
        Ok(())
    }

    pub fn reset(&mut self) {
        self.entries.fill(None);
    }

    pub fn is_current(&self, token: PollToken) -> bool {
        token.endpoint.slot().ok().is_some_and(|slot| self.entries[slot] == Some(token))
    }
}
