// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0

use crate::shadow::{self, Clock, Gm30, Log, Shadow};
use crate::store::{self, ShadowState};
use crate::transport::{self, raw::RawNetlink, Transport};
use nix::poll::{poll, PollFd, PollFlags, PollTimeout};
use nix::sys::signal::{pthread_sigmask, SigSet, SigmaskHow, Signal};
use nix::sys::signalfd::{siginfo, SfdFlags, SignalFd};
use nix::sys::time::TimeSpec;
use nix::sys::timerfd::{ClockId, Expiration, TimerFd, TimerFlags, TimerSetTimeFlags};
use nix::time::clock_gettime;
use nix::unistd::read;
use std::io;
use std::mem::size_of;
use std::os::fd::{AsFd, AsRawFd};
use std::path::Path;
use std::time::Duration;

pub struct Boottime;

impl Clock for Boottime {
    fn now_ns(&self) -> io::Result<u64> {
        let time = clock_gettime(nix::time::ClockId::CLOCK_BOOTTIME)?;
        Ok(time.tv_sec() as u64 * 1_000_000_000 + time.tv_nsec() as u64)
    }
}

struct Connection(Option<transport::NetlinkTransport<RawNetlink>>);

impl Transport for Connection {
    fn send_request(
        &mut self,
        request: &[u8],
        sequence: u32,
        timeout: Duration,
    ) -> Result<(), transport::Error> {
        if self.0.is_none() {
            self.0 = Some(
                RawNetlink::open()
                    .map_err(transport::Error::Io)?
                    .transport(),
            );
        }
        let result = self
            .0
            .as_mut()
            .unwrap()
            .send_request(request, sequence, timeout);
        if result.is_err() {
            self.0 = None;
        }
        result
    }

    fn receive_reply(
        &mut self,
        sequence: u32,
        timeout: Duration,
    ) -> Result<Vec<u8>, transport::Error> {
        let result = self
            .0
            .as_mut()
            .ok_or(transport::Error::NoPendingRequest)?
            .receive_reply(sequence, timeout);
        if result.is_err() {
            self.0 = None;
        }
        result
    }
}

struct Waiter {
    timer: TimerFd,
    signal: SignalFd,
    old_mask: SigSet,
}

impl Waiter {
    fn new() -> io::Result<Self> {
        let timer = TimerFd::new(
            ClockId::CLOCK_BOOTTIME,
            TimerFlags::TFD_CLOEXEC | TimerFlags::TFD_NONBLOCK,
        )?;
        let mut mask = SigSet::empty();
        mask.add(Signal::SIGTERM);
        let mut old_mask = SigSet::empty();
        pthread_sigmask(SigmaskHow::SIG_BLOCK, Some(&mask), Some(&mut old_mask))?;
        match SignalFd::with_flags(&mask, SfdFlags::SFD_CLOEXEC | SfdFlags::SFD_NONBLOCK) {
            Ok(signal) => Ok(Self {
                timer,
                signal,
                old_mask,
            }),
            Err(error) => {
                let _ = pthread_sigmask(SigmaskHow::SIG_SETMASK, Some(&old_mask), None);
                Err(error.into())
            }
        }
    }

    fn wait(&self, delay: Duration) -> io::Result<bool> {
        self.timer.set(
            Expiration::OneShot(TimeSpec::from_duration(delay)),
            TimerSetTimeFlags::empty(),
        )?;
        let mut fds = [
            PollFd::new(self.signal.as_fd(), PollFlags::POLLIN),
            PollFd::new(self.timer.as_fd(), PollFlags::POLLIN),
        ];
        loop {
            if let Err(error) = poll(&mut fds, PollTimeout::NONE) {
                let error = io::Error::from(error);
                if error.kind() == io::ErrorKind::Interrupted {
                    continue;
                }
                return Err(error);
            }
            if fds.iter().any(|fd| {
                fd.revents()
                    .unwrap_or(PollFlags::empty())
                    .intersects(PollFlags::POLLERR | PollFlags::POLLHUP | PollFlags::POLLNVAL)
            }) {
                return Err(io::Error::other("timer or signal poll failure"));
            }
            if fds[0]
                .revents()
                .unwrap_or(PollFlags::empty())
                .contains(PollFlags::POLLIN)
            {
                let mut signal = [0u8; size_of::<siginfo>()];
                if read(self.signal.as_fd().as_raw_fd(), &mut signal)? != signal.len() {
                    return Err(io::Error::last_os_error());
                }
                return Ok(false);
            }
            if fds[1]
                .revents()
                .unwrap_or(PollFlags::empty())
                .contains(PollFlags::POLLIN)
            {
                let mut expirations = [0u8; size_of::<u64>()];
                if read(self.timer.as_fd().as_raw_fd(), &mut expirations)? != expirations.len() {
                    return Err(io::Error::last_os_error());
                }
                return Ok(true);
            }
        }
    }
}

impl Drop for Waiter {
    fn drop(&mut self) {
        let _ = pthread_sigmask(SigmaskHow::SIG_SETMASK, Some(&self.old_mask), None);
    }
}

fn configured_period() -> u64 {
    #[cfg(target_os = "android")]
    {
        if let Ok(Some(value)) =
            rustutils::system_properties::read("ro.vendor.mindone.gauge.period_s")
        {
            return shadow::period_seconds(&value);
        }
    }
    shadow::period_seconds("")
}

fn persist(root: &Path, shadow: &Shadow) {
    let state = shadow
        .rows
        .back()
        .map(|row| ShadowState {
            charge_in_uah: row.charge_in_uah,
            acquired_ns: row.acquired_ns,
            sequence: row.sequence,
            ..ShadowState::default()
        })
        .unwrap_or_default();
    if let Err(error) = store::write_shadow_atomic(&root.join("shadow.bin"), state) {
        eprintln!("mindone_gauged store:{error}");
    }
}

pub fn run() -> Result<(), Box<dyn std::error::Error>> {
    let waiter = Waiter::new()?;
    let clock = Boottime;
    let root = Path::new("/data/vendor/mindone_gauge");
    let log = Log::new(root, shadow::LOG_LIMIT)?;
    let mut shadow = Shadow::new(configured_period());
    let mut connection = Connection(None);
    let mut saved_ns = clock.now_ns()?;
    loop {
        let gm = Gm30::read(Path::new("/sys/class/power_supply/battery"));
        let delay = shadow.poll(&mut connection, &clock, gm)?;
        if let Some(row) = shadow.rows.back() {
            if let Err(error) = log.append(row) {
                eprintln!("mindone_gauged log:{error}");
            }
        }
        let now = clock.now_ns()?;
        if now.saturating_sub(saved_ns) >= 600_000_000_000 {
            persist(root, &shadow);
            saved_ns = now;
        }
        if !waiter.wait(delay)? {
            break;
        }
    }
    persist(root, &shadow);
    Ok(())
}
