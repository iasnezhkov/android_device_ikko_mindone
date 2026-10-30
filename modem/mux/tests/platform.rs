#![forbid(unsafe_op_in_unsafe_fn)]
/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */


use mindone_mux_core::chat;
use mindone_mux_core::mcc;
use mindone_mux_core::transport::{EndpointId, Fault, IoResult, PollEpochs};
use mindone_mux_core::{encode, Decoder, Frame, POLL_FINAL, UIH};
use mindone_mux_platform::{board_channels, classify_error, wait_ready, Driver, PosixEndpoint, PtyFactory};
use mindone_mux_runtime::{Effects, Endpoint, Event, Factory, Interest, Limits, State};
use std::collections::{BTreeMap, VecDeque};
use std::fs;
use std::io;
use std::os::fd::{AsFd, AsRawFd};
use std::os::unix::fs::{symlink, MetadataExt, PermissionsExt};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Mutex, MutexGuard};
use std::time::Instant;

extern "C" {
    fn mindone_mux_test_interrupt_begin() -> i32;
    fn mindone_mux_test_interrupt_end() -> i32;
    fn mindone_mux_test_errno(kind: i32) -> i32;
}

static LOCK: Mutex<()> = Mutex::new(());
fn lock() -> MutexGuard<'static, ()> { LOCK.lock().unwrap_or_else(|error| error.into_inner()) }

struct Directory(PathBuf);

impl Directory {
    fn new() -> Self {
        static NEXT: AtomicU64 = AtomicU64::new(0);
        let base = PathBuf::from(std::env::var_os("MINDONE_PTY_TEST_ROOT").expect("Private fixture root is required"));
        let path = base.join(format!("case-{}", NEXT.fetch_add(1, Ordering::SeqCst)));
        fs::create_dir(&path).unwrap();
        fs::set_permissions(&path, fs::Permissions::from_mode(0o700)).unwrap();
        Self(path)
    }
    fn factory(&self) -> PtyFactory { PtyFactory::new(&self.0, 1, false).unwrap() }
}

impl Drop for Directory {
    fn drop(&mut self) { fs::remove_dir_all(&self.0).unwrap(); }
}

fn endpoint_interest(endpoint: &PosixEndpoint, read: bool, write: bool) -> Interest {
    let token = PollEpochs::default().replace(EndpointId::Serial, endpoint.as_fd().as_raw_fd()).unwrap();
    Interest { token, readable: read, writable: write }
}

fn pair() -> (PosixEndpoint, PosixEndpoint) {
    let master = PosixEndpoint::private_pty().unwrap();
    let slave = PosixEndpoint::open_serial(master.slave_path().unwrap()).unwrap();
    (master, slave)
}

fn read_available(endpoint: &mut PosixEndpoint) -> Vec<u8> {
    let mut result = Vec::new();
    for _ in 0..4096 {
        let mut output = [0; 8192];
        match endpoint.read(&mut output) {
            IoResult::Transferred(0) | IoResult::Fault(Fault::WouldBlock) => return result,
            IoResult::Transferred(bytes) => result.extend_from_slice(&output[..bytes]),
            IoResult::Fault(fault) => panic!("Unexpected read fault: {fault:?}"),
        }
    }
    panic!("Read fixture exceeded its bound");
}

fn write_all(endpoint: &mut PosixEndpoint, mut bytes: &[u8]) {
    for _ in 0..4096 {
        if bytes.is_empty() { return; }
        match endpoint.write(bytes) {
            IoResult::Transferred(0) => panic!("No write progress"),
            IoResult::Transferred(count) => bytes = &bytes[count..],
            IoResult::Fault(fault) => panic!("Unexpected write fault: {fault:?}"),
        }
    }
    panic!("Write fixture exceeded its bound");
}

#[test]
fn board_mapping_has_exact_order_names_atci_and_bounds() {
    let _guard = lock();
    let mapping = board_channels(4).unwrap();
    assert_eq!(mapping.len(), 46);
    assert_eq!(mapping[0].name, "pttycmd4");
    assert_eq!(mapping[5].name, "atci1");
    assert_eq!(mapping[5].config.channel, 26);
    assert!(mapping[5].config.atci);
    assert_eq!(mapping[11].name, "ptty2cmd4");
    assert_eq!(mapping[43].config.channel, 40);
    assert_eq!(mapping[44].name, "pttynwcmd");
    assert_eq!(mapping[44].config.channel, 45);
    assert_eq!(mapping[45].name, "pttynwurc");
    let mut channels: Vec<_> = mapping.iter().map(|entry| entry.config.channel).collect();
    channels.sort(); channels.dedup(); assert_eq!(channels.len(), 46);
    assert_eq!(mapping.iter().filter(|entry| entry.config.atci).count(), 4);
    for sims in 1..=4 { assert_eq!(board_channels(sims).unwrap().len(), sims * 11 + 2); }
    assert!(board_channels(0).is_err()); assert!(board_channels(5).is_err());
    for entry in mapping { println!("mapping {} {} {}", entry.config.channel, entry.name, entry.config.atci); }
}

#[test]
fn real_pty_is_raw_nonblocking_cloexec_private_and_bidirectional() {
    let _guard = lock();
    let (mut master, mut slave) = pair();
    master.prepare().unwrap(); slave.prepare().unwrap();
    assert_eq!(fs::metadata(master.slave_path().unwrap()).unwrap().mode() & 0o777, 0o600);
    let data: Vec<_> = (0..=255).cycle().take(1024).collect();
    for chunk in data.chunks(128) {
        write_all(&mut master, chunk);
        assert_eq!(read_available(&mut slave), chunk);
        write_all(&mut slave, chunk);
        assert_eq!(read_available(&mut master), chunk);
    }
    assert_eq!(master.read(&mut [0; 16]), IoResult::Fault(Fault::WouldBlock));
    assert_eq!(slave.read(&mut [0; 16]), IoResult::Fault(Fault::WouldBlock));
}

#[test]
fn serial_ownership_refuses_competing_open_and_releases_on_drop() {
    let _guard = lock();
    let master = PosixEndpoint::private_pty().unwrap();
    let path = master.slave_path().unwrap();
    let first = PosixEndpoint::open_serial(path).unwrap();
    assert!(PosixEndpoint::open_serial(path).is_err());
    drop(first);
    PosixEndpoint::open_serial(path).unwrap();
}

#[test]
fn serial_refuses_regular_file_symlink_and_nul() {
    let _guard = lock();
    let directory = Directory::new();
    let file = directory.0.join("file");
    fs::write(&file, b"unchanged").unwrap();
    assert!(PosixEndpoint::open_serial(&file).is_err());
    assert_eq!(fs::read(&file).unwrap(), b"unchanged");
    let master = PosixEndpoint::private_pty().unwrap();
    let link = directory.0.join("serial");
    symlink(master.slave_path().unwrap(), &link).unwrap();
    assert!(PosixEndpoint::open_serial(&link).is_err());
    assert!(PosixEndpoint::open_serial(Path::new("bad\0path")).is_err());
}

#[test]
fn factory_publishes_cleans_and_reopens_all_board_channels() {
    let _guard = lock();
    let directory = Directory::new();
    let mut factory = PtyFactory::new(&directory.0, 4, false).unwrap();
    let mappings = board_channels(4).unwrap();
    for mapping in mappings {
        let endpoint = factory.open(mapping.config.channel).unwrap();
        endpoint.prepare().unwrap();
        let link = directory.0.join(&mapping.name);
        assert_eq!(fs::read_link(&link).unwrap(), endpoint.slave_path().unwrap());
        assert!(factory.open(mapping.config.channel).is_err());
        drop(endpoint);
        assert!(fs::symlink_metadata(&link).is_err());
        drop(factory.open(mapping.config.channel).unwrap());
        assert!(fs::symlink_metadata(&link).is_err());
    }
    assert!(factory.open(0).is_err()); assert!(factory.open(63).is_err());
    assert_eq!(fs::read_dir(&directory.0).unwrap().count(), 0);
}

#[test]
fn factory_preserves_existing_file_and_replacement_link() {
    let _guard = lock();
    let directory = Directory::new();
    let mut factory = directory.factory();
    let link = directory.0.join("pttycmd4");
    fs::write(&link, b"existing").unwrap();
    assert!(factory.open(1).is_err());
    assert_eq!(fs::read(&link).unwrap(), b"existing");
    fs::remove_file(&link).unwrap();
    let endpoint = factory.open(1).unwrap();
    fs::remove_file(&link).unwrap();
    symlink("replacement", &link).unwrap();
    drop(endpoint);
    assert_eq!(fs::read_link(&link).unwrap(), Path::new("replacement"));
}

#[test]
fn factory_replaces_a_stale_link_left_by_a_killed_instance() {
    let _guard = lock();
    let directory = Directory::new();
    let mut factory = directory.factory();
    let link = directory.0.join("pttycmd4");
    symlink("/dev/pts/stale-from-killed-mux", &link).unwrap();
    let endpoint = factory.open(1).unwrap();
    assert_eq!(fs::read_link(&link).unwrap(), endpoint.slave_path().unwrap());
    assert_eq!(fs::read_dir(&directory.0).unwrap().count(), 1);
    drop(endpoint);
    assert!(fs::symlink_metadata(&link).is_err());
    assert_eq!(fs::read_dir(&directory.0).unwrap().count(), 0);
}

#[test]
fn factory_pins_directory_across_path_replacement() {
    let _guard = lock();
    let directory = Directory::new();
    let original = directory.0.join("original");
    fs::create_dir(&original).unwrap();
    let mut factory = PtyFactory::new(&original, 1, false).unwrap();
    let moved = directory.0.join("moved");
    fs::rename(&original, &moved).unwrap();
    fs::create_dir(&original).unwrap();
    let replacement = original.join("pttycmd4");
    fs::write(&replacement, b"preserved").unwrap();
    let endpoint = factory.open(1).unwrap();
    assert!(moved.join("pttycmd4").is_symlink());
    drop(endpoint);
    assert!(!moved.join("pttycmd4").is_symlink());
    assert_eq!(fs::read(replacement).unwrap(), b"preserved");
}

#[test]
fn factory_refuses_untrusted_directory_and_symlink() {
    let _guard = lock();
    let directory = Directory::new();
    fs::set_permissions(&directory.0, fs::Permissions::from_mode(0o777)).unwrap();
    assert!(PtyFactory::new(&directory.0, 1, false).is_err());
    fs::set_permissions(&directory.0, fs::Permissions::from_mode(0o700)).unwrap();
    let link = directory.0.join("alias");
    symlink(&directory.0, &link).unwrap();
    assert!(PtyFactory::new(&link, 1, false).is_err());
}

#[test]
fn poll_preserves_generation_and_maps_readiness() {
    let _guard = lock();
    let (master, mut slave) = pair();
    let interest = endpoint_interest(&master, true, false);
    assert!(wait_ready(&[interest], 0, None, 0).unwrap().is_empty());
    write_all(&mut slave, b"readiness");
    let ready = wait_ready(&[interest], 0, None, 10).unwrap();
    assert_eq!(ready.len(), 1);
    assert_eq!(ready[0].token, interest.token);
    assert!(ready[0].readable); assert!(!ready[0].writable); assert!(!ready[0].hangup);
    let writable = wait_ready(&[endpoint_interest(&slave, false, true)], 0, None, 10).unwrap();
    assert!(writable[0].writable); assert!(!writable[0].readable);
}

#[test]
fn poll_deadline_and_maximum_are_bounded_without_spin() {
    let _guard = lock();
    let start = Instant::now();
    assert!(wait_ready(&[], 100, Some(120), 1000).unwrap().is_empty());
    assert!(start.elapsed().as_millis() >= 15);
    assert!(start.elapsed().as_millis() < 500);
    let start = Instant::now();
    assert!(wait_ready(&[], 100, Some(99), 1000).unwrap().is_empty());
    assert!(start.elapsed().as_millis() < 200);
    let start = Instant::now();
    wait_ready(&[], 100, None, 3).unwrap();
    assert!(start.elapsed().as_millis() >= 2);
    assert!(start.elapsed().as_millis() < 200);
    assert!(wait_ready(&[], 0, None, 1001).is_err());
    let (master, _slave) = pair();
    assert!(wait_ready(&vec![endpoint_interest(&master, true, false); 48], 0, None, 0).is_err());
}

#[test]
fn poll_hangup_and_invalid_descriptor_are_observed() {
    let _guard = lock();
    let (master, slave) = pair();
    let interest = endpoint_interest(&slave, true, false);
    drop(master);
    let ready = wait_ready(&[interest], 0, None, 50).unwrap();
    assert_eq!(ready.len(), 1);
    assert!(ready[0].hangup);
    drop(slave);
    assert!(wait_ready(&[interest], 0, None, 0).is_err());
}

#[test]
fn poll_interruption_returns_control_after_one_signal() {
    let _guard = lock();
    assert_eq!(unsafe { mindone_mux_test_interrupt_begin() }, 0);
    let start = Instant::now();
    let result = wait_ready(&[], 0, None, 1000);
    let elapsed = start.elapsed();
    assert_eq!(unsafe { mindone_mux_test_interrupt_end() }, 1);
    assert!(result.unwrap().is_empty());
    assert!(elapsed.as_millis() < 500);
}

#[test]
fn errno_classes_preserve_modem_specific_faults() {
    let _guard = lock();
    for (kind, expected) in [Fault::Interrupted, Fault::WouldBlock, Fault::Busy, Fault::DeviceReset, Fault::Failure, Fault::Failure, Fault::DeviceReset].into_iter().enumerate() {
        let error = io::Error::from_raw_os_error(unsafe { mindone_mux_test_errno(kind as i32) });
        assert_eq!(classify_error(&error), expected);
    }
    assert_eq!(classify_error(&io::Error::other("unclassified")), Fault::Failure);
}

#[test]
fn actual_pty_short_writes_and_backpressure_preserve_accepted_bytes() {
    let _guard = lock();
    let (mut master, mut slave) = pair();
    let block: Vec<_> = (0..=255).cycle().take(65536).collect();
    let mut accepted = Vec::new();
    let mut short = false;
    let mut blocked = false;
    for _ in 0..4096 {
        match slave.write(&block) {
            IoResult::Transferred(0) => panic!("Zero write"),
            IoResult::Transferred(bytes) => { short |= bytes < block.len(); accepted.extend_from_slice(&block[..bytes]); }
            IoResult::Fault(Fault::WouldBlock) => { blocked = true; break; }
            IoResult::Fault(fault) => panic!("Unexpected backpressure fault: {fault:?}"),
        }
    }
    assert!(blocked); assert!(short); assert!(!accepted.is_empty());
    let mut observed = Vec::new();
    for _ in 0..4096 {
        observed.extend(read_available(&mut master));
        if observed.len() == accepted.len() { break; }
        wait_ready(&[endpoint_interest(&master, true, false)], 0, None, 1).unwrap();
    }
    assert_eq!(observed, accepted);
    write_all(&mut slave, b"resumed");
    assert_eq!(read_available(&mut master), b"resumed");
    println!("pty-backpressure accepted={} short_write=true would_block=true", accepted.len());
}

#[derive(Default)]
struct Publications(Vec<Event>);
impl Effects for Publications {
    fn apply(&mut self, event: Event) -> bool { self.0.push(event); true }
}

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

struct Harness {
    driver: Driver<Publications>,
    modem: Modem,
    directory: Directory,
}

impl Harness {
    fn new() -> Self {
        let directory = Directory::new();
        let (modem, serial) = pair();
        let factory = directory.factory();
        let config = chat::Config { frame_limit: 512, baud_index: 5, user_build: false, pin: None };
        let driver = Driver::new(serial, factory, Publications::default(), config).unwrap();
        let mut result = Self { driver, modem: Modem::new(modem), directory };
        result.modem.bytes(b"+EIND: 128\r\n");
        for _ in 0..4096 {
            result.turn();
            if result.driver.runtime().effects().0.iter().any(|event| matches!(event, Event::Ready { .. })) { return result; }
        }
        panic!("PTY startup exceeded its bound: {:?} {:?}", result.driver.runtime().state(), result.modem.commands);
    }
    fn turn(&mut self) {
        self.modem.pump();
        let report = self.driver.step(0, Limits::default()).unwrap();
        assert!(report.error.is_none(), "{report:?}");
        assert!(report.calls <= 128);
        self.modem.pump();
    }
    fn client(&self, name: &str) -> PosixEndpoint {
        PosixEndpoint::open_serial(&fs::read_link(self.directory.0.join(name)).unwrap()).unwrap()
    }
}

#[test]
fn actual_pty_runtime_startup_and_bidirectional_framing() {
    let _guard = lock();
    let mut harness = Harness::new();
    assert_eq!(harness.driver.runtime().state(), State::Multiplexing);
    assert_eq!(harness.modem.commands.last().unwrap(), b"AT+CMUX=0,0,5,512");
    assert_eq!(fs::read_dir(&harness.directory.0).unwrap().count(), 13);
    let mut client = harness.client("pttycmd4");
    let payload: Vec<_> = (0..=255).cycle().take(32768).collect();
    for chunk in payload.chunks(257) {
        write_all(&mut client, chunk);
        for _ in 0..4 { harness.turn(); }
    }
    for _ in 0..4096 {
        if harness.modem.received.get(&1) == Some(&payload) { break; }
        harness.turn();
    }
    assert_eq!(harness.modem.received.get(&1), Some(&payload));
    let mut observed = Vec::new();
    for chunk in payload.chunks(251) {
        harness.modem.frame(1, UIH, chunk);
        for _ in 0..4 { harness.turn(); }
        observed.extend(read_available(&mut client));
    }
    assert_eq!(observed, payload);
    harness.driver.stop();
    assert_eq!(harness.driver.runtime().state(), State::Closed);
    assert_eq!(fs::read_dir(&harness.directory.0).unwrap().count(), 0);
    println!("pty-runtime uplink_bytes={} downlink_bytes={} channels=13", payload.len(), observed.len());
}

#[test]
fn actual_pty_hangup_recreates_endpoint_and_advances_generation() {
    let _guard = lock();
    let mut harness = Harness::new();
    let client = harness.client("pttycmd4");
    let old = harness.driver.runtime().interests().into_iter().find(|entry| entry.token.endpoint() == EndpointId::Channel(1)).unwrap();
    drop(client);
    let mut current = old;
    for _ in 0..128 {
        harness.turn();
        current = harness.driver.runtime().interests().into_iter().find(|entry| entry.token.endpoint() == EndpointId::Channel(1)).unwrap();
        if current.token != old.token { break; }
    }
    assert_ne!(current.token, old.token);
    let mut client = harness.client("pttycmd4");
    write_all(&mut client, b"after-reopen");
    for _ in 0..128 { harness.turn(); }
    assert_eq!(harness.modem.received.get(&1).unwrap(), b"after-reopen");
}

#[test]
fn actual_pty_peer_close_flushes_reply_and_cleans_links() {
    let _guard = lock();
    let mut harness = Harness::new();
    harness.modem.frame(0, UIH, &[0xc3, 1]);
    harness.modem.pump();
    for _ in 0..128 {
        let report = harness.driver.step(0, Limits::default()).unwrap();
        assert!(report.error.is_none(), "{report:?}");
        if report.state == State::Closed { break; }
    }
    assert_eq!(harness.driver.runtime().state(), State::Closed);
    assert_eq!(fs::read_dir(&harness.directory.0).unwrap().count(), 0);
}

#[test]
fn driver_error_drops_owned_descriptors_and_published_links() {
    let _guard = lock();
    let mut harness = Harness::new();
    assert!(harness.driver.step(1001, Limits::default()).is_err());
    assert_eq!(harness.driver.runtime().state(), State::Closed);
    assert_eq!(fs::read_dir(&harness.directory.0).unwrap().count(), 0);
}

#[test]
fn endpoint_drop_and_failed_publication_do_not_leak_descriptors() {
    let _guard = lock();
    let descriptors = if cfg!(target_os = "linux") { "/proc/self/fd" } else { "/dev/fd" };
    let directory = Directory::new();
    let count = || fs::read_dir(descriptors).unwrap().count();
    let before = count();
    {
        let mut factory = directory.factory();
        for _ in 0..256 {
            let endpoint = factory.open(1).unwrap();
            assert!(factory.open(1).is_err());
            drop(endpoint);
        }
    }
    assert_eq!(count(), before);
    assert_eq!(fs::read_dir(&directory.0).unwrap().count(), 0);
}

#[test]
fn terminal_driver_returns_without_poll_wait() {
    let _guard = lock();
    let mut harness = Harness::new();
    harness.driver.stop();
    let start = Instant::now();
    assert_eq!(harness.driver.step(1000, Limits::default()).unwrap().state, State::Closed);
    assert!(start.elapsed().as_millis() < 500);
}

#[test]
fn cleanup_preserves_a_replacement_symlink_to_the_same_slave() {
    let _guard = lock();
    let directory = Directory::new();
    let mut factory = directory.factory();
    let endpoint = factory.open(1).unwrap();
    let link = directory.0.join("pttycmd4");
    let target = fs::read_link(&link).unwrap();
    fs::rename(&link, directory.0.join("original")).unwrap();
    symlink(&target, &link).unwrap();
    drop(endpoint);
    assert_eq!(fs::read_link(link).unwrap(), target);
}

#[test]
fn actual_serial_hangup_terminates_runtime_and_cleans_links() {
    let _guard = lock();
    let mut harness = Harness::new();
    drop(harness.modem);
    for _ in 0..128 {
        let report = harness.driver.step(1, Limits::default()).unwrap();
        if report.state == State::Failed { break; }
    }
    assert_eq!(harness.driver.runtime().state(), State::Failed);
    assert_eq!(fs::read_dir(&harness.directory.0).unwrap().count(), 0);
}

#[test]
fn fresh_pty_waits_for_first_client_without_hangup_or_spin() {
    let _guard = lock();
    let endpoint = PosixEndpoint::private_pty().unwrap();
    let start = Instant::now();
    assert!(wait_ready(&[endpoint_interest(&endpoint, true, false)], 0, None, 10).unwrap().is_empty());
    assert!(start.elapsed().as_millis() >= 8);
    assert!(start.elapsed().as_millis() < 200);
}
