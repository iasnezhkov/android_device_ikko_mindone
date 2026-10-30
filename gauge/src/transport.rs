// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

use crate::proto::{self, Message, Notification};
use std::collections::VecDeque;
use std::io;
use std::time::{Duration, Instant};

pub const NETLINK_PROTOCOL: i32 = 26;
pub const NETLINK_HEADER_LEN: usize = 16;
pub const MAX_DATAGRAM_LEN: usize = NETLINK_HEADER_LEN + proto::HEADER_LEN + proto::MAX_DATA_LEN;
pub const MAX_QUEUED_NOTIFICATIONS: usize = 64;

#[derive(Debug)]
pub enum Error {
    Io(io::Error),
    Protocol(proto::Error),
    Timeout,
    ShortWrite,
    ShortRead,
    Oversized,
    NetlinkHeader,
    Sender,
    Sequence { expected: u32, actual: u32 },
    ReservedSequence,
    Busy,
    NoPendingRequest,
    NotificationOverflow,
    Command,
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}

impl std::error::Error for Error {}

impl From<proto::Error> for Error {
    fn from(value: proto::Error) -> Self {
        Self::Protocol(value)
    }
}

fn io_error(value: io::Error) -> Error {
    if value.kind() == io::ErrorKind::TimedOut {
        Error::Timeout
    } else {
        Error::Io(value)
    }
}

fn remaining(start: Instant, timeout: Duration) -> Result<Duration, Error> {
    timeout
        .checked_sub(start.elapsed())
        .filter(|v| !v.is_zero())
        .ok_or(Error::Timeout)
}

#[derive(Clone, Copy, Debug)]
pub struct Received {
    pub length: usize,
    pub sender_port_id: u32,
    pub sender_groups: u32,
}

pub trait DatagramIo {
    fn send(&mut self, bytes: &[u8], timeout: Duration) -> io::Result<usize>;
    fn receive(&mut self, bytes: &mut [u8], timeout: Duration) -> io::Result<Received>;
}

pub trait Transport {
    fn send_request(
        &mut self,
        request: &[u8],
        sequence: u32,
        timeout: Duration,
    ) -> Result<(), Error>;
    fn receive_reply(&mut self, sequence: u32, timeout: Duration) -> Result<Vec<u8>, Error>;
}

#[derive(Clone, Copy)]
struct Pending {
    sequence: u32,
    nl_cmd: u32,
    fgd_cmd: u32,
}

pub struct NetlinkTransport<I> {
    io: I,
    pending: Option<Pending>,
    notifications: VecDeque<Notification>,
}

impl<I: DatagramIo> NetlinkTransport<I> {
    pub fn new(io: I) -> Self {
        Self {
            io,
            pending: None,
            notifications: VecDeque::new(),
        }
    }

    pub fn pop_notification(&mut self) -> Option<Notification> {
        self.notifications.pop_front()
    }

    pub fn io(&self) -> &I {
        &self.io
    }

    pub fn into_io(self) -> I {
        self.io
    }
}

pub fn encode_datagram(payload: &[u8], sequence: u32) -> Result<Vec<u8>, Error> {
    if payload.len() > MAX_DATAGRAM_LEN - NETLINK_HEADER_LEN {
        return Err(Error::Oversized);
    }
    let mut out = vec![0; NETLINK_HEADER_LEN + payload.len()];
    let length = out.len() as u32;
    out[..4].copy_from_slice(&length.to_le_bytes());
    out[8..12].copy_from_slice(&sequence.to_le_bytes());
    out[NETLINK_HEADER_LEN..].copy_from_slice(payload);
    Ok(out)
}

fn decode_datagram(bytes: &[u8]) -> Result<(u32, &[u8]), Error> {
    if bytes.len() < NETLINK_HEADER_LEN {
        return Err(Error::ShortRead);
    }
    let length = proto::u32_at(bytes, 0) as usize;
    if length > MAX_DATAGRAM_LEN {
        return Err(Error::Oversized);
    }
    if length < NETLINK_HEADER_LEN || length != bytes.len() || bytes[4..8] != [0; 4] {
        return Err(Error::NetlinkHeader);
    }
    Ok((proto::u32_at(bytes, 8), &bytes[NETLINK_HEADER_LEN..]))
}

impl<I: DatagramIo> Transport for NetlinkTransport<I> {
    fn send_request(
        &mut self,
        request: &[u8],
        sequence: u32,
        timeout: Duration,
    ) -> Result<(), Error> {
        if self.pending.is_some() {
            return Err(Error::Busy);
        }
        if sequence == 0 {
            return Err(Error::ReservedSequence);
        }
        let message = Message::decode(request)?;
        let bytes = encode_datagram(request, sequence)?;
        let start = Instant::now();
        loop {
            match self.io.send(&bytes, remaining(start, timeout)?) {
                Ok(length) if length == bytes.len() => break,
                Ok(_) => return Err(Error::ShortWrite),
                Err(error) if error.kind() == io::ErrorKind::Interrupted => continue,
                Err(error) => return Err(io_error(error)),
            }
        }
        self.pending = Some(Pending {
            sequence,
            nl_cmd: message.header.nl_cmd,
            fgd_cmd: message.header.fgd_cmd,
        });
        Ok(())
    }

    fn receive_reply(&mut self, sequence: u32, timeout: Duration) -> Result<Vec<u8>, Error> {
        let pending = self.pending.ok_or(Error::NoPendingRequest)?;
        if pending.sequence != sequence {
            return Err(Error::Sequence {
                expected: pending.sequence,
                actual: sequence,
            });
        }
        self.pending = None;
        let start = Instant::now();
        let mut buffer = [0; MAX_DATAGRAM_LEN];
        loop {
            let received = match self.io.receive(&mut buffer, remaining(start, timeout)?) {
                Ok(received) => received,
                Err(error) if error.kind() == io::ErrorKind::Interrupted => continue,
                Err(error) => return Err(io_error(error)),
            };
            if received.length > buffer.len() {
                return Err(Error::Oversized);
            }
            if received.sender_port_id != 0 || received.sender_groups != 0 {
                return Err(Error::Sender);
            }
            let (actual, payload) = decode_datagram(&buffer[..received.length])?;
            let message = Message::decode(payload)?;
            if actual == 0 {
                let notification = proto::decode_notification(payload)?;
                if self.notifications.len() == MAX_QUEUED_NOTIFICATIONS {
                    return Err(Error::NotificationOverflow);
                }
                self.notifications.push_back(notification);
                continue;
            }
            if actual != sequence {
                return Err(Error::Sequence {
                    expected: sequence,
                    actual,
                });
            }
            if message.header.nl_cmd != pending.nl_cmd || message.header.fgd_cmd != pending.fgd_cmd
            {
                return Err(Error::Command);
            }
            return Ok(payload.to_vec());
        }
    }
}

pub mod mock {
    use super::*;

    #[derive(Default)]
    pub struct MockIo {
        pub sent: Vec<Vec<u8>>,
        pub send_results: VecDeque<io::Result<usize>>,
        pub incoming: VecDeque<io::Result<(Vec<u8>, Received)>>,
    }

    impl MockIo {
        pub fn push_kernel(&mut self, bytes: Vec<u8>) {
            let received = Received {
                length: bytes.len(),
                sender_port_id: 0,
                sender_groups: 0,
            };
            self.incoming.push_back(Ok((bytes, received)));
        }
    }

    impl DatagramIo for MockIo {
        fn send(&mut self, bytes: &[u8], _timeout: Duration) -> io::Result<usize> {
            self.sent.push(bytes.to_vec());
            self.send_results.pop_front().unwrap_or(Ok(bytes.len()))
        }

        fn receive(&mut self, bytes: &mut [u8], _timeout: Duration) -> io::Result<Received> {
            let (data, received) = self
                .incoming
                .pop_front()
                .unwrap_or_else(|| Err(io::ErrorKind::TimedOut.into()))?;
            let count = bytes.len().min(data.len());
            bytes[..count].copy_from_slice(&data[..count]);
            Ok(received)
        }
    }

    pub type MockTransport = NetlinkTransport<MockIo>;
}

#[cfg(any(
    target_os = "android",
    all(target_os = "linux", feature = "raw-netlink")
))]
pub mod raw;
