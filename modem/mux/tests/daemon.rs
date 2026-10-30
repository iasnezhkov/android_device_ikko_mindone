/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */
use super::*;
use mindone_mux_core::{encode, Decoder, Frame, POLL_FINAL, UIH};
use mindone_mux_core::{mcc, transport::{Fault, IoResult}};
use mindone_mux_runtime::Endpoint;
use mindone_mux_service::{Property, Value};
use std::cell::{Cell, RefCell};
use std::collections::{BTreeMap, VecDeque};
use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::rc::Rc;
use std::time::{Duration, Instant};

include!("service_pty.rs");

#[derive(Clone)]
struct Recorded(Rc<RefCell<Vec<(Property, Value)>>>);
impl Properties for Recorded {
    fn set(&mut self, property: Property, value: Value) -> bool { self.0.borrow_mut().push((property, value)); true }
}
struct OpenFake { directory: PathBuf, peer: Rc<RefCell<Option<Modem>>> }
impl Opener for OpenFake {
    fn open(&mut self) -> io::Result<(PosixEndpoint, PtyFactory)> {
        let master = PosixEndpoint::private_pty()?;
        let serial = PosixEndpoint::open_serial(master.slave_path().unwrap())?;
        let mut modem = Modem::new(master);
        modem.bytes(b"+EIND: 128\r\n");
        *self.peer.borrow_mut() = Some(modem);
        Ok((serial, PtyFactory::new(&self.directory, 1, false)?))
    }
}

#[test]
fn daemon_loop_negotiates_transfers_recovers_and_stops_on_sigterm() {
    extern "C" { fn raise(number: c_int) -> c_int; }
    assert_eq!(unsafe { mindone_mux_initialize() }, 0);
    let directory = PathBuf::from(std::env::var_os("MINDONE_DAEMON_TEST_ROOT").unwrap());
    fs::set_permissions(&directory, fs::Permissions::from_mode(0o700)).unwrap();
    let peer = Rc::new(RefCell::new(None));
    let record = Recorded(Rc::default());
    let opener = OpenFake { directory: directory.clone(), peer: peer.clone() };
    let config = Config { frame_limit: 512, baud_index: 5, user_build: false };
    let mut service = Service::new(record.clone(), opener, MonotonicClock::default(), config).unwrap();
    service.daemon_mode(-12, 123);
    let mut clients = Vec::new();
    let mut targets = Vec::new();
    let mut received = vec![Vec::new(); 13];
    let mut stage = 0;
    let mut recovery_started = Instant::now();
    let start = Instant::now();
    let turns = Cell::new(0);
    let exit = run(service, || {
        turns.set(turns.get() + 1);
        assert!(start.elapsed() < Duration::from_secs(15), "daemon stalled at stage {stage}");
        let mut peer = peer.borrow_mut();
        let Some(modem) = peer.as_mut() else { return; };
        modem.pump();
        let mappings = mindone_mux_platform::board_channels(1).unwrap();
        if stage == 0 && record.0.borrow().contains(&(Property::RilReady, Value::One)) {
            assert_eq!(modem.commands, [b"AT".to_vec(), b"ATZ".to_vec(), b"ATE0".to_vec(), b"AT+CPIN=0123".to_vec(), b"AT+CMUX=0,0,-12,512".to_vec()]);
            assert_eq!(*record.0.borrow(), [(Property::ReporterRunning, Value::Zero), (Property::ModemException, Value::Zero), (Property::MuxReady, Value::One), (Property::RilReady, Value::One)]);
            assert_eq!(fs::read_dir(&directory).unwrap().count(), 13);
            for mapping in &mappings {
                let target = fs::read_link(directory.join(&mapping.name)).unwrap();
                let mut client = PosixEndpoint::open_serial(&target).unwrap();
                assert_eq!(client.write(&[mapping.config.channel, 0x55]), IoResult::Transferred(2));
                modem.frame(mapping.config.channel, UIH, &[mapping.config.channel, 0x66]);
                clients.push(client); targets.push(target);
            }
            stage = 1;
        }
        if stage == 1 || stage == 3 {
            for (index, client) in clients.iter_mut().enumerate() {
                let mut bytes = [0; 16];
                match client.read(&mut bytes) {
                    IoResult::Transferred(count) => received[index].extend_from_slice(&bytes[..count]),
                    IoResult::Fault(Fault::WouldBlock) => {},
                    other => panic!("client read {other:?}"),
                }
            }
            let done = mappings.iter().enumerate().all(|(index, mapping)| {
                let expected = if stage == 1 { vec![mapping.config.channel, 0x55] } else { vec![mapping.config.channel, 0x55, mapping.config.channel, 0x77] };
                modem.received.get(&mapping.config.channel) == Some(&expected) && received[index] == if stage == 1 { vec![mapping.config.channel, 0x66] } else { vec![mapping.config.channel, 0x66, mapping.config.channel, 0x88] }
            });
            if done && stage == 1 {
                modem.frame(1, 0x53, &[]); modem.frame(2, 0x1f, &[]);
                recovery_started = Instant::now(); stage = 2;
            } else if done {
                assert_eq!(unsafe { raise(15) }, 0);
                stage = 4;
            }
        }
        if stage == 2 && recovery_started.elapsed() >= Duration::from_millis(1250) {
            for (index, mapping) in mappings.iter().enumerate() {
                assert_eq!(fs::read_link(directory.join(&mapping.name)).unwrap(), targets[index]);
                assert_eq!(clients[index].write(&[mapping.config.channel, 0x77]), IoResult::Transferred(2));
                modem.frame(mapping.config.channel, UIH, &[mapping.config.channel, 0x88]);
            }
            stage = 3;
        }
        modem.pump();
    }, || unsafe { mindone_mux_signal() != 0 });
    assert_eq!(exit, Exit::Stopped);
    assert_eq!(stage, 4);
    assert_eq!(fs::read_dir(&directory).unwrap().count(), 0);
    assert!(turns.get() > 10);
}

#[test]
fn sim_discovery_matches_c_first_character_and_fallbacks() {
    for (boot, config, expected) in [("4garbage", "ss", 4), ("0", "dsda", 2), ("", "tsts", 3), ("9", "qsqs", 4), ("", "unknown", 1)] {
        assert_eq!(sims(boot, config), expected);
    }
}

#[test]
fn c_option_set_clustered_options_atoi_and_ignored_operands() {
    let values = ["mux", "-vdo", "-s", "/dev/example", "-n", "8", "-m", "basic", "-f0", "-f512junk", "-b-12", "-P123", "-r", "/tmp/radio", "operand"];
    let args = arguments(values.iter().map(OsString::from).collect()).unwrap_or_else(|_| panic!("C options rejected"));
    assert_eq!(args.serial, PathBuf::from("/dev/example"));
    assert_eq!(args.directory, PathBuf::from("/tmp/radio"));
    assert_eq!((args.frame, args.baud, args.pin, args.verbose), (512, -12, 123, true));
}

#[test]
fn recovery_retries_three_times_keeps_endpoints_and_limits_rebuilds() {
    use mindone_mux_core::session::{Action, ChannelConfig, Session, Sink};
    #[derive(Default)]
    struct Effects { opens: usize, closes: usize, sabms: Vec<u8>, ready: usize }
    impl Sink for Effects {
        fn open_endpoint(&mut self, _: u8) -> bool { self.opens += 1; true }
        fn apply(&mut self, action: Action<'_>) -> bool {
            match action {
                Action::Transmit(frame) if frame.control & !POLL_FINAL == 0x2f => self.sabms.push(frame.channel()),
                Action::CloseEndpoint(_) => self.closes += 1,
                Action::Ready { .. } => self.ready += 1,
                _ => {},
            }
            true
        }
    }
    let channels = [ChannelConfig { channel: 1, atci: false }];
    let mut session = Session::new(&channels, 512).unwrap();
    let mut effects = Effects::default();
    session.enable_recovery(0);
    session.begin(0, &mut effects).unwrap();
    for channel in [0, 1] { session.receive(Frame::command(channel, 0x73, &[]).unwrap(), &mut effects).unwrap(); }
    session.receive(Frame::command(1, 0x53, &[]).unwrap(), &mut effects).unwrap();
    assert!(session.channel(1).unwrap().endpoint_present);
    assert!(!session.can_read_endpoint(1, true));
    assert!(!session.recovery_tick(999, &mut effects).unwrap());
    for now in [1000, 2000, 3000] { assert!(!session.recovery_tick(now, &mut effects).unwrap()); }
    assert_eq!(effects.sabms, [0, 1, 1, 1, 1]);
    assert!(session.recovery_tick(4000, &mut effects).unwrap());
    assert_eq!(session.recovery_cooldown(4000), 300000);
    assert_eq!((effects.opens, effects.closes, effects.ready), (1, 0, 1));
    let mut next = Session::new(&channels, 512).unwrap();
    next.enable_recovery(300000);
    next.begin(0, &mut effects).unwrap();
    for channel in [0, 1] { next.receive(Frame::command(channel, 0x73, &[]).unwrap(), &mut effects).unwrap(); }
    next.receive(Frame::command(1, 0x1f, &[]).unwrap(), &mut effects).unwrap();
    for now in [1000, 2000, 3000, 4000, 299999] { assert!(!next.recovery_tick(now, &mut effects).unwrap()); }
    assert!(next.recovery_tick(300000, &mut effects).unwrap());
}

#[test]
fn daemon_session_rebuild_restarts_ril_once_after_new_setup() {
    let directory = PathBuf::from(std::env::var_os("MINDONE_DAEMON_TEST_ROOT").unwrap()).join("restart");
    fs::create_dir(&directory).unwrap();
    fs::set_permissions(&directory, fs::Permissions::from_mode(0o700)).unwrap();
    let peer = Rc::new(RefCell::new(None));
    let record = Recorded(Rc::default());
    let opener = OpenFake { directory: directory.clone(), peer: peer.clone() };
    let mut service = Service::new(record.clone(), opener, MonotonicClock::default(), Config { frame_limit: 512, baud_index: 5, user_build: false }).unwrap();
    service.daemon_mode(5, -1);
    let stop = Cell::new(false);
    let mut closed = false;
    let start = Instant::now();
    assert_eq!(run(service, || {
        assert!(start.elapsed() < Duration::from_secs(12));
        let mut peer = peer.borrow_mut();
        if let Some(modem) = peer.as_mut() {
            modem.pump();
            if !closed && record.0.borrow().contains(&(Property::RilReady, Value::One)) {
                modem.frame(0, UIH, &[0xc3, 1]); closed = true;
            }
            modem.pump();
        }
        if record.0.borrow().contains(&(Property::RilRestart, Value::One)) { stop.set(true); }
    }, || stop.get()), Exit::Stopped);
    let record = record.0.borrow();
    assert_eq!(record.iter().filter(|(key, _)| *key == Property::RilRestart).count(), 1);
    assert_eq!(record.iter().filter(|(key, _)| *key == Property::MuxReady).count(), 1);
    assert_eq!(record.iter().filter(|(key, _)| *key == Property::RilReady).count(), 1);
    assert_eq!(fs::read_dir(directory).unwrap().count(), 0);
}

#[test]
fn c_numeric_pin_and_entire_signed_baud_range_format_without_truncation() {
    use mindone_mux_core::chat::{Config, Controller, Pin};
    for baud in [i32::MIN, -1, 0, 256, i32::MAX] {
        let mut controller = Controller::new(Config { frame_limit: 512, baud_index: baud, user_build: false, pin: Pin::from_c_integer(i32::MAX) }, 0).unwrap();
        controller.feed(0, b"+EIND: 128\r\n").unwrap();
        let expected = ["AT\r".to_owned(), "ATZ\r".to_owned(), "ATE0\r".to_owned(), "AT+CPIN=2147483647\r".to_owned(), format!("AT+CMUX=0,0,{baud},512\r")];
        for command in expected {
            let request = controller.pending_write().unwrap();
            assert_eq!(request.bytes, command.as_bytes());
            let (token, count) = (request.token, request.bytes.len());
            controller.accept_written(0, token, count).unwrap();
            controller.feed(0, b"OK\r\n").unwrap();
        }
    }
    assert!(Pin::from_c_integer(-1).is_none());
}

#[allow(dead_code)]
#[path = "runtime_support.rs"]
mod runtime_support;

#[test]
fn exhausted_recovery_drains_disc_then_cld_and_closes_without_busy_deadline() {
    use mindone_mux_runtime::State;
    let mut h = runtime_support::Harness::new();
    h.runtime.enable_recovery(0);
    h.boot();
    h.modem.auto = false;
    h.modem.frame(1, 0x53, &[]);
    h.steps(8);
    assert_eq!(h.store.borrow().opens[&1], 1);
    for _ in 0..3 {
        h.now = h.runtime.next_deadline().unwrap();
        h.step(); h.steps(4);
    }
    assert_eq!(h.modem.frames.iter().filter(|(channel, control, _)| *channel == 1 && control & !POLL_FINAL == 0x2f).count(), 4);
    h.now = h.runtime.next_deadline().unwrap();
    h.step();
    assert_eq!(h.runtime.state(), State::Multiplexing);
    let close_deadline = h.runtime.next_deadline().unwrap();
    assert!(close_deadline > h.now);
    h.now = close_deadline;
    h.step(); h.modem.pump();
    assert_eq!(h.runtime.state(), State::Closed);
    assert!(h.modem.frames.iter().any(|(channel, control, _)| *channel == 26 && control & !POLL_FINAL == 0x43));
    assert!(h.modem.frames.iter().any(|(channel, _, payload)| *channel == 0 && payload == &[0xc3, 1]));
    assert!(h.runtime.recovery_cooldown() > 297000);
}
