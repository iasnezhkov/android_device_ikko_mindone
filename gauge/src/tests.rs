// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

use crate::{proto, sample, store, transport};
use proto::{Header, PtimSampleV1};
use std::fs;
use std::io;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Duration;
use transport::mock::{MockIo, MockTransport};
use transport::{Error as TransportError, Transport};

const TIMEOUT: Duration = Duration::from_secs(1);
const GOLDEN_PTIM: [u8; 56] = [
    0x01, 0, 0, 0, 0x38, 0, 0, 0, 0, 0, 0, 0, 0x01, 0, 0, 0, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03,
    0x02, 0x01, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x28, 0x27, 0x26, 0x25, 0x24, 0x23,
    0x22, 0x21, 0xe6, 0xa0, 0, 0, 0x2e, 0xfb, 0xff, 0xff, 0x64, 0, 0, 0, 0xe8, 0x03, 0, 0,
];

fn wire() -> PtimSampleV1 {
    PtimSampleV1 {
        sequence: 1,
        started_ns: 100,
        acquired_ns: 200,
        voltage_01mv: 41_190,
        current_01ma: -1234,
        r_fg_value: 100,
        car_tune_value: 1000,
    }
}

fn put32(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

fn reply() -> Vec<u8> {
    let mut header = Header::decode(&proto::ptim_request()).unwrap();
    header.fgd_data_len = 56;
    header.fgd_ret_data_len = 0;
    let mut bytes = header.encode().to_vec();
    bytes.extend(wire().encode());
    bytes
}

fn notification() -> Vec<u8> {
    let mut header = Header::decode(&proto::ptim_request()).unwrap();
    header.fgd_cmd = 10;
    header.fgd_subcmd = 7;
    header.fgd_subcmd_para1 = (-3i32) as u32;
    header.fgd_data_len = 4;
    header.fgd_ret_data_len = 0;
    let mut bytes = header.encode().to_vec();
    bytes.extend(0x8000_0021u32.to_le_bytes());
    bytes
}

fn datagram(payload: &[u8], sequence: u32) -> Vec<u8> {
    transport::encode_datagram(payload, sequence).unwrap()
}

fn pending(mock: MockIo) -> MockTransport {
    let mut transport = MockTransport::new(mock);
    transport
        .send_request(&proto::ptim_request(), 73, TIMEOUT)
        .unwrap();
    transport
}

fn receive(bytes: Vec<u8>) -> Result<Vec<u8>, TransportError> {
    let mut mock = MockIo::default();
    mock.push_kernel(bytes);
    pending(mock).receive_reply(73, TIMEOUT)
}

#[test]
fn kernel_request_golden() {
    let expected = [
        0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 56, 0, 0, 0, 0x4f,
        0x61, 0x1b, 0x78,
    ];
    assert_eq!(proto::ptim_request(), expected);
    assert_eq!(Header::decode(&expected).unwrap().encode(), expected);
    let packet = datagram(&expected, 73);
    assert_eq!(
        &packet[..16],
        &[48, 0, 0, 0, 0, 0, 0, 0, 73, 0, 0, 0, 0, 0, 0, 0]
    );
    assert_eq!(&packet[16..], &expected);
}

#[test]
fn header_every_word_has_kernel_offset() {
    let header = Header {
        nl_cmd: 0x01020304,
        fgd_cmd: 0x11121314,
        fgd_cmd_hash: 0x21222324,
        fgd_subcmd: 0x31323334,
        fgd_subcmd_para1: 0x41424344,
        fgd_data_len: 4,
        fgd_ret_data_len: 56,
        identity: proto::IDENTITY,
    };
    let expected = [
        4, 3, 2, 1, 0x14, 0x13, 0x12, 0x11, 0x24, 0x23, 0x22, 0x21, 0x34, 0x33, 0x32, 0x31, 0x44,
        0x43, 0x42, 0x41, 4, 0, 0, 0, 56, 0, 0, 0, 0x4f, 0x61, 0x1b, 0x78,
    ];
    assert_eq!(header.encode(), expected);
    assert_eq!(Header::decode(&expected), Ok(header));
}

#[test]
fn ptim_kernel_offsets_and_signed_golden() {
    let expected = PtimSampleV1 {
        sequence: 0x0102030405060708,
        started_ns: 0x1112131415161718,
        acquired_ns: 0x2122232425262728,
        ..wire()
    };
    assert_eq!(expected.encode(), GOLDEN_PTIM);
    assert_eq!(PtimSampleV1::decode(&GOLDEN_PTIM), Ok(expected));
    assert_eq!(proto::decode_ptim_reply(&reply()), Ok(wire()));
}

#[test]
fn header_rejects_all_short_lengths_and_trailing_data() {
    for length in 0..32 {
        assert!(matches!(
            Header::decode(&proto::ptim_request()[..length]),
            Err(proto::Error::Length { .. })
        ));
        assert!(matches!(
            proto::Message::decode(&proto::ptim_request()[..length]),
            Err(proto::Error::Length { .. })
        ));
    }
    let mut bytes = proto::ptim_request().to_vec();
    bytes.push(0);
    assert!(matches!(
        Header::decode(&bytes),
        Err(proto::Error::Length { .. })
    ));
    assert!(matches!(
        proto::Message::decode(&bytes),
        Err(proto::Error::Length { .. })
    ));
}

#[test]
fn header_rejects_wrong_identity_oversize_and_length_mismatch() {
    let mut bytes = proto::ptim_request();
    put32(&mut bytes, 28, 0);
    assert_eq!(Header::decode(&bytes), Err(proto::Error::Identity(0)));
    for offset in [20, 24] {
        let mut bytes = proto::ptim_request();
        put32(&mut bytes, offset, 9201);
        assert_eq!(Header::decode(&bytes), Err(proto::Error::Oversized));
        put32(&mut bytes, offset, u32::MAX);
        assert_eq!(Header::decode(&bytes), Err(proto::Error::Oversized));
    }
    let mut bytes = proto::ptim_request();
    put32(&mut bytes, 20, 1);
    assert!(matches!(
        proto::Message::decode(&bytes),
        Err(proto::Error::Length { .. })
    ));
}

#[test]
fn ptim_rejects_short_extra_version_and_size() {
    for length in 0..56 {
        assert!(matches!(
            PtimSampleV1::decode(&GOLDEN_PTIM[..length]),
            Err(proto::Error::Length { .. })
        ));
    }
    let mut extra = GOLDEN_PTIM.to_vec();
    extra.push(0);
    assert!(matches!(
        PtimSampleV1::decode(&extra),
        Err(proto::Error::Length { .. })
    ));
    let mut bytes = GOLDEN_PTIM;
    put32(&mut bytes, 0, 2);
    assert_eq!(PtimSampleV1::decode(&bytes), Err(proto::Error::Version(2)));
    for size in [0, 55, 57, u32::MAX] {
        let mut bytes = GOLDEN_PTIM;
        put32(&mut bytes, 4, size);
        assert!(matches!(
            PtimSampleV1::decode(&bytes),
            Err(proto::Error::Length { .. })
        ));
    }
}

#[test]
fn ptim_rejects_missing_or_unknown_valid_flags() {
    for flags in [0, 2, 3, u32::MAX] {
        let mut bytes = GOLDEN_PTIM;
        put32(&mut bytes, 12, flags);
        assert_eq!(
            PtimSampleV1::decode(&bytes),
            Err(proto::Error::Flags(flags))
        );
    }
}

#[test]
fn ptim_maps_failure_status_without_fabricating_sample() {
    for (status, expected) in [
        (-19, proto::KernelError::NoDevice),
        (-95, proto::KernelError::Unsupported),
        (-5, proto::KernelError::AcquisitionErrno(5)),
        (-110, proto::KernelError::AcquisitionErrno(110)),
        (-61, proto::KernelError::AcquisitionErrno(61)),
        (-4095, proto::KernelError::AcquisitionErrno(4095)),
    ] {
        let mut bytes = [0; 56];
        put32(&mut bytes, 0, 1);
        put32(&mut bytes, 4, 56);
        put32(&mut bytes, 8, status as u32);
        assert_eq!(
            PtimSampleV1::decode(&bytes),
            Err(proto::Error::Kernel(expected))
        );
        for offset in 12..56 {
            let mut malformed = bytes;
            malformed[offset] = 1;
            assert_eq!(
                PtimSampleV1::decode(&malformed),
                Err(proto::Error::FailurePayload)
            );
        }
    }
    for status in [1, i32::MAX, -4096, i32::MIN] {
        let mut bytes = GOLDEN_PTIM;
        put32(&mut bytes, 8, status as u32);
        assert_eq!(
            PtimSampleV1::decode(&bytes),
            Err(proto::Error::InvalidStatus(status))
        );
    }
}

#[test]
fn ptim_reply_rejects_commands_reserved_fields_and_payload_length() {
    for offset in [0, 4] {
        let mut bytes = reply();
        put32(&mut bytes, offset, 2);
        assert_eq!(proto::decode_ptim_reply(&bytes), Err(proto::Error::Command));
    }
    for offset in [8, 12, 16, 24] {
        let mut bytes = reply();
        put32(&mut bytes, offset, 1);
        assert_eq!(
            proto::decode_ptim_reply(&bytes),
            Err(proto::Error::ReservedFields)
        );
    }
    let mut bytes = reply();
    bytes.pop();
    assert!(matches!(
        proto::decode_ptim_reply(&bytes),
        Err(proto::Error::Length { .. })
    ));
    put32(&mut bytes, 20, 55);
    assert!(matches!(
        proto::decode_ptim_reply(&bytes),
        Err(proto::Error::Length { .. })
    ));
}

#[test]
fn notification_keeps_full_bitmask_and_signed_parameters() {
    assert_eq!(
        proto::decode_notification(&notification()),
        Ok(proto::Notification {
            flow_state: 0x8000_0021,
            command: 7,
            parameter: -3,
        })
    );
    for offset in [0, 4] {
        let mut bytes = notification();
        put32(&mut bytes, offset, 11);
        assert_eq!(
            proto::decode_notification(&bytes),
            Err(proto::Error::Command)
        );
    }
    for offset in [8, 24] {
        let mut bytes = notification();
        put32(&mut bytes, offset, 1);
        assert_eq!(
            proto::decode_notification(&bytes),
            Err(proto::Error::ReservedFields)
        );
    }
    let mut bytes = notification();
    bytes.pop();
    put32(&mut bytes, 20, 3);
    assert!(matches!(
        proto::decode_notification(&bytes),
        Err(proto::Error::Length { .. })
    ));
}

#[test]
fn transport_matches_reply_and_preserves_interleaved_notifications() {
    let mut mock = MockIo::default();
    mock.push_kernel(datagram(&notification(), 0));
    let mut packet = datagram(&reply(), 73);
    put32(&mut packet, 12, 42);
    mock.push_kernel(packet);
    let mut transport = pending(mock);
    assert_eq!(transport.receive_reply(73, TIMEOUT).unwrap(), reply());
    assert_eq!(
        transport.pop_notification(),
        proto::decode_notification(&notification()).ok()
    );
    assert_eq!(transport.pop_notification(), None);
    assert_eq!(
        transport.io().sent,
        vec![datagram(&proto::ptim_request(), 73)]
    );
}

#[test]
fn transport_retries_eintr_for_send_and_receive() {
    let mut mock = MockIo::default();
    mock.send_results
        .push_back(Err(io::ErrorKind::Interrupted.into()));
    mock.incoming
        .push_back(Err(io::ErrorKind::Interrupted.into()));
    mock.push_kernel(datagram(&reply(), 73));
    let mut transport = pending(mock);
    assert_eq!(transport.receive_reply(73, TIMEOUT).unwrap(), reply());
    assert_eq!(transport.io().sent.len(), 2);
}

#[test]
fn transport_rejects_short_reads_and_malformed_netlink_header() {
    for length in 0..16 {
        assert!(matches!(
            receive(vec![0; length]),
            Err(TransportError::ShortRead)
        ));
    }
    for length in [0, 15, 16, 103, 105] {
        let mut packet = datagram(&reply(), 73);
        put32(&mut packet, 0, length);
        assert!(matches!(
            receive(packet),
            Err(TransportError::NetlinkHeader)
        ));
    }
    for offset in 4..8 {
        let mut packet = datagram(&reply(), 73);
        packet[offset] = 1;
        assert!(matches!(
            receive(packet),
            Err(TransportError::NetlinkHeader)
        ));
    }
    assert!(matches!(
        receive(datagram(&[0; 31], 73)),
        Err(TransportError::Protocol(proto::Error::Length { .. }))
    ));
}

#[test]
fn transport_rejects_wrong_sequence_identity_command_and_sender() {
    assert!(matches!(
        receive(datagram(&reply(), 74)),
        Err(TransportError::Sequence {
            expected: 73,
            actual: 74
        })
    ));
    let mut bytes = reply();
    put32(&mut bytes, 28, 0);
    assert!(matches!(
        receive(datagram(&bytes, 73)),
        Err(TransportError::Protocol(proto::Error::Identity(0)))
    ));
    for offset in [0, 4] {
        let mut bytes = reply();
        put32(&mut bytes, offset, 99);
        assert!(matches!(
            receive(datagram(&bytes, 73)),
            Err(TransportError::Command)
        ));
    }
    for (port, groups) in [(1, 0), (0, 1)] {
        let packet = datagram(&reply(), 73);
        let received = transport::Received {
            length: packet.len(),
            sender_port_id: port,
            sender_groups: groups,
        };
        let mut mock = MockIo::default();
        mock.incoming.push_back(Ok((packet, received)));
        assert!(matches!(
            pending(mock).receive_reply(73, TIMEOUT),
            Err(TransportError::Sender)
        ));
    }
}

#[test]
fn transport_rejects_oversize_and_truncation() {
    assert!(matches!(
        transport::encode_datagram(&vec![0; transport::MAX_DATAGRAM_LEN], 1),
        Err(TransportError::Oversized)
    ));
    let mut packet = datagram(&reply(), 73);
    put32(&mut packet, 0, u32::MAX);
    assert!(matches!(receive(packet), Err(TransportError::Oversized)));
    assert!(matches!(
        receive(vec![0; transport::MAX_DATAGRAM_LEN + 1]),
        Err(TransportError::Oversized)
    ));
}

#[test]
fn transport_enforces_request_lifecycle() {
    let mut transport = MockTransport::new(MockIo::default());
    assert!(matches!(
        transport.receive_reply(73, TIMEOUT),
        Err(TransportError::NoPendingRequest)
    ));
    assert!(matches!(
        transport.send_request(&proto::ptim_request(), 0, TIMEOUT),
        Err(TransportError::ReservedSequence)
    ));
    assert!(matches!(
        transport.send_request(&[0; 31], 73, TIMEOUT),
        Err(TransportError::Protocol(_))
    ));
    transport
        .send_request(&proto::ptim_request(), 73, TIMEOUT)
        .unwrap();
    assert!(matches!(
        transport.send_request(&proto::ptim_request(), 74, TIMEOUT),
        Err(TransportError::Busy)
    ));
    assert!(matches!(
        transport.receive_reply(74, TIMEOUT),
        Err(TransportError::Sequence { .. })
    ));
    assert!(matches!(
        transport.receive_reply(73, TIMEOUT),
        Err(TransportError::Timeout)
    ));
    assert!(matches!(
        transport.receive_reply(73, TIMEOUT),
        Err(TransportError::NoPendingRequest)
    ));
}

#[test]
fn transport_reports_timeouts_short_writes_and_io_failures() {
    let mut transport = MockTransport::new(MockIo::default());
    assert!(matches!(
        transport.send_request(&proto::ptim_request(), 73, Duration::ZERO),
        Err(TransportError::Timeout)
    ));
    for kind in [io::ErrorKind::TimedOut, io::ErrorKind::PermissionDenied] {
        let mut mock = MockIo::default();
        mock.send_results.push_back(Err(kind.into()));
        let result = MockTransport::new(mock).send_request(&proto::ptim_request(), 73, TIMEOUT);
        assert!(matches!(
            (&result, kind),
            (Err(TransportError::Timeout), io::ErrorKind::TimedOut)
                | (Err(TransportError::Io(_)), io::ErrorKind::PermissionDenied)
        ));
        let mut mock = MockIo::default();
        mock.incoming.push_back(Err(kind.into()));
        let result = pending(mock).receive_reply(73, TIMEOUT);
        assert!(matches!(
            (&result, kind),
            (Err(TransportError::Timeout), io::ErrorKind::TimedOut)
                | (Err(TransportError::Io(_)), io::ErrorKind::PermissionDenied)
        ));
    }
    let mut mock = MockIo::default();
    mock.send_results.push_back(Ok(47));
    assert!(matches!(
        MockTransport::new(mock).send_request(&proto::ptim_request(), 73, TIMEOUT),
        Err(TransportError::ShortWrite)
    ));
    assert!(matches!(
        pending(MockIo::default()).receive_reply(73, Duration::ZERO),
        Err(TransportError::Timeout)
    ));
}

#[test]
fn transport_bounds_notification_queue_and_rejects_zero_sequence_reply() {
    let mut mock = MockIo::default();
    for _ in 0..=transport::MAX_QUEUED_NOTIFICATIONS {
        mock.push_kernel(datagram(&notification(), 0));
    }
    assert!(matches!(
        pending(mock).receive_reply(73, TIMEOUT),
        Err(TransportError::NotificationOverflow)
    ));
    assert!(matches!(
        receive(datagram(&reply(), 0)),
        Err(TransportError::Protocol(proto::Error::Command))
    ));
}

#[test]
fn sample_rejects_incoherent_fields() {
    assert_eq!(
        sample::Sample::try_from(PtimSampleV1 {
            sequence: 0,
            ..wire()
        }),
        Err(sample::Error::Sequence)
    );
    for acquired_ns in [0, 99, 100] {
        assert_eq!(
            sample::Sample::try_from(PtimSampleV1 {
                acquired_ns,
                ..wire()
            }),
            Err(sample::Error::Timestamps)
        );
    }
    for voltage_01mv in [i32::MIN, 0, 14_999, 55_001, i32::MAX] {
        assert_eq!(
            sample::Sample::try_from(PtimSampleV1 {
                voltage_01mv,
                ..wire()
            }),
            Err(sample::Error::Voltage)
        );
    }
    for current_01ma in [i32::MIN, -300_001, 300_001, i32::MAX] {
        assert_eq!(
            sample::Sample::try_from(PtimSampleV1 {
                current_01ma,
                ..wire()
            }),
            Err(sample::Error::Current)
        );
    }
    for value in [i32::MIN, 0, 10_001, i32::MAX] {
        assert_eq!(
            sample::Sample::try_from(PtimSampleV1 {
                r_fg_value: value,
                ..wire()
            }),
            Err(sample::Error::Calibration)
        );
        assert_eq!(
            sample::Sample::try_from(PtimSampleV1 {
                car_tune_value: value,
                ..wire()
            }),
            Err(sample::Error::Calibration)
        );
    }
}

#[test]
fn sample_accepts_range_edges_and_both_current_signs() {
    for voltage_01mv in [15_000, 55_000] {
        for current_01ma in [-300_000, 0, 300_000] {
            assert!(sample::Sample::try_from(PtimSampleV1 {
                voltage_01mv,
                current_01ma,
                ..wire()
            })
            .is_ok());
        }
    }
}

#[test]
fn freshness_uninitialized_valid_boundary_stale_and_future() {
    let mut cache = sample::SampleCache::new(Duration::from_nanos(50));
    assert_eq!(cache.freshness(0), Ok(sample::Freshness::Uninitialized));
    let sample = cache.update(wire()).unwrap();
    assert_eq!(sample.wire(), wire());
    assert_eq!(cache.freshness(200), Ok(sample::Freshness::Valid(sample)));
    assert_eq!(cache.freshness(250), Ok(sample::Freshness::Valid(sample)));
    assert_eq!(cache.freshness(251), Ok(sample::Freshness::Stale(sample)));
    assert_eq!(cache.freshness(199), Err(sample::Error::FutureSample));
    let mut zero = sample::SampleCache::new(Duration::ZERO);
    let sample = zero.update(wire()).unwrap();
    assert_eq!(zero.freshness(200), Ok(sample::Freshness::Valid(sample)));
    assert_eq!(zero.freshness(201), Ok(sample::Freshness::Stale(sample)));
}

#[test]
fn sample_cache_retains_last_good_after_errors_and_rejects_replays() {
    let mut cache = sample::SampleCache::new(TIMEOUT);
    let first = cache.update(wire()).unwrap();
    assert_eq!(cache.update(wire()), Err(sample::Error::Sequence));
    assert_eq!(
        cache.update(PtimSampleV1 {
            sequence: 0,
            ..wire()
        }),
        Err(sample::Error::Sequence)
    );
    assert_eq!(
        cache.update(PtimSampleV1 {
            sequence: 2,
            started_ns: 199,
            acquired_ns: 300,
            ..wire()
        }),
        Err(sample::Error::Timestamps)
    );
    assert_eq!(
        cache.update(PtimSampleV1 {
            sequence: 2,
            voltage_01mv: 0,
            ..wire()
        }),
        Err(sample::Error::Voltage)
    );
    assert_eq!(cache.freshness(200), Ok(sample::Freshness::Valid(first)));
    let second = cache
        .update(PtimSampleV1 {
            sequence: 4,
            started_ns: 200,
            acquired_ns: 300,
            ..wire()
        })
        .unwrap();
    assert_eq!(cache.freshness(300), Ok(sample::Freshness::Valid(second)));
}

fn learned() -> store::LearnedState {
    store::LearnedState {
        qmax_uah: store::MicroampHours::new(3_500_000).unwrap(),
        aging_factor_permille: store::Permille::new(950).unwrap(),
        cycle_count: store::CycleCount(123),
        last_soc_permille: store::Permille::new(730).unwrap(),
        last_soc_sample_sequence: store::SampleSequence::new(42).unwrap(),
    }
}

struct TestDirectory(PathBuf);

impl TestDirectory {
    fn new() -> Self {
        static NEXT: AtomicU64 = AtomicU64::new(0);
        let path = option_env!("CARGO_MANIFEST_DIR")
            .map(PathBuf::from)
            .unwrap_or_else(std::env::temp_dir);
        let path = path.join("target").join(format!(
            "store-test-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir_all(&path).unwrap();
        Self(path)
    }

    fn file(&self) -> PathBuf {
        self.0.join("learned.bin")
    }
}

impl Drop for TestDirectory {
    fn drop(&mut self) {
        fs::remove_dir_all(&self.0).unwrap();
    }
}

#[test]
fn store_crc32_standard_check_vector() {
    assert_eq!(store::crc32(b"123456789"), 0xcbf4_3926);
    assert_eq!(store::crc32(b""), 0);
}

#[test]
fn store_roundtrip_and_explicit_units() {
    let state = learned();
    assert_eq!(store::LearnedState::decode(&state.encode()).unwrap(), state);
    assert_eq!(state.qmax_uah.get(), 3_500_000);
    assert_eq!(state.aging_factor_permille.get(), 950);
    assert_eq!(state.last_soc_permille.get(), 730);
    assert_eq!(state.last_soc_sample_sequence.get(), 42);
    assert_eq!(&state.encode()[..16], b"M1GAUGE\0\x01\0\0\0\x18\0\0\0");
}

#[test]
fn store_rejects_every_torn_length_trailing_bytes_and_header_corruption() {
    let bytes = learned().encode();
    for length in 0..store::RECORD_LEN {
        assert!(matches!(
            store::LearnedState::decode(&bytes[..length]),
            Err(store::Error::Length)
        ));
    }
    let mut extra = bytes.to_vec();
    extra.push(0);
    assert!(matches!(
        store::LearnedState::decode(&extra),
        Err(store::Error::Length)
    ));
    let mut invalid = bytes;
    invalid[0] ^= 1;
    assert!(matches!(
        store::LearnedState::decode(&invalid),
        Err(store::Error::Magic)
    ));
    let mut invalid = bytes;
    put32(&mut invalid, 8, 2);
    assert!(matches!(
        store::LearnedState::decode(&invalid),
        Err(store::Error::Version(2))
    ));
    let mut invalid = bytes;
    put32(&mut invalid, 12, 23);
    assert!(matches!(
        store::LearnedState::decode(&invalid),
        Err(store::Error::Length)
    ));
}

#[test]
fn store_detects_every_single_bit_corruption() {
    for index in 0..store::RECORD_LEN {
        for bit in 0..8 {
            let mut bytes = learned().encode();
            bytes[index] ^= 1 << bit;
            assert!(store::LearnedState::decode(&bytes).is_err());
        }
    }
    let mut bytes = learned().encode();
    bytes[16] ^= 1;
    assert!(matches!(
        store::LearnedState::decode(&bytes),
        Err(store::Error::Crc)
    ));
}

#[test]
fn store_rejects_semantically_invalid_values_even_with_valid_crc() {
    for (offset, value, error) in [
        (16, 0, store::ValueError::ZeroQmax),
        (20, 1001, store::ValueError::Permille),
        (28, u32::MAX, store::ValueError::Permille),
        (32, 0, store::ValueError::ZeroSampleSequence),
    ] {
        let mut bytes = learned().encode();
        put32(&mut bytes, offset, value);
        let crc = store::crc32(&bytes[..40]);
        put32(&mut bytes, 40, crc);
        assert!(
            matches!(store::LearnedState::decode(&bytes), Err(store::Error::Value(actual)) if actual == error)
        );
    }
    assert_eq!(
        store::MicroampHours::new(0),
        Err(store::ValueError::ZeroQmax)
    );
    assert_eq!(store::Permille::new(1001), Err(store::ValueError::Permille));
    assert_eq!(
        store::SampleSequence::new(0),
        Err(store::ValueError::ZeroSampleSequence)
    );
    assert!(store::Permille::new(0).is_ok());
    assert!(store::Permille::new(1000).is_ok());
}

#[test]
fn store_atomic_replace_roundtrip_leaves_no_temp() {
    let directory = TestDirectory::new();
    let path = directory.file();
    store::write_atomic(&path, learned()).unwrap();
    assert_eq!(store::read(&path).unwrap(), learned());
    let next = store::LearnedState {
        cycle_count: store::CycleCount(124),
        ..learned()
    };
    store::write_atomic(&path, next).unwrap();
    assert_eq!(store::read(&path).unwrap(), next);
    assert_eq!(fs::read_dir(&directory.0).unwrap().count(), 1);
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        assert_eq!(
            fs::metadata(&path).unwrap().permissions().mode() & 0o777,
            0o600
        );
    }
}

#[test]
fn store_failed_write_keeps_previous_good_file_and_removes_temp() {
    let directory = TestDirectory::new();
    let path = directory.file();
    store::write_atomic(&path, learned()).unwrap();
    let next = store::LearnedState {
        cycle_count: store::CycleCount(124),
        ..learned()
    };
    let result = store::write_atomic_inner(&path, &next.encode(), || {
        Err(io::ErrorKind::Interrupted.into())
    });
    assert!(matches!(result, Err(store::Error::Io(_))));
    assert_eq!(store::read(&path).unwrap(), learned());
    assert_eq!(fs::read_dir(&directory.0).unwrap().count(), 1);
}

#[test]
fn store_strict_read_never_replaces_good_state_with_corruption() {
    let directory = TestDirectory::new();
    let path = directory.file();
    store::write_atomic(&path, learned()).unwrap();
    let last_good = store::read(&path).unwrap();
    fs::write(&path, &learned().encode()[..20]).unwrap();
    assert!(matches!(store::read(&path), Err(store::Error::Length)));
    assert_eq!(last_good, learned());
    assert_eq!(fs::read(&path).unwrap().len(), 20);
    fs::write(&path, vec![0; 100_000]).unwrap();
    assert!(matches!(store::read(&path), Err(store::Error::Length)));
}

#[test]
fn store_reports_missing_parent_read_and_rename_failures() {
    let directory = TestDirectory::new();
    assert!(matches!(
        store::read(&directory.file()),
        Err(store::Error::Io(_))
    ));
    assert!(matches!(
        store::write_atomic(&directory.0.join("missing/file"), learned()),
        Err(store::Error::Io(_))
    ));
    assert!(matches!(
        store::write_atomic(std::path::Path::new("/"), learned()),
        Err(store::Error::Io(_))
    ));
    fs::create_dir(directory.file()).unwrap();
    assert!(matches!(
        store::write_atomic(&directory.file(), learned()),
        Err(store::Error::Io(_))
    ));
    assert_eq!(fs::read_dir(&directory.0).unwrap().count(), 1);
}

struct FakeClock(u64);

impl crate::shadow::Clock for FakeClock {
    fn now_ns(&self) -> io::Result<u64> {
        Ok(self.0)
    }
}

fn gm(counter: i64) -> crate::shadow::Gm30 {
    use crate::shadow::{Gm30, Value};
    Gm30 {
        capacity: Value::Ok(70),
        voltage: Value::Ok(4_000_000),
        current: Value::Ok(-100_000),
        charge_counter: Value::Ok(counter),
        status: Value::Ok("Discharging".into()),
        temp: Value::Ok(250),
    }
}

fn shadow_reply(sequence: u64, acquired_ns: u64, current: i32) -> Vec<u8> {
    let mut bytes = reply();
    bytes[32..].copy_from_slice(
        &PtimSampleV1 {
            sequence,
            acquired_ns,
            started_ns: acquired_ns - 1,
            current_01ma: current,
            ..wire()
        }
        .encode(),
    );
    bytes
}

#[test]
fn shadow_integrator_trapezoid_units_sign_and_interruption() {
    let mut integrator = crate::shadow::Integrator::default();
    let sample = |sequence, acquired_ns, current_01ma| {
        sample::Sample::try_from(PtimSampleV1 {
            sequence,
            acquired_ns,
            started_ns: acquired_ns - 1,
            current_01ma,
            ..wire()
        })
        .unwrap()
    };
    integrator.push(sample(1, 1_000_000_000, 1000));
    integrator.push(sample(2, 3_601_000_000_000, 3000));
    assert!((integrator.charge_in_uah + 200_000.0).abs() < 1e-9);
    integrator.push(sample(3, 7_201_000_000_000, -3000));
    assert!((integrator.charge_in_uah + 200_000.0).abs() < 1e-9);
    integrator.interrupt();
    integrator.push(sample(4, 10_801_000_000_000, -1000));
    assert!((integrator.charge_in_uah + 200_000.0).abs() < 1e-9);
    integrator.push(sample(5, 14_401_000_000_000, -1000));
    assert!((integrator.charge_in_uah + 100_000.0).abs() < 1e-9);
}

#[test]
fn shadow_charge_in_and_counter_track_both_directions() {
    for current in [-3600, 3600] {
        let mut mock = MockIo::default();
        mock.push_kernel(datagram(&shadow_reply(1, 1_000_000_000, current), 1));
        mock.push_kernel(datagram(&shadow_reply(2, 11_000_000_000, current), 2));
        let mut transport = MockTransport::new(mock);
        let mut shadow = crate::shadow::Shadow::new(10);
        let delta = -i64::from(current) * 1000 / 3600;
        for (time, counter) in [(1, 5000), (11, 5000 + delta)] {
            shadow
                .poll(
                    &mut transport,
                    &FakeClock(time * 1_000_000_000),
                    gm(counter),
                )
                .unwrap();
        }
        assert_eq!(shadow.rows[1].charge_in_uah, delta as f64);
        assert_eq!(shadow.rows[1].drift_uah, Some(0.0));
        assert_eq!(shadow.rows[1].current, Some(current));
        if current < 0 {
            assert!(shadow.rows[1].charge_in_uah > shadow.rows[0].charge_in_uah);
        }
    }
}

#[test]
fn shadow_mock_transport_injected_clock_drift_gap_and_exact_command() {
    use crate::shadow::Shadow;
    let mut mock = MockIo::default();
    mock.push_kernel(datagram(&shadow_reply(1, 1_000_000_000, -3600), 1));
    mock.push_kernel(datagram(&shadow_reply(2, 11_000_000_000, -3600), 2));
    mock.push_kernel(datagram(&shadow_reply(3, 41_000_000_000, -3600), 3));
    let mut transport = MockTransport::new(mock);
    let mut shadow = Shadow::new(10);
    for (time, counter) in [(1, 1000), (11, 1900), (41, 4900)] {
        assert_eq!(
            shadow
                .poll(
                    &mut transport,
                    &FakeClock(time * 1_000_000_000),
                    gm(counter)
                )
                .unwrap(),
            Duration::from_secs(10)
        );
    }
    assert_eq!(shadow.rows[0].charge_in_uah, 0.0);
    assert_eq!(shadow.rows[1].charge_in_uah, 1000.0);
    assert_eq!(shadow.rows[1].drift_uah, Some(100.0));
    assert!(!shadow.rows[1].gap);
    assert!(shadow.rows[2].gap);
    assert_eq!(shadow.rows[2].charge_in_uah, 1000.0);
    assert_eq!(shadow.rows[2].drift_uah, Some(0.0));
    for (index, bytes) in transport.io().sent.iter().enumerate() {
        assert_eq!(*bytes, datagram(&proto::ptim_request(), index as u32 + 1));
    }
}

#[test]
fn shadow_invalid_future_stale_replay_and_transport_errors_do_not_integrate() {
    let mut mock = MockIo::default();
    for (request, sample_seq, acquired, current) in [
        (1, 1, 1, -3600),
        (2, 2, 11, 300_001),
        (3, 3, 21, -3600),
        (4, 4, 100, -3600),
        (5, 3, 21, -3600),
        (6, 6, 1, -3600),
    ] {
        mock.push_kernel(datagram(
            &shadow_reply(sample_seq, acquired * 1_000_000_000, current),
            request,
        ));
    }
    let mut transport = MockTransport::new(mock);
    let mut shadow = crate::shadow::Shadow::new(10);
    for seconds in [1, 11, 21, 31, 41, 51, 61] {
        shadow
            .poll(
                &mut transport,
                &FakeClock(seconds * 1_000_000_000),
                gm(1000),
            )
            .unwrap();
    }
    assert!(shadow.rows.iter().all(|row| row.charge_in_uah == 0.0));
    assert_eq!(shadow.rows[1].status, "sample:Current");
    assert_eq!(shadow.rows[3].status, "sample:FutureSample");
    assert_eq!(shadow.rows[4].status, "sample:Sequence");
    assert_eq!(shadow.rows[5].status, "sample:Stale");
    assert_eq!(shadow.rows[6].status, "transport:Timeout");
}

#[test]
fn shadow_error_restarts_drift_window() {
    let mut mock = MockIo::default();
    mock.push_kernel(datagram(&shadow_reply(1, 1_000_000_000, -3600), 1));
    mock.push_kernel(datagram(&shadow_reply(2, 11_000_000_000, -3600), 2));
    mock.push_kernel(datagram(&shadow_reply(3, 21_000_000_000, -3600), 99));
    mock.push_kernel(datagram(&shadow_reply(3, 31_000_000_000, -3600), 4));
    mock.push_kernel(datagram(&shadow_reply(4, 41_000_000_000, -3600), 5));
    let mut transport = MockTransport::new(mock);
    let mut shadow = crate::shadow::Shadow::new(10);
    for (time, counter) in [(1, 1000), (11, 1900), (21, 2900), (31, 3900), (41, 4900)] {
        shadow
            .poll(
                &mut transport,
                &FakeClock(time * 1_000_000_000),
                gm(counter),
            )
            .unwrap();
    }
    assert_eq!(shadow.rows[1].drift_uah, Some(100.0));
    assert!(shadow.rows[2].status.starts_with("transport:"));
    assert_eq!(shadow.rows[2].drift_uah, None);
    assert_eq!(shadow.rows[3].drift_uah, Some(0.0));
    assert_eq!(shadow.rows[4].drift_uah, Some(0.0));
}

#[test]
fn shadow_backoff_caps_resets_and_period_is_bounded() {
    use crate::shadow::{period_seconds, Backoff};
    for (text, result) in [
        ("", 10),
        ("bad", 10),
        ("0", 1),
        ("1", 1),
        ("301", 300),
        ("300", 300),
        (" 20 ", 20),
    ] {
        assert_eq!(period_seconds(text), result);
    }
    let mut backoff = Backoff::new(10);
    for seconds in [20, 40, 80, 160, 300] {
        assert_eq!(backoff.record(false), Duration::from_secs(seconds));
    }
    for _ in 0..10_000 {
        assert_eq!(backoff.record(false), Duration::from_secs(300));
    }
    assert_eq!(backoff.record(true), Duration::from_secs(10));
    assert_eq!(backoff.record(false), Duration::from_secs(20));
}

#[test]
fn shadow_log_format_units_error_values_and_gap() {
    let mut gm = gm(1000);
    gm.status = crate::shadow::Value::Err("errno:2,\n".into());
    let row = crate::shadow::Row {
        acquired_ns: 123,
        sequence: 4,
        status: "ok".into(),
        voltage: Some(40000),
        current: Some(-1000),
        charge_in_uah: -1.5,
        gm,
        drift_uah: Some(0.25),
        gap: true,
    };
    let fields: Vec<_> = crate::shadow::HEADER.trim_end().split(',').collect();
    assert_eq!(fields[4], "ptim_discharge_01ma");
    assert_eq!(fields[5], "charge_in_uah");
    let line = row.line();
    assert_eq!(line, "123,4,ok,40000,-1000,-1.500000,70,4000000,-100000,1000,err:errno:2__,250,0.250000,gap_gt_2_periods:sampling_paused_suspend_or_delay\n");
    assert_eq!(
        line.trim_end().split(',').count(),
        crate::shadow::HEADER.trim_end().split(',').count()
    );
}

#[test]
fn shadow_rotation_replaces_archive_keeps_two_files_within_run() {
    let directory = TestDirectory::new();
    let limit = crate::shadow::HEADER.len() as u64 + 4;
    let log = crate::shadow::Log::new(&directory.0, limit).unwrap();
    log.append_line("one\n").unwrap();
    log.append_line("two\n").unwrap();
    assert_eq!(
        fs::read_to_string(directory.0.join("shadow.csv.1")).unwrap(),
        format!("{}one\n", crate::shadow::HEADER)
    );
    log.append_line("tri\n").unwrap();
    assert_eq!(fs::read_dir(&directory.0).unwrap().count(), 2);
    assert_eq!(
        fs::read_to_string(directory.0.join("shadow.csv.1")).unwrap(),
        format!("{}two\n", crate::shadow::HEADER)
    );
    assert_eq!(
        fs::read_to_string(directory.0.join("shadow.csv")).unwrap(),
        format!("{}tri\n", crate::shadow::HEADER)
    );
    assert!(log.append_line("oversized\n").is_err());
    assert!(fs::read_dir(&directory.0).unwrap().all(|file| file
        .unwrap()
        .metadata()
        .unwrap()
        .len()
        <= limit));
}

#[test]
fn shadow_log_start_archives_previous_run_and_preserves_it_during_size_rotation() {
    use crate::shadow::{Log, HEADER};
    let directory = TestDirectory::new();
    let limit = HEADER.len() as u64 + 4;
    let log = Log::new(&directory.0, limit).unwrap();
    log.append_line("one\n").unwrap();
    log.append_line("two\n").unwrap();
    let log = Log::new(&directory.0, limit).unwrap();
    assert!(!directory.0.join("shadow.csv").exists());
    assert!(!directory.0.join("shadow.csv.1").exists());
    log.append_line("tri\n").unwrap();
    log.append_line("new\n").unwrap();
    for (name, row) in [
        ("shadow.csv", "new\n"),
        ("shadow.csv.1", "tri\n"),
        ("shadow.csv.previous", "two\n"),
        ("shadow.csv.previous.1", "one\n"),
    ] {
        assert_eq!(
            fs::read_to_string(directory.0.join(name)).unwrap(),
            format!("{HEADER}{row}")
        );
    }
    assert_eq!(fs::read_dir(&directory.0).unwrap().count(), 4);
    let log = Log::new(&directory.0, limit).unwrap();
    log.append_line("end\n").unwrap();
    assert_eq!(fs::read_dir(&directory.0).unwrap().count(), 3);
    let log = Log::new(&directory.0, limit).unwrap();
    log.append_line("fin\n").unwrap();
    assert_eq!(fs::read_dir(&directory.0).unwrap().count(), 2);
    assert_eq!(
        fs::read_to_string(directory.0.join("shadow.csv.previous")).unwrap(),
        format!("{HEADER}end\n")
    );
}

#[test]
fn shadow_log_start_drops_older_run_when_only_size_rotated_part_remains() {
    use crate::shadow::{Log, LOG_LIMIT};
    let directory = TestDirectory::new();
    fs::write(directory.0.join("shadow.csv.previous"), "older\n").unwrap();
    fs::write(directory.0.join("shadow.csv.1"), "last\n").unwrap();
    Log::new(&directory.0, LOG_LIMIT).unwrap();
    assert!(!directory.0.join("shadow.csv.previous").exists());
    assert!(!directory.0.join("shadow.csv.1").exists());
    assert_eq!(
        fs::read_to_string(directory.0.join("shadow.csv.previous.1")).unwrap(),
        "last\n"
    );
}

#[test]
fn shadow_log_start_confines_nuls_and_torn_line_to_previous_run() {
    use crate::shadow::{Log, HEADER, LOG_LIMIT};
    let directory = TestDirectory::new();
    let torn = format!("{HEADER}old\n\0\0\0partial");
    fs::write(directory.0.join("shadow.csv"), torn.as_bytes()).unwrap();
    let log = Log::new(&directory.0, LOG_LIMIT).unwrap();
    log.append_line("new\n").unwrap();
    assert_eq!(
        fs::read(directory.0.join("shadow.csv.previous")).unwrap(),
        torn.as_bytes()
    );
    assert_eq!(
        fs::read_to_string(directory.0.join("shadow.csv")).unwrap(),
        format!("{HEADER}new\n")
    );
}

#[test]
fn shadow_log_writes_header_once_per_file_and_keeps_private_mode() {
    use crate::shadow::{Log, HEADER};
    let directory = TestDirectory::new();
    let log = Log::new(&directory.0, HEADER.len() as u64 + 8).unwrap();
    for line in ["one\n", "two\n", "tri\n", "end\n"] {
        log.append_line(line).unwrap();
    }
    for (name, rows) in [("shadow.csv.1", "one\ntwo\n"), ("shadow.csv", "tri\nend\n")] {
        let path = directory.0.join(name);
        assert_eq!(
            fs::read_to_string(&path).unwrap(),
            format!("{HEADER}{rows}")
        );
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            assert_eq!(
                fs::metadata(path).unwrap().permissions().mode() & 0o777,
                0o600
            );
        }
    }
}

#[test]
fn shadow_sysfs_missing_errno_invalid_and_bounded_reads() {
    use crate::shadow::{Gm30, Value};
    let directory = TestDirectory::new();
    fs::write(directory.0.join("capacity"), "70\n").unwrap();
    fs::write(directory.0.join("current_now"), "bad\n").unwrap();
    fs::write(directory.0.join("status"), "Discharging\n").unwrap();
    fs::write(directory.0.join("temp"), vec![b'1'; 300]).unwrap();
    let gm = Gm30::read(&directory.0);
    assert_eq!(gm.capacity, Value::Ok(70));
    assert_eq!(gm.current, Value::Err("invalid".into()));
    assert_eq!(gm.voltage, Value::Err("errno:2".into()));
    assert_eq!(gm.status, Value::Ok("Discharging".into()));
    assert_eq!(gm.temp, Value::Err("oversized".into()));
}

#[test]
fn shadow_ring_is_bounded_even_during_transport_failure() {
    let mut shadow = crate::shadow::Shadow::new(1);
    let mut transport = MockTransport::new(MockIo::default());
    for seconds in 0..8650 {
        shadow
            .poll(
                &mut transport,
                &FakeClock(seconds * 1_000_000_000),
                gm(1000),
            )
            .unwrap();
    }
    assert_eq!(shadow.rows.len(), crate::shadow::RING_CAPACITY);
    assert_eq!(shadow.rows.front().unwrap().acquired_ns, 10_000_000_000);
}

#[test]
fn shadow_store_unknown_learning_roundtrip_crc_and_atomic_replace() {
    let directory = TestDirectory::new();
    let state = store::ShadowState {
        charge_in_uah: -123.25,
        acquired_ns: 1_000_000_000,
        sequence: 42,
        ..store::ShadowState::default()
    };
    let bytes = state.encode().unwrap();
    assert_eq!(store::ShadowState::decode(&bytes).unwrap(), state);
    assert_eq!(&bytes[16..32], &[0; 16]);
    assert!(
        state.qmax_uah.is_none()
            && state.aging_factor_permille.is_none()
            && state.cycle_count.is_none()
    );
    for index in 0..bytes.len() {
        let mut invalid = bytes;
        invalid[index] ^= 1;
        assert!(store::ShadowState::decode(&invalid).is_err());
    }
    assert_eq!(u32::from_le_bytes(bytes[8..12].try_into().unwrap()), 3);
    let mut previous = bytes;
    previous[8..12].copy_from_slice(&2u32.to_le_bytes());
    let crc = store::crc32(&previous[..64]);
    previous[64..].copy_from_slice(&crc.to_le_bytes());
    assert!(matches!(
        store::ShadowState::decode(&previous),
        Err(store::Error::Version(2))
    ));
    store::write_shadow_atomic(&directory.file(), state).unwrap();
    assert_eq!(
        store::ShadowState::decode(&fs::read(directory.file()).unwrap()).unwrap(),
        state
    );
    assert_eq!(fs::read_dir(&directory.0).unwrap().count(), 1);
}
