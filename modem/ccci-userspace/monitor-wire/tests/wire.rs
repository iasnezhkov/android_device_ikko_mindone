/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_monitor_wire::{Error, Kind, Message, MAGIC, MONITOR_WORD};

fn packet(id: u32, reserved: u32) -> Vec<u8> {
    [MAGIC, id, MONITOR_WORD, reserved].into_iter().flat_map(u32::to_le_bytes).collect()
}

#[test]
fn explicit_little_endian_vector_preserves_message_id_and_opaque_reserved_word() {
    let bytes = [0xff, 0xff, 0xff, 0xff, 5, 0, 0xf5, 0xfa, 0, 0, 0, 0xf0, 0x78, 0x56, 0x34, 0x92];
    let message = Message::parse(&bytes).unwrap();
    assert_eq!(message.id(), 0xfaf50005);
    assert_eq!(message.kind(), Kind::ResetRequest);
    assert_eq!(message.reserved(), 0x92345678);
}

#[test]
fn ten_known_message_ids_are_classification_without_lifecycle_or_nvram_actions() {
    let kinds = [Kind::ForceStopRequest, Kind::FlightStopRequest, Kind::ForceStartRequest, Kind::FlightStartRequest,
        Kind::ResetRequest, Kind::Exception, Kind::SendBatteryInfo, Kind::StoreNvramModemType, Kind::ConfigUpdate, Kind::RandomPattern];
    for (offset, kind) in kinds.into_iter().enumerate() {
        let bytes = packet(0xfaf50001 + offset as u32, 0);
        let message = Message::parse(&bytes).unwrap();
        assert_eq!(message.kind(), kind);
        assert_eq!(message.id(), 0xfaf50001 + offset as u32);
    }
}

#[test]
fn unknown_events_remain_explicitly_unsupported_with_the_original_numeric_id() {
    for id in [0, 1, 0xfaf50000, 0xfaf5000b, u32::MAX] {
        let message = Message::parse(&packet(id, 0)).unwrap();
        assert_eq!(message.kind(), Kind::Unsupported(id));
        assert_eq!(message.id(), id);
    }
}

#[test]
fn full_virtual_monitor_word_is_checked_instead_of_its_low_channel_half() {
    for value in [0u32, 0x00000020, 0x70000000, 0x80000000, 0xffff0000, 0xf0000001, u32::MAX] {
        let mut bytes = packet(0xfaf50005, 0);
        bytes[8..12].copy_from_slice(&value.to_le_bytes());
        assert_eq!(Message::parse(&bytes), Err(Error::MonitorWord));
    }
}

#[test]
fn invalid_magic_is_rejected_even_when_message_and_monitor_word_are_valid() {
    for value in [0u32, 1, 0xfffffffe, 0xffffff00, 0xf0000000] {
        let mut bytes = packet(0xfaf50005, 0);
        bytes[..4].copy_from_slice(&value.to_le_bytes());
        assert_eq!(Message::parse(&bytes), Err(Error::Magic));
    }
}

#[test]
fn every_truncated_prefix_suffix_and_concatenation_is_rejected_as_a_record() {
    let bytes = packet(0xfaf50005, 0);
    for length in 0..16 { assert_eq!(Message::parse(&bytes[..length]), Err(Error::RecordLength)); }
    for extra in 1..=32 {
        let mut extended = bytes.clone();
        extended.extend(vec![0; extra]);
        assert_eq!(Message::parse(&extended), Err(Error::RecordLength));
    }
    let mut joined = bytes.clone();
    joined.extend(&bytes);
    assert_eq!(Message::parse(&joined), Err(Error::RecordLength));
    for offset in 1..16 { assert_eq!(Message::parse(&bytes[offset..]), Err(Error::RecordLength)); }
}

#[test]
fn decoded_values_are_owned_and_cannot_be_changed_by_later_input_reuse() {
    let mut bytes = packet(0xfaf50008, 0xdeadbeef);
    let message = Message::parse(&bytes).unwrap();
    bytes.fill(0);
    assert_eq!(message.kind(), Kind::StoreNvramModemType);
    assert_eq!(message.reserved(), 0xdeadbeef);
    assert_eq!(Message::parse(&bytes), Err(Error::Magic));
}

#[test]
fn reserved_is_opaque_for_all_bit_positions_and_never_treated_as_length_or_an_action() {
    for reserved in std::iter::once(0).chain((0..32).map(|shift| 1u32 << shift)).chain([u32::MAX]) {
        let message = Message::parse(&packet(0xfaf50006, reserved)).unwrap();
        assert_eq!(message.kind(), Kind::Exception);
        assert_eq!(message.reserved(), reserved);
    }
}

#[test]
fn actual_kernel_producer_records_decode_to_the_original_inputs() {
    let path = std::env::var_os("MINDONE_MONITOR_FIXTURE").expect("The check runner supplies the exact kernel producer fixture");
    let bytes = std::fs::read(path).unwrap();
    assert!(!bytes.is_empty());
    assert_eq!(bytes.len() % 24, 0);
    for record in bytes.chunks_exact(24) {
        let id = u32::from_le_bytes(record[..4].try_into().unwrap());
        let reserved = u32::from_le_bytes(record[4..8].try_into().unwrap());
        let message = Message::parse(&record[8..]).unwrap();
        assert_eq!((message.id(), message.reserved()), (id, reserved));
        if !(0xfaf50001..=0xfaf5000a).contains(&id) { assert_eq!(message.kind(), Kind::Unsupported(id)); }
    }
    println!("monitor-producer records={}", bytes.len() / 24);
}

#[test]
fn deterministic_arbitrary_inputs_and_all_monitor_word_bits_are_bounded() {
    let mut random = 0x729faf5u64;
    for iteration in 0..20000 {
        random = random.wrapping_mul(6364136223846793005).wrapping_add(1);
        let mut bytes = vec![0; iteration % 65];
        for byte in &mut bytes {
            random = random.wrapping_mul(6364136223846793005).wrapping_add(1);
            *byte = (random >> 32) as u8;
        }
        let _ = Message::parse(&bytes);
        let id = random as u32;
        let reserved = (random >> 32) as u32;
        let message = Message::parse(&packet(id, reserved)).unwrap();
        assert_eq!((message.id(), message.reserved()), (id, reserved));
    }
    for bit in 0..32 {
        let mut bytes = packet(0xfaf50005, 0);
        bytes[8..12].copy_from_slice(&(MONITOR_WORD ^ (1 << bit)).to_le_bytes());
        assert_eq!(Message::parse(&bytes), Err(Error::MonitorWord));
    }
    println!("monitor-fuzz arbitrary=20000 valid_records=20000 word_bit_faults=32");
}
