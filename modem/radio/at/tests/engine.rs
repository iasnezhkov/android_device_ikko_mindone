/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#![forbid(unsafe_code)]

use mindone_radio_at::engine::*;
use mindone_radio_at::line::{Framer, Record, LINE_BYTES};

fn engine() -> Engine { Engine::new(0, &["+EREG:", "+EIND:", "+ECSQ:"]).unwrap() }

fn command(deadline: u64) -> Command { Command::new("AT+EREG?", Some("+EREG:"), 16, deadline).unwrap() }

fn write(engine: &mut Engine, now: u64, expected: &[u8], events: &mut Vec<Event>) -> WriteToken {
    let request = engine.pending_write().unwrap();
    assert_eq!(request.bytes, expected);
    let token = request.token;
    let count = request.bytes.len();
    engine.accept_written(now, token, count, |event| events.push(event)).unwrap();
    token
}

fn start(engine: &mut Engine, command: Command, events: &mut Vec<Event>) -> CommandId {
    let id = engine.enqueue(command).unwrap();
    engine.tick(0, |event| events.push(event)).unwrap();
    write(engine, 0, b"AT+EREG?\r", events);
    id
}

fn response(events: &[Event]) -> &Response {
    events.iter().find_map(|event| match event { Event::Completed(response) => Some(response), _ => None }).unwrap()
}

#[test]
fn evidence_transcript_every_split_and_echo_mode() {
    for echo in [false, true] {
        let transcript = format!("{}\r\n+EREG: 2,0\r\n+EIND: 128\r\n+ECSQ: 99,99,255,255\r\nOK\r\n", if echo { "AT+EREG?\r\n" } else { "" });
        for split in 0..=transcript.len() {
            let mut engine = engine();
            let mut events = Vec::new();
            let id = start(&mut engine, command(5000), &mut events);
            engine.feed(1, &transcript.as_bytes()[..split], |event| events.push(event)).unwrap();
            engine.feed(2, &transcript.as_bytes()[split..], |event| events.push(event)).unwrap();
            assert_eq!(events.len(), 3);
            assert_eq!(response(&events), &Response { id, intermediates: vec!["+EREG: 2,0".into()], result: Ok("OK".into()) });
            assert!(events.contains(&Event::Urc { line: "+EIND: 128".into(), registered: true }));
            assert!(events.contains(&Event::Urc { line: "+ECSQ: 99,99,255,255".into(), registered: true }));
        }
    }
}

#[test]
fn one_byte_reads_and_noise() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(5000), &mut events);
    for byte in b"\0AT+EREG?\r\n+EREG: 2,0\r\nO\x01K\xff\r\n" {
        engine.feed(1, &[*byte], |event| events.push(event)).unwrap();
    }
    assert_eq!(events.len(), 1);
    assert_eq!(response(&events).result, Ok("OK".into()));
}

#[test]
fn all_standard_finals_are_preserved() {
    for terminal in ["OK", "ERROR", "+CME ERROR: 10", "+CMS ERROR: 500", "NO CARRIER"] {
        let mut engine = engine();
        let mut events = Vec::new();
        start(&mut engine, command(5000), &mut events);
        engine.feed(1, format!("{terminal}\r").as_bytes(), |event| events.push(event)).unwrap();
        assert_eq!(response(&events).result, Ok(terminal.into()));
    }
}

#[test]
fn extra_final_is_exact() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(5000).with_extra_finals(&["CONNECT"]).unwrap(), &mut events);
    engine.feed(1, b"CONNECT 1\rCONNECT\r", |event| events.push(event)).unwrap();
    assert_eq!(response(&events).result, Ok("CONNECT".into()));
    assert_eq!(events[0], Event::Urc { line: "CONNECT 1".into(), registered: false });
}

#[test]
fn prefix_overlap_belongs_to_command() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(5000), &mut events);
    engine.feed(1, b"+EREG: 0\r+EREG: 2,0\rOK\r", |event| events.push(event)).unwrap();
    assert_eq!(response(&events).intermediates, ["+EREG: 0", "+EREG: 2,0"]);
    assert_eq!(events.len(), 1);
}

#[test]
fn no_prefix_routes_all_intermediates_to_urc() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, Command::new("AT+EREG?", None, 0, 5000).unwrap(), &mut events);
    engine.feed(1, b"+EREG: 2,0\runknown\rOKAY\r+CME ERROR: bad\rOK\r", |event| events.push(event)).unwrap();
    assert!(response(&events).intermediates.is_empty());
    assert_eq!(events.len(), 5);
}

#[test]
fn urcs_between_commands() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(5000), &mut events);
    engine.enqueue(command(5000)).unwrap();
    engine.feed(1, b"OK\r+EIND: 128\r", |event| events.push(event)).unwrap();
    assert_eq!(engine.phase(), Phase::Idle);
    assert!(engine.pending_write().is_none());
    assert_eq!(events.len(), 2);
    engine.tick(1, |event| events.push(event)).unwrap();
    assert_eq!(engine.phase(), Phase::Writing);
}

#[test]
fn queue_bound_excludes_inflight_and_rejects_overload() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(5000), &mut events);
    for _ in 0..QUEUE_COMMANDS { engine.enqueue(command(5000)).unwrap(); }
    assert_eq!(engine.enqueue(command(5000)), Err(Error::QueueFull));
    assert_eq!(engine.queued(), QUEUE_COMMANDS);
}

#[test]
fn timeout_late_ok_and_silence_resynchronization() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(100), &mut events);
    engine.enqueue(command(5000)).unwrap();
    engine.tick(100, |event| events.push(event)).unwrap();
    assert_eq!(response(&events).result, Err(Error::Timeout));
    assert!(engine.is_quarantined());
    engine.feed(399, b"OK\r", |event| events.push(event)).unwrap();
    engine.tick(698, |event| events.push(event)).unwrap();
    assert!(engine.pending_write().is_none());
    engine.tick(699, |event| events.push(event)).unwrap();
    write(&mut engine, 699, b"AT+CMEE?\r", &mut events);
    engine.feed(700, b"AT+CMEE?\r+CMEE: 1\rOK\r+EIND: 128\r", |event| events.push(event)).unwrap();
    assert_eq!(engine.phase(), Phase::Idle);
    assert!(events.contains(&Event::Resynchronized));
    assert_eq!(events.iter().filter(|event| matches!(event, Event::Completed(_))).count(), 1);
    engine.tick(700, |event| events.push(event)).unwrap();
    write(&mut engine, 700, b"AT+EREG?\r", &mut events);
}

#[test]
fn any_byte_restarts_silence_even_at_boundary() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(1), &mut events);
    engine.tick(1, |event| events.push(event)).unwrap();
    engine.feed(301, b"\0", |event| events.push(event)).unwrap();
    engine.tick(301, |event| events.push(event)).unwrap();
    assert!(engine.pending_write().is_none());
    engine.tick(601, |event| events.push(event)).unwrap();
    assert!(engine.pending_write().is_some());
}

#[test]
fn late_custom_final_and_partial_old_ok_are_discarded() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(1).with_extra_finals(&["CONNECT"]).unwrap(), &mut events);
    engine.tick(1, |event| events.push(event)).unwrap();
    engine.feed(2, b"CONNECT\rO", |event| events.push(event)).unwrap();
    engine.tick(302, |event| events.push(event)).unwrap();
    write(&mut engine, 302, b"AT+CMEE?\r", &mut events);
    engine.feed(303, b"K\r", |event| events.push(event)).unwrap();
    assert!(engine.is_quarantined());
    engine.feed(304, b"+CMEE: 1\rOK\r", |event| events.push(event)).unwrap();
    assert!(!engine.is_quarantined());
}

#[test]
fn resync_error_retries_are_bounded() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(1), &mut events);
    engine.tick(1, |event| events.push(event)).unwrap();
    for attempt in 1..=u64::from(RESYNC_ATTEMPTS) {
        let now = 1 + attempt * SILENCE_MS;
        engine.tick(now, |event| events.push(event)).unwrap();
        write(&mut engine, now, b"AT+CMEE?\r", &mut events);
        engine.feed(now, b"ERROR\r", |event| events.push(event)).unwrap();
    }
    assert_eq!(engine.tick(1201, |event| events.push(event)), Err(Error::ResyncFailed));
    assert_eq!(engine.phase(), Phase::Failed);
}

#[test]
fn resync_probe_timeout_fails_queued_commands_once() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(1), &mut events);
    engine.enqueue(command(20000)).unwrap();
    engine.tick(1, |event| events.push(event)).unwrap();
    engine.tick(301, |event| events.push(event)).unwrap();
    write(&mut engine, 301, b"AT+CMEE?\r", &mut events);
    assert_eq!(engine.tick(5301, |event| events.push(event)), Err(Error::ResyncFailed));
    assert_eq!(events.iter().filter(|event| matches!(event, Event::Completed(_))).count(), 2);
    assert_eq!(engine.tick(5302, |_| panic!()), Err(Error::Failed));
}

#[test]
fn line_limit_and_overflow_tail_are_bounded() {
    let mut framer = Framer::new();
    for _ in 0..LINE_BYTES { assert_eq!(framer.push(b'x'), None); }
    assert_eq!(framer.buffered_bytes(), LINE_BYTES);
    assert_eq!(framer.push(b'x'), Some(Record::TooLarge));
    for byte in b"OK\r" { assert_eq!(framer.push(*byte), None); }
    assert_eq!(framer.buffered_bytes(), 0);
    assert_eq!(framer.push(b'O'), None);
    assert_eq!(framer.push(b'K'), None);
    assert_eq!(framer.push(b'\n'), Some(Record::Line("OK".into())));
}

#[test]
fn oversized_line_quarantines_command() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(5000), &mut events);
    engine.feed(1, &vec![b'x'; LINE_BYTES + 1], |event| events.push(event)).unwrap();
    engine.feed(2, b"OK\r", |event| events.push(event)).unwrap();
    assert_eq!(response(&events).result, Err(Error::LineTooLarge));
    assert!(engine.is_quarantined());
}

#[test]
fn transaction_counts_noise_and_urcs() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(5000), &mut events);
    engine.feed(1, &vec![0; TRANSACTION_BYTES], |event| events.push(event)).unwrap();
    assert!(events.is_empty());
    engine.feed(2, b"\0", |event| events.push(event)).unwrap();
    assert_eq!(response(&events).result, Err(Error::TransactionTooLarge));
}

#[test]
fn max_intermediate_lines_quarantines() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, Command::new("AT+EREG?", Some("+EREG:"), 1, 5000).unwrap(), &mut events);
    engine.feed(1, b"+EREG: 2,0\r+EREG: 2,0\rOK\r", |event| events.push(event)).unwrap();
    assert_eq!(response(&events).result, Err(Error::TooManyLines));
    assert_eq!(response(&events).intermediates.len(), 1);
}

#[test]
fn queued_deadlines_expire_without_writes() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(100), &mut events);
    engine.enqueue(command(2)).unwrap();
    engine.tick(2, |event| events.push(event)).unwrap();
    assert_eq!(engine.phase(), Phase::Reading);
    assert_eq!(response(&events).result, Err(Error::Timeout));
    assert_eq!(engine.queued(), 0);
}

#[test]
fn exact_deadline_wins_over_incoming_ok() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(100), &mut events);
    engine.feed(100, b"OK\r", |event| events.push(event)).unwrap();
    assert_eq!(response(&events).result, Err(Error::Timeout));
    assert!(engine.is_quarantined());
}

#[test]
fn partial_writes_only_expose_remaining_bytes() {
    let mut engine = engine();
    engine.enqueue(command(5000)).unwrap();
    engine.tick(0, |_| {}).unwrap();
    let token = engine.pending_write().unwrap().token;
    engine.accept_written(1, token, 2, |_| {}).unwrap();
    assert_eq!(engine.pending_write().unwrap().bytes, b"+EREG?\r");
    assert_eq!(engine.pending_write().unwrap().token, token);
    engine.accept_written(2, token, 7, |_| {}).unwrap();
    assert_eq!(engine.phase(), Phase::Reading);
}

#[test]
fn stale_and_cross_engine_tokens_fail_closed() {
    let mut first = engine();
    let mut second = engine();
    for engine in [&mut first, &mut second] {
        engine.enqueue(command(5000)).unwrap();
        engine.tick(0, |_| {}).unwrap();
    }
    let token = first.pending_write().unwrap().token;
    assert_eq!(second.accept_written(1, token, 1, |_| {}), Err(Error::StaleProgress));
    first.accept_written(1, token, 9, |_| {}).unwrap();
    assert_eq!(first.accept_written(1, token, 1, |_| {}), Err(Error::StaleProgress));
}

#[test]
fn invalid_write_progress_fails_closed_and_an_early_final_quarantines() {
    for count in [0, 10] {
        let mut engine = engine();
        engine.enqueue(command(5000)).unwrap();
        engine.tick(0, |_| {}).unwrap();
        let token = engine.pending_write().unwrap().token;
        assert_eq!(engine.accept_written(1, token, count, |_| {}), Err(Error::InvalidProgress));
    }
    let mut engine = engine();
    let id = engine.enqueue(command(5000)).unwrap();
    engine.tick(0, |_| {}).unwrap();
    let mut events = Vec::new();
    engine.feed(1, b"OK\r", |event| events.push(event)).unwrap();
    assert!(engine.is_quarantined());
    assert!(engine.pending_write().is_none());
    assert!(events.contains(&Event::Completed(Response { id, intermediates: Vec::new(), result: Err(Error::UnexpectedFinal) })));
    assert!(events.contains(&Event::Error(Error::UnexpectedFinal)));
}

#[test]
fn monotonic_clock_and_resync_overflow_fail_closed() {
    let mut engine = engine();
    engine.tick(10, |_| {}).unwrap();
    assert_eq!(engine.feed(9, b"", |_| {}), Err(Error::ClockRegression));
    let mut engine = Engine::new(u64::MAX - 1000, &[]).unwrap();
    engine.enqueue(command(u64::MAX - 900)).unwrap();
    engine.tick(u64::MAX - 1000, |_| {}).unwrap();
    engine.tick(u64::MAX - 900, |_| {}).unwrap();
    assert_eq!(engine.tick(u64::MAX - 600, |_| {}), Err(Error::ClockOverflow));
}

#[test]
fn configuration_rejects_injection_and_unbounded_fields() {
    for text in ["", "AT\rAT", "AT\n", "AT\0", "OTHER", "ATé"] {
        assert!(matches!(Command::new(text, None, 0, 0), Err(Error::Configuration)));
    }
    assert!(Command::new("AT", Some(""), 0, 0).is_err());
    assert!(Command::new("AT", None, TRANSACTION_BYTES + 1, 0).is_err());
    assert!(Command::new("AT", None, 0, 0).unwrap().with_extra_finals(&vec!["DONE"; EXTRA_FINALS + 1]).is_err());
    assert!(Engine::new(0, &vec!["+EIND:"; PREFIXES + 1]).is_err());
}

#[test]
fn unterminated_lines_do_not_complete() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(100), &mut events);
    engine.feed(1, b"OK", |event| events.push(event)).unwrap();
    assert!(events.is_empty());
    engine.tick(100, |event| events.push(event)).unwrap();
    assert_eq!(response(&events).result, Err(Error::Timeout));
}

#[test]
fn late_write_progress_times_out_once_and_quarantines() {
    let mut engine = engine();
    let mut events = Vec::new();
    engine.enqueue(command(100)).unwrap();
    engine.tick(0, |_| {}).unwrap();
    let token = engine.pending_write().unwrap().token;
    engine.accept_written(1, token, 1, |_| {}).unwrap();
    assert_eq!(engine.accept_written(100, token, 1, |event| events.push(event)), Err(Error::Timeout));
    assert!(engine.is_quarantined());
    assert!(engine.pending_write().is_none());
    assert_eq!(response(&events).result, Err(Error::Timeout));
    engine.tick(101, |event| events.push(event)).unwrap();
    assert_eq!(events.iter().filter(|event| matches!(event, Event::Completed(_))).count(), 1);
}

#[test]
fn late_final_invalidates_unwritten_probe() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(1), &mut events);
    engine.tick(1, |event| events.push(event)).unwrap();
    engine.tick(301, |event| events.push(event)).unwrap();
    assert!(engine.pending_write().is_some());
    engine.feed(302, b"OK\r", |event| events.push(event)).unwrap();
    assert_eq!(engine.phase(), Phase::Quarantine(Quarantine::Silence));
    assert!(engine.pending_write().is_none());
    assert!(!events.contains(&Event::Resynchronized));
}

#[test]
fn late_final_during_partial_probe_restarts_silence() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(1), &mut events);
    engine.tick(1, |event| events.push(event)).unwrap();
    engine.tick(301, |event| events.push(event)).unwrap();
    let token = engine.pending_write().unwrap().token;
    engine.accept_written(301, token, 1, |event| events.push(event)).unwrap();
    engine.feed(302, b"OK\r", |event| events.push(event)).unwrap();
    assert_eq!(engine.phase(), Phase::Quarantine(Quarantine::Silence));
    assert!(engine.pending_write().is_none());
}

#[test]
fn exact_line_and_transaction_limits_accept_final() {
    let mut framer = Framer::new();
    for _ in 0..LINE_BYTES { framer.push(b'x'); }
    assert_eq!(framer.push(b'\r'), Some(Record::Line("x".repeat(LINE_BYTES))));
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(5000), &mut events);
    engine.feed(1, &vec![0; TRANSACTION_BYTES - 3], |event| events.push(event)).unwrap();
    engine.feed(2, b"OK\r", |event| events.push(event)).unwrap();
    assert_eq!(response(&events).result, Ok("OK".into()));
}

#[test]
fn late_ok_after_silence_window_requires_another_probe() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(1), &mut events);
    engine.enqueue(command(5000)).unwrap();
    engine.tick(1, |event| events.push(event)).unwrap();
    engine.tick(301, |event| events.push(event)).unwrap();
    write(&mut engine, 301, b"AT+CMEE?\r", &mut events);
    engine.feed(302, b"OK\r+CMEE: 1\rOK\r", |event| events.push(event)).unwrap();
    assert_eq!(engine.phase(), Phase::Quarantine(Quarantine::Silence));
    assert!(!events.contains(&Event::Resynchronized));
    engine.tick(601, |event| events.push(event)).unwrap();
    assert!(engine.pending_write().is_none());
    engine.tick(602, |event| events.push(event)).unwrap();
    write(&mut engine, 602, b"AT+CMEE?\r", &mut events);
    engine.feed(603, b"+CMEE: 1\rOK\r", |event| events.push(event)).unwrap();
    assert_eq!(engine.phase(), Phase::Idle);
    engine.tick(603, |event| events.push(event)).unwrap();
    write(&mut engine, 603, b"AT+EREG?\r", &mut events);
    engine.feed(604, b"+EREG: 2,0\rOK\r", |event| events.push(event)).unwrap();
    assert_eq!(events.iter().filter(|event| matches!(event, Event::Completed(_))).count(), 2);
    assert_eq!(engine.phase(), Phase::Idle);
}

#[test]
fn back_to_back_late_and_probe_oks_do_not_release_quarantine() {
    let mut engine = engine();
    let mut events = Vec::new();
    start(&mut engine, command(1), &mut events);
    engine.enqueue(command(5000)).unwrap();
    engine.tick(1, |event| events.push(event)).unwrap();
    engine.tick(301, |event| events.push(event)).unwrap();
    write(&mut engine, 301, b"AT+CMEE?\r", &mut events);
    engine.feed(302, b"OK\rOK\r", |event| events.push(event)).unwrap();
    engine.tick(302, |event| events.push(event)).unwrap();
    assert!(engine.is_quarantined());
    assert!(engine.pending_write().is_none());
    assert!(!events.contains(&Event::Resynchronized));
}

#[test]
fn stray_idle_final_resynchronizes_before_next_command() {
    for terminal in ["OK", "ERROR", "NO CARRIER", "+CME ERROR: 10", "+CMS ERROR: 20", "CONNECT"] {
        let mut engine = engine();
        let mut events = Vec::new();
        let id = engine.enqueue(command(5000).with_extra_finals(&["CONNECT"]).unwrap()).unwrap();
        engine.feed(1, format!("{terminal}\r").as_bytes(), |event| events.push(event)).unwrap();
        assert_eq!(events, [Event::Error(Error::UnexpectedFinal)]);
        engine.tick(1, |event| events.push(event)).unwrap();
        assert!(engine.pending_write().is_none());
        engine.tick(301, |event| events.push(event)).unwrap();
        write(&mut engine, 301, b"AT+CMEE?\r", &mut events);
        engine.feed(302, b"+CMEE: 2\rOK\r", |event| events.push(event)).unwrap();
        engine.tick(302, |event| events.push(event)).unwrap();
        write(&mut engine, 302, b"AT+EREG?\r", &mut events);
        engine.feed(303, b"+EREG: 2,0\rOK\r", |event| events.push(event)).unwrap();
        assert_eq!(response(&events), &Response { id, intermediates: vec!["+EREG: 2,0".into()], result: Ok("OK".into()) });
    }
}

#[test]
fn successful_probe_followed_by_stray_ok_reenters_quarantine() {
    let mut engine = engine();
    let mut events = Vec::new();
    engine.enqueue(command(5000)).unwrap();
    engine.feed(0, b"OK\r", |event| events.push(event)).unwrap();
    engine.tick(300, |event| events.push(event)).unwrap();
    write(&mut engine, 300, b"AT+CMEE?\r", &mut events);
    engine.feed(301, b"+CMEE: 1\rOK\rOK\r", |event| events.push(event)).unwrap();
    assert!(events.contains(&Event::Resynchronized));
    assert!(engine.is_quarantined());
    engine.tick(301, |event| events.push(event)).unwrap();
    assert!(engine.pending_write().is_none());
    assert!(!events.iter().any(|event| matches!(event, Event::Completed(_))));
}

#[test]
fn probe_answer_accepts_every_read_split_and_interleaved_urc() {
    let transcript = b"AT+CMEE?\r\n+CMEE: 123\r\n+EIND: 128\r\nOK\r\n";
    for split in 0..=transcript.len() {
        let mut engine = engine();
        let mut events = Vec::new();
        engine.feed(0, b"OK\r", |event| events.push(event)).unwrap();
        engine.tick(300, |event| events.push(event)).unwrap();
        write(&mut engine, 300, b"AT+CMEE?\r", &mut events);
        engine.feed(301, &transcript[..split], |event| events.push(event)).unwrap();
        engine.feed(302, &transcript[split..], |event| events.push(event)).unwrap();
        assert_eq!(engine.phase(), Phase::Idle);
        assert_eq!(events, [Event::Error(Error::UnexpectedFinal), Event::Urc { line: "+EIND: 128".into(), registered: true }, Event::Resynchronized]);
    }
}

#[test]
fn malformed_probe_intermediates_and_non_ok_finals_cannot_resynchronize() {
    for intermediate in ["+CMEE:", "+CMEE: bad", "+CMEE: -1", "+CMEE: 1,2", "+CMEE: 1 trailing"] {
        let mut engine = engine();
        let mut events = Vec::new();
        engine.feed(0, b"OK\r", |event| events.push(event)).unwrap();
        engine.tick(300, |event| events.push(event)).unwrap();
        write(&mut engine, 300, b"AT+CMEE?\r", &mut events);
        engine.feed(301, format!("{intermediate}\rOK\r").as_bytes(), |event| events.push(event)).unwrap();
        assert_eq!(engine.phase(), Phase::Quarantine(Quarantine::Silence));
        assert!(!events.contains(&Event::Resynchronized));
    }
    for terminal in ["ERROR", "+CME ERROR: 10", "+CMS ERROR: 20", "NO CARRIER", "CONNECT"] {
        let mut engine = engine();
        let mut events = Vec::new();
        start(&mut engine, command(1).with_extra_finals(&["CONNECT"]).unwrap(), &mut events);
        engine.tick(1, |event| events.push(event)).unwrap();
        engine.tick(301, |event| events.push(event)).unwrap();
        write(&mut engine, 301, b"AT+CMEE?\r", &mut events);
        engine.feed(302, format!("+CMEE: 1\r{terminal}\rOK\r").as_bytes(), |event| events.push(event)).unwrap();
        assert_eq!(engine.phase(), Phase::Quarantine(Quarantine::Silence));
        assert!(!events.contains(&Event::Resynchronized));
    }
}

#[test]
fn timed_out_cmee_query_still_requires_matched_pair() {
    let mut engine = engine();
    let mut events = Vec::new();
    engine.enqueue(Command::new("AT+CMEE?", Some("+CMEE:"), 1, 1).unwrap()).unwrap();
    engine.tick(0, |event| events.push(event)).unwrap();
    write(&mut engine, 0, b"AT+CMEE?\r", &mut events);
    engine.tick(1, |event| events.push(event)).unwrap();
    engine.tick(301, |event| events.push(event)).unwrap();
    write(&mut engine, 301, b"AT+CMEE?\r", &mut events);
    for byte in b"+CMEE: 1\rOK\r" {
        engine.feed(302, &[*byte], |event| events.push(event)).unwrap();
    }
    assert_eq!(engine.phase(), Phase::Idle);
    assert_eq!(response(&events).result, Err(Error::Timeout));
    assert!(events.contains(&Event::Resynchronized));
}

#[test]
fn channels_reject_empty_duplicate_ids_and_invalid_prefixes() {
    use mindone_radio_at::Channels;
    assert!(matches!(Channels::new(0, &[], &[]), Err(Error::Configuration)));
    assert!(matches!(Channels::new(0, &[1, 6, 1], &[]), Err(Error::Configuration)));
    assert!(matches!(Channels::new(0, &[1], &["bad\rprefix"]), Err(Error::Configuration)));
}

#[test]
fn channels_preserve_ids_and_keep_engines_independent() {
    use mindone_radio_at::Channels;
    let mut channels = Channels::new(0, &[8, 1, 6], &["+EREG:"]).unwrap();
    assert_eq!(channels.ids().collect::<Vec<_>>(), [8, 1, 6]);
    assert!(channels.get(3).is_none());
    assert!(channels.get_mut(3).is_none());
    channels.get_mut(1).unwrap().enqueue(command(1000)).unwrap();
    assert_eq!(channels.get(1).unwrap().queued(), 1);
    assert_eq!(channels.get(6).unwrap().queued(), 0);
    assert_eq!(channels.get(8).unwrap().queued(), 0);
}

#[test]
fn responses_and_events_clone_without_losing_payload() {
    let mut engine = engine();
    let id = engine.enqueue(command(1000)).unwrap();
    let event = Event::Completed(Response { id, intermediates: vec!["+EREG: 1".into()], result: Ok("OK".into()) });
    assert_eq!(event.clone(), event);
    let Event::Completed(response) = event else { panic!() };
    assert_eq!(response.clone(), response);
}
