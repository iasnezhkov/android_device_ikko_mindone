/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use crate::{lock, Clock, Config, Endpoint, Interest, MonotonicClock, Opener, Poller, Ready, Wake, DIRECTORY, OUTPUT_CAPACITY, TOPOLOGY};
use std::collections::BTreeMap;
use std::ffi::{c_char, c_int, CString};
use std::io;
use std::sync::{Arc, Mutex, Weak};
use std::time::Duration;

#[repr(C)]
struct PollFd { fd: c_int, interests: u32, events: u32 }

extern "C" {
    fn mindone_radio_open(path: *const c_char) -> c_int;
    fn mindone_radio_close(fd: c_int);
    fn mindone_radio_read(fd: c_int, bytes: *mut u8, length: usize) -> isize;
    fn mindone_radio_write(fd: c_int, bytes: *const u8, length: usize) -> isize;
    fn mindone_radio_pipe(descriptors: *mut c_int) -> c_int;
    fn mindone_radio_poll(entries: *mut PollFd, count: usize, timeout: c_int) -> c_int;
}

fn checked(value: isize) -> io::Result<usize> {
    if value < 0 { Err(io::Error::from_raw_os_error(-value as i32)) } else { Ok(value as usize) }
}

struct Descriptor(c_int);
impl Drop for Descriptor { fn drop(&mut self) { unsafe { mindone_radio_close(self.0); } } }
impl Descriptor {
    fn read(&self, bytes: &mut [u8]) -> io::Result<usize> {
        checked(unsafe { mindone_radio_read(self.0, bytes.as_mut_ptr(), bytes.len()) })
    }
    fn write(&self, bytes: &[u8]) -> io::Result<usize> {
        checked(unsafe { mindone_radio_write(self.0, bytes.as_ptr(), bytes.len()) })
    }
}

struct PosixEndpoint { descriptor: Arc<Descriptor>, hung_up: bool }
impl Endpoint for PosixEndpoint {
    fn read(&mut self, bytes: &mut [u8]) -> io::Result<usize> {
        let result = self.descriptor.read(bytes);
        if matches!(result, Ok(0)) { self.hung_up = true; }
        result
    }
    fn write(&mut self, bytes: &[u8]) -> io::Result<usize> { self.descriptor.write(bytes) }
    fn hung_up(&self) -> bool { self.hung_up }
}

type Registry = Arc<Mutex<BTreeMap<u8, Weak<Descriptor>>>>;
struct PosixOpener { registry: Registry }
impl Opener for PosixOpener {
    fn open(&mut self, directory: &str, name: &str) -> io::Result<Box<dyn Endpoint>> {
        let channel = TOPOLOGY.iter().find(|entry| entry.1 == name).map(|entry| entry.0)
            .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "Unknown endpoint"))?;
        let path = CString::new(format!("{directory}/{name}"))
            .map_err(|_| io::Error::new(io::ErrorKind::InvalidInput, "Path contains NUL"))?;
        let descriptor = Arc::new(Descriptor(checked(unsafe { mindone_radio_open(path.as_ptr()) } as isize)? as c_int));
        lock(&self.registry).insert(channel, Arc::downgrade(&descriptor));
        Ok(Box::new(PosixEndpoint { descriptor, hung_up: false }))
    }
}

struct PipeWake { writer: Descriptor, _reader: Arc<Descriptor> }
impl Wake for PipeWake {
    fn wake(&self) -> io::Result<()> {
        for _ in 0..4 {
            match self.writer.write(&[1]) {
                Ok(_) => return Ok(()),
                Err(error) if error.kind() == io::ErrorKind::WouldBlock => return Ok(()),
                Err(error) if error.kind() == io::ErrorKind::Interrupted => continue,
                Err(error) => return Err(error),
            }
        }
        Err(io::Error::from(io::ErrorKind::Interrupted))
    }
}

struct PosixPoller { registry: Registry, reader: Arc<Descriptor> }
impl Poller for PosixPoller {
    fn poll(&mut self, interests: &[Interest], timeout: Option<Duration>) -> io::Result<Vec<Ready>> {
        let registry = lock(&self.registry);
        let descriptors: Vec<_> = interests.iter().filter_map(|interest|
            registry.get(&interest.channel)?.upgrade().map(|descriptor| (*interest, descriptor))).collect();
        drop(registry);
        let mut entries = vec![PollFd { fd: self.reader.0, interests: 1, events: 0 }];
        for (interest, descriptor) in &descriptors {
            entries.push(PollFd { fd: descriptor.0,
                interests: u32::from(interest.readable) | (u32::from(interest.writable) << 1), events: 0 });
        }
        checked(unsafe { mindone_radio_poll(entries.as_mut_ptr(), entries.len(),
            timeout.map_or(-1, |timeout| timeout.as_millis().min(i32::MAX as u128) as c_int)) } as isize)?;
        if entries[0].events & 4 != 0 { return Err(io::Error::from(io::ErrorKind::BrokenPipe)); }
        if entries[0].events & 1 != 0 {
            let mut bytes = [0u8; 512];
            for _ in 0..4 {
                match self.reader.read(&mut bytes) {
                    Ok(0) => return Err(io::Error::from(io::ErrorKind::BrokenPipe)),
                    Ok(_) => (),
                    Err(error) if error.kind() == io::ErrorKind::WouldBlock => break,
                    Err(error) if error.kind() == io::ErrorKind::Interrupted => (),
                    Err(error) => return Err(error),
                }
            }
        }
        Ok(descriptors.iter().zip(entries.iter().skip(1)).filter_map(|((interest, _), entry)| {
            (entry.events != 0).then_some(Ready { channel: interest.channel,
                readable: entry.events & 1 != 0, writable: entry.events & 2 != 0, hung_up: entry.events & 4 != 0 })
        }).collect())
    }
}

pub fn config() -> io::Result<Config> {
    let mut descriptors = [-1; 2];
    checked(unsafe { mindone_radio_pipe(descriptors.as_mut_ptr()) } as isize)?;
    let registry = Arc::new(Mutex::new(BTreeMap::new()));
    let reader = Arc::new(Descriptor(descriptors[0]));
    let clock: Arc<dyn Clock> = Arc::new(MonotonicClock::default());
    Ok(Config { directory: DIRECTORY.into(), opener: Box::new(PosixOpener { registry: registry.clone() }),
        poller: Box::new(PosixPoller { registry, reader: reader.clone() }),
        wake: Arc::new(PipeWake { writer: Descriptor(descriptors[1]), _reader: reader }), clock, output_capacity: OUTPUT_CAPACITY })
}
