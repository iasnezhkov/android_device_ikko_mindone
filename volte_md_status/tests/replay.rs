// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0
#[path = "../protocol.rs"]
mod protocol;

use protocol::Device;
use std::io::{self, Read};

struct Replay {
    data: Vec<u8>,
    offset: usize,
}

impl Read for Replay {
    fn read(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        let end = self.offset + 4;
        let count = i32::from_le_bytes(self.data[self.offset..end].try_into().unwrap());
        self.offset = end;
        println!("read {count}");
        if count < 0 {
            return Err(io::Error::from_raw_os_error(-count));
        }
        let count = count as usize;
        buffer[..count].copy_from_slice(&self.data[self.offset..self.offset + count]);
        self.offset += count;
        Ok(count)
    }
}

impl Device for Replay {
    fn reset_lock(&mut self, request: u64) -> io::Result<()> {
        println!("ioctl {request}");
        Ok(())
    }

    fn publish(&mut self, event: i32, value: &'static str) {
        println!("publish {event} {value}");
    }

    fn fault(&mut self, _operation: &str, _count: i64, _error: i32) {}
}

fn main() {
    let mut data = Vec::new();
    io::stdin().read_to_end(&mut data).unwrap();
    let mut device = Replay { data, offset: 0 };
    while device.offset < device.data.len() {
        println!("open");
        protocol::run_session(&mut device);
        println!("close\nsleep 1");
    }
}
