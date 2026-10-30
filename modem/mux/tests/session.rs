/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_mux_core::session::*;
use mindone_mux_core::{encode, Decoder, Frame, POLL_FINAL, UIH};

#[derive(Clone, Debug, Eq, PartialEq)]
enum Effect {
    Open(u8),
    Close(u8),
    Transmit(u8, u8, Vec<u8>),
    Ready(usize),
    Atci,
}

#[derive(Default)]
struct MemorySink {
    effects: Vec<Effect>,
    reject_next: bool,
    reject_open: Option<u8>,
}

impl Sink for MemorySink {
    fn open_endpoint(&mut self, channel: u8) -> bool {
        self.effects.push(Effect::Open(channel));
        self.reject_open != Some(channel)
    }

    fn apply(&mut self, action: Action<'_>) -> bool {
        self.effects.push(match action {
            Action::Transmit(frame) => Effect::Transmit(frame.channel(), frame.control, frame.payload.to_vec()),
            Action::CloseEndpoint(channel) => Effect::Close(channel),
            Action::Ready { refused_channels } => Effect::Ready(refused_channels),
            Action::AtciUnavailable => Effect::Atci,
        });
        !std::mem::take(&mut self.reject_next)
    }
}

fn configured() -> Session {
    Session::new(&[
        ChannelConfig { channel: 1, atci: false },
        ChannelConfig { channel: 26, atci: true },
    ], 512).unwrap()
}

fn receive(session: &mut Session, channel: u8, control: u8, sink: &mut MemorySink) {
    assert_eq!(session.receive(Frame::command(channel, control, &[]).unwrap(), sink), Ok(Handoff::None));
}

fn running(sink: &mut MemorySink) -> Session {
    let mut session = configured();
    session.begin(100, sink).unwrap();
    for channel in [0, 1, 26] {
        receive(&mut session, channel, 0x73, sink);
    }
    assert_eq!(session.phase(), Phase::Running);
    session
}

#[test]
fn configuration_and_begin_are_bounded_and_ordered() {
    for channels in [vec![ChannelConfig { channel: 0, atci: false }],
                     vec![ChannelConfig { channel: 64, atci: false }],
                     vec![ChannelConfig { channel: 1, atci: false }; 2]] {
        assert!(matches!(Session::new(&channels, 512), Err(Error::Configuration)));
    }
    assert!(matches!(Session::new(&[], 0), Err(Error::Configuration)));
    let mut session = configured();
    let mut sink = MemorySink::default();
    session.begin(100, &mut sink).unwrap();
    assert_eq!(sink.effects, [Effect::Transmit(0, 0x3f, vec![]), Effect::Open(1),
        Effect::Transmit(1, 0x3f, vec![]), Effect::Open(26), Effect::Transmit(26, 0x3f, vec![])]);
    assert_eq!(session.setup_deadline(), 30100);
    assert_eq!(session.unresolved(), 3);
    assert_eq!(session.begin(101, &mut sink), Err(Error::AlreadyStarted));
    assert!(!session.can_read_endpoint(1, true));
}

#[test]
fn acknowledgements_resolve_once_and_atci_refusal_is_explicit() {
    let mut session = configured();
    let mut sink = MemorySink::default();
    session.begin(100, &mut sink).unwrap();
    receive(&mut session, 0, 0x73, &mut sink);
    receive(&mut session, 0, 0x73, &mut sink);
    assert_eq!(session.unresolved(), 2);
    receive(&mut session, 26, 0x1f, &mut sink);
    assert_eq!(sink.effects.last(), Some(&Effect::Atci));
    assert!(session.channel(26).unwrap().refused);
    receive(&mut session, 26, 0x1f, &mut sink);
    assert_eq!(session.unresolved(), 1);
    receive(&mut session, 1, 0x73, &mut sink);
    assert_eq!(sink.effects.last(), Some(&Effect::Ready(1)));
    assert_eq!(session.application_input(26), Ok(ApplicationInput::ReportRefusal));
    assert_eq!(session.application_input(26), Ok(ApplicationInput::Discard));
    receive(&mut session, 26, 0x3f, &mut sink);
    assert!(!session.channel(26).unwrap().refused);
    assert!(!session.channel(26).unwrap().refusal_reported);
    assert_eq!(session.application_input(26), Ok(ApplicationInput::Forward));
    assert_eq!(sink.effects.iter().filter(|value| matches!(value, Effect::Ready(_))).count(), 1);
}

#[test]
fn unknown_and_disconnected_channels_are_answered_without_exposing_data() {
    let mut sink = MemorySink::default();
    let mut session = running(&mut sink);
    sink.effects.clear();
    for control in [0x3f, 0x53] {
        receive(&mut session, 63, control, &mut sink);
        assert_eq!(sink.effects.last(), Some(&Effect::Transmit(63, 0x1f, vec![])));
    }
    receive(&mut session, 1, 0x53, &mut sink);
    assert!(!session.channel(1).unwrap().endpoint_present);
    assert_eq!(sink.effects.last(), Some(&Effect::Close(1)));
    let frame = Frame::command(1, UIH, b"drop").unwrap();
    assert_eq!(session.receive(frame, &mut sink), Ok(Handoff::EndpointMissing { channel: 1, bytes: 4 }));
    receive(&mut session, 1, 0x53, &mut sink);
    assert_eq!(sink.effects.last(), Some(&Effect::Transmit(1, 0x1f, vec![])));
    sink.reject_open = Some(1);
    receive(&mut session, 1, 0x3f, &mut sink);
    assert_eq!(sink.effects.last(), Some(&Effect::Transmit(1, 0x1f, vec![])));
    assert!(!session.channel(1).unwrap().open);
    sink.reject_open = None;
    receive(&mut session, 1, 0x3f, &mut sink);
    assert!(session.channel(1).unwrap().endpoint_present);
    assert!(session.channel(1).unwrap().open);
}

#[test]
fn local_disconnect_ack_and_peer_control_close_have_distinct_states() {
    let mut sink = MemorySink::default();
    let mut session = running(&mut sink);
    session.request_disconnect(1, &mut sink).unwrap();
    assert!(session.channel(1).unwrap().disconnecting);
    assert_eq!(sink.effects.last(), Some(&Effect::Transmit(1, 0x53, vec![])));
    receive(&mut session, 1, 0x73, &mut sink);
    assert!(!session.channel(1).unwrap().open);
    assert!(!session.channel(1).unwrap().disconnecting);
    assert!(session.channel(1).unwrap().endpoint_present);
    receive(&mut session, 0, 0x53, &mut sink);
    assert_eq!(session.phase(), Phase::PeerClosing);
    assert!(!session.can_read_endpoint(26, true));
    assert_eq!(session.poll_timeout(u64::MAX, true), Ok(None));
    let mut session = configured();
    session.begin(100, &mut sink).unwrap();
    receive(&mut session, 0, 0x1f, &mut sink);
    assert_eq!(session.phase(), Phase::Closing);
}

#[test]
fn flow_control_and_recreation_preserve_the_protocol_channel() {
    let mut sink = MemorySink::default();
    let mut session = running(&mut sink);
    assert!(session.can_read_endpoint(1, true));
    assert!(!session.can_read_endpoint(1, false));
    session.apply_peer_signals(1, 2).unwrap();
    assert!(!session.can_read_endpoint(1, true));
    session.apply_peer_signals(1, 0).unwrap();
    assert!(session.can_read_endpoint(1, true));
    session.set_local_pause(1, true, &mut sink).unwrap();
    assert_eq!(sink.effects.last(), Some(&Effect::Transmit(0, 0xff, vec![0xe3, 5, 7, 0x8f])));
    sink.effects.clear();
    session.recreate_endpoint(1, &mut sink).unwrap();
    assert_eq!(sink.effects, [Effect::Close(1),
        Effect::Transmit(0, 0xff, vec![0xe3, 5, 7, 0x8d]), Effect::Open(1)]);
    let state = session.channel(1).unwrap();
    assert!(state.open && state.endpoint_present);
    assert!(!state.local_paused && !state.peer_paused);
    session.apply_peer_signals(255, 2).unwrap();
    assert!(session.channel(255).is_none());
}

#[test]
fn poll_final_echo_is_consumed_only_by_an_eligible_status_response() {
    let mut sink = MemorySink::default();
    let mut session = running(&mut sink);
    let incoming = Frame::command(63, UIH | POLL_FINAL, b"unknown").unwrap();
    assert_eq!(session.receive(incoming, &mut sink), Ok(Handoff::None));
    session.send_control(UIH, b"other", &mut sink).unwrap();
    assert!(session.poll_final_pending());
    session.send_control(UIH | POLL_FINAL, &[0xe1, 5, 7, 0x8d], &mut sink).unwrap();
    assert!(session.poll_final_pending());
    session.send_control(UIH, &[0xe1, 5, 7, 0x8d], &mut sink).unwrap();
    assert!(!session.poll_final_pending());
    assert_eq!(sink.effects.last(), Some(&Effect::Transmit(0, 0xff, vec![0xe1, 5, 7, 0x8d])));
}

#[test]
fn restart_clears_channel_state_but_does_not_publish_ready_twice() {
    let mut sink = MemorySink::default();
    let mut session = running(&mut sink);
    session.set_local_pause(1, true, &mut sink).unwrap();
    session.apply_peer_signals(0, 2).unwrap();
    session.reset_after_transport_close();
    assert_eq!(session.phase(), Phase::Idle);
    assert_eq!(session.unresolved(), 0);
    assert!(!session.poll_final_pending());
    assert!(!session.channel(1).unwrap().endpoint_present);
    assert!(session.channel(0).unwrap().peer_paused);
    session.begin(1000, &mut sink).unwrap();
    for channel in [0, 1, 26] {
        receive(&mut session, channel, 0x73, &mut sink);
    }
    assert_eq!(sink.effects.iter().filter(|value| matches!(value, Effect::Ready(_))).count(), 1);
}

#[test]
fn setup_deadline_and_clock_overflow_never_create_unbounded_waits() {
    let mut sink = MemorySink::default();
    let mut session = configured();
    assert_eq!(session.begin(u64::MAX, &mut sink), Err(Error::ClockOverflow));
    assert!(sink.effects.is_empty());
    assert_eq!(session.phase(), Phase::Idle);
    session.begin(100, &mut sink).unwrap();
    assert_eq!(session.poll_timeout(100, false), Ok(Some(30000)));
    assert_eq!(session.poll_timeout(101, true), Ok(Some(10)));
    assert_eq!(session.poll_timeout(30099, true), Ok(Some(1)));
    assert_eq!(session.poll_timeout(30100, false), Err(Error::SetupTimeout));
    assert_eq!(session.phase(), Phase::Failed);
    assert_eq!(session.send_control(UIH, b"x", &mut sink), Err(Error::Failed));
}

#[test]
fn required_effect_rejection_is_terminal_and_failed_ready_can_retry_after_reset() {
    let mut sink = MemorySink { reject_next: true, ..MemorySink::default() };
    let mut session = configured();
    assert_eq!(session.begin(100, &mut sink), Err(Error::EffectRejected));
    assert_eq!(session.phase(), Phase::Failed);
    let count = sink.effects.len();
    assert_eq!(session.recreate_endpoint(1, &mut sink), Err(Error::Failed));
    assert_eq!(sink.effects.len(), count);
    session.reset_after_transport_close();
    session.begin(100, &mut sink).unwrap();
    receive(&mut session, 0, 0x73, &mut sink);
    receive(&mut session, 1, 0x73, &mut sink);
    sink.reject_next = true;
    assert_eq!(session.receive(Frame::command(26, 0x73, &[]).unwrap(), &mut sink), Err(Error::EffectRejected));
    assert!(!session.ready_started());
    session.reset_after_transport_close();
    session.begin(1000, &mut sink).unwrap();
    for channel in [0, 1, 26] {
        receive(&mut session, channel, 0x73, &mut sink);
    }
    assert!(session.ready_started());
}

#[test]
fn endpoint_creation_and_oversized_transmit_failures_are_explicit() {
    let mut sink = MemorySink { reject_open: Some(1), ..MemorySink::default() };
    let mut session = configured();
    assert_eq!(session.begin(100, &mut sink), Err(Error::EndpointUnavailable));
    assert_eq!(session.phase(), Phase::Failed);
    sink.reject_open = None;
    session.reset_after_transport_close();
    session.begin(100, &mut sink).unwrap();
    assert_eq!(session.send_control(UIH, &[0; 513], &mut sink), Err(Error::OversizedPayload));
    assert_eq!(session.phase(), Phase::Failed);
}

#[test]
fn only_complete_validated_frames_reach_the_session_handoff() {
    let mut sink = MemorySink::default();
    let mut session = running(&mut sink);
    let mut wire = [0; 64];
    let length = encode(Frame::command(1, UIH, b"hello").unwrap(), 512, &mut wire).unwrap();
    let mut decoder = Decoder::<519>::new(512).unwrap();
    decoder.push(&wire[..length - 1]).unwrap();
    assert!(decoder.next_frame().is_none());
    decoder.push(&wire[length - 1..length]).unwrap();
    let frame = decoder.next_frame().unwrap();
    assert_eq!(session.receive(frame, &mut sink), Ok(Handoff::Data { channel: 1, payload: b"hello" }));
    assert!(decoder.next_frame().is_none());
    decoder.reset();
    wire[length - 2] ^= 1;
    decoder.push(&wire[..length]).unwrap();
    assert!(decoder.next_frame().is_none());
    assert_eq!(session.unresolved(), 0);
}
