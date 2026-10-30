// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0
use std::io::{self, Read};

pub const EVENT_SIZE: usize = 56;
pub const HOLD_RESET: u64 = 0x4200;
pub const FREE_RESET: u64 = 0x4201;

pub fn event_type(bytes: &[u8; EVENT_SIZE]) -> i32 {
    i32::from_le_bytes([bytes[20], bytes[21], bytes[22], bytes[23]])
}

pub fn status(event: i32) -> &'static str {
    match event {
        0 => "invalid",
        1 => "reset",
        2 => "assert_req",
        3 => "stop_req",
        4 => "start_req",
        5..=8 => "flightmode",
        9 => "hs1",
        10 => "ready",
        11 => "exception",
        12 => "stop",
        13..=15 => "",
        _ => "invalid",
    }
}

pub trait Device: Read {
    fn reset_lock(&mut self, request: u64) -> io::Result<()>;
    fn publish(&mut self, event: i32, value: &'static str);
    fn fault(&mut self, operation: &str, count: i64, error: i32);
}

fn reset_lock(device: &mut impl Device, request: u64) -> io::Result<()> {
    loop {
        match device.reset_lock(request) {
            Err(error) if error.kind() == io::ErrorKind::Interrupted => continue,
            result => return result,
        }
    }
}

pub fn run_session(device: &mut impl Device) {
    if let Err(error) = reset_lock(device, HOLD_RESET) {
        device.fault("reset lock acquire", -1, error.raw_os_error().unwrap_or(0));
        return;
    }
    loop {
        let mut bytes = [0; EVENT_SIZE];
        match device.read(&mut bytes) {
            Ok(EVENT_SIZE) => {
                let event = event_type(&bytes);
                device.publish(event, status(event));
            }
            Err(error) if error.kind() == io::ErrorKind::Interrupted => continue,
            Ok(count) => {
                device.fault("status read", count as i64, 0);
                break;
            }
            Err(error) => {
                device.fault("status read", -1, error.raw_os_error().unwrap_or(0));
                break;
            }
        }
    }
    if let Err(error) = reset_lock(device, FREE_RESET) {
        device.fault("reset lock release", -1, error.raw_os_error().unwrap_or(0));
    }
}
