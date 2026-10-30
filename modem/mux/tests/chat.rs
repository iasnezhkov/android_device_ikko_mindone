/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_mux_core::chat::*;
use mindone_mux_core::transport::Fault;

fn new_controller(pin: Option<&[u8]>, user_build: bool) -> Controller {
    Controller::new(Config { frame_limit: 512, baud_index: 5, user_build,
        pin: pin.map(|value| Pin::new(value).unwrap()) }, 0).unwrap()
}

fn ready(controller: &mut Controller, now: u64) {
    controller.feed(now, b"\r\n+EIND: 128\r\n").unwrap();
    assert_eq!(controller.phase(), Phase::Writing);
}

fn write_all(controller: &mut Controller, now: u64) -> Vec<u8> {
    let request = controller.pending_write().unwrap();
    let bytes = request.bytes.to_vec();
    let token = request.token;
    controller.accept_written(now, token, bytes.len()).unwrap();
    assert_eq!(controller.phase(), Phase::Reading);
    assert!(controller.pending_write().is_none());
    bytes
}

fn to_cmux(controller: &mut Controller) {
    ready(controller, 0);
    for expected in [b"AT\r".as_slice(), b"ATZ\r", b"ATE0\r"] {
        assert_eq!(write_all(controller, 0), expected);
        controller.feed(0, b"\r\nOK\r\n").unwrap();
    }
    assert_eq!(controller.command(), Some(Command::Cmux));
    assert_eq!(write_all(controller, 0), b"AT+CMUX=0,0,5,512\r");
}

#[test]
fn startup_with_ready_indication_and_no_pin_follows_exact_commands() {
    let mut controller = new_controller(None, false);
    to_cmux(&mut controller);
    controller.feed(5, b"\r\n+CMUX: READY\r\n").unwrap();
    let input = b"\r\nOK\r\n\xf9\x03\xef\0\xff";
    let result = controller.feed(6, input).unwrap();
    assert_eq!(result.consumed, 6);
    assert_eq!(result.mux_tail, Some(&input[6..]));
    assert_eq!(controller.phase(), Phase::Ready);
    assert!(controller.indication_seen() && controller.cmux_ready_seen());
    assert_eq!(controller.expires_at(), None);
}

fn echo_handshake(chunk: usize) {
    let mut controller = new_controller(None, false);
    ready(&mut controller, 0);
    for (command, response) in [
        (b"AT\r".as_slice(), b"AT\r\r\nOK\r\n".as_slice()),
        (b"ATZ\r", b"ATZ\r\r\nOK\r\n"),
        (b"ATE0\r", b"ATE0\r\r\nOK\r\n"),
        (b"AT+CMUX=0,0,5,512\r", b"\r\nOK\r\n"),
    ] {
        assert_eq!(write_all(&mut controller, 0), command);
        for input in response.chunks(chunk) {
            let result = controller.feed(0, input).unwrap();
            assert_eq!(result.consumed, input.len());
            assert!(result.mux_tail.is_none_or(|tail| tail.is_empty()));
        }
    }
    assert_eq!(controller.phase(), Phase::Settling);
    assert_eq!(controller.expires_at(), Some(SETTLE_MS));
    controller.tick(SETTLE_MS).unwrap();
    assert_eq!(controller.phase(), Phase::Ready);
}

#[test]
fn echoed_commands_are_ignored_during_the_full_handshake() {
    echo_handshake(CHAT_BYTES);
}

#[test]
fn echoed_handshake_survives_a_feed_boundary_after_every_byte() {
    echo_handshake(1);
}

#[test]
fn bare_cr_terminated_responses_complete_the_handshake() {
    let mut controller = new_controller(None, false);
    controller.feed(0, b"\r+EIND: 128\r").unwrap();
    assert!(controller.indication_seen());
    for expected in [b"AT\r".as_slice(), b"ATZ\r", b"ATE0\r", b"AT+CMUX=0,0,5,512\r"] {
        assert_eq!(write_all(&mut controller, 0), expected);
        let result = controller.feed(0, b"\rOK\r").unwrap();
        assert_eq!(result.consumed, 4);
        assert!(result.mux_tail.is_none_or(|tail| tail.is_empty()));
    }
    assert_eq!(controller.phase(), Phase::Settling);
    controller.tick(SETTLE_MS).unwrap();
    assert_eq!(controller.phase(), Phase::Ready);
    let input = b"\xf9\x03\xef\0\xff";
    let result = controller.feed(SETTLE_MS, input).unwrap();
    assert_eq!(result.consumed, 0);
    assert_eq!(result.mux_tail, Some(input.as_slice()));
}

#[test]
fn non_printable_noise_is_dropped_without_changing_exact_record_matching() {
    for input in [b"\0\xff\r\nOK\r\n".as_slice(), b"\0OK\r", b"O\0\xffK\n"] {
        let mut controller = new_controller(None, false);
        ready(&mut controller, 0);
        write_all(&mut controller, 0);
        for byte in input.chunks(1) { controller.feed(1, byte).unwrap(); }
        assert_eq!(controller.phase(), Phase::Writing);
        assert_eq!(controller.command(), Some(Command::Reset));
    }
    let mut controller = new_controller(None, false);
    ready(&mut controller, 0);
    write_all(&mut controller, 0);
    for input in [b"N\0OK\r".as_slice(), b"+URC:\xff OK\n", b"\0\xffOK"] {
        controller.feed(1, input).unwrap();
        assert_eq!(controller.phase(), Phase::Reading);
        assert_eq!(controller.command(), Some(Command::At));
    }
}

#[test]
fn cmux_frame_noise_waits_for_the_existing_command_deadline() {
    let mut controller = new_controller(None, false);
    ready(&mut controller, 0);
    write_all(&mut controller, 0);
    controller.feed(0, b"OK\r\n").unwrap();
    assert_eq!(controller.command(), Some(Command::Reset));
    write_all(&mut controller, 0);
    for now in (1..COMMAND_TIMEOUT_MS).step_by(10) {
        controller.feed(now, b"\xf9\x03\xef\x01\x70\xf9").unwrap();
        assert_eq!(controller.phase(), Phase::Reading);
        assert_eq!(controller.expires_at(), Some(COMMAND_TIMEOUT_MS));
    }
    assert_eq!(controller.tick(COMMAND_TIMEOUT_MS), Err(Error::Timeout));
    assert_eq!(controller.phase(), Phase::Failed);
    assert!(controller.pending_write().is_none());
}

#[test]
fn pins_accept_four_to_eight_ascii_digits_and_preserve_leading_zeros() {
    for pin in [b"0000".as_slice(), b"01234", b"000001", b"0012345", b"00001234", b"12345678"] {
        let mut controller = new_controller(Some(pin), false);
        ready(&mut controller, 0);
        for _ in 0..3 {
            write_all(&mut controller, 0);
            controller.feed(0, b"OK\r\n").unwrap();
        }
        assert_eq!(controller.command(), Some(Command::Pin));
        let mut expected = b"AT+CPIN=".to_vec();
        expected.extend_from_slice(pin);
        expected.push(b'\r');
        assert_eq!(write_all(&mut controller, 0), expected);
        controller.feed(0, b"OK\r\n").unwrap();
        assert_eq!(controller.command(), Some(Command::Cmux));
        assert!(!controller.pending_write().unwrap().bytes.windows(4).any(|bytes| bytes == b"CPIN"));
    }
    for invalid in [b"".as_slice(), b"123", b"123456789", b"12\r4", b"12\0\0", b"123x"] {
        assert!(matches!(Pin::new(invalid), Err(Error::InvalidPin)));
    }
}

#[test]
fn missing_eind_is_advisory_at_the_exact_user_or_normal_deadline() {
    for (user, wait) in [(true, USER_READY_WAIT_MS), (false, READY_WAIT_MS)] {
        let mut controller = new_controller(None, user);
        assert_eq!(controller.expires_at(), Some(wait));
        controller.tick(wait - 1).unwrap();
        assert_eq!(controller.phase(), Phase::WaitingReady);
        controller.tick(wait).unwrap();
        assert_eq!(controller.command(), Some(Command::At));
        assert_eq!(controller.expires_at(), Some(wait + 5000));
        assert!(!controller.indication_seen());
        assert_eq!(write_all(&mut controller, wait), b"AT\r");
    }
}

#[test]
fn the_first_completed_at_gets_only_one_error_or_response_timeout_retry() {
    for timeout in [false, true] {
        let mut controller = new_controller(None, false);
        ready(&mut controller, 0);
        write_all(&mut controller, 0);
        if timeout { controller.tick(5000).unwrap(); }
        else { controller.feed(1, b"ERROR\r\n").unwrap(); }
        assert_eq!(controller.command(), Some(Command::AtRetry));
        let now = if timeout { 5000 } else { 1 };
        assert_eq!(write_all(&mut controller, now), b"AT\r");
        assert!(matches!(controller.feed(now + 1, b"ERROR\r\n"), Err(Error::Rejected)));
        assert_eq!(controller.phase(), Phase::Failed);
        assert!(controller.pending_write().is_none());
    }
    let mut controller = new_controller(None, false);
    ready(&mut controller, 0);
    write_all(&mut controller, 0);
    controller.tick(5000).unwrap();
    write_all(&mut controller, 5000);
    assert_eq!(controller.tick(10000), Err(Error::Timeout));
}

#[test]
fn partial_writes_advance_only_accepted_bytes_without_refreshing_the_deadline() {
    let mut controller = new_controller(None, false);
    ready(&mut controller, 100);
    let request = controller.pending_write().unwrap();
    let token = request.token;
    assert_eq!(request.bytes, b"AT\r");
    controller.accept_written(101, token, 1).unwrap();
    assert_eq!(controller.pending_write().unwrap().bytes, b"T\r");
    for now in 102..5100 {
        controller.write_fault(now, token, Fault::Interrupted).unwrap();
        assert_eq!(controller.expires_at(), Some(5100));
    }
    controller.accept_written(5099, token, 2).unwrap();
    assert_eq!(controller.phase(), Phase::Reading);
    assert_eq!(controller.expires_at(), Some(5100));
    controller.tick(5100).unwrap();
    assert_eq!(controller.command(), Some(Command::AtRetry));
}

#[test]
fn timed_out_partial_at_is_terminal_and_never_concatenated_with_a_retry() {
    let mut controller = new_controller(None, false);
    ready(&mut controller, 0);
    let token = controller.pending_write().unwrap().token;
    controller.accept_written(1, token, 1).unwrap();
    assert_eq!(controller.tick(5000), Err(Error::Timeout));
    assert_eq!(controller.command(), Some(Command::At));
    assert!(controller.pending_write().is_none());
    assert_eq!(controller.phase(), Phase::Failed);
}

#[test]
fn invalid_zero_late_and_foreign_progress_fail_closed() {
    for (bytes, expected) in [(0, Error::NoProgress), (4, Error::InvalidProgress)] {
        let mut controller = new_controller(None, false);
        ready(&mut controller, 0);
        let token = controller.pending_write().unwrap().token;
        assert_eq!(controller.accept_written(0, token, bytes), Err(expected));
        assert!(controller.pending_write().is_none());
    }
    let mut first = new_controller(None, false);
    let mut second = new_controller(None, false);
    ready(&mut first, 0);
    ready(&mut second, 0);
    let foreign = first.pending_write().unwrap().token;
    assert_eq!(second.accept_written(0, foreign, 1), Err(Error::StaleProgress));
    assert_eq!(first.accept_written(5000, foreign, 1), Err(Error::LateProgress));
}

#[test]
fn substring_and_unterminated_tokens_never_acknowledge_a_command() {
    let mut controller = new_controller(None, false);
    controller.feed(0, b"prefix +EIND: 128 suffix\r\n").unwrap();
    assert_eq!(controller.phase(), Phase::WaitingReady);
    ready(&mut controller, 0);
    write_all(&mut controller, 0);
    for line in [b"NOK\r\n".as_slice(), b"+URC: OK\r\n", b"+URC: ERROR\r\n", b"OK"] {
        controller.feed(100, line).unwrap();
        assert_eq!(controller.phase(), Phase::Reading);
        assert_eq!(controller.command(), Some(Command::At));
    }
    assert_eq!(controller.expires_at(), Some(5000));
    controller.tick(5000).unwrap();
    assert_eq!(controller.command(), Some(Command::AtRetry));
}

#[test]
fn repeated_urcs_and_interrupted_reads_do_not_extend_command_time() {
    let mut controller = new_controller(None, false);
    ready(&mut controller, 0);
    write_all(&mut controller, 0);
    for now in 1..5000 {
        controller.feed(now, b"+URC: 1\r\n").unwrap();
        controller.read_fault(now, Fault::Interrupted).unwrap();
        assert_eq!(controller.expires_at(), Some(5000));
    }
    controller.tick(5000).unwrap();
    assert_eq!(controller.command(), Some(Command::AtRetry));
}

#[test]
fn old_chunk_acknowledgments_are_not_reused_for_unwritten_commands() {
    let mut controller = new_controller(None, false);
    ready(&mut controller, 0);
    write_all(&mut controller, 0);
    controller.feed(1, b"OK\r\nOK\r\nERROR\r\npart").unwrap();
    assert_eq!(controller.phase(), Phase::Writing);
    assert_eq!(controller.command(), Some(Command::Reset));
    write_all(&mut controller, 2);
    assert_eq!(controller.buffered_bytes(), 0);
    controller.feed(3, b"OK\r\n").unwrap();
    assert_eq!(controller.command(), Some(Command::EchoOff));
}

#[test]
fn final_ascii_record_hands_off_every_tail_byte_across_all_delimiter_splits() {
    let mut packet = b"\r\nOK\r\n".to_vec();
    let binary: Vec<u8> = (0..=255).collect();
    packet.extend_from_slice(&binary);
    for split in 0..=packet.len() {
        let mut controller = new_controller(None, false);
        to_cmux(&mut controller);
        controller.feed(5, b"+CMUX: READY\r\n").unwrap();
        let mut tail = Vec::new();
        if let Some(value) = controller.feed(6, &packet[..split]).unwrap().mux_tail { tail.extend_from_slice(value); }
        if let Some(value) = controller.feed(6, &packet[split..]).unwrap().mux_tail { tail.extend_from_slice(value); }
        assert_eq!(controller.phase(), Phase::Ready);
        assert_eq!(tail, binary);
    }
}

#[test]
fn settling_is_separate_absolute_and_preserves_incoming_binary_without_parsing() {
    let mut controller = new_controller(None, false);
    to_cmux(&mut controller);
    let result = controller.feed(10, b"OK\r\n\xf9\0").unwrap();
    assert_eq!(result.mux_tail, Some(b"\xf9\0".as_slice()));
    assert_eq!(controller.phase(), Phase::Settling);
    assert_eq!(controller.expires_at(), Some(1010));
    for now in 11..1010 {
        let binary = [0xff, 0, 1];
        let result = controller.feed(now, &binary).unwrap();
        assert_eq!(result.consumed, 0);
        assert_eq!(result.mux_tail, Some(binary.as_slice()));
        assert_eq!(controller.expires_at(), Some(1010));
    }
    controller.tick(1010).unwrap();
    assert_eq!(controller.phase(), Phase::Ready);
}

#[test]
fn bounded_ascii_errors_and_eof_never_publish_mux_readiness() {
    let mut controller = new_controller(Some(b"00001234"), false);
    assert!(matches!(controller.feed(0, &[b'x'; CHAT_BYTES + 1]), Err(Error::InputTooLarge)));
    assert_eq!(controller.phase(), Phase::Failed);
    assert_eq!(controller.buffered_bytes(), 0);
    assert!(controller.pending_write().is_none());
    let mut controller = new_controller(None, false);
    controller.feed(0, &[b'x'; CHAT_BYTES]).unwrap();
    assert!(matches!(controller.feed(0, b"x"), Err(Error::RecordTooLarge)));
    for fault in [Fault::EndOfStream, Fault::DeviceReset, Fault::Failure] {
        let mut controller = new_controller(None, false);
        assert!(controller.read_fault(0, fault).is_err());
        assert_eq!(controller.phase(), Phase::Failed);
        assert!(controller.pending_write().is_none());
    }
}

#[test]
fn clock_failure_and_cancellation_are_terminal() {
    let config = || Config { frame_limit: 512, baud_index: 5, user_build: false, pin: None };
    assert!(matches!(Controller::new(config(), u64::MAX), Err(Error::ClockOverflow)));
    let mut controller = Controller::new(config(), u64::MAX - READY_WAIT_MS).unwrap();
    assert!(matches!(controller.feed(u64::MAX - 1, b"+EIND: 128\r\n"), Err(Error::ClockOverflow)));
    let mut controller = Controller::new(config(), 100).unwrap();
    assert_eq!(controller.tick(99), Err(Error::ClockRegression));
    let mut controller = Controller::new(config(), 0).unwrap();
    assert_eq!(controller.cancel(), Err(Error::Cancelled));
    assert_eq!(controller.tick(1), Err(Error::Failed));
}

#[test]
fn device_transcript_with_an_unframed_cmux_answer_reaches_ready() {
    let mut controller = new_controller(None, false);
    for input in [b"\r\n+EIND: 64\r\n".as_slice(), b"\r\n+EIMSIFVER:AP:8F,MD:8B\r\n", b"\r\n+EIND: 128\r\n"] {
        controller.feed(0, input).unwrap();
    }
    for (command, reads) in [
        (b"AT\r".as_slice(), [b"A".as_slice(), b"T", b"\r", b"\r\nOK\r\n"].as_slice()),
        (b"ATZ\r", [b"A".as_slice(), b"T", b"Z", b"\r", b"\r\nOK\r\n"].as_slice()),
        (b"ATE0\r", [b"A".as_slice(), b"T", b"E", b"0", b"\r", b"\r\nOK\r\n"].as_slice()),
    ] {
        assert_eq!(write_all(&mut controller, 1), command);
        for input in reads { controller.feed(1, input).unwrap(); }
    }
    assert_eq!(write_all(&mut controller, 1), b"AT+CMUX=0,0,5,512\r");
    let result = controller.feed(2, b"+CMUX: READY").unwrap();
    assert_eq!(result.mux_tail, None);
    assert!(!controller.cmux_ready_seen());
    let result = controller.feed(3, b"OK").unwrap();
    assert!(controller.cmux_ready_seen());
    assert_eq!(result.mux_tail, None);
    assert_eq!(controller.phase(), Phase::Reading);
    assert_eq!(controller.expires_at(), Some(3 + TOKEN_SETTLE_MS));
    controller.tick(2 + TOKEN_SETTLE_MS).unwrap();
    assert_eq!(controller.phase(), Phase::Reading);
    controller.tick(3 + TOKEN_SETTLE_MS).unwrap();
    assert_eq!(controller.phase(), Phase::Ready);
    let frame = b"\xf9\x03\x3f\x01\x1c\xf9";
    let result = controller.feed(30, frame).unwrap();
    assert_eq!(result.consumed, 0);
    assert_eq!(result.mux_tail, Some(frame.as_slice()));
}

#[test]
fn unframed_and_framed_cmux_answers_hand_off_the_same_mux_bytes_at_every_split() {
    let frame = b"\xf9\x03\x3f\x01\x1c\xf9";
    for answer in [b"+CMUX: READYOK".as_slice(), b"\r\n+CMUX: READY\r\n\r\nOK\r\n", b"+CMUX: READY\r\nOK\r\n"] {
        let mut packet = answer.to_vec();
        packet.extend_from_slice(frame);
        for split in 0..=packet.len() {
            let mut controller = new_controller(None, false);
            to_cmux(&mut controller);
            let mut tail = Vec::new();
            for part in [&packet[..split], &packet[split..]] {
                if let Some(value) = controller.feed(5, part).unwrap().mux_tail { tail.extend_from_slice(value); }
            }
            assert_eq!(controller.phase(), Phase::Ready);
            assert_eq!(tail, frame);
        }
    }
}

#[test]
fn an_unframed_cmux_error_is_a_rejection_and_other_commands_stay_line_framed() {
    let mut controller = new_controller(None, false);
    to_cmux(&mut controller);
    controller.feed(5, b"ERROR").unwrap();
    assert_eq!(controller.tick(5 + TOKEN_SETTLE_MS), Err(Error::Rejected));
    let mut controller = new_controller(None, false);
    to_cmux(&mut controller);
    assert!(matches!(controller.feed(5, b"ERROR\r\n"), Err(Error::Rejected)));
    let mut controller = new_controller(None, false);
    ready(&mut controller, 0);
    write_all(&mut controller, 0);
    controller.feed(1, b"OK").unwrap();
    assert_eq!(controller.phase(), Phase::Reading);
    assert_eq!(controller.command(), Some(Command::At));
}

#[test]
fn a_cmux_token_that_starts_a_longer_record_is_not_a_result() {
    let input = b"+CMUX: READY\r\nOK, still waiting\r\nERROR\r\n";
    for split in 0..=input.len() {
        let mut controller = new_controller(None, false);
        to_cmux(&mut controller);
        let first = controller.feed(5, &input[..split]);
        let result = first.and_then(|feed| { assert_eq!(feed.mux_tail, None); controller.feed(6, &input[split..]) });
        assert!(matches!(result, Err(Error::Rejected)), "split {split}");
        assert_ne!(controller.phase(), Phase::Ready);
    }
    for record in [b"OKAY\r\n".as_slice(), b"OK+\r\n", b"ERRORS\r\n", b"+CMUX: READY, maybe\r\n"] {
        let mut controller = new_controller(None, false);
        to_cmux(&mut controller);
        controller.feed(5, record).unwrap();
        assert_eq!(controller.phase(), Phase::Reading, "{record:?}");
        assert!(!controller.cmux_ready_seen() || record.starts_with(b"+CMUX: READY\r"));
    }
}

#[test]
fn a_pending_cmux_token_completes_on_a_frame_flag_or_after_the_settle_time() {
    let mut controller = new_controller(None, false);
    to_cmux(&mut controller);
    controller.feed(5, b"+CMUX: READY").unwrap();
    controller.feed(6, b"OK").unwrap();
    let result = controller.feed(7, b"\xf9\x03").unwrap();
    assert_eq!(controller.phase(), Phase::Ready);
    assert_eq!(result.mux_tail, Some(b"\xf9\x03".as_slice()));
    let mut controller = new_controller(None, false);
    to_cmux(&mut controller);
    controller.feed(5, b"OK").unwrap();
    let result = controller.feed(5 + TOKEN_SETTLE_MS, b"\r\n\xf9").unwrap();
    assert_eq!(controller.phase(), Phase::Settling);
    assert_eq!(result.mux_tail, Some(b"\xf9".as_slice()));
    let mut controller = new_controller(None, false);
    to_cmux(&mut controller);
    controller.feed(COMMAND_TIMEOUT_MS - 1, b"OK").unwrap();
    controller.tick(COMMAND_TIMEOUT_MS).unwrap();
    assert_eq!(controller.phase(), Phase::Reading);
    controller.tick(COMMAND_TIMEOUT_MS - 1 + TOKEN_SETTLE_MS).unwrap();
    assert_eq!(controller.phase(), Phase::Settling);
}

#[test]
fn bytes_already_read_decide_a_pending_token_before_its_settle_timer() {
    for delay in [TOKEN_SETTLE_MS, TOKEN_SETTLE_MS + 1, 10 * TOKEN_SETTLE_MS] {
        let mut controller = new_controller(None, false);
        to_cmux(&mut controller);
        controller.feed(5, b"+CMUX: READY\r\nOK").unwrap();
        assert!(controller.token_pending());
        let result = controller.feed(5 + delay, b", still waiting\r\nERROR\r\n");
        assert!(matches!(result, Err(Error::Rejected)), "delay {delay}");
        let mut controller = new_controller(None, false);
        to_cmux(&mut controller);
        controller.feed(5, b"+CMUX: READY\r\nOK").unwrap();
        let result = controller.feed(5 + delay, b"\r\n\xf9\x01").unwrap();
        assert_eq!(controller.phase(), Phase::Ready, "delay {delay}");
        assert_eq!(result.mux_tail, Some(b"\xf9\x01".as_slice()));
    }
}
