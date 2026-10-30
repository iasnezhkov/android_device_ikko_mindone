#![no_std]
/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#![forbid(unsafe_code)]

pub const HEADER_BYTES: usize = 16;
pub const RECORD_BYTES: usize = 24;
pub const MAX_RECORD_BYTES: usize = 2048;
pub const MAX_ARGUMENTS: usize = 6;
pub const MAX_REQUEST_INDEX: u32 = 2;
pub const RX_CHANNEL: u16 = 32;
pub const TX_CHANNEL: u16 = 33;
pub const RESPONSE_MASK: u32 = 0xffff0000;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    Truncated,
    OversizedRecord,
    Channel,
    RequestIndex,
    ResponseOpcode,
    TooManyArguments,
    ArgumentLength,
    MissingPadding,
    TrailingBytes,
    OutputCapacity,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Operation {
    QueryApSystemProperty,
    SarTableIndexQuery,
    AmmsDrdiControl,
    SaveModemCapid,
    Unsupported(u32),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Header {
    pub data0: u32,
    pub data1: u32,
    pub channel: u16,
    pub sequence: u16,
    pub asserted: bool,
    pub request_index: u32,
}

pub struct Request<'a> {
    header: Header,
    opcode: u32,
    arguments: [&'a [u8]; MAX_ARGUMENTS],
    count: usize,
}

fn word(input: &[u8], offset: usize) -> Result<u32, Error> {
    let bytes = input.get(offset..offset + 4).ok_or(Error::Truncated)?;
    Ok(u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]))
}

fn aligned(length: usize) -> usize { (length + 3) & !3 }

impl<'a> Request<'a> {
    pub fn parse(input: &'a [u8]) -> Result<Self, Error> {
        if input.len() < RECORD_BYTES { return Err(Error::Truncated); }
        if input.len() > MAX_RECORD_BYTES { return Err(Error::OversizedRecord); }
        let channel_word = word(input, 8)?;
        let header = Header {
            data0: word(input, 0)?, data1: word(input, 4)?, channel: channel_word as u16,
            sequence: ((channel_word >> 16) & 0x7fff) as u16, asserted: channel_word & 0x80000000 != 0,
            request_index: word(input, 12)?,
        };
        if header.channel != RX_CHANNEL { return Err(Error::Channel); }
        if header.request_index > MAX_REQUEST_INDEX { return Err(Error::RequestIndex); }
        let opcode = word(input, 16)?;
        if opcode & RESPONSE_MASK != 0 { return Err(Error::ResponseOpcode); }
        let count = word(input, 20)? as usize;
        if count > MAX_ARGUMENTS { return Err(Error::TooManyArguments); }
        let mut arguments = [&[][..]; MAX_ARGUMENTS];
        let mut offset = RECORD_BYTES;
        for argument in arguments.iter_mut().take(count) {
            let length = word(input, offset)? as usize;
            offset += 4;
            if length > input.len() - offset { return Err(Error::ArgumentLength); }
            let end = offset + length;
            *argument = &input[offset..end];
            offset += aligned(length);
            if offset > input.len() { return Err(Error::MissingPadding); }
        }
        if offset != input.len() { return Err(Error::TrailingBytes); }
        Ok(Self { header, opcode, arguments, count })
    }

    pub fn header(&self) -> Header { self.header }
    pub fn opcode(&self) -> u32 { self.opcode }
    pub fn argument_count(&self) -> usize { self.count }
    pub fn argument(&self, index: usize) -> Option<&'a [u8]> {
        self.arguments.get(..self.count)?.get(index).copied()
    }
    pub fn arguments(&self) -> impl ExactSizeIterator<Item = &'a [u8]> + '_ {
        self.arguments[..self.count].iter().copied()
    }
    pub fn operation(&self) -> Operation {
        match self.opcode {
            0x400f => Operation::QueryApSystemProperty,
            0x4010 => Operation::SarTableIndexQuery,
            0x4014 => Operation::AmmsDrdiControl,
            0x4015 => Operation::SaveModemCapid,
            value => Operation::Unsupported(value),
        }
    }
}

pub fn encode_response(request: &Request<'_>, arguments: &[&[u8]], output: &mut [u8]) -> Result<usize, Error> {
    if arguments.len() > MAX_ARGUMENTS { return Err(Error::TooManyArguments); }
    let mut length = RECORD_BYTES;
    for argument in arguments {
        if argument.len() > MAX_RECORD_BYTES { return Err(Error::OversizedRecord); }
        length += 4 + aligned(argument.len());
        if length > MAX_RECORD_BYTES { return Err(Error::OversizedRecord); }
    }
    if output.len() < length { return Err(Error::OutputCapacity); }
    output[..length].fill(0);
    output[..4].copy_from_slice(&request.header.data0.to_le_bytes());
    output[4..8].copy_from_slice(&(length as u32).to_le_bytes());
    let channel_word = u32::from(TX_CHANNEL) | (u32::from(request.header.sequence) << 16)
        | if request.header.asserted { 0x80000000 } else { 0 };
    output[8..12].copy_from_slice(&channel_word.to_le_bytes());
    output[12..16].copy_from_slice(&request.header.request_index.to_le_bytes());
    output[16..20].copy_from_slice(&(request.opcode | RESPONSE_MASK).to_le_bytes());
    output[20..24].copy_from_slice(&(arguments.len() as u32).to_le_bytes());
    let mut offset = RECORD_BYTES;
    for argument in arguments {
        output[offset..offset + 4].copy_from_slice(&(argument.len() as u32).to_le_bytes());
        offset += 4;
        output[offset..offset + argument.len()].copy_from_slice(argument);
        offset += aligned(argument.len());
    }
    Ok(length)
}
