/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_mux_core::{encode, Decoder, Frame};
use std::io::{self, Read};

fn number(bytes: &mut &[u8]) -> u32 {
    let (head, tail) = bytes.split_at(4);
    *bytes = tail;
    u32::from_le_bytes(head.try_into().unwrap())
}

fn payload<'a>(bytes: &mut &'a [u8], length: usize) -> &'a [u8] {
    let (head, tail) = bytes.split_at(length);
    *bytes = tail;
    head
}

fn print_hex(data: &[u8]) {
    for byte in data {
        print!("{byte:02x}");
    }
    println!();
}

fn main() {
    let mut input = Vec::new();
    io::stdin().read_to_end(&mut input).unwrap();
    let mut bytes = input.as_slice();
    let mut decoder = Decoder::<65536>::new(512).unwrap();
    while !bytes.is_empty() {
        let operation = payload(&mut bytes, 1)[0];
        match operation {
            1 => {
                let fields = payload(&mut bytes, 2);
                let limit = number(&mut bytes) as usize;
                let length = number(&mut bytes) as usize;
                let data = payload(&mut bytes, length);
                let mut output = [0; 65536];
                let frame = Frame::command(fields[0], fields[1], data).unwrap();
                match encode(frame, limit, &mut output) {
                    Ok(length) => {
                        print!("TX 1 ");
                        print_hex(&output[..length]);
                    }
                    Err(_) => println!("TX 0 "),
                }
            }
            2 => {
                let limit = number(&mut bytes) as usize;
                decoder = Decoder::new(limit).unwrap();
                println!("LIMIT {limit}");
            }
            3 => {
                let length = number(&mut bytes) as usize;
                decoder.push(payload(&mut bytes, length)).unwrap();
                while let Some(frame) = decoder.next_frame() {
                    print!("FRAME {} {:02x} ", frame.channel(), frame.control);
                    print_hex(frame.payload);
                }
                print!("PENDING ");
                print_hex(decoder.buffered());
            }
            4 => println!("RESET {}", decoder.reset()),
            _ => panic!("Unexpected replay operation {operation}"),
        }
    }
}
