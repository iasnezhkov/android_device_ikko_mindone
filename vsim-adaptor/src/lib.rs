// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

pub mod protocol;
pub mod state;

#[cfg(test)]
mod tests {
    use crate::protocol::*;
    use crate::state::*;

    #[test]
    fn frame_round_trip_and_bounds() {
        let frame = Frame {
            tx_id: 42,
            msg_id: MSG_ID_UICC_APDU_REQUEST,
            slot_id: 1,
            payload: vec![0x05, 0x00, 0x00, 0x00, 0x00, 0xa4, 0x04, 0x00, 0x00],
        };
        let encoded = frame.encode().unwrap();
        assert_eq!(encoded.len(), HEADER_LEN + 9);
        assert_eq!(&encoded[0..4], &42u32.to_le_bytes());
        assert_eq!(&encoded[4..8], &1005u32.to_le_bytes());
        assert_eq!(&encoded[8..12], &1u32.to_le_bytes());
        assert_eq!(&encoded[12..16], &9u32.to_le_bytes());
        assert_eq!(Frame::decode_slice(&encoded).unwrap(), frame);

        let oversized = Frame {
            tx_id: 1,
            msg_id: 5,
            slot_id: 1,
            payload: vec![0u8; MAX_PAYLOAD_LEN + 1],
        };
        assert_eq!(
            oversized.encode(),
            Err(ProtocolError::PayloadTooLarge(MAX_PAYLOAD_LEN + 1))
        );

        let mut bad_hdr = [0u8; HEADER_LEN];
        bad_hdr[12..16].copy_from_slice(&((MAX_PAYLOAD_LEN as u32) + 1).to_le_bytes());
        assert_eq!(
            Frame::decode_header(&bad_hdr),
            Err(ProtocolError::PayloadTooLarge(MAX_PAYLOAD_LEN + 1))
        );
    }

    #[test]
    fn hex_codec_validates_strictly() {
        assert_eq!(decode_hex("").unwrap(), Vec::<u8>::new());
        assert_eq!(decode_hex("3b9f9580").unwrap(), vec![0x3b, 0x9f, 0x95, 0x80]);
        assert_eq!(encode_hex(&[0x3b, 0x9f, 0x00, 0xa4]), "3B9F00A4");
        assert_eq!(decode_hex("abc"), Err(ProtocolError::InvalidHex));
        assert_eq!(decode_hex("0g"), Err(ProtocolError::InvalidHex));
    }

    #[test]
    fn full_vsim_session_lifecycle() {
        let mut engine = VsimEngine::new(3);
        let actions = engine.on_client_connected();
        assert_eq!(
            actions,
            vec![Action::SendVsimNotification {
                serial: 1,
                tx_id: 0,
                event_id: EVENT_EXTERNAL_SIM_CONNECTED,
                sim_type: 1,
            }]
        );

        assert!(engine
            .on_client_frame(Frame {
                tx_id: 1,
                msg_id: MSG_ID_INITIALIZATION,
                slot_id: 0,
                payload: Vec::new(),
            })
            .is_empty());

        let cap = engine.on_client_frame(Frame {
            tx_id: 2,
            msg_id: MSG_ID_GET_PLATFORM_CAPABILITY,
            slot_id: 0,
            payload: encode_i32_list(&[2, 2]),
        });
        assert_eq!(
            cap,
            vec![Action::SendClient(Frame {
                tx_id: 2,
                msg_id: MSG_ID_GET_PLATFORM_CAPABILITY_RESPONSE,
                slot_id: 0,
                payload: encode_i32_list(&[0, 0, 3, 1]),
            })]
        );

        let enable = engine.on_client_frame(Frame {
            tx_id: 3,
            msg_id: MSG_ID_EVENT_REQUEST,
            slot_id: 1,
            payload: encode_i32_list(&[EVENT_ENABLE_EXTERNAL_SIM, 2]),
        });
        assert_eq!(
            enable,
            vec![Action::SendVsimNotification {
                serial: 2,
                tx_id: 3,
                event_id: EVENT_ENABLE_EXTERNAL_SIM,
                sim_type: 2,
            }]
        );
        let enable_rsp = engine.on_ril_notification_response(2, 0);
        assert_eq!(
            enable_rsp,
            vec![Action::SendClient(Frame {
                tx_id: 3,
                msg_id: MSG_ID_EVENT_RESPONSE,
                slot_id: 1,
                payload: encode_i32_list(&[0]),
            })]
        );
        assert_eq!(engine.slot_state(), SlotState::Enabled);

        let plugin = engine.on_client_frame(Frame {
            tx_id: 4,
            msg_id: MSG_ID_EVENT_REQUEST,
            slot_id: 1,
            payload: encode_i32_list(&[EVENT_PLUG_IN, 2]),
        });
        assert_eq!(
            plugin,
            vec![Action::SendVsimNotification {
                serial: 3,
                tx_id: 4,
                event_id: EVENT_PLUG_IN,
                sim_type: 2,
            }]
        );
        assert_eq!(engine.on_ril_notification_response(3, 0).len(), 1);
        assert_eq!(engine.slot_state(), SlotState::PluggedIn);

        let reset_req = engine.on_ril_event_indication(100, MSG_ID_UICC_RESET_REQUEST as i32, "", 1000);
        assert_eq!(
            reset_req,
            vec![Action::SendClient(Frame {
                tx_id: 100,
                msg_id: MSG_ID_UICC_RESET_REQUEST,
                slot_id: 1,
                payload: Vec::new(),
            })]
        );
        assert_eq!(engine.next_deadline_ms(), Some(14_000));

        let mut atr_payload = encode_i32_list(&[0, 4]);
        atr_payload.extend_from_slice(&[0x3b, 0x9f, 0x95, 0x80]);
        let reset_rsp = engine.on_client_frame(Frame {
            tx_id: 100,
            msg_id: MSG_ID_UICC_RESET_RESPONSE,
            slot_id: 1,
            payload: atr_payload,
        });
        assert_eq!(
            reset_rsp,
            vec![Action::SendVsimOperation {
                serial: 4,
                tx_id: 100,
                event_id: MSG_ID_UICC_RESET_RESPONSE as i32,
                result: 0,
                data: vec![0x3b, 0x9f, 0x95, 0x80],
            }]
        );
        assert_eq!(engine.next_deadline_ms(), None);

        let apdu_req = engine.on_ril_event_indication(
            101,
            MSG_ID_RIL_INTERNAL_APDU_REQUEST as i32,
            "00A4040000",
            2000,
        );
        assert_eq!(
            apdu_req,
            vec![Action::SendClient(Frame {
                tx_id: 101,
                msg_id: MSG_ID_UICC_APDU_REQUEST,
                slot_id: 1,
                payload: encode_len_prefixed_bytes(&[0x00, 0xa4, 0x04, 0x00, 0x00]).unwrap(),
            })]
        );

        let mut rapdu_payload = encode_i32_list(&[0, 2]);
        rapdu_payload.extend_from_slice(&[0x90, 0x00]);
        let apdu_rsp = engine.on_client_frame(Frame {
            tx_id: 101,
            msg_id: MSG_ID_UICC_APDU_RESPONSE,
            slot_id: 1,
            payload: rapdu_payload,
        });
        assert_eq!(
            apdu_rsp,
            vec![Action::SendVsimOperation {
                serial: 5,
                tx_id: 101,
                event_id: MSG_ID_UICC_APDU_RESPONSE as i32,
                result: 0,
                data: vec![0x90, 0x00],
            }]
        );

        let fin = engine.on_client_frame(Frame {
            tx_id: 9,
            msg_id: MSG_ID_FINALIZATION,
            slot_id: 0,
            payload: Vec::new(),
        });
        assert_eq!(
            fin,
            vec![
                Action::SendVsimNotification {
                    serial: 6,
                    tx_id: 0,
                    event_id: EVENT_PLUG_OUT,
                    sim_type: 0,
                },
                Action::SendVsimNotification {
                    serial: 7,
                    tx_id: 0,
                    event_id: EVENT_DISABLE_EXTERNAL_SIM,
                    sim_type: 0,
                },
                Action::SendVsimNotification {
                    serial: 8,
                    tx_id: 0,
                    event_id: EVENT_EXTERNAL_SIM_CONNECTED,
                    sim_type: 0,
                },
                Action::CloseClient,
            ]
        );
        assert_eq!(engine.slot_state(), SlotState::Idle);
        assert!(!engine.client_connected());
    }

    #[test]
    fn timeout_and_abrupt_disconnect_unblock_modem() {
        let mut engine = VsimEngine::new(3);
        let _ = engine.on_client_connected();

        let _ = engine.on_client_frame(Frame {
            tx_id: 1,
            msg_id: MSG_ID_EVENT_REQUEST,
            slot_id: 1,
            payload: encode_i32_list(&[EVENT_SET_TIMEOUT_TIMER, 5]),
        });
        assert_eq!(engine.timeout_ms(), 5_000);

        let _ = engine.on_client_frame(Frame {
            tx_id: 2,
            msg_id: MSG_ID_EVENT_REQUEST,
            slot_id: 1,
            payload: encode_i32_list(&[EVENT_PLUG_IN, 2]),
        });
        let _ = engine.on_ril_event_indication(50, MSG_ID_UICC_APDU_REQUEST as i32, "00B0000004", 10_000);
        assert_eq!(engine.next_deadline_ms(), Some(15_000));
        assert!(engine.on_tick(14_999).is_empty());

        let timeout_actions = engine.on_tick(15_000);
        assert_eq!(timeout_actions.len(), 1);
        match &timeout_actions[0] {
            Action::SendVsimOperation {
                tx_id,
                event_id,
                result,
                data,
                ..
            } => {
                assert_eq!(*tx_id, 50);
                assert_eq!(*event_id, MSG_ID_UICC_APDU_RESPONSE as i32);
                assert_eq!(*result, RESPONSE_RESULT_GENERIC_ERROR);
                assert_eq!(data, &vec![0, 0]);
            }
            other => panic!("unexpected action: {:?}", other),
        }

        let _ = engine.on_ril_event_indication(51, MSG_ID_UICC_RESET_REQUEST as i32, "", 20_000);
        let drop_actions = engine.on_client_disconnected();
        assert_eq!(drop_actions.len(), 4);
        assert!(matches!(
            drop_actions[0],
            Action::SendVsimOperation {
                tx_id: 51,
                event_id: 4,
                result: -1,
                ..
            }
        ));
        assert!(matches!(
            drop_actions[1],
            Action::SendVsimNotification {
                event_id: EVENT_PLUG_OUT,
                ..
            }
        ));
        assert!(matches!(
            drop_actions[2],
            Action::SendVsimNotification {
                event_id: EVENT_DISABLE_EXTERNAL_SIM,
                ..
            }
        ));
        assert!(matches!(
            drop_actions[3],
            Action::SendVsimNotification {
                event_id: EVENT_EXTERNAL_SIM_CONNECTED,
                sim_type: 0,
                ..
            }
        ));
    }

    #[test]
    fn rejects_second_client_and_invalid_slots_and_orphan_modem_requests() {
        let mut engine = VsimEngine::new(3);
        let orphan = engine.on_ril_event_indication(77, MSG_ID_UICC_RESET_REQUEST as i32, "", 100);
        assert!(matches!(
            orphan.as_slice(),
            [Action::SendVsimOperation {
                tx_id: 77,
                event_id: 4,
                result: -1,
                ..
            }]
        ));

        let _ = engine.on_client_connected();
        assert_eq!(engine.on_client_connected(), vec![Action::CloseClient]);

        let bad_slot = engine.on_client_frame(Frame {
            tx_id: 10,
            msg_id: MSG_ID_EVENT_REQUEST,
            slot_id: 2,
            payload: encode_i32_list(&[EVENT_ENABLE_EXTERNAL_SIM, 2]),
        });
        assert_eq!(
            bad_slot,
            vec![Action::SendClient(Frame {
                tx_id: 10,
                msg_id: MSG_ID_EVENT_RESPONSE,
                slot_id: 2,
                payload: encode_i32_list(&[RESPONSE_RESULT_NOT_SUPPORT]),
            })]
        );

        let sim_dialog = engine.on_client_frame(Frame {
            tx_id: 11,
            msg_id: MSG_ID_EVENT_REQUEST,
            slot_id: 0,
            payload: encode_i32_list(&[EVENT_DISABLE_SIM_DIALOG, 1]),
        });
        assert_eq!(
            sim_dialog,
            vec![Action::SendClient(Frame {
                tx_id: 11,
                msg_id: MSG_ID_EVENT_RESPONSE,
                slot_id: 0,
                payload: encode_i32_list(&[RESPONSE_RESULT_OK]),
            })]
        );

        let bad_hex = engine.on_ril_event_indication(88, MSG_ID_UICC_APDU_REQUEST as i32, "ZZZ", 200);
        assert!(matches!(
            bad_hex.as_slice(),
            [Action::SendVsimOperation {
                tx_id: 88,
                event_id: 5,
                result: -1,
                ..
            }]
        ));

        let died = engine.on_ril_died();
        assert_eq!(died, vec![Action::CloseClient]);
        assert!(!engine.client_connected());
        assert_eq!(engine.slot_state(), SlotState::Idle);
    }

    #[test]
    fn capability_not_ready_timeout_clamping_and_error_rollback() {
        let mut engine = VsimEngine::new(0);
        let _ = engine.on_client_connected();

        let cap = engine.on_client_frame(Frame {
            tx_id: 1,
            msg_id: MSG_ID_GET_PLATFORM_CAPABILITY,
            slot_id: 0,
            payload: encode_i32_list(&[2, 2]),
        });
        assert_eq!(
            cap,
            vec![Action::SendClient(Frame {
                tx_id: 1,
                msg_id: MSG_ID_GET_PLATFORM_CAPABILITY_RESPONSE,
                slot_id: 0,
                payload: encode_i32_list(&[RESPONSE_RESULT_PLATFORM_NOT_READY, 0, 0, 0]),
            })]
        );

        let _ = engine.on_client_frame(Frame {
            tx_id: 2,
            msg_id: MSG_ID_EVENT_REQUEST,
            slot_id: 1,
            payload: encode_i32_list(&[EVENT_SET_TIMEOUT_TIMER, 0]),
        });
        assert_eq!(engine.timeout_ms(), 1_000);

        let _ = engine.on_client_frame(Frame {
            tx_id: 3,
            msg_id: MSG_ID_EVENT_REQUEST,
            slot_id: 1,
            payload: encode_i32_list(&[EVENT_SET_TIMEOUT_TIMER, 999]),
        });
        assert_eq!(engine.timeout_ms(), 60_000);

        let test_mode = engine.on_client_frame(Frame {
            tx_id: 4,
            msg_id: MSG_ID_UICC_TEST_MODE_REQUEST,
            slot_id: 1,
            payload: Vec::new(),
        });
        assert_eq!(
            test_mode,
            vec![Action::SendClient(Frame {
                tx_id: 4,
                msg_id: MSG_ID_EVENT_RESPONSE,
                slot_id: 1,
                payload: encode_i32_list(&[RESPONSE_RESULT_NOT_SUPPORT]),
            })]
        );

        let enable_actions = engine.on_client_frame(Frame {
            tx_id: 5,
            msg_id: MSG_ID_EVENT_REQUEST,
            slot_id: 1,
            payload: encode_i32_list(&[EVENT_ENABLE_EXTERNAL_SIM, 2]),
        });
        let serial = match enable_actions[0] {
            Action::SendVsimNotification { serial, .. } => serial,
            _ => panic!("expected notification"),
        };
        assert_eq!(engine.slot_state(), SlotState::Enabled);

        let err_rsp = engine.on_ril_notification_response(serial, 6);
        assert_eq!(
            err_rsp,
            vec![Action::SendClient(Frame {
                tx_id: 5,
                msg_id: MSG_ID_EVENT_RESPONSE,
                slot_id: 1,
                payload: encode_i32_list(&[RESPONSE_RESULT_GENERIC_ERROR]),
            })]
        );
        assert_eq!(engine.slot_state(), SlotState::Idle);
    }

    #[test]
    fn shutdown_mid_apdu_tears_down_cleanly() {
        let mut engine = VsimEngine::new(3);
        let _ = engine.on_client_connected();
        let _ = engine.on_client_frame(Frame {
            tx_id: 1,
            msg_id: MSG_ID_EVENT_REQUEST,
            slot_id: 1,
            payload: encode_i32_list(&[EVENT_PLUG_IN, 2]),
        });
        let _ = engine.on_ril_event_indication(
            99,
            MSG_ID_UICC_APDU_REQUEST as i32,
            "00A4040000",
            5_000,
        );
        let actions = engine.on_shutdown();
        assert_eq!(actions.len(), 5);
        assert!(matches!(
            actions[0],
            Action::SendVsimOperation {
                tx_id: 99,
                event_id: 5,
                result: -1,
                ..
            }
        ));
        assert!(matches!(
            actions[1],
            Action::SendVsimNotification {
                event_id: EVENT_PLUG_OUT,
                ..
            }
        ));
        assert!(matches!(
            actions[2],
            Action::SendVsimNotification {
                event_id: EVENT_DISABLE_EXTERNAL_SIM,
                ..
            }
        ));
        assert!(matches!(
            actions[3],
            Action::SendVsimNotification {
                event_id: EVENT_EXTERNAL_SIM_CONNECTED,
                sim_type: 0,
                ..
            }
        ));
        assert_eq!(actions[4], Action::CloseClient);
        assert_eq!(engine.slot_state(), SlotState::Idle);
        assert!(!engine.client_connected());
    }
}
