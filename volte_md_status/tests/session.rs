// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0
#[path = "../protocol.rs"]
mod protocol;

use protocol::{Device, EVENT_SIZE, FREE_RESET, HOLD_RESET};
use std::collections::VecDeque;
use std::io::{self, Read};

enum Input {
    Bytes(Vec<u8>),
    Error(io::ErrorKind),
}

#[derive(Default)]
struct Mock {
    input: VecDeque<Input>,
    locks: VecDeque<io::Result<()>>,
    requests: Vec<u64>,
    published: Vec<(i32, &'static str)>,
    faults: Vec<String>,
    reads: usize,
}

impl Read for Mock {
    fn read(&mut self, target: &mut [u8]) -> io::Result<usize> {
        assert_eq!(target.len(), EVENT_SIZE);
        self.reads += 1;
        match self.input.pop_front().expect("scripted read") {
            Input::Bytes(bytes) => {
                target[..bytes.len()].copy_from_slice(&bytes);
                Ok(bytes.len())
            }
            Input::Error(kind) => Err(kind.into()),
        }
    }
}

impl Device for Mock {
    fn reset_lock(&mut self, request: u64) -> io::Result<()> {
        self.requests.push(request);
        self.locks.pop_front().unwrap_or(Ok(()))
    }

    fn publish(&mut self, event: i32, value: &'static str) {
        self.published.push((event, value));
    }

    fn fault(&mut self, operation: &str, _count: i64, _error: i32) {
        self.faults.push(operation.into());
    }
}

fn event(kind: i32) -> Input {
    let mut bytes = vec![0xa5; EVENT_SIZE];
    bytes[20..24].copy_from_slice(&kind.to_le_bytes());
    Input::Bytes(bytes)
}

#[test]
fn every_short_read_is_terminal_without_publication() {
    for size in 0..EVENT_SIZE {
        let mut device = Mock::default();
        device.input.push_back(Input::Bytes(vec![0; size]));
        device.input.push_back(event(10));
        protocol::run_session(&mut device);
        assert!(device.published.is_empty(), "size={size}");
        assert_eq!(device.reads, 1);
        assert_eq!(device.input.len(), 1);
        assert_eq!(device.requests, [HOLD_RESET, FREE_RESET]);
        assert_eq!(device.faults, ["status read"]);
    }
}

#[test]
fn interrupted_reads_retry_without_republishing() {
    let mut device = Mock::default();
    device.input.extend([
        Input::Error(io::ErrorKind::Interrupted),
        event(10),
        Input::Error(io::ErrorKind::Interrupted),
        event(12),
        Input::Bytes(vec![]),
    ]);
    protocol::run_session(&mut device);
    assert_eq!(device.published, [(10, "ready"), (12, "stop")]);
    assert_eq!(device.requests, [HOLD_RESET, FREE_RESET]);
}

#[test]
fn lock_interrupts_retry_and_terminal_errors_do_not_read() {
    let mut device = Mock::default();
    device.locks.extend([
        Err(io::ErrorKind::Interrupted.into()),
        Err(io::ErrorKind::PermissionDenied.into()),
    ]);
    protocol::run_session(&mut device);
    assert_eq!(device.requests, [HOLD_RESET, HOLD_RESET]);
    assert_eq!(device.reads, 0);
    assert_eq!(device.faults, ["reset lock acquire"]);
}

#[test]
fn unlock_interrupts_retry_and_failed_unlock_returns_to_owner() {
    let mut device = Mock::default();
    device.locks.extend([
        Ok(()),
        Err(io::ErrorKind::Interrupted.into()),
        Err(io::ErrorKind::BrokenPipe.into()),
    ]);
    device.input.push_back(Input::Bytes(vec![]));
    protocol::run_session(&mut device);
    assert_eq!(device.requests, [HOLD_RESET, FREE_RESET, FREE_RESET]);
    assert_eq!(device.faults, ["status read", "reset lock release"]);
}

#[test]
fn persistent_backend_errors_release_each_session_without_events() {
    for kind in [
        io::ErrorKind::WouldBlock,
        io::ErrorKind::NotFound,
        io::ErrorKind::BrokenPipe,
    ] {
        let mut device = Mock::default();
        for _ in 0..20 {
            device.input.push_back(Input::Error(kind));
            protocol::run_session(&mut device);
        }
        assert_eq!(device.reads, 20);
        assert_eq!(device.requests, [HOLD_RESET, FREE_RESET].repeat(20));
        assert!(device.published.is_empty());
    }
}

#[test]
fn linux_native_wire_layout_ignores_non_event_fields() {
    let names = [
        "invalid",
        "reset",
        "assert_req",
        "stop_req",
        "start_req",
        "flightmode",
        "flightmode",
        "flightmode",
        "flightmode",
        "hs1",
        "ready",
        "exception",
        "stop",
        "",
        "",
        "",
    ];
    for (kind, name) in names.iter().enumerate() {
        let Input::Bytes(bytes) = event(kind as i32) else {
            unreachable!()
        };
        let bytes: [u8; EVENT_SIZE] = bytes.try_into().unwrap();
        assert_eq!(protocol::status(protocol::event_type(&bytes)), *name);
    }
    for kind in [i32::MIN, -1, 16, 256, i32::MAX] {
        assert_eq!(protocol::status(kind), "invalid");
    }
    assert_eq!(HOLD_RESET, 0x4200);
    assert_eq!(FREE_RESET, 0x4201);
}
