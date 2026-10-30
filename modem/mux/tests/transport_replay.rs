/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_mux_core::transport::*;
use mindone_mux_core::Frame;
use std::io::{self, Read, Write};

struct Input<'a>(&'a [u8]);

impl Input<'_> {
    fn byte(&mut self) -> u8 {
        let value = self.0[0];
        self.0 = &self.0[1..];
        value
    }

    fn number(&mut self) -> usize {
        let value = u32::from_le_bytes(self.0[..4].try_into().unwrap()) as usize;
        self.0 = &self.0[4..];
        value
    }

    fn bytes(&mut self, length: usize) -> &[u8] {
        let value = &self.0[..length];
        self.0 = &self.0[length..];
        value
    }
}

struct Writer {
    steps: Vec<(u8, usize)>,
    calls: usize,
    accepted: Vec<u8>,
}

impl ByteWriter for Writer {
    fn write(&mut self, input: &[u8]) -> IoResult {
        let (kind, count) = self.steps.get(self.calls).copied().unwrap_or((2, 0));
        self.calls += 1;
        if kind == 0 {
            let count = count.min(input.len());
            self.accepted.extend_from_slice(&input[..count]);
            return IoResult::Transferred(count);
        }
        if kind == 5 {
            return IoResult::Transferred(0);
        }
        IoResult::Fault(match kind {
            1 => Fault::Interrupted,
            2 => Fault::WouldBlock,
            3 => Fault::Busy,
            6 => Fault::DeviceReset,
            _ => Fault::Failure,
        })
    }
}

fn dump(output: &mut impl Write, data: &[u8]) {
    write!(output, " {}:", data.len()).unwrap();
    for byte in data {
        write!(output, "{byte:02x}").unwrap();
    }
}

fn main() {
    let mut bytes = Vec::new();
    io::stdin().read_to_end(&mut bytes).unwrap();
    let mut input = Input(&bytes);
    let mut output = io::BufWriter::new(io::stdout().lock());
    let mut transmit = TransmitQueue::new(512).unwrap();
    let mut pending = PendingQueue::<36863>::new(512).unwrap();
    while !input.0.is_empty() {
        match input.byte() {
            1 => {
                let limit = input.number();
                transmit = TransmitQueue::new(limit).unwrap();
                pending = PendingQueue::new(limit).unwrap();
                writeln!(output, "RESET").unwrap();
            }
            2 => {
                let channel = input.byte();
                let control = input.byte();
                let length = input.number();
                let frame = Frame::command(channel, control, input.bytes(length)).unwrap();
                write!(output, "FRAME {}", u8::from(transmit.admit_frame(frame).is_ok())).unwrap();
                dump(&mut output, transmit.pending());
                writeln!(output).unwrap();
            }
            3 => {
                let count = input.byte();
                let mut writer = Writer { steps: Vec::new(), calls: 0, accepted: Vec::new() };
                for _ in 0..count {
                    writer.steps.push((input.byte(), input.number()));
                }
                let result = transmit.drain(&mut writer, &mut TurnBudget::new(MAX_TURN_CALLS, MAX_TURN_BYTES).unwrap());
                let stop = match result.stop {
                    Stop::Complete => 0,
                    Stop::Blocked => 1,
                    Stop::Failure | Stop::NoProgress => 3,
                    Stop::DeviceReset => 4,
                    _ => panic!("Replay exceeded the valid C differential scope: {:?}", result.stop),
                };
                write!(output, "DRAIN {stop} {}", writer.calls).unwrap();
                dump(&mut output, &writer.accepted);
                dump(&mut output, transmit.pending());
                writeln!(output).unwrap();
            }
            4 => {
                let length = input.number();
                let _ = pending.buffer_remainder(input.bytes(length));
                write!(output, "PENDING").unwrap();
                dump(&mut output, pending.pending());
                writeln!(output).unwrap();
            }
            5 => writeln!(output, "ROOM {}", u8::from(transmit.can_accept_pty_read())).unwrap(),
            value => panic!("Invalid replay operation: {value}"),
        }
    }
}
