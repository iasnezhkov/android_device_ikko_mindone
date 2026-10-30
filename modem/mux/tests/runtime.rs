/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

mod runtime_support;

use mindone_mux_core::transport::{EndpointId, Fault};
use mindone_mux_core::{encode, Frame, UIH};
use mindone_mux_runtime::{Error, Event, Limits, Ready, State};
use runtime_support::*;
use std::io::Read;
use std::net::Shutdown;
use std::os::unix::net::UnixStream;

#[test]
fn actual_host_startup_and_duplex_channels_preserve_bytes_and_accounting() {
    let _guard = lock();
    let mut h = Harness::new();
    h.boot();
    assert_eq!(h.modem.commands, [b"AT".to_vec(), b"ATZ".to_vec(), b"ATE0".to_vec(), b"AT+CMUX=0,0,5,512".to_vec()]);
    assert_eq!(h.published.0.borrow().events, [Event::Ready { refused_channels: 0 }]);
    h.send(1, b"application-one");
    h.send(26, b"application-atci");
    h.modem.frame(1, UIH, b"modem-one");
    h.modem.frame(26, UIH, b"modem-atci");
    h.steps(20);
    assert_eq!(h.modem.received[&1], b"application-one");
    assert_eq!(h.modem.received[&26], b"application-atci");
    assert_eq!(h.received(1), b"modem-one");
    assert_eq!(h.received(26), b"modem-atci");
    assert_eq!(h.runtime.held_tail_bytes(), 0);
    assert!(h.runtime.buffered_rx_bytes() <= 1);
    let serial = h.serial.borrow();
    let (mut calls, mut bytes) = (serial.read_calls + serial.write_calls, serial.read_bytes + serial.write_bytes);
    for plan in h.store.borrow().plans.values() {
        let plan = plan.borrow();
        calls += plan.read_calls + plan.write_calls;
        bytes += plan.read_bytes + plan.write_bytes;
    }
    assert_eq!(h.reports.iter().map(|report| report.calls).sum::<usize>(), calls);
    assert_eq!(h.reports.iter().map(|report| report.bytes).sum::<usize>(), bytes);
}

#[test]
fn short_io_interrupted_calls_and_tiny_turns_make_fair_progress() {
    let _guard = lock();
    let mut h = Harness::new();
    h.limits = Limits { calls: 1, bytes: 7 };
    h.serial.borrow_mut().read_limit = Some(3);
    h.serial.borrow_mut().write_limit = Some(2);
    h.serial.borrow_mut().writes.extend([Step::Fault(Fault::Interrupted), Step::Limit(1), Step::Fault(Fault::WouldBlock)]);
    h.boot();
    h.plan(1).borrow_mut().read_limit = Some(3);
    h.plan(1).borrow_mut().write_limit = Some(2);
    h.plan(1).borrow_mut().writes.push_back(Step::Fault(Fault::Interrupted));
    h.send(1, b"0123456789abcdefghijklmnop");
    h.send(26, b"fair-atci");
    h.modem.frame(1, UIH, b"small-downlink");
    h.steps(1000);
    assert_eq!(h.runtime.state(), State::Multiplexing);
    assert_eq!(h.modem.received[&1], b"0123456789abcdefghijklmnop");
    assert_eq!(h.modem.received[&26], b"fair-atci");
    assert_eq!(h.received(1), b"small-downlink");
    assert_eq!(h.published.0.borrow().events.iter().filter(|event| matches!(event, Event::Ready { .. })).count(), 1);
}

#[test]
fn cmux_tail_is_delivered_once_after_the_bounded_settle() {
    let _guard = lock();
    let mut h = Harness::new();
    h.modem.cmux_ready = false;
    let mut wire = [0; 519];
    let count = encode(Frame::command(1, UIH, b"tail-exact").unwrap(), 512, &mut wire).unwrap();
    h.modem.cmux_tail.extend_from_slice(&wire[..count]);
    for _ in 0..100 {
        h.step();
        if h.runtime.held_tail_bytes() > 0 { break; }
    }
    assert_eq!(h.runtime.state(), State::Startup);
    assert_eq!(h.runtime.held_tail_bytes(), count);
    assert!(h.store.borrow().opens.is_empty());
    let saved = h.now;
    h.now = 999;
    h.step();
    assert!(h.store.borrow().opens.is_empty());
    h.now = saved + 1000;
    h.boot();
    h.steps(20);
    assert_eq!(h.received(1), b"tail-exact");
    assert!(h.received(1).is_empty());
    assert_eq!(h.runtime.held_tail_bytes(), 0);
}

#[test]
fn stale_generation_cannot_read_a_recreated_numeric_descriptor() {
    let _guard = lock();
    let mut h = Harness::new();
    h.boot();
    let old = h.token(EndpointId::Channel(1));
    h.plan(1).borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock));
    h.modem.frame(1, UIH, b"buffer-before-recreate");
    h.steps(1);
    h.plan(1).borrow_mut().writes.push_back(Step::Fault(Fault::Failure));
    let before = h.plan(1).borrow().read_calls;
    let report = h.drive(&[Ready { token: old, readable: true, writable: true, hangup: false }]);
    assert_eq!(h.store.borrow().opens[&1], 2);
    let new = h.token(EndpointId::Channel(1));
    assert_eq!(new.descriptor(), old.descriptor());
    assert_ne!(new.generation(), old.generation());
    assert_eq!(h.plan(1).borrow().read_calls, before);
    assert_eq!(report.stale_events, 1);
    h.send(1, b"new-generation");
    let report = h.drive(&[Ready { token: old, readable: true, writable: true, hangup: false }]);
    assert_eq!(report.stale_events, 1);
    assert_eq!(h.plan(1).borrow().read_calls, before);
    h.steps(20);
    assert_eq!(h.modem.received[&1], b"new-generation");
}

#[test]
fn invalid_progress_fails_closed_with_conservative_byte_reservation() {
    let _guard = lock();
    let mut h = Harness::new();
    h.boot();
    h.serial.borrow_mut().reads.push_back(Step::Invalid(4097));
    let report = h.step();
    assert_eq!((report.state, report.error), (State::Failed, Some(Error::InvalidProgress)));
    assert_eq!(report.bytes, h.limits.bytes);
    assert!(h.runtime.interests().is_empty());
    let mut input = [0; 1];
    assert_eq!(h.store.borrow_mut().peers.get_mut(&1).unwrap().read(&mut input).unwrap(), 0);
    let calls = h.serial.borrow().read_calls;
    let report = h.step();
    assert_eq!(report.calls, 0);
    assert_eq!(h.serial.borrow().read_calls, calls);
}

#[test]
fn cld_echo_has_a_bounded_flush_and_late_ua_never_publishes_ready() {
    let _guard = lock();
    let mut h = Harness::new();
    h.modem.auto = false;
    h.modem.outgoing.clear();
    h.modem.bytes(b"+EIND: 128\r\n");
    for command in [b"AT".as_slice(), b"ATZ", b"ATE0", b"AT+CMUX=0,0,5,512"] {
        for _ in 0..100 {
            h.step();
            if h.modem.commands.last().is_some_and(|value| value == command) { break; }
        }
        if command.starts_with(b"AT+CMUX") {
            h.modem.bytes(b"+CMUX: READY\r\nOK\r\n");
            h.modem.mode_mux = true;
        } else { h.modem.bytes(b"OK\r\n"); }
    }
    for _ in 0..100 {
        h.step();
        if h.runtime.state() == State::Multiplexing { break; }
    }
    assert_eq!(h.runtime.state(), State::Multiplexing);
    h.serial.borrow_mut().write_limit = Some(0);
    h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock));
    h.modem.frame(0, UIH, &[0xc3, 1]);
    h.modem.frame(0, 0x73, &[]);
    h.modem.frame(1, 0x73, &[]);
    h.modem.frame(26, 0x73, &[]);
    let report = h.step();
    assert_eq!(report.state, State::PeerClosing);
    assert!(h.published.0.borrow().events.is_empty());
    let deadline = h.now + 500;
    h.serial.borrow_mut().write_limit = None;
    h.serial.borrow_mut().writes.extend(std::iter::repeat_n(Step::Fault(Fault::WouldBlock), 100));
    h.now = deadline;
    let report = h.step();
    assert_eq!(report.state, State::Closed);
    assert!(h.published.0.borrow().events.is_empty());
    assert!(h.runtime.interests().is_empty());
}

#[test]
fn malformed_mcc_preserves_only_the_already_delivered_prefix_then_closes() {
    let _guard = lock();
    let mut h = Harness::new();
    h.boot();
    h.modem.frame(1, UIH, b"accepted-prefix");
    h.modem.frame(0, UIH, &[0x23, 1, 9]);
    h.modem.frame(1, UIH, b"forbidden-tail");
    let report = h.step();
    assert_eq!(report.state, State::Failed);
    assert_eq!(report.error, Some(Error::Mcc(mindone_mux_core::mcc::Error::TrailingBytes)));
    assert_eq!(report.frames, 2);
    assert_eq!(h.received(1), b"accepted-prefix");
    let mut input = [0; 1];
    assert_eq!(h.store.borrow_mut().peers.get_mut(&1).unwrap().read(&mut input).unwrap(), 0);
    assert_eq!(h.runtime.buffered_rx_bytes(), 0);
}

#[test]
fn serial_eof_discards_partial_frames_and_stop_closes_all_owned_endpoints() {
    let _guard = lock();
    let mut h = Harness::new();
    h.boot();
    h.modem.bytes(&[0xf9, 7, UIH, 9]);
    h.step();
    assert!(h.runtime.buffered_rx_bytes() >= 4);
    h.modem.peer.shutdown(Shutdown::Both).unwrap();
    let report = h.step();
    assert_eq!((report.state, report.error), (State::Failed, Some(Error::Io)));
    assert!(report.discarded >= 4);
    assert_eq!(h.runtime.buffered_rx_bytes(), 0);
    let mut h = Harness::new();
    h.boot();
    h.runtime.stop();
    assert_eq!(h.runtime.state(), State::Closed);
    assert!(h.runtime.interests().is_empty());
    let mut serial_input = [0; 1];
    assert_eq!(h.modem.peer.read(&mut serial_input).unwrap(), 0);
    for channel in [1, 26] {
        assert!(h.received(channel).is_empty());
        let mut input = [0; 1];
        assert_eq!(h.store.borrow_mut().peers.get_mut(&channel).unwrap().read(&mut input).unwrap(), 0);
    }
    let calls = h.serial.borrow().read_calls + h.serial.borrow().write_calls;
    assert_eq!(h.step().calls, 0);
    assert_eq!(h.serial.borrow().read_calls + h.serial.borrow().write_calls, calls);
}

#[test]
fn device_reset_is_terminal_during_chat_but_a_reported_queue_reset_in_mux() {
    let _guard = lock();
    let mut h = Harness::new();
    h.step();
    h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::DeviceReset));
    let report = h.step();
    assert_eq!(report.state, State::Failed);
    assert_eq!(report.error, Some(Error::Chat(mindone_mux_core::chat::Error::DeviceReset)));
    assert!(h.published.0.borrow().events.is_empty());
    let mut h = Harness::new();
    h.boot();
    h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::DeviceReset));
    h.send(1, b"discard-on-reset");
    let report = h.step();
    assert_eq!(report.state, State::Multiplexing);
    assert!(report.discarded >= b"discard-on-reset".len());
    assert_eq!(h.runtime.pending_tx(), 0);
    assert_eq!(h.published.0.borrow().events, [Event::Ready { refused_channels: 0 }, Event::ModemException]);
    h.send(1, b"after-reset");
    h.steps(20);
    assert_eq!(h.modem.received[&1], b"after-reset");
    h.published.0.borrow_mut().reject = Some(Event::ModemException);
    h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::DeviceReset));
    h.send(1, b"rejected-required-reset-effect");
    assert_eq!(h.step().error, Some(Error::EffectRejected));
    assert_eq!(h.runtime.state(), State::Failed);
}

#[test]
fn peer_and_local_flow_control_apply_with_bounded_pending_loss() {
    let _guard = lock();
    let mut h = Harness::new();
    h.boot();
    h.modem.frame(0, UIH, &[0xe3, 5, 7, 0x8f]);
    h.steps(3);
    h.send(1, b"peer-paused-data");
    h.steps(10);
    assert!(!h.modem.received.contains_key(&1));
    h.modem.frame(0, UIH, &[0xe3, 5, 7, 0x8d]);
    h.steps(20);
    assert_eq!(h.modem.received[&1], b"peer-paused-data");
    h.plan(1).borrow_mut().writes.extend(std::iter::repeat_n(Step::Fault(Fault::WouldBlock), 100));
    let before = h.reports.len();
    for index in 0..10 { h.modem.frame(1, UIH, &[index; 512]); }
    h.steps(10);
    assert!(h.reports[before..].iter().map(|report| report.discarded).sum::<usize>() >= 512);
    assert!(h.modem.frames.iter().any(|(channel, _, payload)| *channel == 0 && payload.as_slice() == [0xe3, 5, 7, 0x8f]));
    h.plan(1).borrow_mut().writes.clear();
    h.steps(20);
    let delivered = h.received(1);
    assert_eq!(delivered.len(), 4096);
    for (index, block) in delivered.chunks(512).enumerate() { assert!(block.iter().all(|byte| usize::from(*byte) == index)); }
    assert!(h.modem.frames.iter().any(|(channel, _, payload)| *channel == 0 && payload.as_slice() == [0xe3, 5, 7, 0x8d]));
}

#[test]
fn corrupted_frames_resynchronize_and_unknown_mcc_receives_nsc() {
    let _guard = lock();
    let mut h = Harness::new();
    h.boot();
    let mut wire = [0; 519];
    let count = encode(Frame::command(1, UIH, b"bad").unwrap(), 512, &mut wire).unwrap();
    wire[count - 2] ^= 1;
    h.modem.bytes(&wire[..count]);
    h.modem.frame(1, UIH, b"good");
    h.modem.frame(0, UIH, &[0x53, 1]);
    h.steps(20);
    assert_eq!(h.runtime.state(), State::Multiplexing);
    assert_eq!(h.received(1), b"good");
    assert!(h.modem.frames.iter().any(|(channel, _, payload)| *channel == 0 && payload.as_slice() == [0x11, 3, 0x53]));
}

#[test]
fn factory_and_required_publication_failures_close_every_owned_endpoint() {
    let _guard = lock();
    let mut h = Harness::new();
    h.store.borrow_mut().fail_channel = Some(26);
    for _ in 0..100 {
        if h.step().state == State::Failed { break; }
    }
    assert_eq!(h.runtime.error(), Some(Error::Session(mindone_mux_core::session::Error::EndpointUnavailable)));
    assert!(h.runtime.interests().is_empty());
    let mut input = [0; 1];
    assert_eq!(h.store.borrow_mut().peers.get_mut(&1).unwrap().read(&mut input).unwrap(), 0);
    let mut h = Harness::new();
    h.published.0.borrow_mut().reject = Some(Event::Ready { refused_channels: 0 });
    for _ in 0..100 {
        if h.step().state == State::Failed { break; }
    }
    assert_eq!(h.runtime.error(), Some(Error::Session(mindone_mux_core::session::Error::EffectRejected)));
    assert!(h.runtime.interests().is_empty());
}

#[test]
fn duplicate_foreign_and_stale_events_never_duplicate_io_or_hide_current_events() {
    let _guard = lock();
    let mut h = Harness::new();
    h.boot();
    let token = h.token(EndpointId::Channel(1));
    h.send(1, b"once");
    let before = h.plan(1).borrow().read_calls;
    let event = Ready { token, readable: true, writable: false, hangup: false };
    h.drive(&[event; 47]);
    assert_eq!(h.plan(1).borrow().read_calls - before, 1);
    h.steps(20);
    assert_eq!(h.modem.received[&1], b"once");
    let foreign = mindone_mux_core::transport::PollEpochs::default().replace(EndpointId::Channel(1), token.descriptor()).unwrap();
    let event = Ready { token: foreign, readable: true, writable: true, hangup: true };
    let report = h.drive(&[event]);
    assert_eq!((report.stale_events, report.calls), (1, 0));
    h.send(1, b"current");
    h.drive(&[event, Ready { token, readable: true, writable: false, hangup: false }]);
    h.steps(20);
    assert_eq!(h.modem.received[&1], b"oncecurrent");
}

#[test]
fn modem_mode_change_invalidates_old_ascii_readiness_and_flushes_setup_immediately() {
    let _guard = lock();
    let mut h = Harness::new();
    for _ in 0..100 {
        h.step();
        if h.modem.commands.last().is_some_and(|command| command.starts_with(b"AT+CMUX")) { break; }
    }
    assert_eq!(h.runtime.state(), State::Startup);
    let old = h.token(EndpointId::Serial);
    h.modem.pump();
    let before = h.serial.borrow().write_calls;
    let report = h.drive(&[Ready { token: old, readable: true, writable: true, hangup: false }]);
    assert_eq!(report.state, State::Multiplexing);
    assert_eq!(h.serial.borrow().write_calls, before + 1);
    assert_eq!(h.runtime.pending_tx(), 0);
    assert_eq!(report.stale_events, 1);
    let new = h.token(EndpointId::Serial);
    assert_eq!(new.descriptor(), old.descriptor());
    assert_ne!(new.generation(), old.generation());
    h.boot();
}

#[test]
fn deterministic_adversarial_real_socket_replay_preserves_every_accepted_byte() {
    let _guard = lock();
    let mut h = Harness::new();
    h.boot();
    let mut random = 0x7292026u64;
    let (mut uplink_bytes, mut downlink_bytes) = (0, 0);
    for index in 0..2048 {
        random = random.wrapping_mul(6364136223846793005).wrapping_add(1);
        let channel = if random & 1 == 0 { 1 } else { 26 };
        let length = ((random >> 11) as usize % 96) + 1;
        let up: Vec<u8> = (0..length).map(|offset| (random.wrapping_add(offset as u64) >> 7) as u8).collect();
        let down: Vec<u8> = up.iter().rev().map(|byte| byte ^ 0xa5).collect();
        h.serial.borrow_mut().read_limit = Some((random as usize % 23) + 1);
        h.serial.borrow_mut().write_limit = Some(((random >> 5) as usize % 17) + 1);
        h.plan(channel).borrow_mut().write_limit = Some(((random >> 9) as usize % 11) + 1);
        if index % 11 == 0 { h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::Interrupted)); }
        if index % 13 == 0 { h.plan(channel).borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock)); }
        let before = h.modem.received.get(&channel).map_or(0, Vec::len);
        h.send(channel, &up);
        h.modem.frame(channel, UIH, &down);
        h.steps(128);
        assert_eq!(h.runtime.state(), State::Multiplexing, "Replay failed at iteration {index}");
        assert_eq!(&h.modem.received[&channel][before..], up);
        assert_eq!(h.received(channel), down);
        uplink_bytes += up.len();
        downlink_bytes += down.len();
    }
    assert_eq!(h.published.0.borrow().events.iter().filter(|event| matches!(event, Event::Ready { .. })).count(), 1);
    assert_eq!(h.store.borrow().opens[&1], 1);
    assert_eq!(h.store.borrow().opens[&26], 1);
    println!("socket-replay operations=4096 turns=262144 uplink_bytes={uplink_bytes} downlink_bytes={downlink_bytes}");
}

#[test]
fn accepted_cld_echo_flushes_before_close_and_ignores_later_data() {
    let _guard = lock();
    let mut h = Harness::new();
    h.boot();
    h.modem.frame(0, UIH, &[0xc3, 1]);
    h.modem.frame(1, UIH, b"after-close");
    let report = h.step();
    assert_eq!(report.state, State::Closed);
    assert!(h.modem.frames.iter().any(|(channel, _, payload)| *channel == 0 && payload.as_slice() == [0xc1, 1]));
    assert!(h.received(1).is_empty());
    assert!(h.runtime.interests().is_empty());
}

#[test]
fn partial_at_timeout_never_concatenates_a_retry_on_the_same_stream() {
    let _guard = lock();
    let mut h = Harness::new();
    h.modem.auto = false;
    h.limits = Limits { calls: 1, bytes: 64 };
    h.serial.borrow_mut().write_limit = Some(1);
    h.step();
    h.step();
    assert_eq!(h.serial.borrow().write_bytes, 1);
    h.now = 5000;
    let report = h.step();
    assert_eq!(report.state, State::Failed);
    assert_eq!(report.error, Some(Error::Chat(mindone_mux_core::chat::Error::Timeout)));
    assert_eq!(h.serial.borrow().write_bytes, 1);
    assert!(h.modem.commands.is_empty());
}

#[test]
fn busy_reads_are_hard_failures_while_busy_mux_writes_retry() {
    let _guard = lock();
    let mut h = Harness::new();
    h.serial.borrow_mut().reads.push_back(Step::Fault(Fault::Busy));
    let report = h.step();
    assert_eq!(report.state, State::Failed);
    assert_eq!(report.error, Some(Error::Chat(mindone_mux_core::chat::Error::IoFailure)));
    let mut h = Harness::new();
    h.boot();
    h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::Busy));
    h.send(1, b"busy-retry");
    assert_eq!(h.step().state, State::Multiplexing);
    assert!(h.runtime.pending_tx() > 0);
    h.steps(30);
    assert_eq!(h.modem.received[&1], b"busy-retry");
    assert_eq!(h.published.0.borrow().events, [Event::Ready { refused_channels: 0 }]);
}

#[test]
fn hard_channel_faults_recreate_only_the_affected_endpoint() {
    let _guard = lock();
    for fault in [Fault::Busy, Fault::EndOfStream, Fault::DeviceReset, Fault::Failure] {
        let mut h = Harness::new();
        h.boot();
        h.plan(1).borrow_mut().reads.push_back(Step::Fault(fault));
        let token = h.token(EndpointId::Channel(1));
        h.drive(&[Ready { token, readable: true, writable: false, hangup: false }]);
        assert_eq!(h.runtime.state(), State::Multiplexing);
        assert_eq!(h.store.borrow().opens[&1], 2);
        assert_eq!(h.store.borrow().opens[&26], 1);
        assert_eq!(h.published.0.borrow().events, [Event::Ready { refused_channels: 0 }]);
        h.send(1, b"recreated-channel");
        h.steps(20);
        assert_eq!(h.modem.received[&1], b"recreated-channel");
    }
}

#[test]
fn bad_limits_clock_regression_and_oversized_readiness_fail_without_extra_io() {
    let _guard = lock();
    for mode in 0..3 {
        let mut h = Harness::new();
        h.boot();
        let before = h.serial.borrow().read_calls + h.serial.borrow().write_calls;
        let report = match mode {
            0 => { h.limits = Limits { calls: 65, bytes: 65536 }; h.runtime.drive(h.now, &[], h.limits) }
            1 => h.runtime.drive(0, &[], h.limits),
            _ => {
                let event = Ready { token: h.token(EndpointId::Serial), readable: true, writable: true, hangup: false };
                h.runtime.drive(h.now, &[event; 48], h.limits)
            }
        };
        assert_eq!(report.state, State::Failed);
        assert_eq!(report.error, Some(if mode == 1 { Error::ClockRegression } else { Error::Configuration }));
        assert_eq!(report.calls, 0);
        assert_eq!(h.serial.borrow().read_calls + h.serial.borrow().write_calls, before);
        assert!(h.runtime.interests().is_empty());
    }
}

#[test]
fn unsupported_local_close_and_endpoint_prepare_failure_are_explicit() {
    let _guard = lock();
    let mut h = Harness::new();
    h.modem.refuse = Some(0);
    for _ in 0..100 {
        if h.step().state == State::Failed { break; }
    }
    assert_eq!(h.runtime.error(), Some(Error::UnsupportedLocalClose));
    let mut h = Harness::new();
    h.store.borrow_mut().plans.entry(1).or_default().borrow_mut().prepare_fails = true;
    for _ in 0..100 {
        if h.step().state == State::Failed { break; }
    }
    assert_eq!(h.runtime.error(), Some(Error::Session(mindone_mux_core::session::Error::EndpointUnavailable)));
    assert!(h.runtime.interests().is_empty());
}

#[test]
fn direct_standard_unix_stream_adapter_owns_the_serial_endpoint_until_stop() {
    let _guard = lock();
    struct Empty;
    impl mindone_mux_runtime::Factory<UnixStream> for Empty {
        fn open(&mut self, _: u8) -> std::io::Result<UnixStream> { Err(std::io::Error::other("No configured endpoints")) }
    }
    #[derive(Default)]
    struct Collected(Vec<Event>);
    impl mindone_mux_runtime::Effects for Collected {
        fn apply(&mut self, event: Event) -> bool { self.0.push(event); true }
    }
    let (serial, peer) = UnixStream::pair().unwrap();
    let config = mindone_mux_core::chat::Config { frame_limit: 512, baud_index: 5, user_build: false, pin: None };
    let mut owner = mindone_mux_runtime::Runtime::new(serial, Empty, Collected::default(), &[], config, 0).unwrap();
    let mut modem = Modem::new(peer);
    modem.bytes(b"+EIND: 128\r\n");
    for now in 0..100 {
        modem.pump();
        let ready: Vec<_> = owner.interests().into_iter().map(|interest| Ready {
            token: interest.token, readable: interest.readable, writable: interest.writable, hangup: false,
        }).collect();
        assert_ne!(owner.drive(now, &ready, Limits::default()).state, State::Failed);
        modem.pump();
        if !owner.effects().0.is_empty() { break; }
    }
    assert_eq!(owner.effects().0, [Event::Ready { refused_channels: 0 }]);
    owner.stop();
    let mut input = [0; 1];
    assert_eq!(modem.peer.read(&mut input).unwrap(), 0);
}

#[test]
fn expired_peer_close_discards_before_timer_or_ready_io() {
    let _guard = lock();
    for ready_event in [false, true] {
        for elapsed in [499, 500, 501, 100000] {
            let mut h = Harness::new();
            h.boot();
            h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock));
            h.modem.frame(0, UIH, &[0xc3, 1]);
            let started = h.now;
            assert_eq!(h.step().state, State::PeerClosing);
            assert!(h.runtime.pending_tx() > 0);
            let calls = h.serial.borrow().write_calls;
            let bytes = h.serial.borrow().write_bytes;
            let ready = h.ready();
            h.now = started + elapsed;
            let report = h.drive(if ready_event { &ready } else { &[] });
            let actual_calls = h.serial.borrow().write_calls - calls;
            let actual_bytes = h.serial.borrow().write_bytes - bytes;
            assert_eq!(report.state, State::Closed);
            if elapsed < 500 {
                assert_eq!((actual_calls, actual_bytes, report.calls, report.bytes), (1, 8, 1, 8));
            } else {
                assert_eq!((actual_calls, actual_bytes, report.calls, report.bytes), (0, 0, 0, 0));
                assert!(report.discarded >= 8);
            }
            h.modem.pump();
            let echoed = h.modem.frames.iter().any(|(channel, _, payload)| *channel == 0 && payload.as_slice() == [0xc1, 1]);
            assert_eq!(echoed, elapsed < 500);
            assert!(h.runtime.interests().is_empty());
        }
    }
}

#[test]
fn blocked_serial_retry_waits_without_writable_events() {
    let _guard = lock();
    let mut observed = Vec::new();
    for closing in [false, true] {
        let mut h = Harness::new();
        h.boot();
        h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock));
        if closing { h.modem.frame(0, UIH, &[0xc3, 1]); }
        else { h.send(1, b"bounded-retry"); }
        let started = h.now;
        h.step();
        assert!(h.runtime.pending_tx() > 0);
        let before = h.serial.borrow().write_calls;
        for elapsed in 1..10 {
            h.now = started + elapsed;
            assert_eq!(h.drive(&[]).calls, 0);
            assert_eq!(h.runtime.next_deadline(), Some(started + 10));
        }
        observed.push(h.serial.borrow().write_calls - before);
    }
    println!("retry-readiness baseline_or_fixed mux_wakeups={} peer_close_wakeups={}", observed[0], observed[1]);
    assert_eq!(observed, [0, 0]);
}

#[test]
fn startup_deadline_combines_ready_command_and_delayed_write_retry_without_io() {
    let _guard = lock();
    let mut h = Harness::new();
    assert_eq!(h.runtime.next_deadline(), Some(mindone_mux_core::chat::READY_WAIT_MS));
    h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock));
    let started = h.now;
    h.step();
    let token = h.token(EndpointId::Serial);
    assert_eq!(h.runtime.next_deadline(), Some(started + 10));
    let calls = h.serial.borrow().write_calls;
    for _ in 0..100 { assert_eq!(h.runtime.next_deadline(), Some(started + 10)); }
    assert_eq!(h.serial.borrow().write_calls, calls);
    h.now = started + 9;
    assert_eq!(h.drive(&[]).calls, 0);
    h.now = started + 10;
    assert_eq!(h.drive(&[]).calls, 1);
    assert_eq!(h.runtime.next_deadline(), Some(mindone_mux_core::chat::COMMAND_TIMEOUT_MS));
    assert_eq!(h.token(EndpointId::Serial), token);
    h.modem.pump();
    assert_eq!(h.modem.commands, [b"AT".to_vec()]);
}

#[test]
fn timed_driver_skips_blocked_write_wakeups_and_delivers_actual_socket_bytes_at_retry() {
    let _guard = lock();
    for closing in [false, true] {
        let mut h = Harness::new();
        h.boot();
        assert_eq!(h.runtime.next_deadline(), None);
        let token = h.token(EndpointId::Serial);
        h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock));
        if closing { h.modem.frame(0, UIH, &[0xc3, 1]); }
        else { h.send(1, b"timed-retry"); }
        let started = h.now;
        h.step();
        let retry = h.runtime.next_deadline().unwrap();
        assert_eq!(retry, started + 10);
        assert!(h.runtime.interests().iter().any(|interest| interest.token.endpoint() == EndpointId::Serial && interest.writable));
        let calls = h.serial.borrow().write_calls;
        let bytes = h.serial.borrow().write_bytes;
        h.now = retry - 1;
        assert_eq!(h.drive(&[]).calls, 0);
        assert_eq!(h.serial.borrow().write_calls, calls);
        assert_eq!(h.serial.borrow().write_bytes, bytes);
        assert_eq!(h.runtime.next_deadline(), Some(retry));
        h.now = retry;
        let report = h.drive(&[]);
        let transferred = if closing { 8 } else { b"timed-retry".len() + 6 };
        assert_eq!((report.calls, report.bytes), (1, transferred));
        assert_eq!(h.serial.borrow().write_calls - calls, 1);
        assert_eq!(h.serial.borrow().write_bytes - bytes, transferred);
        assert_eq!(h.runtime.next_deadline(), None);
        h.modem.pump();
        if closing {
            assert_eq!(report.state, State::Closed);
            assert!(h.modem.frames.iter().any(|(channel, _, payload)| *channel == 0 && payload.as_slice() == [0xc1, 1]));
            assert_eq!(h.runtime.drive(h.now, &[Ready { token, readable: true, writable: true, hangup: false }], h.limits).calls, 0);
        } else {
            assert_eq!(report.state, State::Multiplexing);
            assert_eq!(h.modem.received[&1], b"timed-retry");
            assert_eq!(h.token(EndpointId::Serial), token);
        }
    }
}

fn setup_without_acknowledgements() -> Harness {
    let mut h = Harness::new();
    h.limits = Limits { calls: 1, bytes: 65536 };
    for _ in 0..100 {
        h.step();
        if h.runtime.state() == State::Multiplexing {
            h.modem.auto = false;
            h.modem.outgoing.clear();
            h.limits = Limits::default();
            return h;
        }
    }
    panic!("Expected a bounded transition into mux setup");
}

#[test]
fn setup_deadline_precedes_a_later_retry_and_expires_before_any_io() {
    let _guard = lock();
    let mut h = setup_without_acknowledgements();
    let setup = h.now - 1 + mindone_mux_core::session::SETUP_TIMEOUT_MS;
    assert_eq!(h.runtime.next_deadline(), Some(h.now - 1));
    h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock));
    let started = h.now;
    h.drive(&[]);
    assert_eq!(h.runtime.next_deadline(), Some(started + 10));
    h.now = setup - 5;
    h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock));
    h.drive(&[]);
    assert_eq!(h.runtime.next_deadline(), Some(setup));
    let calls = h.serial.borrow().write_calls;
    h.now = setup;
    let report = h.drive(&[]);
    assert_eq!((report.state, report.error, report.calls), (State::Failed, Some(Error::Session(mindone_mux_core::session::Error::SetupTimeout)), 0));
    assert_eq!(h.serial.borrow().write_calls, calls);
    assert_eq!(h.runtime.next_deadline(), None);
}

#[test]
fn settle_deadline_transitions_to_mux_and_invalidates_ascii_tokens_before_setup_timer() {
    let _guard = lock();
    let mut h = Harness::new();
    h.modem.cmux_ready = false;
    let mut settle = None;
    for _ in 0..100 {
        h.step();
        if h.runtime.next_deadline().is_some_and(|end| end <= h.now + mindone_mux_core::chat::SETTLE_MS) {
            settle = h.runtime.next_deadline();
            break;
        }
    }
    let settle = settle.unwrap();
    assert_eq!(h.runtime.state(), State::Startup);
    let old = h.token(EndpointId::Serial);
    h.modem.auto = false;
    h.now = settle - 1;
    assert_eq!(h.drive(&[]).state, State::Startup);
    assert_eq!(h.runtime.next_deadline(), Some(settle));
    h.now = settle;
    assert_eq!(h.drive(&[]).state, State::Multiplexing);
    assert_eq!(h.runtime.next_deadline(), Some(settle + mindone_mux_core::session::SETUP_TIMEOUT_MS));
    let new = h.token(EndpointId::Serial);
    assert_eq!(old.descriptor(), new.descriptor());
    assert_ne!(old.generation(), new.generation());
    assert_eq!(h.drive(&[Ready { token: old, readable: true, writable: true, hangup: false }]).stale_events, 1);
    assert_eq!(h.token(EndpointId::Serial), new);
}

#[test]
fn chat_and_peer_close_deadlines_precede_retries_that_extend_beyond_them() {
    let _guard = lock();
    let mut h = Harness::new();
    h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock));
    h.step();
    let command = mindone_mux_core::chat::COMMAND_TIMEOUT_MS;
    h.now = command - 1;
    h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock));
    h.step();
    assert_eq!(h.runtime.next_deadline(), Some(command));
    h.now = command;
    let report = h.drive(&[]);
    assert_eq!((report.state, report.error, report.calls), (State::Failed, Some(Error::Chat(mindone_mux_core::chat::Error::Timeout)), 0));
    assert_eq!(h.runtime.next_deadline(), None);
    let mut h = Harness::new();
    h.boot();
    h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock));
    h.modem.frame(0, UIH, &[0xc3, 1]);
    let started = h.now;
    h.step();
    assert_eq!(h.runtime.next_deadline(), Some(started + 10));
    h.now = started + 495;
    h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock));
    h.drive(&[]);
    assert_eq!(h.runtime.next_deadline(), Some(started + 500));
    let calls = h.serial.borrow().write_calls;
    h.now = started + 500;
    assert_eq!(h.drive(&[]).state, State::Closed);
    assert_eq!(h.serial.borrow().write_calls, calls);
    assert_eq!(h.runtime.next_deadline(), None);
}

#[test]
fn clock_regression_overflow_and_stop_leave_no_deadline_or_readiness() {
    let _guard = lock();
    for mode in 0..4 {
        let mut h = Harness::new();
        h.boot();
        match mode {
            0 => h.runtime.stop(),
            1 => { assert_eq!(h.runtime.drive(0, &[], h.limits).error, Some(Error::ClockRegression)); }
            2 => {
                h.now = u64::MAX - 9;
                h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock));
                h.send(1, b"overflow-retry");
                assert_eq!(h.step().error, Some(Error::ClockOverflow));
            }
            _ => {
                h.now = u64::MAX - 499;
                h.modem.frame(0, UIH, &[0xc3, 1]);
                assert_eq!(h.step().error, Some(Error::ClockOverflow));
            }
        }
        assert_eq!(h.runtime.next_deadline(), None);
        assert!(h.runtime.interests().is_empty());
        assert_eq!(h.runtime.drive(u64::MAX, &[], h.limits).calls, 0);
    }
    let mut h = Harness::new_at(u64::MAX - mindone_mux_core::chat::READY_WAIT_MS);
    assert_eq!(h.runtime.next_deadline(), Some(u64::MAX));
    for _ in 0..100 {
        if h.step().state == State::Failed { break; }
    }
    assert_eq!(h.runtime.error(), Some(Error::Session(mindone_mux_core::session::Error::ClockOverflow)));
    assert_eq!(h.runtime.next_deadline(), None);
}

#[test]
fn serial_without_pollout_completes_chat_setup_duplex_recovery_and_close() {
    let _guard = lock();
    for blocked_once in [false, true] {
        let mut h = Harness::new();
        h.serial_writable = false;
        h.runtime.enable_recovery(0);
        if blocked_once { h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock)); }
        h.boot();
        assert_eq!(h.modem.commands, [b"AT".to_vec(), b"ATZ".to_vec(), b"ATE0".to_vec(), b"AT+CMUX=0,0,5,512".to_vec()]);
        for channel in [0, 1, 26] {
            assert!(h.modem.frames.iter().any(|(id, control, _)| *id == channel && control & !0x10 == 0x2f));
        }
        if blocked_once { h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock)); }
        for channel in [1, 26] {
            h.send(channel, b"ril-uplink");
            h.modem.frame(channel, UIH, b"ril-downlink");
        }
        h.steps(40);
        for channel in [1, 26] {
            assert_eq!(h.modem.received[&channel], b"ril-uplink");
            assert_eq!(h.received(channel), b"ril-downlink");
        }
        let sabms = h.modem.frames.iter().filter(|(id, control, _)| *id == 1 && control & !0x10 == 0x2f).count();
        h.modem.frame(1, 0x53, &[]);
        h.steps(5);
        h.now += 1000;
        if blocked_once { h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock)); }
        h.steps(40);
        assert_eq!(h.modem.frames.iter().filter(|(id, control, _)| *id == 1 && control & !0x10 == 0x2f).count(), sabms + 1);
        h.send(1, b"recovered-uplink");
        h.modem.frame(1, UIH, b"recovered-downlink");
        h.steps(40);
        assert_eq!(h.modem.received[&1], b"ril-uplinkrecovered-uplink");
        assert_eq!(h.received(1), b"recovered-downlink");
        if blocked_once { h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock)); }
        h.modem.frame(0, UIH, &[0xc3, 1]);
        h.steps(40);
        assert_eq!(h.runtime.state(), State::Closed);
        assert!(h.modem.frames.iter().any(|(id, _, payload)| *id == 0 && payload == &[0xc1, 1]));
    }
}

#[test]
fn serial_retry_uses_pollout_before_the_backoff_for_chat_and_mux() {
    let _guard = lock();
    for startup in [true, false] {
        let mut h = Harness::new();
        if !startup { h.boot(); h.send(1, b"early-writable"); }
        h.serial.borrow_mut().writes.push_back(Step::Fault(Fault::WouldBlock));
        let started = h.now;
        h.step();
        assert_eq!(h.runtime.next_deadline(), Some(started + 10));
        let token = h.token(EndpointId::Serial);
        let calls = h.serial.borrow().write_calls;
        h.now = started + 1;
        let report = h.drive(&[Ready { token, readable: false, writable: true, hangup: false }]);
        assert_eq!(report.calls, 1);
        assert_eq!(h.serial.borrow().write_calls, calls + 1);
        h.modem.pump();
        if startup { assert_eq!(h.modem.commands, [b"AT".to_vec()]); }
        else { assert_eq!(h.modem.received[&1], b"early-writable"); }
    }
}

#[test]
fn failed_pollout_attempt_waits_for_the_backoff_before_pollout_again() {
    let _guard = lock();
    let mut observed = Vec::new();
    for startup in [true, false] {
        let mut h = Harness::new();
        if !startup { h.boot(); h.send(1, b"busy-uplink"); }
        h.serial.borrow_mut().writes.extend(std::iter::repeat_n(Step::Fault(Fault::Busy), 256));
        let calls = h.serial.borrow().write_calls;
        let started = h.now;
        for elapsed in 0..100 {
            h.now = started + elapsed;
            h.step();
        }
        let attempts = h.serial.borrow().write_calls - calls;
        assert!((10..=21).contains(&attempts), "attempts {attempts}");
        observed.push(attempts);
    }
    println!("busy-pollout attempts_per_100ms startup={} mux={}", observed[0], observed[1]);
}

#[test]
fn serial_partial_writes_continue_with_the_suffix_until_would_block_without_pollout() {
    let _guard = lock();
    for startup in [true, false] {
        let mut h = Harness::new();
        h.serial_writable = false;
        if !startup { h.boot(); h.send(1, b"partial-uplink"); }
        h.serial.borrow_mut().writes.extend([Step::Limit(1), Step::Fault(Fault::WouldBlock)]);
        let started = h.now;
        let calls = h.serial.borrow().write_calls;
        let bytes = h.serial.borrow().write_bytes;
        h.step();
        assert_eq!(h.serial.borrow().write_calls, calls + 2);
        assert_eq!(h.serial.borrow().write_bytes, bytes + 1);
        assert_eq!(h.runtime.next_deadline(), Some(started + 10));
        h.now = started + 9;
        assert_eq!(h.drive(&[]).calls, 0);
        h.now = started + 10;
        assert_eq!(h.drive(&[]).calls, 1);
        h.modem.pump();
        if startup { assert_eq!(h.modem.commands, [b"AT".to_vec()]); }
        else { assert_eq!(h.modem.received[&1], b"partial-uplink"); }
    }
}

#[test]
fn serial_partial_write_without_backpressure_completes_in_the_same_turn() {
    let _guard = lock();
    for startup in [true, false] {
        let mut h = Harness::new();
        h.serial_writable = false;
        if !startup { h.boot(); h.send(1, b"mtu-sized-uplink"); }
        h.serial.borrow_mut().writes.push_back(Step::Limit(1));
        let calls = h.serial.borrow().write_calls;
        h.step();
        assert_eq!(h.serial.borrow().write_calls, calls + 2);
        h.modem.pump();
        if startup { assert_eq!(h.modem.commands, [b"AT".to_vec()]); }
        else { assert_eq!(h.modem.received[&1], b"mtu-sized-uplink"); assert_eq!(h.runtime.pending_tx(), 0); }
    }
}

#[test]
fn budget_yield_advertises_immediate_serial_progress_without_pollout() {
    let _guard = lock();
    let mut h = Harness::new();
    h.serial_writable = false;
    h.limits = Limits { calls: 1, bytes: 64 };
    h.step();
    assert_eq!(h.serial.borrow().write_calls, 0);
    assert_eq!(h.runtime.next_deadline(), Some(h.now - 1));
    assert!(!h.runtime.interests().iter().any(|interest| interest.writable));
    h.boot();
    h.send(1, b"budget-uplink");
    h.steps(100);
    assert_eq!(h.modem.received[&1], b"budget-uplink");
}
