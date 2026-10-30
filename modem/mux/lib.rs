#![no_std]
/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#![forbid(unsafe_code)]

use core::sync::atomic::{AtomicU64, Ordering};

pub mod session;
pub mod transport;
pub mod mcc;
pub mod chat;

pub const FLAG: u8 = 0xf9;
pub const POLL_FINAL: u8 = 0x10;
pub const UI: u8 = 0x03;
pub const UIH: u8 = 0xef;
pub const MAX_PAYLOAD: usize = 32767;
pub const MAX_OVERHEAD: usize = 7;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    InvalidLimit,
    InvalidChannel,
    OversizedPayload,
    InsufficientCapacity,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Frame<'a> {
    pub address: u8,
    pub control: u8,
    pub payload: &'a [u8],
}

impl<'a> Frame<'a> {
    pub fn command(channel: u8, control: u8, payload: &'a [u8]) -> Result<Self, Error> {
        if channel >= 64 {
            return Err(Error::InvalidChannel);
        }
        Ok(Self {
            address: channel << 2 | 3,
            control,
            payload,
        })
    }

    pub fn channel(&self) -> u8 {
        self.address >> 2
    }
}

fn valid_limit(limit: usize) -> Result<(), Error> {
    if !(1..=MAX_PAYLOAD).contains(&limit) {
        return Err(Error::InvalidLimit);
    }
    Ok(())
}

fn next_epoch(counter: &AtomicU64) -> Option<u64> {
    let mut current = counter.load(Ordering::Relaxed);
    loop {
        let next = current.checked_add(1)?;
        match counter.compare_exchange_weak(current, next, Ordering::Relaxed, Ordering::Relaxed) {
            Ok(_) => return Some(next),
            Err(actual) => current = actual,
        }
    }
}

fn crc_update(mut crc: u8, bytes: &[u8]) -> u8 {
    for byte in bytes {
        crc ^= byte;
        for _ in 0..8 {
            crc = if crc & 1 != 0 {
                crc >> 1 ^ 0xe0
            } else {
                crc >> 1
            };
        }
    }
    crc
}

pub fn encode(frame: Frame<'_>, limit: usize, output: &mut [u8]) -> Result<usize, Error> {
    valid_limit(limit)?;
    let length = frame.payload.len();
    if length > limit {
        return Err(Error::OversizedPayload);
    }
    let header_length = if length < 128 { 4 } else { 5 };
    let total = header_length + length + 2;
    if output.len() < total {
        return Err(Error::InsufficientCapacity);
    }
    output[0] = FLAG;
    output[1] = frame.address;
    output[2] = frame.control;
    output[3] = (length << 1) as u8;
    if length < 128 {
        output[3] |= 1;
    } else {
        output[4] = (length >> 7) as u8;
    }
    output[header_length..header_length + length].copy_from_slice(frame.payload);
    let mut crc = crc_update(0xff, &output[1..header_length]);
    if frame.control & !POLL_FINAL == UI {
        crc = crc_update(crc, frame.payload);
    }
    output[header_length + length] = !crc;
    output[header_length + length + 1] = FLAG;
    Ok(total)
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct DecodeStats {
    pub discarded_bytes: usize,
    pub oversized_frames: usize,
    pub bad_checksums: usize,
    pub missing_closing_flags: usize,
}

pub struct Decoder<const CAPACITY: usize> {
    buffer: [u8; CAPACITY],
    length: usize,
    consumed: usize,
    limit: usize,
    stats: DecodeStats,
}

impl<const CAPACITY: usize> Decoder<CAPACITY> {
    pub fn new(limit: usize) -> Result<Self, Error> {
        valid_limit(limit)?;
        if CAPACITY < limit + MAX_OVERHEAD {
            return Err(Error::InsufficientCapacity);
        }
        Ok(Self {
            buffer: [0; CAPACITY],
            length: 0,
            consumed: 0,
            limit,
            stats: DecodeStats::default(),
        })
    }

    pub fn buffered(&self) -> &[u8] {
        &self.buffer[self.consumed..self.length]
    }

    pub fn available_capacity(&self) -> usize {
        CAPACITY - self.buffered().len()
    }

    pub fn stats(&self) -> DecodeStats {
        self.stats
    }

    pub fn reset(&mut self) -> usize {
        let discarded = self.buffered().len();
        self.length = 0;
        self.consumed = 0;
        discarded
    }

    pub fn push(&mut self, bytes: &[u8]) -> Result<(), Error> {
        if bytes.len() > self.available_capacity() {
            return Err(Error::InsufficientCapacity);
        }
        self.buffer.copy_within(self.consumed..self.length, 0);
        self.length -= self.consumed;
        self.consumed = 0;
        self.buffer[self.length..self.length + bytes.len()].copy_from_slice(bytes);
        self.length += bytes.len();
        Ok(())
    }

    pub fn next_frame(&mut self) -> Option<Frame<'_>> {
        let mut index = self.consumed;
        loop {
            while index < self.length && self.buffer[index] != FLAG {
                self.stats.discarded_bytes = self.stats.discarded_bytes.saturating_add(1);
                index += 1;
            }
            while index + 1 < self.length && self.buffer[index + 1] == FLAG {
                index += 1;
            }
            self.consumed = index;
            if self.length - index < 4 {
                return None;
            }
            let start = index + 1;
            let control = self.buffer[start + 1];
            let mut length = (self.buffer[start + 2] >> 1) as usize;
            let mut header_length = 3;
            if self.buffer[start + 2] & 1 == 0 {
                if self.length - start < 4 {
                    return None;
                }
                length |= (self.buffer[start + 3] as usize) << 7;
                header_length = 4;
            }
            if length > self.limit {
                self.stats.oversized_frames = self.stats.oversized_frames.saturating_add(1);
                index = start;
                continue;
            }
            let payload_start = start + header_length;
            let checksum = payload_start + length;
            let closing = checksum + 1;
            if closing >= self.length {
                return None;
            }
            let payload = &self.buffer[payload_start..checksum];
            let mut crc = crc_update(0xff, &self.buffer[start..payload_start]);
            if control & !POLL_FINAL == UI {
                crc = crc_update(crc, payload);
            }
            if crc ^ self.buffer[checksum] != 0xff {
                self.stats.bad_checksums = self.stats.bad_checksums.saturating_add(1);
                index = start;
                continue;
            }
            if self.buffer[closing] != FLAG {
                self.stats.missing_closing_flags =
                    self.stats.missing_closing_flags.saturating_add(1);
                index = start;
                continue;
            }
            self.consumed = closing;
            return Some(Frame {
                address: self.buffer[start],
                control,
                payload,
            });
        }
    }
}
