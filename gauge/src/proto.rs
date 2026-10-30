// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

pub const HEADER_LEN: usize = 32;
pub const PTIM_LEN: usize = 56;
pub const MAX_DATA_LEN: usize = 9200;
pub const IDENTITY: u32 = 2_015_060_303;
pub const GET_PTIM_SAMPLE_V1: u32 = 0x10000;
pub const NOTIFY_DAEMON: u32 = 10;
pub const PTIM_VALID: u32 = 1;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum KernelError {
    NoDevice,
    Unsupported,
    AcquisitionErrno(u16),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Error {
    Length { expected: usize, actual: usize },
    Oversized,
    Identity(u32),
    Command,
    ReservedFields,
    Version(u32),
    Flags(u32),
    InvalidStatus(i32),
    FailurePayload,
    Kernel(KernelError),
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}

impl std::error::Error for Error {}

pub(crate) fn exact(bytes: &[u8], expected: usize) -> Result<(), Error> {
    if bytes.len() != expected {
        return Err(Error::Length {
            expected,
            actual: bytes.len(),
        });
    }
    Ok(())
}

pub(crate) fn u32_at(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap())
}

pub(crate) fn u64_at(bytes: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(bytes[offset..offset + 8].try_into().unwrap())
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Header {
    pub nl_cmd: u32,
    pub fgd_cmd: u32,
    pub fgd_cmd_hash: u32,
    pub fgd_subcmd: u32,
    pub fgd_subcmd_para1: u32,
    pub fgd_data_len: u32,
    pub fgd_ret_data_len: u32,
    pub identity: u32,
}

impl Header {
    pub fn encode(self) -> [u8; HEADER_LEN] {
        let words = [
            self.nl_cmd,
            self.fgd_cmd,
            self.fgd_cmd_hash,
            self.fgd_subcmd,
            self.fgd_subcmd_para1,
            self.fgd_data_len,
            self.fgd_ret_data_len,
            self.identity,
        ];
        let mut out = [0; HEADER_LEN];
        for (chunk, word) in out.chunks_exact_mut(4).zip(words) {
            chunk.copy_from_slice(&word.to_le_bytes());
        }
        out
    }

    pub fn decode(bytes: &[u8]) -> Result<Self, Error> {
        exact(bytes, HEADER_LEN)?;
        let value = Self {
            nl_cmd: u32_at(bytes, 0),
            fgd_cmd: u32_at(bytes, 4),
            fgd_cmd_hash: u32_at(bytes, 8),
            fgd_subcmd: u32_at(bytes, 12),
            fgd_subcmd_para1: u32_at(bytes, 16),
            fgd_data_len: u32_at(bytes, 20),
            fgd_ret_data_len: u32_at(bytes, 24),
            identity: u32_at(bytes, 28),
        };
        if value.identity != IDENTITY {
            return Err(Error::Identity(value.identity));
        }
        if value.fgd_data_len as usize > MAX_DATA_LEN
            || value.fgd_ret_data_len as usize > MAX_DATA_LEN
        {
            return Err(Error::Oversized);
        }
        Ok(value)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Message<'a> {
    pub header: Header,
    pub data: &'a [u8],
}

impl<'a> Message<'a> {
    pub fn decode(bytes: &'a [u8]) -> Result<Self, Error> {
        if bytes.len() < HEADER_LEN {
            return Err(Error::Length {
                expected: HEADER_LEN,
                actual: bytes.len(),
            });
        }
        let header = Header::decode(&bytes[..HEADER_LEN])?;
        exact(bytes, HEADER_LEN + header.fgd_data_len as usize)?;
        Ok(Self {
            header,
            data: &bytes[HEADER_LEN..],
        })
    }
}

pub fn ptim_request() -> [u8; HEADER_LEN] {
    Header {
        nl_cmd: 0,
        fgd_cmd: GET_PTIM_SAMPLE_V1,
        fgd_cmd_hash: 0,
        fgd_subcmd: 0,
        fgd_subcmd_para1: 0,
        fgd_data_len: 0,
        fgd_ret_data_len: PTIM_LEN as u32,
        identity: IDENTITY,
    }
    .encode()
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PtimSampleV1 {
    pub sequence: u64,
    pub started_ns: u64,
    pub acquired_ns: u64,
    pub voltage_01mv: i32,
    pub current_01ma: i32,
    pub r_fg_value: i32,
    pub car_tune_value: i32,
}

impl PtimSampleV1 {
    pub fn encode(self) -> [u8; PTIM_LEN] {
        let mut out = [0; PTIM_LEN];
        out[0..4].copy_from_slice(&1u32.to_le_bytes());
        out[4..8].copy_from_slice(&(PTIM_LEN as u32).to_le_bytes());
        out[12..16].copy_from_slice(&PTIM_VALID.to_le_bytes());
        for (offset, value) in [
            (16, self.sequence),
            (24, self.started_ns),
            (32, self.acquired_ns),
        ] {
            out[offset..offset + 8].copy_from_slice(&value.to_le_bytes());
        }
        for (offset, value) in [
            (40, self.voltage_01mv),
            (44, self.current_01ma),
            (48, self.r_fg_value),
            (52, self.car_tune_value),
        ] {
            out[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        }
        out
    }

    pub fn decode(bytes: &[u8]) -> Result<Self, Error> {
        exact(bytes, PTIM_LEN)?;
        if u32_at(bytes, 0) != 1 {
            return Err(Error::Version(u32_at(bytes, 0)));
        }
        if u32_at(bytes, 4) != PTIM_LEN as u32 {
            return Err(Error::Length {
                expected: PTIM_LEN,
                actual: u32_at(bytes, 4) as usize,
            });
        }
        let status = u32_at(bytes, 8) as i32;
        let flags = u32_at(bytes, 12);
        if status != 0 {
            let error = match status {
                -19 => KernelError::NoDevice,
                -95 => KernelError::Unsupported,
                -4095..=-1 => KernelError::AcquisitionErrno((-status) as u16),
                _ => return Err(Error::InvalidStatus(status)),
            };
            if flags != 0 || bytes[16..].iter().any(|v| *v != 0) {
                return Err(Error::FailurePayload);
            }
            return Err(Error::Kernel(error));
        }
        if flags != PTIM_VALID {
            return Err(Error::Flags(flags));
        }
        Ok(Self {
            sequence: u64_at(bytes, 16),
            started_ns: u64_at(bytes, 24),
            acquired_ns: u64_at(bytes, 32),
            voltage_01mv: u32_at(bytes, 40) as i32,
            current_01ma: u32_at(bytes, 44) as i32,
            r_fg_value: u32_at(bytes, 48) as i32,
            car_tune_value: u32_at(bytes, 52) as i32,
        })
    }
}

pub fn decode_ptim_reply(bytes: &[u8]) -> Result<PtimSampleV1, Error> {
    let message = Message::decode(bytes)?;
    let h = message.header;
    if h.nl_cmd != 0 || h.fgd_cmd != GET_PTIM_SAMPLE_V1 {
        return Err(Error::Command);
    }
    if h.fgd_cmd_hash != 0
        || h.fgd_subcmd != 0
        || h.fgd_subcmd_para1 != 0
        || h.fgd_ret_data_len != 0
    {
        return Err(Error::ReservedFields);
    }
    PtimSampleV1::decode(message.data)
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Notification {
    pub flow_state: u32,
    pub command: i32,
    pub parameter: i32,
}

pub fn decode_notification(bytes: &[u8]) -> Result<Notification, Error> {
    let message = Message::decode(bytes)?;
    let h = message.header;
    if h.nl_cmd != 0 || h.fgd_cmd != NOTIFY_DAEMON {
        return Err(Error::Command);
    }
    if h.fgd_cmd_hash != 0 || h.fgd_ret_data_len != 0 {
        return Err(Error::ReservedFields);
    }
    exact(message.data, 4)?;
    Ok(Notification {
        flow_state: u32_at(message.data, 0),
        command: h.fgd_subcmd as i32,
        parameter: h.fgd_subcmd_para1 as i32,
    })
}
