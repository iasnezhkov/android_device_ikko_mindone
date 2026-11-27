// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

pub const HEADER_LEN: usize = 16;
pub const MAX_PAYLOAD_LEN: usize = 520;
pub const MAX_DATA_LEN: usize = MAX_PAYLOAD_LEN - 8;

pub const MSG_ID_INITIALIZATION: u32 = 1;
pub const MSG_ID_GET_PLATFORM_CAPABILITY: u32 = 2;
pub const MSG_ID_EVENT_REQUEST: u32 = 3;
pub const MSG_ID_UICC_RESET_RESPONSE: u32 = 4;
pub const MSG_ID_UICC_APDU_RESPONSE: u32 = 5;
pub const MSG_ID_UICC_POWER_DOWN_RESPONSE: u32 = 6;
pub const MSG_ID_GET_SERVICE_STATE_REQUEST: u32 = 7;
pub const MSG_ID_FINALIZATION: u32 = 8;
pub const MSG_ID_UICC_AUTHENTICATION_DONE_IND: u32 = 9;
pub const MSG_ID_UICC_TEST_MODE_REQUEST: u32 = 5001;

pub const MSG_ID_GET_PLATFORM_CAPABILITY_RESPONSE: u32 = 1002;
pub const MSG_ID_EVENT_RESPONSE: u32 = 1003;
pub const MSG_ID_UICC_RESET_REQUEST: u32 = 1004;
pub const MSG_ID_UICC_APDU_REQUEST: u32 = 1005;
pub const MSG_ID_UICC_POWER_DOWN_REQUEST: u32 = 1006;
pub const MSG_ID_GET_SERVICE_STATE_RESPONSE: u32 = 1007;
pub const MSG_ID_UICC_AUTHENTICATION_REQUEST_IND: u32 = 1009;
pub const MSG_ID_UICC_AUTHENTICATION_ABORT_IND: u32 = 1010;
pub const MSG_ID_UICC_AUTHENTICATION_REQUEST_IND_V2: u32 = 1011;
pub const MSG_ID_UICC_AUTHENTICATION_DONE_IND_ACK: u32 = 1012;
pub const MSG_ID_RIL_INTERNAL_APDU_REQUEST: u32 = 2001;

pub const EVENT_ENABLE_EXTERNAL_SIM: i32 = 1;
pub const EVENT_DISABLE_EXTERNAL_SIM: i32 = 2;
pub const EVENT_PLUG_OUT: i32 = 3;
pub const EVENT_PLUG_IN: i32 = 4;
pub const EVENT_SET_PERSIST_TYPE: i32 = 5;
pub const EVENT_SET_MAPPING_INFO: i32 = 6;
pub const EVENT_RESET_MAPPING_INFO: i32 = 7;
pub const EVENT_SET_TIMEOUT_TIMER: i32 = 8;
pub const EVENT_DISABLE_SIM_DIALOG: i32 = 9;
pub const EVENT_ENABLE_SIM_DIALOG: i32 = 10;
pub const EVENT_SET_PERSIST_TIMEOUT: i32 = 11;
pub const EVENT_RSIM_AUTH_DONE: i32 = 201;
pub const EVENT_RSIM_AUTH_ABORT: i32 = 202;
pub const EVENT_RSIM_AUTH_IND: i32 = 203;
pub const EVENT_EXTERNAL_SIM_CONNECTED: i32 = 204;

pub const RESPONSE_RESULT_OK: i32 = 0;
pub const RESPONSE_RESULT_GENERIC_ERROR: i32 = -1;
pub const RESPONSE_RESULT_PLATFORM_NOT_READY: i32 = -2;
pub const RESPONSE_RESULT_PLATFORM_RETRYING: i32 = -3;
pub const RESPONSE_RESULT_NOT_SUPPORT: i32 = -4;

pub const MULTISIM_CONFIG_SS: i32 = 0;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Frame {
    pub tx_id: u32,
    pub msg_id: u32,
    pub slot_id: u32,
    pub payload: Vec<u8>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ProtocolError {
    PayloadTooLarge(usize),
    Truncated,
    InvalidPayload,
    InvalidHex,
}

impl Frame {
    pub fn encode(&self) -> Result<Vec<u8>, ProtocolError> {
        if self.payload.len() > MAX_PAYLOAD_LEN {
            return Err(ProtocolError::PayloadTooLarge(self.payload.len()));
        }
        let mut out = Vec::with_capacity(HEADER_LEN + self.payload.len());
        out.extend_from_slice(&self.tx_id.to_le_bytes());
        out.extend_from_slice(&self.msg_id.to_le_bytes());
        out.extend_from_slice(&self.slot_id.to_le_bytes());
        out.extend_from_slice(&(self.payload.len() as u32).to_le_bytes());
        out.extend_from_slice(&self.payload);
        Ok(out)
    }

    pub fn decode_header(header: &[u8; HEADER_LEN]) -> Result<(u32, u32, u32, usize), ProtocolError> {
        let tx_id = u32::from_le_bytes([header[0], header[1], header[2], header[3]]);
        let msg_id = u32::from_le_bytes([header[4], header[5], header[6], header[7]]);
        let slot_id = u32::from_le_bytes([header[8], header[9], header[10], header[11]]);
        let length = u32::from_le_bytes([header[12], header[13], header[14], header[15]]) as usize;
        if length > MAX_PAYLOAD_LEN {
            return Err(ProtocolError::PayloadTooLarge(length));
        }
        Ok((tx_id, msg_id, slot_id, length))
    }

    pub fn decode_slice(bytes: &[u8]) -> Result<Self, ProtocolError> {
        if bytes.len() < HEADER_LEN {
            return Err(ProtocolError::Truncated);
        }
        let mut header = [0u8; HEADER_LEN];
        header.copy_from_slice(&bytes[..HEADER_LEN]);
        let (tx_id, msg_id, slot_id, length) = Self::decode_header(&header)?;
        if bytes.len() != HEADER_LEN + length {
            return Err(ProtocolError::Truncated);
        }
        Ok(Self {
            tx_id,
            msg_id,
            slot_id,
            payload: bytes[HEADER_LEN..].to_vec(),
        })
    }
}

pub fn read_i32_le(bytes: &[u8], offset: usize) -> Result<i32, ProtocolError> {
    if offset + 4 > bytes.len() {
        return Err(ProtocolError::InvalidPayload);
    }
    Ok(i32::from_le_bytes([
        bytes[offset],
        bytes[offset + 1],
        bytes[offset + 2],
        bytes[offset + 3],
    ]))
}

pub fn read_u32_le(bytes: &[u8], offset: usize) -> Result<u32, ProtocolError> {
    if offset + 4 > bytes.len() {
        return Err(ProtocolError::InvalidPayload);
    }
    Ok(u32::from_le_bytes([
        bytes[offset],
        bytes[offset + 1],
        bytes[offset + 2],
        bytes[offset + 3],
    ]))
}

pub fn encode_i32_list(values: &[i32]) -> Vec<u8> {
    let mut out = Vec::with_capacity(values.len() * 4);
    for &v in values {
        out.extend_from_slice(&v.to_le_bytes());
    }
    out
}

pub fn encode_len_prefixed_bytes(data: &[u8]) -> Result<Vec<u8>, ProtocolError> {
    if data.len() + 4 > MAX_PAYLOAD_LEN {
        return Err(ProtocolError::PayloadTooLarge(data.len() + 4));
    }
    let mut out = Vec::with_capacity(4 + data.len());
    out.extend_from_slice(&(data.len() as u32).to_le_bytes());
    out.extend_from_slice(data);
    Ok(out)
}

pub fn parse_operation_response(payload: &[u8]) -> Result<(i32, Vec<u8>), ProtocolError> {
    if payload.len() < 8 {
        return Err(ProtocolError::InvalidPayload);
    }
    let result = read_i32_le(payload, 0)?;
    let data_len = read_i32_le(payload, 4)?;
    if data_len < 0 {
        return Err(ProtocolError::InvalidPayload);
    }
    let data_len = data_len as usize;
    if data_len > MAX_DATA_LEN || 8 + data_len > payload.len() {
        return Err(ProtocolError::InvalidPayload);
    }
    Ok((result, payload[8..8 + data_len].to_vec()))
}

pub fn decode_hex(input: &str) -> Result<Vec<u8>, ProtocolError> {
    let bytes = input.as_bytes();
    if (bytes.len() & 1) != 0 {
        return Err(ProtocolError::InvalidHex);
    }
    let mut out = Vec::with_capacity(bytes.len() / 2);
    for pair in bytes.chunks_exact(2) {
        let hi = hex_nibble(pair[0]).ok_or(ProtocolError::InvalidHex)?;
        let lo = hex_nibble(pair[1]).ok_or(ProtocolError::InvalidHex)?;
        out.push((hi << 4) | lo);
    }
    Ok(out)
}

pub fn encode_hex(input: &[u8]) -> String {
    const TABLE: &[u8; 16] = b"0123456789ABCDEF";
    let mut out = String::with_capacity(input.len() * 2);
    for &b in input {
        out.push(TABLE[(b >> 4) as usize] as char);
        out.push(TABLE[(b & 0x0f) as usize] as char);
    }
    out
}

fn hex_nibble(b: u8) -> Option<u8> {
    match b {
        b'0'..=b'9' => Some(b - b'0'),
        b'a'..=b'f' => Some(b - b'a' + 10),
        b'A'..=b'F' => Some(b - b'A' + 10),
        _ => None,
    }
}

pub fn slot_mask_valid_for_single_sim(slot_mask: u32) -> bool {
    slot_mask == 1
}

pub fn global_or_single_slot_mask(slot_mask: u32) -> bool {
    slot_mask == 0 || slot_mask == 1
}
