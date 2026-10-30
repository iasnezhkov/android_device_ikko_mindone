// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0
use mindone_sensors_core::*;

#[test]
fn calibration_layouts_copy_signed_words_and_zero_unused_bytes() {
    for (sensor, length, bias, cali, temp) in [(1, 24, 3, 3, 0), (2, 36, 3, 6, 0),
                                             (4, 48, 3, 3, 6), (5, 4, 0, 1, 0)] {
        let values = [i32::MIN, -1, i32::MAX, 9, -19, 0, 3, 4, 5, 6, 7, 8];
        let output = calibration::payload(sensor, &values, &values, &values).unwrap();
        assert_eq!(output.layout.length, length);
        assert!(!output.combined);
        let mut expected = Vec::new();
        for count in [bias, cali, temp] {
            for value in &values[..count] {
                expected.extend_from_slice(&value.to_le_bytes());
            }
        }
        assert_eq!(&output.bytes[..length], expected);
        assert!(output.bytes[length..].iter().all(|byte| *byte == 0));
        let empty = calibration::payload(sensor, &[], &[], &[]).unwrap();
        assert_eq!(empty.bytes, [0; 48]);
    }
}

#[test]
fn calibration_combined_file_overrides_bias_only_at_exact_length() {
    for (sensor, combined_count) in [(1, 6), (2, 9), (4, 6)] {
        let values: Vec<i32> = (1..=combined_count).collect();
        let output = calibration::payload(sensor, &[99; 3], &values, &[]).unwrap();
        assert!(output.combined);
        for (raw, value) in output.bytes.chunks_exact(4).zip(&values) {
            assert_eq!(raw, value.to_le_bytes());
        }
        let short = calibration::payload(sensor, &[99; 3], &values[..values.len() - 1], &[]).unwrap();
        assert!(!short.combined);
        assert_eq!(&short.bytes[..4], &99i32.to_le_bytes());
    }
    let large = vec![17; 100_000];
    let output = calibration::payload(4, &large, &large, &large).unwrap();
    assert!(!output.combined);
    assert!(output.bytes.chunks_exact(4).all(|raw| raw == 17i32.to_le_bytes()));
}

#[test]
fn calibration_events_enforce_supported_regions_and_exact_event_length() {
    let words = [i32::MIN, -1, 0, 1, i32::MAX, -8, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100];
    for sensor in 0..=255 {
        for action in 0..=255 {
            let expected = match (sensor, action) {
                (1 | 2 | 4, 2) | (1 | 4, 3) => Some(3),
                (2, 3) | (4, 4) => Some(6),
                (5, 3) => Some(1),
                _ => None,
            };
            let output = calibration::event(sensor, action, &words);
            if let Some(count) = expected {
                let output = output.unwrap();
                assert_eq!(output.count, count);
                assert_eq!(&output.words[..count], &words[..count]);
                assert_eq!(output.kind as u8, action - 2);
                assert!(output.words[count..].iter().all(|word| *word == 0));
            } else {
                assert!(output.is_err());
            }
        }
    }
    for length in 0..16 {
        assert_eq!(calibration::event(4, 4, &words[..length]), Err(Error::Length));
    }
    assert_eq!(calibration::event(4, 4, &[0; 17]), Err(Error::Length));
    for sensor in [0, 3, 6, 78, 255] {
        assert_eq!(calibration::payload(sensor, &[], &[], &[]), Err(Error::Sensor));
    }
}

#[test]
fn state_transactions_plan_without_mutating_the_snapshot() {
    for active in [false, true] {
        let state = SensorState { active, period_ns: 200_000_000, latency_ns: 7 };
        for enabled in [false, true] {
            let transition = state.activate(1, enabled).unwrap();
            assert_eq!(transition.command, StateCommand::Enable);
            assert_eq!(transition.next, SensorState { active: enabled, ..state });
        }
        for period_ns in [0, 1, i64::MAX] {
            for latency_ns in [0, 1, i64::MAX] {
                let transition = state.batch(77, period_ns, latency_ns).unwrap();
                assert_eq!(transition.command, if active { StateCommand::Enable } else { StateCommand::None });
                assert_eq!(transition.next, SensorState { period_ns, latency_ns, ..state });
                assert_eq!(state.period_ns, 200_000_000);
                assert_eq!(state.latency_ns, 7);
            }
        }
    }
}

#[test]
fn state_rejects_invalid_handles_and_negative_nanoseconds() {
    let state = SensorState { active: true, period_ns: 0, latency_ns: 0 };
    for handle in [i32::MIN, -1, 0, 78, 256, i32::MAX] {
        assert_eq!(state.activate(handle, true), Err(Error::Sensor));
        assert_eq!(state.batch(handle, 0, 0), Err(Error::Sensor));
        assert_eq!(state.flush(handle, false), Err(Error::Sensor));
    }
    for invalid in [i64::MIN, -1] {
        assert_eq!(state.batch(1, invalid, 0), Err(Error::Timing));
        assert_eq!(state.batch(1, 0, invalid), Err(Error::Timing));
        let broken = SensorState { period_ns: invalid, ..state };
        assert_eq!(broken.activate(1, false), Err(Error::Timing));
        assert_eq!(broken.batch(1, 0, 0), Err(Error::Timing));
        assert_eq!(broken.flush(1, false), Err(Error::Timing));
    }
}

#[test]
fn flush_requires_an_active_non_one_shot_sensor_and_retains_state() {
    for active in [false, true] {
        let state = SensorState { active, period_ns: 1, latency_ns: i64::MAX };
        for one_shot in [false, true] {
            let actual = state.flush(1, one_shot);
            if active && !one_shot {
                assert_eq!(actual, Ok(StateTransition { next: state, command: StateCommand::Flush }));
            } else {
                assert_eq!(actual, Err(Error::State));
            }
        }
    }
}

fn event(sensor: u8, action: u8) -> [u8; EVENT_BYTES] {
    let mut bytes = [0xa5; EVENT_BYTES];
    bytes[..8].copy_from_slice(&i64::MAX.to_le_bytes());
    bytes[8] = sensor;
    bytes[9] = 3;
    bytes[10] = action;
    for (index, raw) in bytes[12..].chunks_exact_mut(4).enumerate() {
        raw.copy_from_slice(&(i32::MIN + index as i32).to_le_bytes());
    }
    bytes
}

#[test]
fn event_fields_are_little_endian_and_padding_is_ignored() {
    for sensor in 1..SENSOR_MAX {
        for action in 0..7 {
            let decoded = decode_event(&event(sensor, action)).unwrap();
            assert_eq!(decoded.timestamp_ns, i64::MAX);
            assert_eq!(decoded.sensor_type, sensor);
            assert_eq!(decoded.accuracy, 3);
            assert_eq!(decoded.action as u8, action);
            for (index, value) in decoded.words.iter().enumerate() {
                assert_eq!(*value, i32::MIN + index as i32);
            }
        }
    }
}

#[test]
fn partial_and_extra_event_bytes_are_rejected() {
    let valid = event(1, 0);
    for length in 0..EVENT_BYTES {
        assert_eq!(decode_event(&valid[..length]), Err(Error::Length));
    }
    let mut extra = valid.to_vec();
    extra.push(0);
    assert_eq!(decode_event(&extra), Err(Error::Length));
}

#[test]
fn invalid_sensor_actions_and_data_accuracy_are_explicit_errors() {
    for sensor in [0, SENSOR_MAX, 255] {
        assert_eq!(decode_event(&event(sensor, 0)), Err(Error::Sensor));
    }
    for action in 7..=255 {
        assert_eq!(decode_event(&event(1, action)), Err(Error::Action));
    }
    let mut raw = event(1, 0);
    for accuracy in 4..=255 {
        raw[9] = accuracy;
        assert_eq!(decode_event(&raw), Err(Error::Accuracy));
    }
    raw[10] = 4;
    assert_eq!(decode_event(&raw).unwrap().accuracy, 255);
}

#[test]
fn signed_payload_and_timestamp_bits_are_preserved() {
    let mut raw = event(1, 0);
    for timestamp in [i64::MIN, -1, 0, 1, i64::MAX] {
        raw[..8].copy_from_slice(&timestamp.to_le_bytes());
        assert_eq!(decode_event(&raw).unwrap().timestamp_ns, timestamp);
    }
    for value in [i32::MIN, -1, 0, 1, i32::MAX] {
        raw[12..16].copy_from_slice(&value.to_le_bytes());
        assert_eq!(decode_event(&raw).unwrap().words[0], value);
    }
}

#[test]
fn sensor_info_has_fixed_byte_names_and_unsigned_gain() {
    let mut raw = [0xff; INFO_BYTES];
    raw[0] = SENSOR_MAX - 1;
    let decoded = decode_info(&raw).unwrap();
    assert_eq!(decoded.sensor_type, SENSOR_MAX - 1);
    assert_eq!(decoded.gain, u32::MAX);
    assert_eq!(decoded.name, [0xff; 16]);
    assert_eq!(decoded.vendor, [0xff; 16]);
    raw[0] = 0;
    assert_eq!(decode_info(&raw), Err(Error::Sensor));
    for length in 0..INFO_BYTES {
        assert_eq!(decode_info(&raw[..length]), Err(Error::Length));
    }
}

#[test]
fn enable_and_disable_commands_zero_reserved_fields_and_use_nanoseconds() {
    for sensor in 1..SENSOR_MAX {
        for enabled in [false, true] {
            for value in [0, 1, 200_000_000, i64::MAX] {
                let command = enable_command(sensor, enabled, value, value).unwrap();
                assert_eq!(&command[..4], &[sensor, u8::from(enabled), 16, 0]);
                assert_eq!(&command[4..12], &value.to_le_bytes());
                assert_eq!(&command[12..20], &value.to_le_bytes());
                assert_eq!(&command[20..], &[0; 32]);
            }
        }
    }
}

#[test]
fn invalid_commands_never_produce_wire_bytes() {
    for sensor in [0, SENSOR_MAX, 255] {
        assert_eq!(enable_command(sensor, true, 0, 0), Err(Error::Sensor));
        assert_eq!(flush_command(sensor), Err(Error::Sensor));
        assert_eq!(calibration_command(sensor, &[]), Err(Error::Sensor));
    }
    assert_eq!(enable_command(1, true, -1, 0), Err(Error::Timing));
    assert_eq!(enable_command(1, false, 0, -1), Err(Error::Timing));
    assert_eq!(calibration_command(1, &[0; 49]), Err(Error::Length));
}

#[test]
fn calibration_and_flush_zero_all_unused_payload_bytes() {
    for sensor in 1..SENSOR_MAX {
        let flush = flush_command(sensor).unwrap();
        assert_eq!(&flush[..4], &[sensor, 2, 0, 0]);
        assert_eq!(&flush[4..], &[0; CALIBRATION_BYTES]);
        for length in 0..=CALIBRATION_BYTES {
            let data = [0xa5; CALIBRATION_BYTES];
            let command = calibration_command(sensor, &data[..length]).unwrap();
            assert_eq!(&command[..4], &[sensor, 4, length as u8, 0]);
            assert_eq!(&command[4..4 + length], &data[..length]);
            assert!(command[4 + length..].iter().all(|byte| *byte == 0));
        }
    }
}

#[test]
fn continuous_read_budget_stops_after_two_full_batches() {
    let mut budget = DrainBudget::default();
    assert_eq!(budget.next_read_bytes(), READ_BATCH * EVENT_BYTES);
    assert_eq!(budget.accept_read(READ_BATCH * EVENT_BYTES), Ok(READ_BATCH));
    assert_eq!(budget.next_read_bytes(), READ_BATCH * EVENT_BYTES);
    assert_eq!(budget.accept_read(READ_BATCH * EVENT_BYTES), Ok(READ_BATCH));
    assert_eq!(budget.records(), DRAIN_LIMIT);
    assert_eq!(budget.next_read_bytes(), 0);
    assert_eq!(budget.accept_read(EVENT_BYTES), Err(Error::Capacity));
}

#[test]
fn malformed_and_oversized_reads_do_not_consume_budget() {
    let mut budget = DrainBudget::default();
    for bytes in [1, EVENT_BYTES - 1, EVENT_BYTES + 1] {
        assert_eq!(budget.accept_read(bytes), Err(Error::Length));
        assert_eq!(budget.records(), 0);
    }
    assert_eq!(budget.accept_read((READ_BATCH + 1) * EVENT_BYTES), Err(Error::Capacity));
    assert_eq!(budget.records(), 0);
    assert_eq!(budget.accept_read(0), Ok(0));
    assert_eq!(budget.accept_read(3 * EVENT_BYTES), Ok(3));
    assert_eq!(budget.records(), 3);
}
