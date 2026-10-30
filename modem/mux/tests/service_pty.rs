/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

struct Modem {
    endpoint: PosixEndpoint,
    outgoing: VecDeque<u8>,
    commands: Vec<Vec<u8>>,
    record: Vec<u8>,
    decoder: Decoder<65536>,
    multiplexing: bool,
    received: BTreeMap<u8, Vec<u8>>,
}

impl Modem {
    fn new(endpoint: PosixEndpoint) -> Self {
        Self { endpoint, outgoing: VecDeque::new(), commands: Vec::new(), record: Vec::new(),
            decoder: Decoder::new(512).unwrap(), multiplexing: false, received: BTreeMap::new() }
    }
    fn bytes(&mut self, bytes: &[u8]) { assert!(self.outgoing.len() + bytes.len() <= 131072); self.outgoing.extend(bytes); }
    fn frame(&mut self, channel: u8, control: u8, payload: &[u8]) {
        let mut wire = [0; 519];
        let count = encode(Frame::command(channel, control, payload).unwrap(), 512, &mut wire).unwrap();
        self.bytes(&wire[..count]);
    }
    fn pump(&mut self) {
        for _ in 0..128 {
            if self.outgoing.is_empty() { break; }
            let count = self.outgoing.len().min(31);
            match self.endpoint.write(&self.outgoing.make_contiguous()[..count]) {
                IoResult::Transferred(bytes) => { self.outgoing.drain(..bytes); }
                IoResult::Fault(Fault::WouldBlock) => break,
                result => panic!("Modem write: {result:?}"),
            }
        }
        for _ in 0..128 {
            let mut input = [0; 29];
            let bytes = match self.endpoint.read(&mut input) {
                IoResult::Transferred(0) | IoResult::Fault(Fault::WouldBlock) => break,
                IoResult::Transferred(bytes) => bytes,
                result => panic!("Modem read: {result:?}"),
            };
            if !self.multiplexing {
                for byte in &input[..bytes] {
                    if *byte != b'\r' { self.record.push(*byte); assert!(self.record.len() <= 128); continue; }
                    let command = std::mem::take(&mut self.record);
                    if command.starts_with(b"AT+CMUX=") { self.bytes(b"+CMUX: READY\r\nOK\r\n"); self.multiplexing = true; }
                    else { self.bytes(b"OK\r\n"); }
                    self.commands.push(command);
                }
            } else {
                self.decoder.push(&input[..bytes]).unwrap();
                let mut frames = Vec::new();
                while let Some(frame) = self.decoder.next_frame() { frames.push((frame.channel(), frame.control, frame.payload.to_vec())); }
                for (channel, control, payload) in frames {
                    if control & !POLL_FINAL == 0x2f { self.frame(channel, 0x73, &[]); }
                    else if control & !POLL_FINAL == UIH {
                        if channel != 0 { self.received.entry(channel).or_default().extend(payload); }
                        else {
                            let mut response = [0; 512];
                            if let Some(reply) = mcc::prepare(&payload, 512, &mut response).unwrap().reply { self.frame(0, reply.control, reply.payload); }
                        }
                    }
                }
            }
        }
    }
}
