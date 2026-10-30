/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use crate::{valid_limit, MAX_PAYLOAD, UIH};

pub const MAX_NSC_TYPE_BYTES: usize = 8;
pub const MAX_LENGTH_BYTES: usize = 3;
const COMMAND: u8 = 2;
const TEST: u8 = 0x21;
const PSC: u8 = 0x41;
const PN: u8 = 0x81;
const CLD: u8 = 0xc1;
const MSC: u8 = 0xe1;
const NSC: u8 = 0x11;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    Configuration,
    OversizedInput,
    MissingTypeTerminator,
    MissingLengthTerminator,
    LengthEncoding,
    LengthMismatch,
    TrailingBytes,
    InvalidBody,
    InsufficientCapacity,
    OversizedReply,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Effect {
    None,
    PeerClosing,
    PeerSignals { channel: u8, signals: u8 },
}

#[derive(Debug, Eq, PartialEq)]
pub struct Reply<'a> {
    pub control: u8,
    pub payload: &'a [u8],
}

#[derive(Debug, Eq, PartialEq)]
pub struct Outcome<'a> {
    pub effect: Effect,
    pub reply: Option<Reply<'a>>,
}

fn declared_length(input: &[u8], start: usize) -> Result<(usize, usize), Error> {
    let mut value = 0;
    for index in 0..MAX_LENGTH_BYTES {
        let byte = *input.get(start + index).ok_or(Error::MissingLengthTerminator)?;
        value |= usize::from(byte >> 1) << (index * 7);
        if byte & 1 != 0 {
            return Ok((value, start + index + 1));
        }
    }
    Err(Error::LengthEncoding)
}

fn valid_msc(value: &[u8]) -> bool {
    match value {
        [channel, signals] => channel & 3 == 3 && signals & 1 == 1,
        [channel, signals, break_signal] => channel & 3 == 3 && signals & 1 == 0 && break_signal & 1 == 1,
        _ => false,
    }
}

pub fn prepare<'a>(input: &[u8], frame_limit: usize, output: &'a mut [u8]) -> Result<Outcome<'a>, Error> {
    valid_limit(frame_limit).map_err(|_| Error::Configuration)?;
    if input.len() > frame_limit {
        return Err(Error::OversizedInput);
    }
    let type_bytes = input.iter().position(|byte| byte & 1 != 0)
        .ok_or(Error::MissingTypeTerminator)? + 1;
    let (declared, value_start) = declared_length(input, type_bytes)?;
    let actual = input.len() - value_start;
    if declared > actual {
        return Err(Error::LengthMismatch);
    }
    if declared < actual {
        return Err(Error::TrailingBytes);
    }
    if input[0] & COMMAND == 0 {
        return Ok(Outcome { effect: Effect::None, reply: None });
    }
    let kind = input[0] & !COMMAND;
    let value = &input[value_start..];
    let effect = match kind {
        PSC | CLD if !value.is_empty() => return Err(Error::InvalidBody),
        PN if value.len() != 8 => return Err(Error::InvalidBody),
        MSC if !valid_msc(value) => return Err(Error::InvalidBody),
        CLD => Effect::PeerClosing,
        MSC => Effect::PeerSignals { channel: value[0] >> 2, signals: value[1] },
        _ => Effect::None,
    };
    let echo = matches!(kind, TEST | PSC | PN | CLD | MSC);
    let response_type_bytes = type_bytes.min(MAX_NSC_TYPE_BYTES);
    let length = if echo { input.len() } else { 2 + response_type_bytes };
    if length > frame_limit || length > MAX_PAYLOAD {
        return Err(Error::OversizedReply);
    }
    if length > output.len() {
        return Err(Error::InsufficientCapacity);
    }
    if echo {
        output[..length].copy_from_slice(input);
        output[0] &= !COMMAND;
    } else {
        output[0] = NSC;
        output[1] = (response_type_bytes as u8) << 1 | 1;
        output[2..length].copy_from_slice(&input[..response_type_bytes]);
    }
    Ok(Outcome { effect, reply: Some(Reply { control: UIH, payload: &output[..length] }) })
}
