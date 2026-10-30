/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_rpc_wire::{encode_response, Error, Header, Operation, Request, MAX_ARGUMENTS, MAX_RECORD_BYTES};

fn packet(opcode: u32, index: u32, data1: u32, arguments: &[&[u8]]) -> Vec<u8> {
    let mut bytes = Vec::new();
    for value in [0x12345678u32, data1, 0x92340020, index, opcode, arguments.len() as u32] {
        bytes.extend(value.to_le_bytes());
    }
    for argument in arguments {
        bytes.extend((argument.len() as u32).to_le_bytes());
        bytes.extend_from_slice(argument);
        while bytes.len() % 4 != 0 { bytes.push(0); }
    }
    bytes
}

fn rejected(bytes: &[u8], expected: Error) {
    match Request::parse(bytes) {
        Err(error) => assert_eq!(error, expected),
        Ok(_) => panic!("Unexpected valid request"),
    }
}

#[test]
fn explicit_little_endian_vector_preserves_header_and_borrows_only_argument_bytes() {
    let bytes = [
        0x78, 0x56, 0x34, 0x12, 0xef, 0xcd, 0xab, 0x89,
        0x20, 0x00, 0x34, 0x92, 0x02, 0x00, 0x00, 0x00,
        0x0f, 0x40, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00, b'a', b'b', b'c', 0xaa,
        0x01, 0x00, 0x00, 0x00, 0xfe, 0xbb, 0xcc, 0xdd,
    ];
    let request = Request::parse(&bytes).unwrap();
    assert_eq!(request.header(), Header { data0: 0x12345678, data1: 0x89abcdef, channel: 32,
        sequence: 0x1234, asserted: true, request_index: 2 });
    assert_eq!(request.opcode(), 0x400f);
    assert_eq!(request.operation(), Operation::QueryApSystemProperty);
    assert_eq!(request.argument_count(), 2);
    assert_eq!(request.argument(0), Some(b"abc".as_slice()));
    assert_eq!(request.argument(1), Some([0xfe].as_slice()));
    assert_eq!(request.argument(2), None);
    assert_eq!(request.argument(usize::MAX), None);
    assert_eq!(request.argument(0).unwrap().as_ptr(), bytes[28..].as_ptr());
    assert_eq!(request.arguments().collect::<Vec<_>>(), vec![b"abc".as_slice(), [0xfe].as_slice()]);
}

#[test]
fn rx_data1_is_opaque_even_when_zero_huge_or_a_different_packet_length() {
    for data1 in [0, 1, 16, 24, 2048, u32::MAX, 0x12345678] {
        let bytes = packet(0x4015, 0, data1, &[b"opaque"]);
        let request = Request::parse(&bytes).unwrap();
        assert_eq!(request.header().data1, data1);
        assert_eq!(request.argument(0), Some(b"opaque".as_slice()));
    }
}

#[test]
fn operation_classification_does_not_invent_a_backend_or_tc1_support() {
    for (opcode, operation) in [(0x400f, Operation::QueryApSystemProperty), (0x4010, Operation::SarTableIndexQuery),
        (0x4014, Operation::AmmsDrdiControl), (0x4015, Operation::SaveModemCapid),
        (0, Operation::Unsupported(0)), (0x2001, Operation::Unsupported(0x2001)),
        (0x4321, Operation::Unsupported(0x4321)), (0xffff, Operation::Unsupported(0xffff))] {
        let bytes = packet(opcode, 0, 0, &[]);
        let request = Request::parse(&bytes).unwrap();
        assert_eq!(request.operation(), operation);
        assert_eq!(request.opcode(), opcode);
        assert_eq!(request.arguments().len(), 0);
    }
}

#[test]
fn source_index_boundary_is_inclusive_but_wrong_direction_is_rejected() {
    for index in 0..=2 { assert!(Request::parse(&packet(0x400f, index, 0, &[])).is_ok()); }
    for index in [3, u32::MAX] { rejected(&packet(0x400f, index, 0, &[]), Error::RequestIndex); }
    for channel in [0u16, 31, 33, u16::MAX] {
        let mut bytes = packet(0x400f, 0, 0, &[]);
        bytes[8..10].copy_from_slice(&channel.to_le_bytes());
        rejected(&bytes, Error::Channel);
    }
    for opcode in [0x10000, 0xffff400f, u32::MAX] {
        rejected(&packet(opcode, 0, 0, &[]), Error::ResponseOpcode);
    }
}

#[test]
fn full_record_bound_includes_both_headers_and_all_argument_padding() {
    let body = vec![0x5a; MAX_RECORD_BYTES - 28];
    let bytes = packet(0x4014, 2, 0, &[&body]);
    assert_eq!(bytes.len(), MAX_RECORD_BYTES);
    let request = Request::parse(&bytes).unwrap();
    assert_eq!(request.argument(0), Some(body.as_slice()));
    let mut output = [0xa5; MAX_RECORD_BYTES];
    assert_eq!(encode_response(&request, &[&body], &mut output), Ok(MAX_RECORD_BYTES));
    assert_eq!(&output[28..], body.as_slice());
    let too_large = vec![0x5a; body.len() + 1];
    rejected(&packet(0x4014, 0, 0, &[&too_large]), Error::OversizedRecord);
    let before = output;
    assert_eq!(encode_response(&request, &[&too_large], &mut output), Err(Error::OversizedRecord));
    assert_eq!(output, before);
    assert_eq!(encode_response(&request, &[&[0; MAX_RECORD_BYTES]], &mut output), Err(Error::OversizedRecord));
    assert_eq!(output, before);
}

#[test]
fn six_arguments_include_empty_values_and_all_four_alignment_classes() {
    let arguments = [b"".as_slice(), b"a", b"bc", b"def", b"ghij", b"klmno"];
    let bytes = packet(0x400f, 0, 0, &arguments);
    let request = Request::parse(&bytes).unwrap();
    assert_eq!(request.argument_count(), MAX_ARGUMENTS);
    assert_eq!(request.arguments().collect::<Vec<_>>(), arguments);
    let mut bad = bytes.clone();
    for count in [7u32, u32::MAX] {
        bad[20..24].copy_from_slice(&count.to_le_bytes());
        rejected(&bad, Error::TooManyArguments);
    }
    let mut output = [0xa5; 128];
    let before = output;
    assert_eq!(encode_response(&request, &[b"x".as_slice(); 7], &mut output), Err(Error::TooManyArguments));
    assert_eq!(output, before);
}

#[test]
fn truncated_lengths_payload_padding_and_trailing_records_are_explicit_errors() {
    for length in 0..24 { rejected(&vec![0; length], Error::Truncated); }
    let mut bytes = packet(0x400f, 0, 0, &[b"abc"]);
    for length in 24..28 { rejected(&bytes[..length], Error::Truncated); }
    rejected(&bytes[..30], Error::ArgumentLength);
    rejected(&bytes[..31], Error::MissingPadding);
    for length in [4u32, 2048, u32::MAX] {
        bytes[24..28].copy_from_slice(&length.to_le_bytes());
        rejected(&bytes[..31], Error::ArgumentLength);
    }
    let mut bytes = packet(0x400f, 0, 0, &[]);
    bytes.push(0);
    rejected(&bytes, Error::TrailingBytes);
    let mut bytes = packet(0x400f, 0, 0, &[b"abc"]);
    bytes.extend(packet(0x4010, 1, 0, &[]));
    rejected(&bytes, Error::TrailingBytes);
}

#[test]
fn tx_reply_propagates_opcode_index_and_sequence_but_sets_size_channel_and_zero_padding() {
    let bytes = packet(0x4015, 2, u32::MAX, &[b"incoming"]);
    let request = Request::parse(&bytes).unwrap();
    let mut output = [0xa5; 64];
    let length = encode_response(&request, &[b"x", b"abc"], &mut output).unwrap();
    let expected = [
        0x78, 0x56, 0x34, 0x12, 40, 0, 0, 0, 33, 0, 0x34, 0x92, 2, 0, 0, 0,
        0x15, 0x40, 0xff, 0xff, 2, 0, 0, 0, 1, 0, 0, 0, b'x', 0, 0, 0,
        3, 0, 0, 0, b'a', b'b', b'c', 0,
    ];
    assert_eq!(length, 40);
    assert_eq!(&output[..length], &expected);
    assert_eq!(output[length..], [0xa5; 24]);
    for flags in [0u32, 0x7fff0000, 0x80000000, 0xffff0000] {
        let mut bytes = packet(0x400f, 1, 0, &[]);
        bytes[8..12].copy_from_slice(&(flags | 32).to_le_bytes());
        let request = Request::parse(&bytes).unwrap();
        let mut output = [0xa5; 24];
        assert_eq!(encode_response(&request, &[], &mut output), Ok(24));
        assert_eq!(u32::from_le_bytes(output[8..12].try_into().unwrap()), flags | 33);
    }
}

#[test]
fn every_rejected_reply_preserves_the_entire_output_buffer() {
    let bytes = packet(0x400f, 0, 0, &[]);
    let request = Request::parse(&bytes).unwrap();
    for capacity in 0..32 {
        let mut output = vec![0xa5; capacity];
        let before = output.clone();
        assert_eq!(encode_response(&request, &[b"abc"], &mut output), Err(Error::OutputCapacity));
        assert_eq!(output, before);
    }
    let mut output = [0xa5; MAX_RECORD_BYTES];
    let before = output;
    for arguments in [vec![b"x".as_slice(); 7], vec![&[0; MAX_RECORD_BYTES + 1]], vec![&[0; MAX_RECORD_BYTES - 24]]] {
        assert!(encode_response(&request, &arguments, &mut output).is_err());
        assert_eq!(output, before);
    }
}

#[test]
fn all_payload_lengths_and_record_splits_have_a_bounded_unambiguous_result() {
    for length in 0..=MAX_RECORD_BYTES - 28 {
        let value = vec![(length % 251) as u8; length];
        let bytes = packet(0x4010, (length % 3) as u32, u32::MAX, &[&value]);
        let request = Request::parse(&bytes).unwrap();
        assert_eq!(request.argument(0), Some(value.as_slice()));
        for end in [0, 16, 20, 24, 27, bytes.len() - 1] {
            assert!(Request::parse(&bytes[..end.min(bytes.len() - 1)]).is_err());
        }
    }
}

#[test]
fn deterministic_arbitrary_input_and_source_shaped_roundtrips_never_panic_or_exceed_bounds() {
    let mut random = 0x7294015u64;
    for iteration in 0..20000 {
        random = random.wrapping_mul(6364136223846793005).wrapping_add(1);
        let length = random as usize % 2200;
        let mut bytes = vec![0; length];
        for byte in &mut bytes {
            random = random.wrapping_mul(6364136223846793005).wrapping_add(1);
            *byte = (random >> 32) as u8;
        }
        let _ = Request::parse(&bytes);
        let count = iteration % 7;
        let values: Vec<_> = (0..count).map(|index| vec![(random >> index) as u8; (random as usize >> (index + 4)) % 97]).collect();
        let arguments: Vec<_> = values.iter().map(Vec::as_slice).collect();
        let bytes = packet(0x400f + (iteration % 7) as u32, (iteration % 3) as u32, random as u32, &arguments);
        let request = Request::parse(&bytes).unwrap();
        assert_eq!(request.arguments().collect::<Vec<_>>(), arguments);
        let mut output = [0xa5; MAX_RECORD_BYTES];
        let length = encode_response(&request, &arguments, &mut output).unwrap();
        output[8..10].copy_from_slice(&32u16.to_le_bytes());
        output[18..20].copy_from_slice(&0u16.to_le_bytes());
        let decoded = Request::parse(&output[..length]).unwrap();
        assert_eq!(decoded.arguments().collect::<Vec<_>>(), arguments);
        assert_eq!(decoded.opcode(), request.opcode());
    }
    println!("rpc-fuzz arbitrary=20000 valid_roundtrips=20000 length_boundaries=2021");
}
