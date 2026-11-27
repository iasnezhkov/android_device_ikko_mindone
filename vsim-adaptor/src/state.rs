// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

use std::collections::BTreeMap;

use crate::protocol::*;

pub const DEFAULT_TIMEOUT_MS: u64 = 13_000;
pub const MIN_TIMEOUT_S: i32 = 1;
pub const MAX_TIMEOUT_S: i32 = 60;
pub const SINGLE_SLOT_MASK: u32 = 1;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SlotState {
    Idle,
    Enabled,
    PluggedIn,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Action {
    SendClient(Frame),
    CloseClient,
    SendVsimNotification {
        serial: i32,
        tx_id: i32,
        event_id: i32,
        sim_type: i32,
    },
    SendVsimOperation {
        serial: i32,
        tx_id: i32,
        event_id: i32,
        result: i32,
        data: Vec<u8>,
    },
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct PendingEvent {
    tx_id: u32,
    slot_id: u32,
    event_type: i32,
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct AwaitingModemOp {
    tx_id: i32,
    rsp_msg_id: i32,
    deadline_ms: u64,
}

pub struct VsimEngine {
    modem_capability: i32,
    client_connected: bool,
    slot_state: SlotState,
    timeout_ms: u64,
    next_serial: i32,
    pending_events: BTreeMap<i32, PendingEvent>,
    awaiting_op: Option<AwaitingModemOp>,
}

impl VsimEngine {
    pub fn new(modem_capability: i32) -> Self {
        Self {
            modem_capability,
            client_connected: false,
            slot_state: SlotState::Idle,
            timeout_ms: DEFAULT_TIMEOUT_MS,
            next_serial: 1,
            pending_events: BTreeMap::new(),
            awaiting_op: None,
        }
    }

    pub fn set_modem_capability(&mut self, cap: i32) {
        self.modem_capability = cap;
    }

    pub fn client_connected(&self) -> bool {
        self.client_connected
    }

    pub fn slot_state(&self) -> SlotState {
        self.slot_state
    }

    pub fn timeout_ms(&self) -> u64 {
        self.timeout_ms
    }

    pub fn next_deadline_ms(&self) -> Option<u64> {
        self.awaiting_op.as_ref().map(|op| op.deadline_ms)
    }

    fn alloc_serial(&mut self) -> i32 {
        let s = self.next_serial;
        self.next_serial = if s >= i32::MAX - 1 { 1 } else { s + 1 };
        s
    }

    pub fn on_client_connected(&mut self) -> Vec<Action> {
        if self.client_connected {
            return vec![Action::CloseClient];
        }
        self.client_connected = true;
        let serial = self.alloc_serial();
        vec![Action::SendVsimNotification {
            serial,
            tx_id: 0,
            event_id: EVENT_EXTERNAL_SIM_CONNECTED,
            sim_type: 1,
        }]
    }

    pub fn on_client_frame(&mut self, frame: Frame) -> Vec<Action> {
        if !self.client_connected {
            return vec![Action::CloseClient];
        }
        match frame.msg_id {
            MSG_ID_INITIALIZATION => Vec::new(),
            MSG_ID_FINALIZATION => {
                let mut actions = self.teardown_session(true);
                actions.push(Action::CloseClient);
                actions
            }
            MSG_ID_GET_PLATFORM_CAPABILITY => self.handle_get_capability(&frame),
            MSG_ID_GET_SERVICE_STATE_REQUEST => self.handle_get_service_state(&frame),
            MSG_ID_EVENT_REQUEST => self.handle_event_request(&frame),
            MSG_ID_UICC_RESET_RESPONSE
            | MSG_ID_UICC_APDU_RESPONSE
            | MSG_ID_UICC_POWER_DOWN_RESPONSE => self.handle_operation_response(&frame),
            MSG_ID_UICC_AUTHENTICATION_DONE_IND => self.handle_auth_done(&frame),
            MSG_ID_UICC_TEST_MODE_REQUEST => vec![Action::SendClient(Frame {
                tx_id: frame.tx_id,
                msg_id: MSG_ID_EVENT_RESPONSE,
                slot_id: frame.slot_id,
                payload: encode_i32_list(&[RESPONSE_RESULT_NOT_SUPPORT]),
            })],
            _ => {
                let mut actions = self.teardown_session(true);
                actions.push(Action::CloseClient);
                actions
            }
        }
    }

    fn handle_get_capability(&self, frame: &Frame) -> Vec<Action> {
        if frame.payload.len() < 8 || !global_or_single_slot_mask(frame.slot_id) {
            return vec![Action::SendClient(Frame {
                tx_id: frame.tx_id,
                msg_id: MSG_ID_GET_PLATFORM_CAPABILITY_RESPONSE,
                slot_id: frame.slot_id,
                payload: encode_i32_list(&[RESPONSE_RESULT_NOT_SUPPORT, MULTISIM_CONFIG_SS, 0, 0]),
            })];
        }
        let result = if self.modem_capability > 0 {
            RESPONSE_RESULT_OK
        } else {
            RESPONSE_RESULT_PLATFORM_NOT_READY
        };
        let slot_mask = if self.modem_capability > 0 {
            SINGLE_SLOT_MASK as i32
        } else {
            0
        };
        vec![Action::SendClient(Frame {
            tx_id: frame.tx_id,
            msg_id: MSG_ID_GET_PLATFORM_CAPABILITY_RESPONSE,
            slot_id: frame.slot_id,
            payload: encode_i32_list(&[
                result,
                MULTISIM_CONFIG_SS,
                self.modem_capability,
                slot_mask,
            ]),
        })]
    }

    fn handle_get_service_state(&self, frame: &Frame) -> Vec<Action> {
        if !global_or_single_slot_mask(frame.slot_id) {
            return vec![Action::SendClient(Frame {
                tx_id: frame.tx_id,
                msg_id: MSG_ID_GET_SERVICE_STATE_RESPONSE,
                slot_id: frame.slot_id,
                payload: encode_i32_list(&[RESPONSE_RESULT_NOT_SUPPORT, -1, -1]),
            })];
        }
        vec![Action::SendClient(Frame {
            tx_id: frame.tx_id,
            msg_id: MSG_ID_GET_SERVICE_STATE_RESPONSE,
            slot_id: frame.slot_id,
            payload: encode_i32_list(&[RESPONSE_RESULT_OK, -1, -1]),
        })]
    }

    fn handle_event_request(&mut self, frame: &Frame) -> Vec<Action> {
        if frame.payload.len() < 8 {
            return vec![Action::SendClient(Frame {
                tx_id: frame.tx_id,
                msg_id: MSG_ID_EVENT_RESPONSE,
                slot_id: frame.slot_id,
                payload: encode_i32_list(&[RESPONSE_RESULT_GENERIC_ERROR]),
            })];
        }
        let event_type = match read_i32_le(&frame.payload, 0) {
            Ok(v) => v,
            Err(_) => {
                return vec![Action::SendClient(Frame {
                    tx_id: frame.tx_id,
                    msg_id: MSG_ID_EVENT_RESPONSE,
                    slot_id: frame.slot_id,
                    payload: encode_i32_list(&[RESPONSE_RESULT_GENERIC_ERROR]),
                })];
            }
        };
        let param = match read_i32_le(&frame.payload, 4) {
            Ok(v) => v,
            Err(_) => {
                return vec![Action::SendClient(Frame {
                    tx_id: frame.tx_id,
                    msg_id: MSG_ID_EVENT_RESPONSE,
                    slot_id: frame.slot_id,
                    payload: encode_i32_list(&[RESPONSE_RESULT_GENERIC_ERROR]),
                })];
            }
        };

        if event_type == EVENT_DISABLE_SIM_DIALOG || event_type == EVENT_ENABLE_SIM_DIALOG {
            return vec![Action::SendClient(Frame {
                tx_id: frame.tx_id,
                msg_id: MSG_ID_EVENT_RESPONSE,
                slot_id: frame.slot_id,
                payload: encode_i32_list(&[RESPONSE_RESULT_OK]),
            })];
        }

        if !slot_mask_valid_for_single_sim(frame.slot_id) {
            return vec![Action::SendClient(Frame {
                tx_id: frame.tx_id,
                msg_id: MSG_ID_EVENT_RESPONSE,
                slot_id: frame.slot_id,
                payload: encode_i32_list(&[RESPONSE_RESULT_NOT_SUPPORT]),
            })];
        }

        let supported = matches!(
            event_type,
            EVENT_ENABLE_EXTERNAL_SIM
                | EVENT_DISABLE_EXTERNAL_SIM
                | EVENT_PLUG_OUT
                | EVENT_PLUG_IN
                | EVENT_SET_PERSIST_TYPE
                | EVENT_SET_MAPPING_INFO
                | EVENT_RESET_MAPPING_INFO
                | EVENT_SET_TIMEOUT_TIMER
                | EVENT_SET_PERSIST_TIMEOUT
                | EVENT_RSIM_AUTH_DONE
                | EVENT_RSIM_AUTH_ABORT
                | EVENT_RSIM_AUTH_IND
        );
        if !supported {
            return vec![Action::SendClient(Frame {
                tx_id: frame.tx_id,
                msg_id: MSG_ID_EVENT_RESPONSE,
                slot_id: frame.slot_id,
                payload: encode_i32_list(&[RESPONSE_RESULT_NOT_SUPPORT]),
            })];
        }

        let forwarded_param = if event_type == EVENT_SET_TIMEOUT_TIMER {
            let clamped = param.clamp(MIN_TIMEOUT_S, MAX_TIMEOUT_S);
            self.timeout_ms = (clamped as u64) * 1000;
            clamped
        } else {
            param
        };

        match event_type {
            EVENT_ENABLE_EXTERNAL_SIM if self.slot_state == SlotState::Idle => {
                self.slot_state = SlotState::Enabled;
            }
            EVENT_PLUG_IN => {
                self.slot_state = SlotState::PluggedIn;
            }
            _ => {}
        }

        let serial = self.alloc_serial();
        self.pending_events.insert(
            serial,
            PendingEvent {
                tx_id: frame.tx_id,
                slot_id: frame.slot_id,
                event_type,
            },
        );
        vec![Action::SendVsimNotification {
            serial,
            tx_id: frame.tx_id as i32,
            event_id: event_type,
            sim_type: forwarded_param,
        }]
    }

    fn handle_operation_response(&mut self, frame: &Frame) -> Vec<Action> {
        if !slot_mask_valid_for_single_sim(frame.slot_id) {
            return Vec::new();
        }
        let (result, data) = match parse_operation_response(&frame.payload) {
            Ok(parsed) => parsed,
            Err(_) => {
                let mut actions = self.teardown_session(true);
                actions.push(Action::CloseClient);
                return actions;
            }
        };
        if let Some(op) = &self.awaiting_op {
            if op.tx_id == frame.tx_id as i32 && op.rsp_msg_id == frame.msg_id as i32 {
                self.awaiting_op = None;
            }
        }
        let serial = self.alloc_serial();
        vec![Action::SendVsimOperation {
            serial,
            tx_id: frame.tx_id as i32,
            event_id: frame.msg_id as i32,
            result,
            data,
        }]
    }

    fn handle_auth_done(&mut self, frame: &Frame) -> Vec<Action> {
        if !slot_mask_valid_for_single_sim(frame.slot_id) {
            return Vec::new();
        }
        let serial = self.alloc_serial();
        vec![Action::SendVsimNotification {
            serial,
            tx_id: frame.tx_id as i32,
            event_id: EVENT_RSIM_AUTH_DONE,
            sim_type: 0,
        }]
    }

    pub fn on_ril_notification_response(
        &mut self,
        serial: i32,
        radio_error: i32,
    ) -> Vec<Action> {
        let Some(pending) = self.pending_events.remove(&serial) else {
            return Vec::new();
        };
        let ok = radio_error == 0;
        if ok {
            match pending.event_type {
                EVENT_ENABLE_EXTERNAL_SIM => {
                    if self.slot_state == SlotState::Idle {
                        self.slot_state = SlotState::Enabled;
                    }
                }
                EVENT_PLUG_IN => self.slot_state = SlotState::PluggedIn,
                EVENT_PLUG_OUT => {
                    if self.slot_state == SlotState::PluggedIn {
                        self.slot_state = SlotState::Enabled;
                    }
                }
                EVENT_DISABLE_EXTERNAL_SIM => self.slot_state = SlotState::Idle,
                _ => {}
            }
        } else {
            match pending.event_type {
                EVENT_ENABLE_EXTERNAL_SIM if self.slot_state == SlotState::Enabled => {
                    self.slot_state = SlotState::Idle;
                }
                EVENT_PLUG_IN if self.slot_state == SlotState::PluggedIn => {
                    self.slot_state = SlotState::Enabled;
                }
                _ => {}
            }
        }
        if !self.client_connected {
            return Vec::new();
        }
        let res = if ok {
            RESPONSE_RESULT_OK
        } else {
            RESPONSE_RESULT_GENERIC_ERROR
        };
        vec![Action::SendClient(Frame {
            tx_id: pending.tx_id,
            msg_id: MSG_ID_EVENT_RESPONSE,
            slot_id: pending.slot_id,
            payload: encode_i32_list(&[res]),
        })]
    }

    pub fn on_ril_event_indication(
        &mut self,
        tx_id: i32,
        event_id: i32,
        hex_data: &str,
        now_ms: u64,
    ) -> Vec<Action> {
        let mapped_msg_id = if event_id as u32 == MSG_ID_RIL_INTERNAL_APDU_REQUEST {
            MSG_ID_UICC_APDU_REQUEST
        } else {
            event_id as u32
        };

        let rsp_for_fallback = match mapped_msg_id {
            MSG_ID_UICC_RESET_REQUEST => Some(MSG_ID_UICC_RESET_RESPONSE as i32),
            MSG_ID_UICC_APDU_REQUEST => Some(MSG_ID_UICC_APDU_RESPONSE as i32),
            MSG_ID_UICC_POWER_DOWN_REQUEST => Some(MSG_ID_UICC_POWER_DOWN_RESPONSE as i32),
            _ => None,
        };

        if !self.client_connected {
            if let Some(rsp_id) = rsp_for_fallback {
                let serial = self.alloc_serial();
                return vec![Action::SendVsimOperation {
                    serial,
                    tx_id,
                    event_id: rsp_id,
                    result: RESPONSE_RESULT_GENERIC_ERROR,
                    data: vec![0, 0],
                }];
            }
            return Vec::new();
        }

        match mapped_msg_id {
            MSG_ID_UICC_RESET_REQUEST => {
                self.awaiting_op = Some(AwaitingModemOp {
                    tx_id,
                    rsp_msg_id: MSG_ID_UICC_RESET_RESPONSE as i32,
                    deadline_ms: now_ms.saturating_add(self.timeout_ms),
                });
                vec![Action::SendClient(Frame {
                    tx_id: tx_id as u32,
                    msg_id: MSG_ID_UICC_RESET_REQUEST,
                    slot_id: SINGLE_SLOT_MASK,
                    payload: Vec::new(),
                })]
            }
            MSG_ID_UICC_APDU_REQUEST => {
                let raw = match decode_hex(hex_data) {
                    Ok(bytes) => bytes,
                    Err(_) => {
                        let serial = self.alloc_serial();
                        return vec![Action::SendVsimOperation {
                            serial,
                            tx_id,
                            event_id: MSG_ID_UICC_APDU_RESPONSE as i32,
                            result: RESPONSE_RESULT_GENERIC_ERROR,
                            data: vec![0, 0],
                        }];
                    }
                };
                let payload = match encode_len_prefixed_bytes(&raw) {
                    Ok(p) => p,
                    Err(_) => {
                        let serial = self.alloc_serial();
                        return vec![Action::SendVsimOperation {
                            serial,
                            tx_id,
                            event_id: MSG_ID_UICC_APDU_RESPONSE as i32,
                            result: RESPONSE_RESULT_GENERIC_ERROR,
                            data: vec![0, 0],
                        }];
                    }
                };
                self.awaiting_op = Some(AwaitingModemOp {
                    tx_id,
                    rsp_msg_id: MSG_ID_UICC_APDU_RESPONSE as i32,
                    deadline_ms: now_ms.saturating_add(self.timeout_ms),
                });
                vec![Action::SendClient(Frame {
                    tx_id: tx_id as u32,
                    msg_id: MSG_ID_UICC_APDU_REQUEST,
                    slot_id: SINGLE_SLOT_MASK,
                    payload,
                })]
            }
            MSG_ID_UICC_POWER_DOWN_REQUEST
            | MSG_ID_UICC_AUTHENTICATION_REQUEST_IND
            | MSG_ID_UICC_AUTHENTICATION_ABORT_IND
            | MSG_ID_UICC_AUTHENTICATION_REQUEST_IND_V2
            | MSG_ID_UICC_AUTHENTICATION_DONE_IND_ACK => {
                let payload = if hex_data.is_empty() {
                    Vec::new()
                } else {
                    decode_hex(hex_data)
                        .and_then(|raw| encode_len_prefixed_bytes(&raw))
                        .unwrap_or_default()
                };
                vec![Action::SendClient(Frame {
                    tx_id: tx_id as u32,
                    msg_id: mapped_msg_id,
                    slot_id: SINGLE_SLOT_MASK,
                    payload,
                })]
            }
            _ => Vec::new(),
        }
    }

    pub fn on_tick(&mut self, now_ms: u64) -> Vec<Action> {
        let expired = matches!(&self.awaiting_op, Some(op) if now_ms >= op.deadline_ms);
        if !expired {
            return Vec::new();
        }
        let op = self.awaiting_op.take().unwrap();
        let serial = self.alloc_serial();
        vec![Action::SendVsimOperation {
            serial,
            tx_id: op.tx_id,
            event_id: op.rsp_msg_id,
            result: RESPONSE_RESULT_GENERIC_ERROR,
            data: vec![0, 0],
        }]
    }

    pub fn on_client_disconnected(&mut self) -> Vec<Action> {
        if !self.client_connected {
            return Vec::new();
        }
        self.teardown_session(true)
    }

    pub fn on_shutdown(&mut self) -> Vec<Action> {
        let had_client = self.client_connected;
        let mut actions = self.teardown_session(had_client);
        if had_client {
            actions.push(Action::CloseClient);
        }
        actions
    }

    pub fn on_ril_died(&mut self) -> Vec<Action> {
        let had_client = self.client_connected;
        self.client_connected = false;
        self.slot_state = SlotState::Idle;
        self.pending_events.clear();
        self.awaiting_op = None;
        if had_client {
            vec![Action::CloseClient]
        } else {
            Vec::new()
        }
    }

    fn teardown_session(&mut self, notify_ril: bool) -> Vec<Action> {
        let mut actions = Vec::new();
        if notify_ril {
            if let Some(op) = self.awaiting_op.take() {
                let serial = self.alloc_serial();
                actions.push(Action::SendVsimOperation {
                    serial,
                    tx_id: op.tx_id,
                    event_id: op.rsp_msg_id,
                    result: RESPONSE_RESULT_GENERIC_ERROR,
                    data: vec![0, 0],
                });
            }
            if self.slot_state == SlotState::PluggedIn {
                let serial = self.alloc_serial();
                actions.push(Action::SendVsimNotification {
                    serial,
                    tx_id: 0,
                    event_id: EVENT_PLUG_OUT,
                    sim_type: 0,
                });
            }
            if self.slot_state != SlotState::Idle {
                let serial = self.alloc_serial();
                actions.push(Action::SendVsimNotification {
                    serial,
                    tx_id: 0,
                    event_id: EVENT_DISABLE_EXTERNAL_SIM,
                    sim_type: 0,
                });
            }
            let serial = self.alloc_serial();
            actions.push(Action::SendVsimNotification {
                serial,
                tx_id: 0,
                event_id: EVENT_EXTERNAL_SIM_CONNECTED,
                sim_type: 0,
            });
        } else {
            self.awaiting_op = None;
        }
        self.client_connected = false;
        self.slot_state = SlotState::Idle;
        self.pending_events.clear();
        actions
    }
}
