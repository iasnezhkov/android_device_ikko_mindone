/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_mux_core::transport::*;
use mindone_mux_core::{encode, Decoder, Frame, MAX_PAYLOAD, UIH};
use std::collections::VecDeque;

struct Writer {
    results: VecDeque<IoResult>,
    accepted: Vec<u8>,
    calls: usize,
    fallback: IoResult,
}

impl Writer {
    fn new(results: &[IoResult]) -> Self {
        Self { results: results.iter().copied().collect(), accepted: Vec::new(), calls: 0,
            fallback: IoResult::Fault(Fault::WouldBlock) }
    }
}

impl ByteWriter for Writer {
    fn write(&mut self, input: &[u8]) -> IoResult {
        self.calls += 1;
        let result = self.results.pop_front().unwrap_or(self.fallback);
        if let IoResult::Transferred(bytes) = result {
            if bytes <= input.len() {
                self.accepted.extend_from_slice(&input[..bytes]);
            }
        }
        result
    }
}

enum ReadStep {
    Bytes(Vec<u8>),
    Result(IoResult),
}

struct Reader {
    steps: VecDeque<ReadStep>,
    calls: usize,
    continuous: Option<Vec<u8>>,
}

impl Reader {
    fn new(steps: Vec<ReadStep>) -> Self {
        Self { steps: steps.into(), calls: 0, continuous: None }
    }
}

impl ByteReader for Reader {
    fn read(&mut self, output: &mut [u8]) -> IoResult {
        self.calls += 1;
        let step = self.steps.pop_front().unwrap_or_else(|| {
            self.continuous.as_ref().map_or(ReadStep::Result(IoResult::Fault(Fault::WouldBlock)), |bytes| ReadStep::Bytes(bytes.clone()))
        });
        match step {
            ReadStep::Result(result) => result,
            ReadStep::Bytes(mut bytes) => {
                let length = bytes.len().min(output.len());
                output[..length].copy_from_slice(&bytes[..length]);
                if length < bytes.len() {
                    let remaining = bytes.split_off(length);
                    self.steps.push_front(ReadStep::Bytes(remaining));
                }
                IoResult::Transferred(length)
            }
        }
    }
}

#[derive(Default)]
struct Frames {
    values: Vec<(u8, u8, Vec<u8>)>,
    reject: bool,
}

impl FrameSink for Frames {
    fn frame(&mut self, frame: Frame<'_>) -> bool {
        if self.reject {
            return false;
        }
        self.values.push((frame.channel(), frame.control, frame.payload.to_vec()));
        true
    }
}

fn wire(payload: &[u8]) -> Vec<u8> {
    let mut output = vec![0; payload.len() + 7];
    let length = encode(Frame::command(1, UIH, payload).unwrap(), MAX_PAYLOAD, &mut output).unwrap();
    output.truncate(length);
    output
}

fn budget() -> TurnBudget {
    TurnBudget::new(MAX_TURN_CALLS, MAX_TURN_BYTES).unwrap()
}

#[test]
fn fixed_profile_and_invalid_configurations_are_explicit() {
    assert_eq!((TX_BYTES, RX_BYTES, PENDING_BYTES, PTY_READ_BYTES), (65536, 65536, 4096, 1024));
    assert!(matches!(TransmitQueue::new(0), Err(Error::Configuration)));
    assert!(matches!(PendingQueue::<4607>::new(512), Err(Error::Configuration)));
    assert!(PendingQueue::<4608>::new(512).is_ok());
    for (calls, bytes) in [(0, 1), (1, 0), (65, 1), (1, 65537)] {
        assert_eq!(TurnBudget::new(calls, bytes), Err(Error::Configuration));
    }
}

#[test]
fn byte_queue_rejections_are_atomic_and_written_bytes_advance_exactly() {
    let mut queue = ByteQueue::<8>::new(8).unwrap();
    queue.append(b"abcdef").unwrap();
    assert_eq!(queue.append(b"xyz"), Err(Error::Capacity));
    assert_eq!(queue.pending(), b"abcdef");
    assert_eq!(queue.accept_written(7), Err(Error::InvalidProgress));
    assert_eq!(queue.pending(), b"abcdef");
    queue.accept_written(3).unwrap();
    queue.append(b"12345").unwrap();
    assert_eq!(queue.pending(), b"def12345");
    assert_eq!(queue.clear(), 8);
    assert!(queue.pending().is_empty());
}

#[test]
fn frame_and_whole_pty_admission_never_leave_a_partial_frame() {
    let mut queue = TransmitQueue::new(512).unwrap();
    let payload = [0x5a; 512];
    while queue.available() >= 519 {
        queue.admit_frame(Frame::command(1, UIH, &payload).unwrap()).unwrap();
    }
    let original = queue.pending().to_vec();
    assert_eq!(queue.admit_frame(Frame::command(1, UIH, &payload).unwrap()), Err(Error::Capacity));
    assert_eq!(queue.pending(), original);
    assert_eq!(queue.admit_pty_input(1, &[0; 1024]), Err(Error::Capacity));
    assert_eq!(queue.pending(), original);
    assert_eq!(queue.admit_pty_input(0, b"x"), Err(Error::Channel));
    assert_eq!(queue.admit_pty_input(1, &[0; 1025]), Err(Error::OversizedPayload));
    let mut writer = Writer::new(&[IoResult::Transferred(519)]);
    queue.drain(&mut writer, &mut budget());
    assert!(queue.available() >= 519 && queue.available() < 1038);
    let original = queue.pending().to_vec();
    assert_eq!(queue.admit_pty_input(1, &[0; 1024]), Err(Error::Capacity));
    assert_eq!(queue.pending(), original);
    queue.clear();
    queue.admit_pty_input(1, &[0xa5; 1024]).unwrap();
    let mut decoder = Decoder::<65536>::new(512).unwrap();
    decoder.push(queue.pending()).unwrap();
    let mut count = 0;
    while let Some(frame) = decoder.next_frame() {
        assert_eq!(frame.payload, [0xa5; 512]);
        count += 1;
    }
    assert_eq!(count, 2);
}

#[test]
fn accepted_bytes_are_never_replayed_after_short_writes_or_blocking() {
    let mut queue = TransmitQueue::new(512).unwrap();
    queue.admit_frame(Frame::command(1, UIH, b"payload").unwrap()).unwrap();
    let expected = queue.pending().to_vec();
    let mut writer = Writer::new(&[IoResult::Transferred(3), IoResult::Fault(Fault::Interrupted), IoResult::Transferred(2)]);
    let result = queue.drain(&mut writer, &mut budget());
    assert_eq!(result.stop, Stop::Blocked);
    assert_eq!(result.bytes, 5);
    assert_eq!(writer.accepted, expected[..5]);
    assert_eq!(queue.pending(), &expected[5..]);
    writer.results.push_back(IoResult::Transferred(queue.pending().len()));
    let result = queue.drain(&mut writer, &mut budget());
    assert_eq!(result.stop, Stop::Complete);
    assert_eq!(writer.accepted, expected);
}

#[test]
fn persistent_interrupts_yield_without_discarding_or_publishing_bytes() {
    let mut queue = PendingQueue::<4608>::new(512).unwrap();
    queue.buffer_remainder(b"owned").unwrap();
    let mut writer = Writer::new(&[]);
    writer.fallback = IoResult::Fault(Fault::Interrupted);
    let mut turn = TurnBudget::new(7, 1024).unwrap();
    let result = queue.drain(&mut writer, &mut turn);
    assert_eq!(result.stop, Stop::Yield);
    assert_eq!((result.calls, result.bytes, writer.calls), (7, 0, 7));
    assert_eq!(queue.pending(), b"owned");
    assert!(writer.accepted.is_empty());
}

#[test]
fn byte_budget_is_shared_across_channel_work_in_one_turn() {
    let mut first = PendingQueue::<4608>::new(512).unwrap();
    let mut second = PendingQueue::<4608>::new(512).unwrap();
    first.buffer_remainder(b"first").unwrap();
    second.buffer_remainder(b"second").unwrap();
    let mut writer = Writer::new(&[IoResult::Transferred(3)]);
    let mut turn = TurnBudget::new(4, 3).unwrap();
    assert_eq!(first.drain(&mut writer, &mut turn).stop, Stop::Yield);
    assert_eq!(second.drain(&mut writer, &mut turn).stop, Stop::Yield);
    assert_eq!(writer.calls, 1);
    assert_eq!(first.pending(), b"st");
    assert_eq!(second.pending(), b"second");
}

#[test]
fn reset_is_typed_and_invalid_or_zero_progress_never_consumes_data() {
    for (step, expected) in [(IoResult::Transferred(0), Stop::NoProgress),
                            (IoResult::Transferred(999), Stop::InvalidProgress),
                            (IoResult::Fault(Fault::Failure), Stop::Failure),
                            (IoResult::Fault(Fault::EndOfStream), Stop::EndOfStream)] {
        let mut queue = PendingQueue::<4608>::new(512).unwrap();
        queue.buffer_remainder(b"safe").unwrap();
        let mut writer = Writer::new(&[step]);
        let result = queue.drain(&mut writer, &mut budget());
        assert_eq!(result.stop, expected);
        assert_eq!(queue.pending(), b"safe");
        assert_eq!(result.bytes, 0);
    }
    let mut queue = TransmitQueue::new(512).unwrap();
    queue.admit_frame(Frame::command(1, UIH, b"reset").unwrap()).unwrap();
    let pending = queue.pending().len();
    let mut writer = Writer::new(&[IoResult::Fault(Fault::DeviceReset)]);
    let result = queue.drain(&mut writer, &mut budget());
    assert_eq!(result.stop, Stop::DeviceReset);
    assert_eq!(result.discarded, pending);
    assert!(queue.pending().is_empty());
}

#[test]
fn pending_queue_keeps_the_exact_initial_frame_and_append_bounds() {
    let mut queue = PendingQueue::<36863>::new(MAX_PAYLOAD).unwrap();
    queue.buffer_remainder(&[1; 32767]).unwrap();
    assert_eq!(queue.pending().len(), 32767);
    assert_eq!(queue.buffer_remainder(&[2]), Err(Error::Capacity));
    assert_eq!(queue.pending(), [1; 32767]);
    queue.clear();
    for _ in 0..8 {
        queue.buffer_remainder(&[3; 512]).unwrap();
    }
    assert_eq!(queue.pending().len(), 4096);
    assert_eq!(queue.buffer_remainder(&[4]), Err(Error::Capacity));
}

#[test]
fn short_reads_blocking_and_interrupts_preserve_only_the_current_fragment() {
    let packet = wire(b"event");
    let mut reader = Reader::new(vec![ReadStep::Bytes(packet[..4].to_vec()),
        ReadStep::Result(IoResult::Fault(Fault::Interrupted)),
        ReadStep::Result(IoResult::Fault(Fault::WouldBlock))]);
    let mut receiver = Receiver::new(512).unwrap();
    let mut frames = Frames::default();
    let mut scratch = [0; 32];
    let result = receiver.read_turn(&mut reader, &mut scratch, &mut frames, &mut budget());
    assert_eq!(result.stop, Stop::Blocked);
    assert_eq!(receiver.pending(), &packet[..4]);
    assert!(frames.values.is_empty());
    reader.steps.push_back(ReadStep::Bytes(packet[4..].to_vec()));
    receiver.read_turn(&mut reader, &mut scratch, &mut frames, &mut budget());
    assert_eq!(frames.values, [(1, UIH, b"event".to_vec())]);
}

#[test]
fn eof_reset_and_hard_read_errors_cannot_leak_an_old_fragment() {
    for fault in [Fault::EndOfStream, Fault::DeviceReset, Fault::Failure] {
        let packet = wire(b"previous");
        let mut reader = Reader::new(vec![ReadStep::Bytes(packet[..4].to_vec()), ReadStep::Result(IoResult::Fault(fault))]);
        let mut receiver = Receiver::new(512).unwrap();
        let mut frames = Frames::default();
        let mut scratch = [0; 64];
        let result = receiver.read_turn(&mut reader, &mut scratch, &mut frames, &mut budget());
        assert_eq!(result.discarded, 4);
        assert!(receiver.pending().is_empty());
        reader.steps.push_back(ReadStep::Bytes(packet[4..].to_vec()));
        receiver.read_turn(&mut reader, &mut scratch, &mut frames, &mut budget());
        assert!(frames.values.is_empty());
        reader.steps.push_back(ReadStep::Bytes(wire(b"fresh")));
        receiver.read_turn(&mut reader, &mut scratch, &mut frames, &mut budget());
        assert_eq!(frames.values, [(1, UIH, b"fresh".to_vec())]);
    }
}

#[test]
fn continuous_readers_and_interruption_storms_are_bounded() {
    let mut receiver = Receiver::new(512).unwrap();
    let mut reader = Reader::new(vec![]);
    reader.continuous = Some(wire(b"x"));
    let mut frames = Frames::default();
    let mut scratch = [0; 64];
    let mut turn = TurnBudget::new(3, 64).unwrap();
    let result = receiver.read_turn(&mut reader, &mut scratch, &mut frames, &mut turn);
    assert_eq!(result.stop, Stop::Yield);
    assert_eq!((result.calls, frames.values.len()), (3, 3));
    let mut reader = Reader::new((0..100).map(|_| ReadStep::Result(IoResult::Fault(Fault::Interrupted))).collect());
    let result = receiver.read_turn(&mut reader, &mut scratch, &mut frames, &mut TurnBudget::new(5, 64).unwrap());
    assert_eq!((result.stop, result.calls, reader.calls), (Stop::Yield, 5, 5));
}

#[test]
fn reads_and_writes_share_one_turn_budget_without_exceeding_the_byte_limit() {
    let mut receiver = Receiver::new(512).unwrap();
    let mut reader = Reader::new(vec![]);
    reader.continuous = Some(wire(b"x"));
    let mut frames = Frames::default();
    let mut turn = TurnBudget::new(4, 10).unwrap();
    let result = receiver.read_turn(&mut reader, &mut [0; 64], &mut frames, &mut turn);
    assert_eq!((result.stop, result.bytes, result.calls), (Stop::Yield, 10, 2));
    assert_eq!(frames.values.len(), 1);
    assert_eq!(receiver.pending().len(), 3);
    let mut pending = PendingQueue::<4608>::new(512).unwrap();
    pending.buffer_remainder(b"queued").unwrap();
    let mut writer = Writer::new(&[IoResult::Transferred(6)]);
    let result = pending.drain(&mut writer, &mut turn);
    assert_eq!((result.stop, result.calls, result.bytes, writer.calls), (Stop::Yield, 0, 0, 0));
    assert_eq!(pending.pending(), b"queued");
}

#[test]
fn a_later_read_failure_clears_only_the_unpublished_fragment() {
    let mut input = wire(b"accepted");
    input.extend_from_slice(&wire(b"incomplete")[..4]);
    let mut receiver = Receiver::new(512).unwrap();
    let mut reader = Reader::new(vec![ReadStep::Bytes(input), ReadStep::Result(IoResult::Fault(Fault::Failure))]);
    let mut frames = Frames::default();
    let result = receiver.read_turn(&mut reader, &mut [0; 128], &mut frames, &mut budget());
    assert_eq!((result.stop, result.frames, result.discarded), (Stop::Failure, 1, 4));
    assert_eq!(frames.values, [(1, UIH, b"accepted".to_vec())]);
    assert!(receiver.pending().is_empty());
}

#[test]
fn invalid_read_counts_and_rejected_frames_clear_unpublished_input() {
    for step in [ReadStep::Result(IoResult::Transferred(0)), ReadStep::Result(IoResult::Transferred(65))] {
        let mut receiver = Receiver::new(512).unwrap();
        let mut reader = Reader::new(vec![step]);
        let mut frames = Frames::default();
        let result = receiver.read_turn(&mut reader, &mut [0; 64], &mut frames, &mut budget());
        assert!(matches!(result.stop, Stop::EndOfStream | Stop::InvalidProgress));
        assert!(frames.values.is_empty() && receiver.pending().is_empty());
    }
    let mut receiver = Receiver::new(512).unwrap();
    let mut reader = Reader::new(vec![ReadStep::Bytes(wire(b"not published"))]);
    let mut frames = Frames { reject: true, ..Frames::default() };
    let result = receiver.read_turn(&mut reader, &mut [0; 64], &mut frames, &mut budget());
    assert_eq!(result.stop, Stop::Rejected);
    assert_eq!(result.frames, 0);
    assert!(receiver.pending().is_empty() && frames.values.is_empty());
}

#[test]
fn absolute_at_deadlines_are_not_refreshed_by_unsolicited_activity() {
    let deadline = Deadline::at_command(100).unwrap();
    for elapsed in 0..60000 {
        assert_eq!(deadline.expires_at(), 5100);
        assert_eq!(deadline.remaining(100 + elapsed), if elapsed < 5000 { Some(5000 - elapsed) } else { None });
    }
    assert_eq!(Deadline::at_command(u64::MAX), Err(Error::ClockOverflow));
    assert_eq!(Deadline::after(0, 0).unwrap().remaining(0), None);
}

#[test]
fn identical_fd_reuse_reset_and_other_registries_cannot_revive_stale_poll_tokens() {
    let mut registry = PollEpochs::default();
    let first = registry.replace(EndpointId::Channel(1), 7).unwrap();
    assert!(registry.is_current(first));
    let second = registry.replace(EndpointId::Channel(1), 7).unwrap();
    assert!(!registry.is_current(first));
    assert!(registry.is_current(second));
    assert_eq!(first.descriptor(), second.descriptor());
    assert_ne!(first.generation(), second.generation());
    registry.invalidate(EndpointId::Channel(1)).unwrap();
    assert!(!registry.is_current(second));
    let third = registry.replace(EndpointId::Channel(1), 7).unwrap();
    registry.reset();
    assert!(!registry.is_current(third));
    let mut other = PollEpochs::default();
    let fourth = other.replace(EndpointId::Channel(1), 7).unwrap();
    assert!(!registry.is_current(fourth));
    assert!(!other.is_current(third));
    assert_eq!(fourth.endpoint(), EndpointId::Channel(1));
    assert_eq!(other.replace(EndpointId::Channel(0), 1), Err(Error::Channel));
    assert_eq!(other.replace(EndpointId::Channel(64), 1), Err(Error::Channel));
    assert_eq!(other.replace(EndpointId::Serial, -1), Err(Error::Descriptor));
}
