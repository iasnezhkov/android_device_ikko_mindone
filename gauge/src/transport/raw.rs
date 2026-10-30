// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

use super::{DatagramIo, NetlinkTransport, Received, NETLINK_PROTOCOL};
use nix::poll::{poll, PollFd, PollFlags, PollTimeout};
use nix::sys::socket::{bind, recvmsg, sendto, MsgFlags, NetlinkAddr};
use std::io::{self, IoSliceMut};
use rustix::net::{socket_with, AddressFamily, Protocol, SocketFlags, SocketType};
use std::num::NonZeroU32;
use std::os::fd::{AsFd, AsRawFd, OwnedFd};
use std::time::{Duration, Instant};

pub struct RawNetlink {
    fd: OwnedFd,
}

impl RawNetlink {
    pub fn open() -> io::Result<Self> {
        let protocol = NonZeroU32::new(NETLINK_PROTOCOL as u32)
            .ok_or_else(|| io::Error::from(io::ErrorKind::InvalidInput))?;
        let fd = loop {
            match socket_with(
                AddressFamily::NETLINK,
                SocketType::DGRAM,
                SocketFlags::CLOEXEC | SocketFlags::NONBLOCK,
                Some(Protocol::from_raw(protocol)),
            ) {
                Ok(fd) => break fd,
                Err(error) if error == rustix::io::Errno::INTR => continue,
                Err(error) => return Err(io::Error::from(error)),
            }
        };
        let socket = Self { fd };
        let local = NetlinkAddr::new(0, 0);
        loop {
            match bind(socket.fd.as_raw_fd(), &local) {
                Ok(()) => return Ok(socket),
                Err(error) => {
                    let error = io::Error::from(error);
                    if error.kind() != io::ErrorKind::Interrupted {
                        return Err(error);
                    }
                }
            }
        }
    }

    pub fn transport(self) -> NetlinkTransport<Self> {
        NetlinkTransport::new(self)
    }

    fn wait(&self, events: PollFlags, start: Instant, timeout: Duration) -> io::Result<()> {
        loop {
            let remaining = timeout
                .checked_sub(start.elapsed())
                .filter(|d| !d.is_zero())
                .ok_or_else(|| io::Error::from(io::ErrorKind::TimedOut))?;
            let milliseconds = remaining
                .as_millis()
                .saturating_add(u128::from(remaining.subsec_nanos() % 1_000_000 != 0))
                .min(i32::MAX as u128) as i32;
            let mut fds = [PollFd::new(self.fd.as_fd(), events)];
            let result = match poll(&mut fds, PollTimeout::try_from(milliseconds).unwrap()) {
                Ok(result) => result,
                Err(error) => {
                    let error = io::Error::from(error);
                    if error.kind() == io::ErrorKind::Interrupted {
                        continue;
                    }
                    return Err(error);
                }
            };
            if result == 0 {
                continue;
            }
            let revents = fds[0].revents().unwrap_or(PollFlags::empty());
            if revents.intersects(PollFlags::POLLERR | PollFlags::POLLHUP | PollFlags::POLLNVAL) {
                return Err(io::Error::other("netlink poll failure"));
            }
            if revents.intersects(events) {
                return Ok(());
            }
        }
    }
}

impl DatagramIo for RawNetlink {
    fn send(&mut self, bytes: &[u8], timeout: Duration) -> io::Result<usize> {
        let start = Instant::now();
        let kernel = NetlinkAddr::new(0, 0);
        loop {
            self.wait(PollFlags::POLLOUT, start, timeout)?;
            match sendto(self.fd.as_raw_fd(), bytes, &kernel, MsgFlags::MSG_DONTWAIT) {
                Ok(length) => return Ok(length),
                Err(error) => {
                    let error = io::Error::from(error);
                    if matches!(
                        error.kind(),
                        io::ErrorKind::Interrupted | io::ErrorKind::WouldBlock
                    ) {
                        continue;
                    }
                    return Err(error);
                }
            }
        }
    }

    fn receive(&mut self, bytes: &mut [u8], timeout: Duration) -> io::Result<Received> {
        let start = Instant::now();
        loop {
            self.wait(PollFlags::POLLIN, start, timeout)?;
            let mut buffers = [IoSliceMut::new(bytes)];
            match recvmsg::<NetlinkAddr>(
                self.fd.as_raw_fd(),
                &mut buffers,
                None,
                MsgFlags::MSG_DONTWAIT | MsgFlags::MSG_TRUNC,
            ) {
                Ok(message) => {
                    let sender = message.address.ok_or_else(|| {
                        io::Error::new(io::ErrorKind::InvalidData, "invalid netlink sender address")
                    })?;
                    return Ok(Received {
                        length: message.bytes,
                        sender_port_id: sender.pid(),
                        sender_groups: sender.groups(),
                    });
                }
                Err(error) => {
                    let error = io::Error::from(error);
                    if matches!(
                        error.kind(),
                        io::ErrorKind::Interrupted | io::ErrorKind::WouldBlock
                    ) {
                        continue;
                    }
                    return Err(error);
                }
            }
        }
    }
}
