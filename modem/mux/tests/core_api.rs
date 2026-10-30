/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_mux_core::mcc;
use mindone_mux_core::session::{self, Action, ChannelConfig, Handoff, Phase, Session, Sink};
use mindone_mux_core::transport::{self, ByteReader, ByteWriter, Fault, FrameSink, IoResult, Receiver, Stop, TransmitQueue, TurnBudget};
use mindone_mux_core::{encode, Frame, POLL_FINAL, UIH};
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;

#[derive(Default)]
struct Memory {
    effects: usize,
    reject: bool,
    frames: Vec<Vec<u8>>,
}

impl Sink for Memory {
    fn open_endpoint(&mut self, _: u8) -> bool { true }
    fn apply(&mut self, action: Action<'_>) -> bool {
        self.effects += 1;
        if self.reject { return false; }
        if let Action::Transmit(frame) = action { self.frames.push(frame.payload.to_vec()); }
        true
    }
}

fn configured() -> Session {
    Session::new(&[ChannelConfig { channel: 1, atci: false }], 512).unwrap()
}

fn receive(session: &mut Session, channel: u8, control: u8, sink: &mut Memory) {
    assert_eq!(session.receive(Frame::command(channel, control, &[]).unwrap(), sink), Ok(Handoff::None));
}

fn snapshot(session: &Session) -> (Vec<Option<session::Channel>>, usize, bool, bool, bool, u64) {
    ((0..64).map(|channel| session.channel(channel)).collect(), session.unresolved(),
     session.setup_done(), session.ready_started(), session.poll_final_pending(), session.setup_deadline())
}

#[test]
fn zero_progress_refunds_bytes_but_never_calls() {
    let mut budget = TurnBudget::new(64, 65536).unwrap();
    for index in 0..64 {
        let permit = budget.permit().unwrap();
        assert_eq!(permit.max_bytes(), 65536);
        permit.complete(0).unwrap();
        assert_eq!(budget.calls(), index + 1);
        assert_eq!(budget.bytes(), 0);
    }
    assert!(budget.permit().is_none());
    assert_eq!(budget.calls(), 64);
}

#[test]
fn partial_and_invalid_completion_preserve_the_accounted_prefix() {
    let mut budget = TurnBudget::new(5, 19).unwrap();
    let permit = budget.permit().unwrap();
    assert_eq!(permit.max_bytes(), 19);
    permit.complete(7).unwrap();
    let permit = budget.permit().unwrap();
    assert_eq!(permit.max_bytes(), 12);
    assert_eq!(permit.complete(13), Err(transport::Error::InvalidProgress));
    assert_eq!((budget.calls(), budget.bytes(), budget.available_bytes()), (2, 19, 0));
    assert!(budget.permit().is_none());
    let mut budget = TurnBudget::new(5, 19).unwrap();
    budget.permit().unwrap().complete(7).unwrap();
    let permit = budget.permit().unwrap();
    assert_eq!(permit.complete(usize::MAX), Err(transport::Error::InvalidProgress));
    assert_eq!((budget.calls(), budget.bytes()), (2, 19));
    assert!(budget.permit().is_none());
    let mut budget = TurnBudget::new(5, 19).unwrap();
    budget.permit().unwrap().complete(7).unwrap();
    budget.permit().unwrap().complete(12).unwrap();
    assert_eq!((budget.calls(), budget.bytes(), budget.available_bytes()), (2, 19, 0));
    assert!(budget.permit().is_none());
    assert_eq!(budget.calls(), 2);
}

#[test]
fn actual_io_then_drop_forget_or_reject_cannot_reuse_reserved_bytes() {
    for termination in 0..3 {
        let (mut writer, mut reader) = UnixStream::pair().unwrap();
        writer.set_nonblocking(true).unwrap();
        reader.set_nonblocking(true).unwrap();
        let mut budget = TurnBudget::new(8, 8).unwrap();
        let permit = budget.permit().unwrap();
        assert_eq!(permit.max_bytes(), 8);
        assert_eq!(writer.write(b"x").unwrap(), 1);
        match termination {
            0 => drop(permit),
            1 => std::mem::forget(permit),
            _ => assert_eq!(permit.complete(9), Err(transport::Error::InvalidProgress)),
        }
        assert_eq!((budget.calls(), budget.bytes(), budget.available_bytes()), (1, 8, 0));
        assert!(budget.permit().is_none());
        let mut observed = [0; 8];
        assert_eq!(reader.read(&mut observed).unwrap(), 1);
        assert_eq!(observed[0], b'x');
    }
    let mut budget = TurnBudget::new(2, 8).unwrap();
    budget.permit().unwrap().complete(0).unwrap();
    assert_eq!((budget.calls(), budget.bytes()), (1, 0));
    budget.permit().unwrap().complete(1).unwrap();
    assert_eq!((budget.calls(), budget.bytes()), (2, 1));
}

#[test]
fn exact_maximum_bytes_and_calls_have_independent_stops() {
    let mut budget = TurnBudget::new(64, 65536).unwrap();
    budget.permit().unwrap().complete(65536).unwrap();
    assert_eq!((budget.calls(), budget.bytes()), (1, 65536));
    assert!(budget.permit().is_none());
    let mut budget = TurnBudget::new(1, 65536).unwrap();
    budget.permit().unwrap().complete(3).unwrap();
    assert!(budget.permit().is_none());
    assert_eq!((budget.calls(), budget.bytes()), (1, 3));
}

struct Writer(Vec<u8>);
impl ByteWriter for Writer {
    fn write(&mut self, input: &[u8]) -> IoResult {
        let count = input.len().min(2);
        self.0.extend_from_slice(&input[..count]);
        IoResult::Transferred(count)
    }
}

struct Reader(Vec<u8>);
impl ByteReader for Reader {
    fn read(&mut self, output: &mut [u8]) -> IoResult {
        if self.0.is_empty() { return IoResult::Fault(Fault::WouldBlock); }
        let count = output.len().min(self.0.len());
        output[..count].copy_from_slice(&self.0[..count]);
        self.0.drain(..count);
        IoResult::Transferred(count)
    }
}

impl FrameSink for Memory {
    fn frame(&mut self, frame: Frame<'_>) -> bool {
        self.frames.push(frame.payload.to_vec());
        true
    }
}

#[test]
fn raw_permits_queue_drain_and_receiver_share_one_budget() {
    let mut budget = TurnBudget::new(8, 32).unwrap();
    budget.permit().unwrap().complete(3).unwrap();
    let mut queue = TransmitQueue::new(512).unwrap();
    queue.admit_frame(Frame::command(1, UIH, b"a").unwrap()).unwrap();
    let mut writer = Writer(Vec::new());
    let report = queue.drain(&mut writer, &mut budget);
    assert_eq!((report.stop, report.calls, report.bytes), (Stop::Complete, 4, 7));
    assert_eq!((budget.calls(), budget.bytes()), (5, 10));
    let mut wire = [0; 32];
    let count = encode(Frame::command(1, UIH, b"bc").unwrap(), 512, &mut wire).unwrap();
    let mut reader = Reader(wire[..count].to_vec());
    let mut receiver = Receiver::new(512).unwrap();
    let mut sink = Memory::default();
    let report = receiver.read_turn(&mut reader, &mut [0; 32], &mut sink, &mut budget);
    assert_eq!((report.stop, report.calls, report.bytes, report.frames), (Stop::Blocked, 2, 8, 1));
    assert_eq!((budget.calls(), budget.bytes()), (7, 18));
    assert_eq!(sink.frames, [b"bc".to_vec()]);
    drop(budget.permit().unwrap());
    assert!(budget.permit().is_none());
    assert_eq!((budget.calls(), budget.bytes()), (8, 32));
}

#[test]
fn deterministic_permit_model_checks_reservation_drop_reject_and_progress() {
    let mut random = 0x292929u64;
    for _ in 0..1000 {
        random = random.wrapping_mul(6364136223846793005).wrapping_add(1);
        let limit_calls = (random as usize % 64) + 1;
        let limit_bytes = ((random >> 10) as usize % 65536) + 1;
        let mut budget = TurnBudget::new(limit_calls, limit_bytes).unwrap();
        let (mut calls, mut bytes) = (0, 0);
        for _ in 0..65 {
            random = random.wrapping_mul(6364136223846793005).wrapping_add(1);
            let allowed = calls < limit_calls && bytes < limit_bytes;
            match budget.permit() {
                None => assert!(!allowed),
                Some(permit) => {
                    assert!(allowed);
                    calls += 1;
                    assert_eq!(permit.max_bytes(), limit_bytes - bytes);
                    match random % 4 {
                        0 => { drop(permit); bytes = limit_bytes; }
                        1 => {
                            assert_eq!(permit.complete(limit_bytes - bytes + 1), Err(transport::Error::InvalidProgress));
                            bytes = limit_bytes;
                        }
                        _ => {
                            let accepted = (random as usize >> 4) % (limit_bytes - bytes + 1);
                            permit.complete(accepted).unwrap();
                            bytes += accepted;
                        }
                    }
                }
            }
            assert_eq!((budget.calls(), budget.bytes()), (calls, bytes));
        }
    }
}

#[test]
fn peer_close_only_changes_phase_in_setup_running_or_closing() {
    for starting in [Phase::Setup, Phase::Running, Phase::Closing] {
        let mut session = configured();
        let mut sink = Memory::default();
        session.begin(200, &mut sink).unwrap();
        match starting {
            Phase::Running => {
                receive(&mut session, 0, 0x73, &mut sink);
                receive(&mut session, 1, 0x73, &mut sink);
                session.apply_peer_signals(1, 2).unwrap();
                session.set_local_pause(1, true, &mut sink).unwrap();
            }
            Phase::Closing => receive(&mut session, 0, 0x1f, &mut sink),
            _ => {}
        }
        assert_eq!(session.phase(), starting);
        let _ = session.receive(Frame::command(0, UIH | POLL_FINAL, b"fixture").unwrap(), &mut sink).unwrap();
        let before = snapshot(&session);
        let effects = sink.effects;
        session.peer_close().unwrap();
        assert_eq!(session.phase(), Phase::PeerClosing);
        assert_eq!(snapshot(&session), before);
        assert_eq!(sink.effects, effects);
        assert!(!session.can_read_endpoint(1, true));
        assert_eq!(session.poll_timeout(u64::MAX, true), Ok(None));
        session.peer_close().unwrap();
        assert_eq!(snapshot(&session), before);
    }
}

#[test]
fn peer_close_rejects_idle_and_failed_without_mutation() {
    let mut session = configured();
    let before = snapshot(&session);
    assert_eq!(session.peer_close(), Err(session::Error::NotStarted));
    assert_eq!(session.phase(), Phase::Idle);
    assert_eq!(snapshot(&session), before);
    let mut sink = Memory { reject: true, ..Memory::default() };
    assert_eq!(session.begin(100, &mut sink), Err(session::Error::EffectRejected));
    let before = snapshot(&session);
    assert_eq!(session.peer_close(), Err(session::Error::Failed));
    assert_eq!(session.phase(), Phase::Failed);
    assert_eq!(snapshot(&session), before);
}

#[test]
fn cld_effect_is_applied_only_after_reply_admission() {
    for rejected in [false, true] {
        let mut session = configured();
        let mut sink = Memory::default();
        session.begin(100, &mut sink).unwrap();
        let payload = [0xc3, 1];
        let mut output = [0; 2];
        let handoff = session.receive(Frame::command(0, UIH | POLL_FINAL, &payload).unwrap(), &mut sink).unwrap();
        let Handoff::Control(input) = handoff else { panic!("Expected control handoff"); };
        let outcome = mcc::prepare(input, 512, &mut output).unwrap();
        assert_eq!(outcome.effect, mcc::Effect::PeerClosing);
        let reply = outcome.reply.unwrap();
        sink.reject = rejected;
        let admitted = session.send_control(reply.control, reply.payload, &mut sink);
        if admitted.is_ok() { session.peer_close().unwrap(); }
        if rejected {
            assert_eq!(admitted, Err(session::Error::EffectRejected));
            assert_eq!(session.phase(), Phase::Failed);
            assert!(session.poll_final_pending());
        } else {
            assert_eq!(session.phase(), Phase::PeerClosing);
            assert_eq!(sink.frames.last().unwrap(), &[0xc1, 1]);
            assert!(session.poll_final_pending());
        }
    }
}

#[test]
fn late_frames_cannot_revive_or_mutate_a_closed_session_before_owner_reset() {
    for (first, second) in [(0, 1), (1, 0)] {
        for rejected_after_close in [false, true] {
            let mut session = configured();
            let mut sink = Memory::default();
            session.begin(100, &mut sink).unwrap();
            session.send_control(UIH, &[0xc1, 1], &mut sink).unwrap();
            session.peer_close().unwrap();
            let before = snapshot(&session);
            let effects = sink.effects;
            sink.reject = rejected_after_close;
            for _ in 0..3 {
                for (channel, control) in [(first, 0x73), (second, 0x73), (0, 0x1f),
                    (1, 0x3f), (0, 0x53), (63, 0x3f), (0, UIH | POLL_FINAL), (1, UIH)] {
                    let frame = Frame::command(channel, control, b"ignored").unwrap();
                    assert_eq!(session.receive(frame, &mut sink), Ok(Handoff::None));
                    assert_eq!(session.phase(), Phase::PeerClosing);
                    assert_eq!(snapshot(&session), before);
                    assert_eq!(sink.effects, effects);
                }
            }
            sink.reject = false;
            session.reset_after_transport_close();
            session.begin(200, &mut sink).unwrap();
            receive(&mut session, 0, 0x73, &mut sink);
            receive(&mut session, 1, 0x73, &mut sink);
            assert_eq!(session.phase(), Phase::Running);
            assert!(session.ready_started());
        }
    }
    let mut session = configured();
    let mut sink = Memory::default();
    session.begin(100, &mut sink).unwrap();
    receive(&mut session, 0, 0x1f, &mut sink);
    assert_eq!(session.phase(), Phase::Closing);
    let before = snapshot(&session);
    let effects = sink.effects;
    for channel in [0, 1] { receive(&mut session, channel, 0x73, &mut sink); }
    assert_eq!(session.phase(), Phase::Closing);
    assert_eq!(snapshot(&session), before);
    assert_eq!(sink.effects, effects);
}
