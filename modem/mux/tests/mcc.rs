/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_mux_core::mcc::*;
use mindone_mux_core::session::{Action, ChannelConfig, Handoff, Phase, Session, Sink};
use mindone_mux_core::{encode, Frame, POLL_FINAL, UIH};

fn message(kind: u8, value: &[u8]) -> Vec<u8> {
    let mut bytes = vec![kind];
    let mut length = value.len();
    loop {
        let mut byte = (length as u8 & 0x7f) << 1;
        length >>= 7;
        if length == 0 { byte |= 1; }
        bytes.push(byte);
        if length == 0 { break; }
    }
    bytes.extend_from_slice(value);
    bytes
}

#[derive(Default)]
struct Effects {
    wire: Vec<Vec<u8>>,
    reject: bool,
}

impl Sink for Effects {
    fn open_endpoint(&mut self, _: u8) -> bool { true }

    fn apply(&mut self, effect: Action<'_>) -> bool {
        if self.reject { return false; }
        if let Action::Transmit(frame) = effect {
            let mut bytes = vec![0; frame.payload.len() + 7];
            let length = encode(frame, 32767, &mut bytes).unwrap();
            bytes.truncate(length);
            self.wire.push(bytes);
        }
        true
    }
}

fn session() -> (Session, Effects) {
    let mut session = Session::new(&[ChannelConfig { channel: 1, atci: false }], 512).unwrap();
    let mut sink = Effects::default();
    session.begin(0, &mut sink).unwrap();
    for channel in [0, 1] {
        session.receive(Frame::command(channel, 0x73, &[]).unwrap(), &mut sink).unwrap();
    }
    sink.wire.clear();
    (session, sink)
}

#[test]
fn supported_single_commands_echo_exactly_and_declare_effects() {
    for (kind, value, effect) in [
        (0x23, b"test".as_slice(), Effect::None),
        (0x43, b"".as_slice(), Effect::None),
        (0x83, [0; 8].as_slice(), Effect::None),
        (0xc3, b"".as_slice(), Effect::PeerClosing),
        (0xe3, [7, 0x8f].as_slice(), Effect::PeerSignals { channel: 1, signals: 0x8f }),
        (0xe3, [7, 0x8e, 1].as_slice(), Effect::PeerSignals { channel: 1, signals: 0x8e }),
    ] {
        let input = message(kind, value);
        let mut output = vec![0x55; input.len() + 3];
        let result = prepare(&input, 512, &mut output).unwrap();
        assert_eq!(result.effect, effect);
        let reply = result.reply.unwrap();
        assert_eq!(reply.control, UIH);
        assert_eq!(reply.payload[0], kind & !2);
        assert_eq!(&reply.payload[1..], &input[1..]);
        assert_eq!(&output[input.len()..], &[0x55; 3]);
    }
}

#[test]
fn replies_never_echo_or_mutate_the_output() {
    for kind in [0x21, 0x41, 0x81, 0xc1, 0xe1, 0x11, 0x51] {
        let input = message(kind, b"acknowledged");
        let mut output = [0x6b; 32];
        assert_eq!(prepare(&input, 512, &mut output).unwrap(), Outcome { effect: Effect::None, reply: None });
        assert_eq!(output, [0x6b; 32]);
    }
}

#[test]
fn unknown_commands_return_bounded_nsc_type_prefixes() {
    for length in [1, 2, 8, 9, 128, 510] {
        let mut input = vec![0x52; length];
        *input.last_mut().unwrap() |= 1;
        input.push(1);
        let mut output = [0x33; 16];
        let result = prepare(&input, 512, &mut output).unwrap();
        assert_eq!(result.effect, Effect::None);
        let payload = result.reply.unwrap().payload;
        let prefix = length.min(8);
        assert_eq!(payload.len(), prefix + 2);
        assert_eq!(&payload[..2], &[0x11, (prefix as u8) << 1 | 1]);
        assert_eq!(&payload[2..], &input[..prefix]);
        assert_eq!(&output[prefix + 2..], vec![0x33; 16 - prefix - 2]);
    }
}

#[test]
fn extended_lengths_are_bounded_and_preserve_valid_encoding() {
    for length in [0, 1, 127, 128, 16383, 16384, 32763] {
        let input = message(0x23, &vec![0xa5; length]);
        let mut output = vec![0; input.len()];
        let result = prepare(&input, 32767, &mut output).unwrap();
        let response = result.reply.unwrap();
        assert_eq!(response.payload.len(), input.len());
        assert_eq!(&response.payload[1..], &input[1..]);
    }
}

#[test]
fn malformed_type_length_trailing_and_body_never_change_output() {
    let cases: Vec<(Vec<u8>, Error)> = vec![
        (vec![], Error::MissingTypeTerminator),
        (vec![0x22; 512], Error::MissingTypeTerminator),
        (vec![0x23], Error::MissingLengthTerminator),
        (vec![0x23, 0], Error::MissingLengthTerminator),
        (vec![0x23, 0, 0, 0, 1], Error::LengthEncoding),
        (vec![0x23, 5, 1], Error::LengthMismatch),
        (vec![0x23, 1, 1], Error::TrailingBytes),
        (vec![0xc3, 1, 0x23, 1], Error::TrailingBytes),
        (message(0xc3, &[0]), Error::InvalidBody),
        (message(0x43, &[0]), Error::InvalidBody),
        (message(0x83, &[0; 7]), Error::InvalidBody),
        (message(0xe3, &[7]), Error::InvalidBody),
        (message(0xe3, &[7, 0x8f, 1, 1]), Error::InvalidBody),
        (message(0xe3, &[4, 0x8f]), Error::InvalidBody),
        (message(0xe3, &[5, 0x8f]), Error::InvalidBody),
        (message(0xe3, &[7, 0x8e]), Error::InvalidBody),
        (message(0xe3, &[7, 0x8f, 1]), Error::InvalidBody),
        (message(0xe3, &[7, 0x8e, 0]), Error::InvalidBody),
    ];
    for (input, expected) in cases {
        let mut output = [0x39; 512];
        assert_eq!(prepare(&input, 512, &mut output), Err(expected));
        assert_eq!(output, [0x39; 512]);
    }
}

#[test]
fn every_truncated_or_suffixed_supported_message_rejects_atomically() {
    let messages = [message(0x23, &[0xa5; 128]), message(0x43, &[]),
        message(0x83, &[0; 8]), message(0xc3, &[]), message(0xe3, &[7, 0x8f])];
    for input in &messages {
        for end in 0..input.len() {
            let mut output = [0x6d; 512];
            assert!(prepare(&input[..end], 512, &mut output).is_err());
            assert_eq!(output, [0x6d; 512]);
        }
        for suffix in 0..=255 {
            let mut input = input.clone();
            input.push(suffix);
            let mut output = [0x6d; 512];
            assert_eq!(prepare(&input, 512, &mut output), Err(Error::TrailingBytes));
            assert_eq!(output, [0x6d; 512]);
        }
        for second in &messages {
            let mut input = input.clone();
            input.extend_from_slice(second);
            let mut output = [0x6d; 512];
            assert_eq!(prepare(&input, 512, &mut output), Err(Error::TrailingBytes));
            assert_eq!(output, [0x6d; 512]);
        }
    }
}

#[test]
fn all_rejections_preflight_input_reply_and_output_capacity() {
    let input = message(0xe3, &[7, 0x8f]);
    for capacity in 0..input.len() {
        let mut output = [0x5c; 32];
        assert_eq!(prepare(&input, 512, &mut output[..capacity]), Err(Error::InsufficientCapacity));
        assert_eq!(output, [0x5c; 32]);
    }
    let mut output = [0x5c; 32];
    assert_eq!(prepare(&input, 0, &mut output), Err(Error::Configuration));
    assert_eq!(prepare(&input, 32768, &mut output), Err(Error::Configuration));
    assert_eq!(prepare(&input, 3, &mut output), Err(Error::OversizedInput));
    assert_eq!(prepare(&[0x53, 1], 2, &mut output), Err(Error::OversizedReply));
    assert_eq!(output, [0x5c; 32]);
}

#[test]
fn copied_reply_outlives_input_and_releases_the_session_handoff_borrow() {
    let (mut session, mut sink) = session();
    let mut output = [0; 512];
    let outcome = {
        let input = message(0xe3, &[7, 0x8f]);
        let frame = Frame::command(0, UIH | POLL_FINAL, &input).unwrap();
        let handoff = session.receive(frame, &mut sink).unwrap();
        let Handoff::Control(payload) = handoff else { panic!("Missing control handoff"); };
        prepare(payload, 512, &mut output).unwrap()
    };
    let reply = outcome.reply.unwrap();
    session.send_control(reply.control, reply.payload, &mut sink).unwrap();
    if let Effect::PeerSignals { channel, signals } = outcome.effect {
        session.apply_peer_signals(channel, signals).unwrap();
    }
    assert!(session.channel(1).unwrap().peer_paused);
    assert_eq!(sink.wire.len(), 1);
    assert_eq!(sink.wire[0][2], UIH | POLL_FINAL);
    assert!(!session.poll_final_pending());
}

#[test]
fn failed_reply_admission_is_terminal_before_deferred_peer_effects() {
    for (kind, body) in [(0xe3, vec![7, 0x8f]), (0xc3, vec![])] {
        let (mut session, mut sink) = session();
        let mut output = [0; 512];
        let outcome = prepare(&message(kind, &body), 512, &mut output).unwrap();
        assert!(matches!(outcome.effect, Effect::PeerSignals { .. } | Effect::PeerClosing));
        assert!(!session.channel(1).unwrap().peer_paused);
        assert_eq!(session.phase(), Phase::Running);
        sink.reject = true;
        let reply = outcome.reply.unwrap();
        assert!(session.send_control(reply.control, reply.payload, &mut sink).is_err());
        assert_eq!(session.phase(), Phase::Failed);
        assert!(!session.channel(1).unwrap().peer_paused);
        assert!(sink.wire.is_empty());
    }
}
