/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

pub const LINE_BYTES: usize = 4096;

#[derive(Debug, Eq, PartialEq)]
pub enum Record {
    Line(String),
    TooLarge,
}

pub struct Framer {
    bytes: Vec<u8>,
    dropping: bool,
}

impl Default for Framer {
    fn default() -> Self { Self::new() }
}

impl Framer {
    pub fn new() -> Self {
        Self { bytes: Vec::with_capacity(LINE_BYTES), dropping: false }
    }

    pub fn buffered_bytes(&self) -> usize { self.bytes.len() }

    pub fn clear(&mut self) {
        self.bytes.clear();
        self.dropping = false;
    }

    pub fn push(&mut self, byte: u8) -> Option<Record> {
        if byte == b'\r' || byte == b'\n' {
            if self.dropping {
                self.dropping = false;
                return None;
            }
            if self.bytes.is_empty() { return None; }
            let line = self.bytes.iter().map(|byte| char::from(*byte)).collect();
            self.bytes.clear();
            return Some(Record::Line(line));
        }
        if self.dropping || !(0x20..=0x7e).contains(&byte) { return None; }
        if self.bytes.len() == LINE_BYTES {
            self.bytes.clear();
            self.dropping = true;
            return Some(Record::TooLarge);
        }
        self.bytes.push(byte);
        None
    }
}
