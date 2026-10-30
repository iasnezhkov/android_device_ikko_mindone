/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_mux_core::chat::{Config, Controller, Phase, Pin};
use mindone_mux_core::transport::Fault;
use std::collections::VecDeque;
use std::io::{self, Read};

struct Input<'a>(&'a [u8]);

impl Input<'_> {
    fn byte(&mut self) -> u8 {
        let value = self.0[0]; self.0 = &self.0[1..]; value
    }
    fn number(&mut self) -> u32 {
        let value = u32::from_le_bytes(self.0[..4].try_into().unwrap());
        self.0 = &self.0[4..]; value
    }
}

fn main() {
    let mut bytes = Vec::new();
    io::stdin().read_to_end(&mut bytes).unwrap();
    let mut input = Input(&bytes);
    let mut index = 0;
    while !input.0.is_empty() {
        let frame_limit = input.number() as usize;
        let baud_index = input.byte();
        let user_build = input.byte() != 0;
        let pin_number = input.number() as i32;
        let fault_kind = input.byte();
        let chunk = usize::from(input.byte());
        let indication = input.byte() != 0;
        let cmux_ready = input.byte() != 0;
        let reject_first = input.byte() != 0;
        let mode = input.byte();
        let pin = (pin_number >= 0).then(|| Pin::new(format!("{pin_number:04}").as_bytes()).unwrap());
        let mut controller = Controller::new(Config { frame_limit, baud_index: i32::from(baud_index), user_build, pin }, 0).unwrap();
        let mut now = 0;
        let mut outgoing = Vec::new();
        let mut packets = VecDeque::new();
        let mut commands = 0;
        let mut at_commands = 0;
        let mut previous_token = None;
        let mut calls = 0;
        let mut tails = Vec::new();
        println!("CASE {index}");
        index += 1;
        for step in 0..10000 {
            assert!(step < 9999, "Fixture scheduler exceeded its bound");
            match controller.phase() {
                Phase::WaitingReady => {
                    if indication { controller.feed(now, b"\r\n+EIND: 128\r\n").unwrap(); }
                    else { now = controller.expires_at().unwrap(); controller.tick(now).unwrap(); }
                }
                Phase::Writing => {
                    let request = controller.pending_write().unwrap();
                    let token = request.token;
                    let accepted = request.bytes[..request.bytes.len().min(chunk)].to_vec();
                    if previous_token != Some(token) { previous_token = Some(token); calls = 0; }
                    calls += 1;
                    if calls == 1 && fault_kind != 0 {
                        let fault = if fault_kind == 1 { Fault::Interrupted } else { Fault::Busy };
                        controller.write_fault(now, token, fault).unwrap();
                        if fault_kind == 2 { now += 10; }
                        continue;
                    }
                    controller.accept_written(now, token, accepted.len()).unwrap();
                    outgoing.extend_from_slice(&accepted);
                    if outgoing.last() == Some(&b'\r') {
                        print!("COMMAND ");
                        for byte in &outgoing { print!("{byte:02x}"); }
                        println!();
                        commands += 1;
                        packets.clear();
                        if outgoing == b"AT\r" {
                            at_commands += 1;
                            if at_commands == 1 && mode == 1 { packets.push_back((0, b"\r\nNOK\r\n".to_vec())); }
                            else if at_commands == 1 && mode == 2 { packets.push_back((0, b"OK".to_vec())); }
                            else if at_commands == 1 && mode == 3 {
                                for _ in 0..60 { packets.push_back((1000, b"+URC: 1\r\n".to_vec())); }
                            } else {
                                packets.push_back((0, if at_commands == 1 && reject_first { b"\r\nERROR\r\n".to_vec() } else { b"\r\nOK\r\n".to_vec() }));
                            }
                        } else if outgoing.starts_with(b"AT+CMUX=") {
                            if cmux_ready { packets.push_back((0, b"\r\n+CMUX: READY\r\n".to_vec())); }
                            packets.push_back((0, if mode == 4 { b"\r\nOK\r\n\xf9\0\x03".to_vec() } else { b"\r\nOK\r\n".to_vec() }));
                        } else { packets.push_back((0, b"\r\nOK\r\n".to_vec())); }
                        outgoing.clear();
                    }
                }
                Phase::Reading => {
                    if let Some((delay, packet)) = packets.pop_front() {
                        now += delay;
                        if let Some(tail) = controller.feed(now, &packet).unwrap().mux_tail { tails.extend_from_slice(tail); }
                    } else { now = controller.expires_at().unwrap(); controller.tick(now).unwrap(); }
                }
                Phase::Settling => { now = controller.expires_at().unwrap(); controller.tick(now).unwrap(); }
                Phase::Ready => break,
                Phase::Failed => panic!("Fixture controller failed: {:?}", controller.error()),
            }
        }
        if mode == 4 {
            print!("TAIL ");
            for byte in tails { print!("{byte:02x}"); }
            println!();
        }
        println!("RESULT 0 {now} {commands} {}", controller.buffered_bytes());
    }
}
