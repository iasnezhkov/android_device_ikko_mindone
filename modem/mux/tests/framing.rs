/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_mux_core::{encode, Decoder, Error, Frame, FLAG, MAX_PAYLOAD, POLL_FINAL, UI, UIH};

fn encoded(control: u8, payload: &[u8]) -> Vec<u8> {
    let mut output = vec![0; payload.len() + 7];
    let length = encode(Frame::command(3, control, payload).unwrap(), MAX_PAYLOAD, &mut output)
        .unwrap();
    output.truncate(length);
    output
}

#[test]
fn command_wire_vectors_and_length_boundaries_are_exact() {
    let mut output = [0; 6];
    assert_eq!(encode(Frame::command(0, 0x3f, &[]).unwrap(), 512, &mut output), Ok(6));
    assert_eq!(output, [0xf9, 0x03, 0x3f, 0x01, 0x1c, 0xf9]);
    for length in [0, 1, 126, 127, 128, 129, 511, 512, 32766, 32767] {
        let payload: Vec<u8> = (0..length).map(|index| index as u8).collect();
        let wire = encoded(UIH, &payload);
        assert_eq!(wire[3], (length << 1) as u8 | u8::from(length < 128));
        if length >= 128 {
            assert_eq!(wire[4], (length >> 7) as u8);
        }
        let mut decoder = Decoder::<32774>::new(MAX_PAYLOAD).unwrap();
        decoder.push(&wire).unwrap();
        let frame = decoder.next_frame().unwrap();
        assert_eq!(frame.address, 15);
        assert_eq!(frame.channel(), 3);
        assert_eq!(frame.control, UIH);
        assert_eq!(frame.payload, payload);
        assert!(decoder.next_frame().is_none());
        assert_eq!(decoder.buffered(), [FLAG]);
    }
}

#[test]
fn invalid_limits_channels_and_capacities_are_rejected() {
    assert!(matches!(Decoder::<1024>::new(0), Err(Error::InvalidLimit)));
    assert!(matches!(Decoder::<65536>::new(MAX_PAYLOAD + 1), Err(Error::InvalidLimit)));
    assert!(matches!(Decoder::<518>::new(512), Err(Error::InsufficientCapacity)));
    assert_eq!(Frame::command(64, UIH, &[]), Err(Error::InvalidChannel));
    assert!(Frame::command(63, UIH, &[]).is_ok());
    assert!(Decoder::<8>::new(1).is_ok());
}

#[test]
fn rejected_encoding_does_not_modify_output() {
    let mut output = [0x5a; 16];
    let original = output;
    let frame = Frame::command(0, UIH, b"payload").unwrap();
    assert_eq!(encode(frame, 6, &mut output), Err(Error::OversizedPayload));
    assert_eq!(output, original);
    assert_eq!(encode(frame, 0, &mut output), Err(Error::InvalidLimit));
    assert_eq!(output, original);
    assert_eq!(encode(frame, 512, &mut output[..5]), Err(Error::InsufficientCapacity));
    assert_eq!(output, original);
}

#[test]
fn every_split_preserves_one_complete_event_and_a_shared_closing_flag() {
    for control in [UI, UI | POLL_FINAL, UIH, UIH | POLL_FINAL, 0x3f] {
        let payload: Vec<u8> = (0..140).map(|index| index as u8).collect();
        let wire = encoded(control, &payload);
        for split in 0..wire.len() {
            let mut decoder = Decoder::<519>::new(512).unwrap();
            decoder.push(&wire[..split]).unwrap();
            assert!(decoder.next_frame().is_none());
            decoder.push(&wire[split..]).unwrap();
            assert_eq!(decoder.next_frame().unwrap().payload, payload);
            assert!(decoder.next_frame().is_none());
            decoder.push(&wire[1..]).unwrap();
            assert_eq!(decoder.next_frame().unwrap().payload, payload);
            assert!(decoder.next_frame().is_none());
        }
    }
}

#[test]
fn payload_checksum_matches_each_control_type() {
    for control in [UI, UI | POLL_FINAL, UIH, UIH | POLL_FINAL] {
        let mut wire = encoded(control, b"data");
        wire[4] ^= 0x20;
        let mut decoder = Decoder::<519>::new(512).unwrap();
        decoder.push(&wire).unwrap();
        if control & !POLL_FINAL == UI {
            assert!(decoder.next_frame().is_none());
            assert_eq!(decoder.stats().bad_checksums, 1);
        } else {
            assert_eq!(decoder.next_frame().unwrap().payload, b"Data");
            assert_eq!(decoder.stats().bad_checksums, 0);
        }
    }
}

#[test]
fn malformed_frames_resynchronize_without_publishing_corrupt_data() {
    let good = encoded(UI, b"accepted");
    let mut bad_checksum = encoded(UI, b"bad");
    let offset = bad_checksum.len() - 2;
    bad_checksum[offset] ^= 1;
    let mut bad_closing = encoded(UI, b"bad");
    *bad_closing.last_mut().unwrap() = 0;
    let oversized = [FLAG, 3, UIH, 0, 5];
    let mut decoder = Decoder::<519>::new(512).unwrap();
    for bad in [b"noise".as_slice(), &bad_checksum, &bad_closing, &oversized] {
        decoder.push(bad).unwrap();
        decoder.push(&good).unwrap();
        assert_eq!(decoder.next_frame().unwrap().payload, b"accepted");
        assert!(decoder.next_frame().is_none());
    }
    assert_eq!(decoder.stats().bad_checksums, 1);
    assert_eq!(decoder.stats().missing_closing_flags, 1);
    assert_eq!(decoder.stats().oversized_frames, 1);
    assert!(decoder.stats().discarded_bytes >= 5);
}

#[test]
fn backpressure_is_explicit_and_preserves_the_pending_frame() {
    let wire = encoded(UIH, &[0x42; 512]);
    let mut decoder = Decoder::<519>::new(512).unwrap();
    decoder.push(&wire[..wire.len() - 1]).unwrap();
    assert!(decoder.next_frame().is_none());
    let pending = decoder.buffered().to_vec();
    assert_eq!(decoder.available_capacity(), 1);
    assert_eq!(decoder.push(b"overflow"), Err(Error::InsufficientCapacity));
    assert_eq!(decoder.buffered(), pending);
    decoder.push(&wire[wire.len() - 1..]).unwrap();
    assert_eq!(decoder.next_frame().unwrap().payload, [0x42; 512]);
    assert_eq!(decoder.available_capacity(), 518);
}

#[test]
fn reset_discards_partial_input_without_replaying_stale_frames() {
    let wire = encoded(UI, b"stale");
    let mut decoder = Decoder::<519>::new(512).unwrap();
    decoder.push(&wire[..4]).unwrap();
    assert!(decoder.next_frame().is_none());
    assert_eq!(decoder.reset(), 4);
    decoder.push(&wire[4..]).unwrap();
    assert!(decoder.next_frame().is_none());
    decoder.reset();
    decoder.push(&wire).unwrap();
    assert_eq!(decoder.next_frame().unwrap().payload, b"stale");
    assert!(decoder.next_frame().is_none());
}
