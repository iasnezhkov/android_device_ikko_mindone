/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#![no_std]
#![forbid(unsafe_code)]

pub mod calibration;

pub const EVENT_BYTES: usize = 76;
pub const COMMAND_BYTES: usize = 52;
pub const CALIBRATION_BYTES: usize = 48;
pub const INFO_BYTES: usize = 40;
pub const SENSOR_MAX: u8 = 78;
pub const READ_BATCH: usize = 64;
pub const DRAIN_LIMIT: usize = 128;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    Length,
    Sensor,
    Action,
    Accuracy,
    Timing,
    Capacity,
    State,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SensorState {
    pub active: bool,
    pub period_ns: i64,
    pub latency_ns: i64,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum StateCommand {
    None,
    Enable,
    Flush,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct StateTransition {
    pub next: SensorState,
    pub command: StateCommand,
}

impl SensorState {
    fn validate(self, sensor_type: i32) -> Result<(), Error> {
        if sensor_type <= 0 || sensor_type >= i32::from(SENSOR_MAX) {
            return Err(Error::Sensor);
        }
        if self.period_ns < 0 || self.latency_ns < 0 {
            return Err(Error::Timing);
        }
        Ok(())
    }

    pub fn activate(self, sensor_type: i32, enabled: bool) -> Result<StateTransition, Error> {
        self.validate(sensor_type)?;
        Ok(StateTransition {
            next: Self { active: enabled, ..self },
            command: StateCommand::Enable,
        })
    }

    pub fn batch(self, sensor_type: i32, period_ns: i64, latency_ns: i64) -> Result<StateTransition, Error> {
        self.validate(sensor_type)?;
        if period_ns < 0 || latency_ns < 0 {
            return Err(Error::Timing);
        }
        Ok(StateTransition {
            next: Self { period_ns, latency_ns, ..self },
            command: if self.active { StateCommand::Enable } else { StateCommand::None },
        })
    }

    pub fn flush(self, sensor_type: i32, one_shot: bool) -> Result<StateTransition, Error> {
        self.validate(sensor_type)?;
        if !self.active || one_shot {
            return Err(Error::State);
        }
        Ok(StateTransition { next: self, command: StateCommand::Flush })
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Action {
    Data = 0,
    Flush = 1,
    Bias = 2,
    Calibration = 3,
    Temperature = 4,
    Test = 5,
    Raw = 6,
}

impl TryFrom<u8> for Action {
    type Error = Error;

    fn try_from(value: u8) -> Result<Self, Error> {
        match value {
            0 => Ok(Self::Data),
            1 => Ok(Self::Flush),
            2 => Ok(Self::Bias),
            3 => Ok(Self::Calibration),
            4 => Ok(Self::Temperature),
            5 => Ok(Self::Test),
            6 => Ok(Self::Raw),
            _ => Err(Error::Action),
        }
    }
}

fn sensor(value: u8) -> Result<u8, Error> {
    if value == 0 || value >= SENSOR_MAX {
        Err(Error::Sensor)
    } else {
        Ok(value)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Event {
    pub timestamp_ns: i64,
    pub sensor_type: u8,
    pub accuracy: u8,
    pub action: Action,
    pub words: [i32; 16],
}

pub fn decode_event(input: &[u8]) -> Result<Event, Error> {
    if input.len() != EVENT_BYTES {
        return Err(Error::Length);
    }
    let sensor_type = sensor(input[8])?;
    let action = Action::try_from(input[10])?;
    if action == Action::Data && input[9] > 3 {
        return Err(Error::Accuracy);
    }
    let mut timestamp = [0; 8];
    timestamp.copy_from_slice(&input[..8]);
    let mut words = [0; 16];
    for (word, raw) in words.iter_mut().zip(input[12..].chunks_exact(4)) {
        let mut bytes = [0; 4];
        bytes.copy_from_slice(raw);
        *word = i32::from_le_bytes(bytes);
    }
    Ok(Event {
        timestamp_ns: i64::from_le_bytes(timestamp),
        sensor_type,
        accuracy: input[9],
        action,
        words,
    })
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SensorInfo {
    pub sensor_type: u8,
    pub gain: u32,
    pub name: [u8; 16],
    pub vendor: [u8; 16],
}

pub fn decode_info(input: &[u8]) -> Result<SensorInfo, Error> {
    if input.len() != INFO_BYTES {
        return Err(Error::Length);
    }
    let sensor_type = sensor(input[0])?;
    let mut gain = [0; 4];
    gain.copy_from_slice(&input[4..8]);
    let mut name = [0; 16];
    name.copy_from_slice(&input[8..24]);
    let mut vendor = [0; 16];
    vendor.copy_from_slice(&input[24..40]);
    Ok(SensorInfo {
        sensor_type,
        gain: u32::from_le_bytes(gain),
        name,
        vendor,
    })
}

pub fn enable_command(
    sensor_type: u8,
    enabled: bool,
    delay_ns: i64,
    latency_ns: i64,
) -> Result<[u8; COMMAND_BYTES], Error> {
    sensor(sensor_type)?;
    if delay_ns < 0 || latency_ns < 0 {
        return Err(Error::Timing);
    }
    let mut output = [0; COMMAND_BYTES];
    output[0] = sensor_type;
    output[1] = u8::from(enabled);
    output[2] = 16;
    output[4..12].copy_from_slice(&delay_ns.to_le_bytes());
    output[12..20].copy_from_slice(&latency_ns.to_le_bytes());
    Ok(output)
}

pub fn flush_command(sensor_type: u8) -> Result<[u8; COMMAND_BYTES], Error> {
    sensor(sensor_type)?;
    let mut output = [0; COMMAND_BYTES];
    output[0] = sensor_type;
    output[1] = 2;
    Ok(output)
}

pub fn calibration_command(
    sensor_type: u8,
    data: &[u8],
) -> Result<[u8; COMMAND_BYTES], Error> {
    sensor(sensor_type)?;
    if data.len() > CALIBRATION_BYTES {
        return Err(Error::Length);
    }
    let mut output = [0; COMMAND_BYTES];
    output[0] = sensor_type;
    output[1] = 4;
    output[2] = data.len() as u8;
    output[4..4 + data.len()].copy_from_slice(data);
    Ok(output)
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct DrainBudget {
    records: usize,
}

impl DrainBudget {
    pub fn next_read_bytes(&self) -> usize {
        (DRAIN_LIMIT - self.records).min(READ_BATCH) * EVENT_BYTES
    }

    pub fn accept_read(&mut self, bytes: usize) -> Result<usize, Error> {
        if bytes % EVENT_BYTES != 0 {
            return Err(Error::Length);
        }
        if bytes > self.next_read_bytes() {
            return Err(Error::Capacity);
        }
        let records = bytes / EVENT_BYTES;
        self.records += records;
        Ok(records)
    }

    pub fn records(&self) -> usize {
        self.records
    }
}
