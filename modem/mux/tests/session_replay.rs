/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_mux_core::session::*;
use mindone_mux_core::{encode, Frame};
use std::io::{self, Read};

fn take<'a>(input: &mut &'a [u8], length: usize) -> &'a [u8] {
    let (head, tail) = input.split_at(length);
    *input = tail;
    head
}

fn number(input: &mut &[u8], width: usize) -> u64 {
    let mut value = 0;
    for (index, byte) in take(input, width).iter().enumerate() {
        value |= u64::from(*byte) << (index * 8);
    }
    value
}

fn print_hex(bytes: &[u8]) {
    for byte in bytes {
        print!("{byte:02x}");
    }
    println!();
}

struct TraceSink {
    rejected_endpoint: u8,
}

impl Sink for TraceSink {
    fn open_endpoint(&mut self, channel: u8) -> bool {
        println!("OPEN {channel}");
        channel != self.rejected_endpoint
    }

    fn apply(&mut self, action: Action<'_>) -> bool {
        match action {
            Action::Transmit(frame) => {
                let mut output = [0; 519];
                let length = encode(frame, 512, &mut output).unwrap();
                print!("TX ");
                print_hex(&output[..length]);
            }
            Action::CloseEndpoint(channel) => println!("CLOSE {channel}"),
            Action::Ready { refused_channels } => println!("READY {refused_channels}"),
            Action::AtciUnavailable => println!("ATCI"),
        }
        true
    }
}

fn snapshot(session: &Session) {
    let phase = match session.phase() {
        Phase::Idle | Phase::Setup => 0,
        Phase::Running => 1,
        Phase::Closing => 2,
        Phase::PeerClosing => 3,
        Phase::Failed => 4,
    };
    println!("STATE {phase} {} {} {} {} {}", session.unresolved(),
        u8::from(session.setup_done()), u8::from(session.ready_started()),
        u8::from(session.poll_final_pending()), session.setup_deadline());
    for channel in [0, 1, 26] {
        let value = session.channel(channel).unwrap();
        println!("CHANNEL {channel} {} {} {} {} {} {} {} {} {}",
            u8::from(value.endpoint_present), u8::from(value.opening), u8::from(value.open),
            u8::from(value.disconnecting), u8::from(value.refused), u8::from(value.refusal_reported),
            u8::from(value.peer_paused), u8::from(value.local_paused),
            u8::from(session.can_read_endpoint(channel, true)));
    }
}

fn main() {
    let mut input = Vec::new();
    io::stdin().read_to_end(&mut input).unwrap();
    let mut bytes = input.as_slice();
    let mut session = Session::new(&[
        ChannelConfig { channel: 1, atci: false },
        ChannelConfig { channel: 26, atci: true },
    ], 512).unwrap();
    let mut sink = TraceSink { rejected_endpoint: 255 };
    let mut started = false;
    while !bytes.is_empty() {
        match number(&mut bytes, 1) {
            1 => {
                let now = number(&mut bytes, 8);
                if started {
                    for channel in [1, 26] {
                        sink.apply(Action::CloseEndpoint(channel));
                    }
                    session.reset_after_transport_close();
                }
                session.begin(now, &mut sink).unwrap();
                started = true;
            }
            2 => {
                let channel = number(&mut bytes, 1) as u8;
                let control = number(&mut bytes, 1) as u8;
                let length = number(&mut bytes, 4) as usize;
                let frame = Frame::command(channel, control, take(&mut bytes, length)).unwrap();
                match session.receive(frame, &mut sink).unwrap() {
                    Handoff::None => {}
                    Handoff::Control(payload) => {
                        print!("CONTROL ");
                        print_hex(payload);
                    }
                    Handoff::Data { channel, payload } => {
                        print!("DELIVER {channel} ");
                        print_hex(payload);
                    }
                    Handoff::EndpointMissing { channel, bytes } => println!("DROP {channel} {bytes}"),
                }
            }
            3 => {
                let channel = number(&mut bytes, 1) as u8;
                let signals = number(&mut bytes, 1) as u8;
                session.apply_peer_signals(channel, signals).unwrap();
            }
            4 => {
                let channel = number(&mut bytes, 1) as u8;
                let paused = number(&mut bytes, 1) != 0;
                if session.channel(channel).is_some_and(|value| value.endpoint_present) {
                    session.set_local_pause(channel, paused, &mut sink).unwrap();
                }
            }
            5 => {
                let channel = number(&mut bytes, 1) as u8;
                session.recreate_endpoint(channel, &mut sink).unwrap();
            }
            6 => sink.rejected_endpoint = number(&mut bytes, 1) as u8,
            7 => {
                let control = number(&mut bytes, 1) as u8;
                let length = number(&mut bytes, 4) as usize;
                session.send_control(control, take(&mut bytes, length), &mut sink).unwrap();
            }
            8 => {
                let channel = number(&mut bytes, 1) as u8;
                let result = match session.application_input(channel).unwrap() {
                    ApplicationInput::Forward => 0,
                    ApplicationInput::ReportRefusal => 1,
                    ApplicationInput::Discard => 2,
                };
                println!("APP {result}");
            }
            9 => {
                for channel in [1, 26] {
                    session.request_disconnect(channel, &mut sink).unwrap();
                }
                session.send_control(0xef, &[0xc3, 1], &mut sink).unwrap();
            }
            10 => {
                let now = number(&mut bytes, 8);
                let pending = number(&mut bytes, 1) != 0;
                let timeout = session.poll_timeout(now, pending).unwrap();
                println!("POLL {}", timeout.map_or(-1, |value| value as i64));
            }
            operation => panic!("Invalid operation {operation}"),
        }
        snapshot(&session);
    }
}
