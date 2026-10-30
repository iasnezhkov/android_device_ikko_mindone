/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_rpc_wire::{encode_response, Request, MAX_RECORD_BYTES};
use std::io::{self, Read, Write};

fn main() {
    let mut input = Vec::new();
    io::stdin().read_to_end(&mut input).unwrap();
    let mut offset = 0;
    let mut output = io::stdout().lock();
    while offset < input.len() {
        let length = u32::from_le_bytes(input[offset..offset + 4].try_into().unwrap()) as usize;
        offset += 4;
        let request = Request::parse(&input[offset..offset + length]).unwrap();
        offset += length;
        let arguments: Vec<_> = request.arguments().collect();
        let mut response = [0xa5; MAX_RECORD_BYTES];
        let bytes = encode_response(&request, &arguments, &mut response).unwrap();
        output.write_all(&(bytes as u32).to_le_bytes()).unwrap();
        output.write_all(&response[..bytes]).unwrap();
    }
}
