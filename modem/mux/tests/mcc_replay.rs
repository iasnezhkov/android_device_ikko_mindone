/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_mux_core::mcc::{prepare, Effect};
use mindone_mux_core::session::{Action, ChannelConfig, Handoff, Session, Sink};
use mindone_mux_core::{encode, Frame};
use std::io::{self, Read};

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
        let result = &self.0[..length];
        self.0 = &self.0[length..];
        result
    }
}

struct Effects {
    quiet: bool,
}

impl Sink for Effects {
    fn open_endpoint(&mut self, _: u8) -> bool { true }

    fn apply(&mut self, effect: Action<'_>) -> bool {
        if let Action::Transmit(frame) = effect {
            if !self.quiet {
                let mut output = vec![0; frame.payload.len() + 7];
                let length = encode(frame, 32767, &mut output).unwrap();
                print!("TX ");
                for byte in &output[..length] { print!("{byte:02x}"); }
                println!();
            }
        }
        true
    }
}

fn main() {
    let mut bytes = Vec::new();
    io::stdin().read_to_end(&mut bytes).unwrap();
    let mut input = Input(&bytes);
    let mut session = Session::new(&[], 512).unwrap();
    let mut sink = Effects { quiet: false };
    let mut limit = 512;
    while !input.0.is_empty() {
        match input.byte() {
            1 => {
                limit = input.number();
                session = Session::new(&[
                    ChannelConfig { channel: 1, atci: false },
                    ChannelConfig { channel: 26, atci: true },
                ], limit).unwrap();
                sink.quiet = true;
                session.begin(0, &mut sink).unwrap();
                for channel in [0, 1, 26] {
                    session.receive(Frame::command(channel, 0x73, &[]).unwrap(), &mut sink).unwrap();
                }
                sink.quiet = false;
                println!("RESET");
            }
            2 => {
                let control = input.byte();
                let length = input.number();
                let payload = input.bytes(length);
                let mut output = [0; 32767];
                let outcome = {
                    let handoff = session.receive(Frame::command(0, control, payload).unwrap(), &mut sink).unwrap();
                    let Handoff::Control(data) = handoff else { panic!("Missing MCC handoff"); };
                    prepare(data, limit, &mut output).unwrap()
                };
                if let Some(reply) = outcome.reply {
                    session.send_control(reply.control, reply.payload, &mut sink).unwrap();
                }
                if let Effect::PeerSignals { channel, signals } = outcome.effect {
                    session.apply_peer_signals(channel, signals).unwrap();
                }
                println!("STATE {} {} {} {} {}", u8::from(outcome.effect == Effect::PeerClosing),
                    u8::from(session.channel(0).unwrap().peer_paused),
                    u8::from(session.channel(1).unwrap().peer_paused),
                    u8::from(session.channel(26).unwrap().peer_paused),
                    u8::from(session.poll_final_pending()));
            }
            value => panic!("Invalid MCC replay operation: {value}"),
        }
    }
}
