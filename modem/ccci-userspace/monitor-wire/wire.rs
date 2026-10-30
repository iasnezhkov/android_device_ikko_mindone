#![no_std]
/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#![forbid(unsafe_code)]

pub const RECORD_BYTES: usize = 16;
pub const MAGIC: u32 = 0xffffffff;
pub const MONITOR_WORD: u32 = 0xf0000000;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error { RecordLength, Magic, MonitorWord }

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Kind {
    ForceStopRequest,
    FlightStopRequest,
    ForceStartRequest,
    FlightStartRequest,
    ResetRequest,
    Exception,
    SendBatteryInfo,
    StoreNvramModemType,
    ConfigUpdate,
    RandomPattern,
    Unsupported(u32),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Message {
    id: u32,
    reserved: u32,
}

fn word(input: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes([input[offset], input[offset + 1], input[offset + 2], input[offset + 3]])
}

impl Message {
    pub fn parse(input: &[u8]) -> Result<Self, Error> {
        if input.len() != RECORD_BYTES { return Err(Error::RecordLength); }
        if word(input, 0) != MAGIC { return Err(Error::Magic); }
        if word(input, 8) != MONITOR_WORD { return Err(Error::MonitorWord); }
        Ok(Self { id: word(input, 4), reserved: word(input, 12) })
    }
    pub fn id(&self) -> u32 { self.id }
    pub fn reserved(&self) -> u32 { self.reserved }
    pub fn kind(&self) -> Kind {
        match self.id {
            0xfaf50001 => Kind::ForceStopRequest,
            0xfaf50002 => Kind::FlightStopRequest,
            0xfaf50003 => Kind::ForceStartRequest,
            0xfaf50004 => Kind::FlightStartRequest,
            0xfaf50005 => Kind::ResetRequest,
            0xfaf50006 => Kind::Exception,
            0xfaf50007 => Kind::SendBatteryInfo,
            0xfaf50008 => Kind::StoreNvramModemType,
            0xfaf50009 => Kind::ConfigUpdate,
            0xfaf5000a => Kind::RandomPattern,
            value => Kind::Unsupported(value),
        }
    }
}
