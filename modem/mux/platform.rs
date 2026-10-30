/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_mux_core::chat;
use mindone_mux_core::session::ChannelConfig;
use mindone_mux_core::transport::{Fault, IoResult};
use mindone_mux_runtime::{Effects, Endpoint, Factory, Interest, Limits, Ready, Report, Runtime, State};
use std::ffi::{c_char, c_int, CString};
use std::fs::File;
use std::io::{self, Read, Write};
use std::os::fd::{AsFd, AsRawFd, BorrowedFd, FromRawFd};
use std::os::unix::ffi::{OsStrExt, OsStringExt};
use std::path::{Path, PathBuf};
use std::collections::BTreeSet;
use std::sync::{Arc, Mutex};
use std::time::Instant;

pub const MAX_WAIT_MS: u64 = 1000;
pub const SERIAL_PATH: &str = "/dev/ttyC0";
pub const LINK_DIRECTORY: &str = "/dev/radio";

#[repr(C)]
#[derive(Default)]
struct LinkId { device: u64, inode: u64 }

#[repr(C)]
struct PollFd { fd: i32, interests: u32, events: u32 }

extern "C" {
    fn mindone_mux_errno_kind(error: c_int) -> c_int;
    fn mindone_mux_check_fd(fd: c_int) -> c_int;
    fn mindone_mux_open_serial(path: *const c_char) -> c_int;
    fn mindone_mux_open_directory(path: *const c_char) -> c_int;
    fn mindone_mux_open_radio_directory(path: *const c_char) -> c_int;
    fn mindone_mux_open_pty(path: *mut c_char, capacity: usize, radio: c_int) -> c_int;
    fn mindone_mux_publish(directory: c_int, name: *const c_char, target: *const c_char, identity: *mut LinkId) -> c_int;
    fn mindone_mux_unpublish(directory: c_int, name: *const c_char, target: *const c_char, identity: *const LinkId) -> c_int;
    fn mindone_mux_poll(entries: *mut PollFd, count: usize, timeout: c_int) -> c_int;
}

fn checked(value: c_int) -> io::Result<c_int> {
    if value < 0 { Err(io::Error::from_raw_os_error(-value)) } else { Ok(value) }
}

fn path_string(path: &Path) -> io::Result<CString> {
    CString::new(path.as_os_str().as_bytes()).map_err(|_| io::Error::new(io::ErrorKind::InvalidInput, "Path contains NUL"))
}

pub fn classify_error(error: &io::Error) -> Fault {
    let kind = unsafe { mindone_mux_errno_kind(error.raw_os_error().unwrap_or(0)) };
    match kind {
        1 => Fault::Interrupted,
        2 => Fault::WouldBlock,
        3 => Fault::Busy,
        4 => Fault::DeviceReset,
        _ => Fault::Failure,
    }
}

fn transferred(value: io::Result<usize>) -> IoResult {
    match value { Ok(bytes) => IoResult::Transferred(bytes), Err(error) => IoResult::Fault(classify_error(&error)) }
}

struct Publication {
    directory: Arc<File>,
    name: CString,
    target: CString,
    identity: LinkId,
    live: Arc<Mutex<BTreeSet<CString>>>,
}

impl Drop for Publication {
    fn drop(&mut self) {
        unsafe { mindone_mux_unpublish(self.directory.as_raw_fd(), self.name.as_ptr(), self.target.as_ptr(), &self.identity); }
        if let Ok(mut live) = self.live.lock() { live.remove(&self.name); }
    }
}

pub struct PosixEndpoint {
    publication: Option<Publication>,
    file: File,
    slave: Option<PathBuf>,
}

impl PosixEndpoint {
    pub fn open_serial(path: &Path) -> io::Result<Self> {
        let path = path_string(path)?;
        let descriptor = checked(unsafe { mindone_mux_open_serial(path.as_ptr()) })?;
        let file = unsafe { File::from_raw_fd(descriptor) };
        Ok(Self { publication: None, file, slave: None })
    }

    pub fn private_pty() -> io::Result<Self> { Self::pty(false) }

    fn pty(radio: bool) -> io::Result<Self> {
        let mut path = [0u8; 256];
        let descriptor = checked(unsafe { mindone_mux_open_pty(path.as_mut_ptr().cast(), path.len(), i32::from(radio)) })?;
        let file = unsafe { File::from_raw_fd(descriptor) };
        let length = path.iter().position(|byte| *byte == 0).ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "Unterminated PTY name"))?;
        let slave = PathBuf::from(std::ffi::OsString::from_vec(path[..length].to_vec()));
        Ok(Self { publication: None, file, slave: Some(slave) })
    }

    pub fn slave_path(&self) -> Option<&Path> { self.slave.as_deref() }
}

impl AsFd for PosixEndpoint {
    fn as_fd(&self) -> BorrowedFd<'_> { self.file.as_fd() }
}

impl Endpoint for PosixEndpoint {
    fn prepare(&self) -> io::Result<()> { checked(unsafe { mindone_mux_check_fd(self.file.as_raw_fd()) }).map(|_| ()) }
    fn read(&mut self, output: &mut [u8]) -> IoResult { transferred(self.file.read(output)) }
    fn write(&mut self, input: &[u8]) -> IoResult { transferred(self.file.write(input)) }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Mapping { pub config: ChannelConfig, pub name: String }

pub fn board_channels(sims: usize) -> io::Result<Vec<Mapping>> {
    const DLCI: [[u8; 11]; 4] = [
        [1, 2, 3, 4, 5, 26, 61, 60, 59, 58, 43],
        [6, 7, 8, 9, 10, 27, 57, 56, 55, 54, 42],
        [11, 12, 13, 14, 15, 28, 53, 52, 51, 50, 41],
        [16, 17, 18, 19, 20, 29, 49, 48, 47, 46, 40],
    ];
    const KINDS: [&str; 11] = ["cmd4", "noti", "cmd1", "cmd2", "cmd3", "", "cmd7", "cmd8", "cmd9", "cmd10", "cmd11"];
    if !(1..=4).contains(&sims) { return Err(io::Error::new(io::ErrorKind::InvalidInput, "SIM count must be 1..4")); }
    let mut result = Vec::new();
    for (sim, channels) in DLCI.iter().enumerate().take(sims) {
        for (kind, channel) in KINDS.iter().zip(channels) {
            let atci = kind.is_empty();
            let name = if atci { format!("atci{}", sim + 1) }
                else if sim == 0 { format!("ptty{kind}") }
                else { format!("ptty{}{kind}", sim + 1) };
            result.push(Mapping { config: ChannelConfig { channel: *channel, atci }, name });
        }
    }
    for (channel, name) in [(45, "pttynwcmd"), (44, "pttynwurc")] {
        result.push(Mapping { config: ChannelConfig { channel, atci: false }, name: name.to_owned() });
    }
    Ok(result)
}

enum LinkNamespace { Private, SharedRadio }

pub struct PtyFactory {
    directory: Arc<File>,
    mappings: Vec<Mapping>,
    radio: bool,
    live: Arc<Mutex<BTreeSet<CString>>>,
}

impl PtyFactory {
    pub fn new(directory: &Path, sims: usize, radio: bool) -> io::Result<Self> {
        Self::with_namespace(directory, sims, radio, LinkNamespace::Private)
    }
    pub fn radio_shared(sims: usize) -> io::Result<Self> {
        Self::with_namespace(Path::new(LINK_DIRECTORY), sims, true, LinkNamespace::SharedRadio)
    }
    fn with_namespace(directory: &Path, sims: usize, radio: bool, namespace: LinkNamespace) -> io::Result<Self> {
        let mappings = board_channels(sims)?;
        let path = path_string(directory)?;
        let descriptor = checked(match namespace {
            LinkNamespace::Private => unsafe { mindone_mux_open_directory(path.as_ptr()) },
            LinkNamespace::SharedRadio => unsafe { mindone_mux_open_radio_directory(path.as_ptr()) },
        })?;
        let directory = Arc::new(unsafe { File::from_raw_fd(descriptor) });
        Ok(Self { directory, mappings, radio, live: Arc::default() })
    }
    pub fn channels(&self) -> Vec<ChannelConfig> { self.mappings.iter().map(|mapping| mapping.config).collect() }
}

impl Factory<PosixEndpoint> for PtyFactory {
    fn open(&mut self, channel: u8) -> io::Result<PosixEndpoint> {
        let mapping = self.mappings.iter().find(|mapping| mapping.config.channel == channel)
            .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "Unmapped channel"))?;
        let name = CString::new(mapping.name.as_str()).map_err(|_| io::Error::new(io::ErrorKind::InvalidInput, "Invalid channel name"))?;
        let mut live = self.live.lock().map_err(|_| io::Error::other("Poisoned link registry"))?;
        if live.contains(&name) { return Err(io::Error::from_raw_os_error(17)); }
        let mut endpoint = PosixEndpoint::pty(self.radio)?;
        let target = path_string(endpoint.slave_path().ok_or_else(|| io::Error::other("Missing PTY path"))?)?;
        let mut identity = LinkId::default();
        checked(unsafe { mindone_mux_publish(self.directory.as_raw_fd(), name.as_ptr(), target.as_ptr(), &mut identity) })?;
        live.insert(name.clone());
        drop(live);
        endpoint.publication = Some(Publication { directory: Arc::clone(&self.directory), name, target, identity, live: Arc::clone(&self.live) });
        Ok(endpoint)
    }
}

pub fn wait_ready(interests: &[Interest], now: u64, deadline: Option<u64>, maximum: u64) -> io::Result<Vec<Ready>> {
    if interests.len() > mindone_mux_runtime::MAX_CHANNELS + 1 || maximum > MAX_WAIT_MS {
        return Err(io::Error::new(io::ErrorKind::InvalidInput, "Invalid poll bounds"));
    }
    let timeout = deadline.map_or(maximum, |end| maximum.min(end.saturating_sub(now))) as i32;
    let mut descriptors: Vec<_> = interests.iter().map(|interest| PollFd {
        fd: interest.token.descriptor(), interests: u32::from(interest.readable) | (u32::from(interest.writable) << 1), events: 0,
    }).collect();
    let result = checked(unsafe { mindone_mux_poll(descriptors.as_mut_ptr(), descriptors.len(), timeout) });
    if let Err(error) = result {
        return if classify_error(&error) == Fault::Interrupted { Ok(Vec::new()) } else { Err(error) };
    }
    let mut ready = Vec::new();
    for (descriptor, interest) in descriptors.iter().zip(interests) {
        if descriptor.events & 8 != 0 { return Err(io::Error::other("Poll descriptor is invalid")); }
        if descriptor.events != 0 {
            ready.push(Ready { token: interest.token,
                readable: descriptor.events & 1 != 0 && interest.readable,
                writable: descriptor.events & 2 != 0 && interest.writable,
                hangup: descriptor.events & 4 != 0 });
        }
    }
    Ok(ready)
}

pub struct Driver<P: Effects> {
    runtime: Runtime<PosixEndpoint, PtyFactory, P>,
    origin: Instant,
}

impl<P: Effects> Driver<P> {
    pub fn new(serial: PosixEndpoint, factory: PtyFactory, effects: P, config: chat::Config) -> Result<Self, mindone_mux_runtime::Error> {
        let channels = factory.channels();
        let origin = Instant::now();
        let runtime = Runtime::new(serial, factory, effects, &channels, config, 0)?;
        Ok(Self { runtime, origin })
    }
    pub fn enable_recovery(&mut self, escalation_after: u64) { self.runtime.enable_recovery(escalation_after); }
    pub fn recovery_cooldown(&self) -> u64 { self.runtime.recovery_cooldown() }
    pub fn runtime(&self) -> &Runtime<PosixEndpoint, PtyFactory, P> { &self.runtime }
    fn now(&self) -> io::Result<u64> {
        self.origin.elapsed().as_millis().try_into().map_err(|_| io::Error::other("Monotonic clock overflow"))
    }
    pub fn step(&mut self, maximum: u64, limits: Limits) -> io::Result<Report> {
        let result = (|| {
            let now = self.now()?;
            if matches!(self.runtime.state(), State::Closed | State::Failed) {
                return Ok(self.runtime.drive(now, &[], limits));
            }
            let interests = self.runtime.interests();
            let events = wait_ready(&interests, now, self.runtime.next_deadline(), maximum)?;
            Ok(self.runtime.drive(self.now()?, &events, limits))
        })();
        if result.is_err() { self.runtime.stop(); }
        result
    }
    pub fn stop(&mut self) { self.runtime.stop(); }
}
