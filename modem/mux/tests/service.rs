/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use super::*;
use mindone_mux_core::mcc;
use mindone_mux_core::session::ChannelConfig;
use mindone_mux_core::transport::{Fault, IoResult};
use mindone_mux_core::{encode, Decoder, Frame, POLL_FINAL, UIH};
use mindone_mux_runtime::{Endpoint, Ready, Runtime};
use std::cell::Cell;
use std::collections::{BTreeMap, VecDeque};
use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::os::unix::net::UnixStream;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};

#[allow(dead_code)]
#[path = "runtime_support.rs"]
mod runtime_support;

include!("service_pty.rs");

#[derive(Clone)]
struct Recorded {
    values: Rc<RefCell<Vec<(Property, Value)>>>,
    fail_at: Rc<Cell<Option<usize>>>,
    directory: PathBuf,
    peer: Rc<RefCell<Option<Modem>>>,
    time: Rc<Cell<u64>>,
    report_delay: Rc<Cell<u64>>,
}

impl Properties for Recorded {
    fn set(&mut self, property: Property, value: Value) -> bool {
        if property == Property::ReportCase {
            assert_eq!(fs::read_dir(&self.directory).unwrap().count(), 0, "recovery preceded endpoint cleanup");
            if let Some(modem) = self.peer.borrow().as_ref() {
                let acquired = PosixEndpoint::open_serial(modem.endpoint.slave_path().unwrap());
                assert!(acquired.is_ok(), "recovery preceded serial descriptor release");
            }
        }
        if property == Property::Report { self.time.set(self.time.get() + self.report_delay.get()); }
        let index = self.values.borrow().len();
        self.values.borrow_mut().push((property, value));
        self.fail_at.get() != Some(index)
    }
}

struct OpenPrivate {
    directory: PathBuf,
    peer: Rc<RefCell<Option<Modem>>>,
    opens: Rc<Cell<usize>>,
    fail: Rc<Cell<bool>>,
}

impl Opener for OpenPrivate {
    fn open(&mut self) -> io::Result<(PosixEndpoint, PtyFactory)> {
        self.opens.set(self.opens.get() + 1);
        if self.fail.get() { return Err(io::Error::other("Injected open failure")); }
        let master = PosixEndpoint::private_pty()?;
        let serial = PosixEndpoint::open_serial(master.slave_path().unwrap())?;
        let mut modem = Modem::new(master);
        modem.bytes(b"+EIND: 128\r\n");
        *self.peer.borrow_mut() = Some(modem);
        Ok((serial, PtyFactory::new(&self.directory, 1, false)?))
    }
}

#[derive(Clone)]
struct ManualClock(Rc<Cell<u64>>);
impl Clock for ManualClock {
    fn now_ms(&self) -> io::Result<u64> { Ok(self.0.get()) }
}

struct Harness {
    owner: Service<Recorded, OpenPrivate, ManualClock>,
    record: Recorded,
    clock: ManualClock,
    opens: Rc<Cell<usize>>,
    fail_open: Rc<Cell<bool>>,
}

impl Harness {
    fn new() -> Self {
        static NEXT: AtomicU64 = AtomicU64::new(0);
        let directory = PathBuf::from(std::env::var_os("MINDONE_SERVICE_TEST_ROOT").unwrap())
            .join(format!("case-{}", NEXT.fetch_add(1, Ordering::Relaxed)));
        fs::create_dir(&directory).unwrap();
        fs::set_permissions(&directory, fs::Permissions::from_mode(0o700)).unwrap();
        let clock = ManualClock(Rc::new(Cell::new(0)));
        let record = Recorded { values: Rc::default(), fail_at: Rc::new(Cell::new(None)), directory: directory.clone(), peer: Rc::default(),
            time: clock.0.clone(), report_delay: Rc::new(Cell::new(0)) };
        let opens = Rc::new(Cell::new(0));
        let fail_open = Rc::new(Cell::new(false));
        let opener = OpenPrivate { directory, peer: record.peer.clone(), opens: opens.clone(), fail: fail_open.clone() };
        let config = Config { frame_limit: 512, baud_index: 5, user_build: false };
        let owner = Service::new(record.clone(), opener, clock.clone(), config).unwrap();
        Self { owner, record, clock, opens, fail_open }
    }
    fn pump(&mut self) {
        if let Some(modem) = self.record.peer.borrow_mut().as_mut() { modem.pump(); }
    }
    fn turn(&mut self) -> Phase {
        self.pump();
        let result = self.owner.step(0, Limits::default());
        if result == Phase::Running { self.pump(); }
        result
    }
    fn boot(&mut self) {
        for _ in 0..4096 {
            let phase = self.turn();
            assert!(!matches!(phase, Phase::Finished(_)), "unexpected early exit: {phase:?}");
            if self.record.values.borrow().contains(&(Property::RilReady, Value::One)) { return; }
        }
        panic!("bounded private PTY setup did not finish");
    }
    fn until(&mut self, expected: fn(Phase) -> bool) -> Phase {
        for _ in 0..512 {
            let phase = self.turn();
            if expected(phase) { return phase; }
        }
        panic!("bounded transition did not finish: {:?}", self.owner.phase());
    }
    fn malformed(&mut self) {
        self.record.peer.borrow_mut().as_mut().unwrap().frame(0, UIH, &[0xc3, 1, 0]);
    }
    fn peer_close(&mut self) {
        self.record.peer.borrow_mut().as_mut().unwrap().frame(0, UIH, &[0xc3, 1]);
    }
    fn values(&self) -> Vec<(Property, Value)> { self.record.values.borrow().clone() }
}

fn startup_ready() -> Vec<(Property, Value)> {
    vec![(Property::ReporterRunning, Value::Zero), (Property::ModemException, Value::Zero),
         (Property::MuxReady, Value::One), (Property::RilReady, Value::One)]
}

#[test]
fn private_pty_ready_peer_close_and_bounded_restart_keep_process_publications() {
    let mut h = Harness::new();
    h.boot();
    assert_eq!(h.values(), startup_ready());
    assert_eq!(fs::read_dir(&h.record.directory).unwrap().count(), 13);
    h.peer_close();
    assert_eq!(h.until(|phase| matches!(phase, Phase::Restarting { .. })), Phase::Restarting { deadline: 5000 });
    assert_eq!(fs::read_dir(&h.record.directory).unwrap().count(), 0);
    assert_eq!(h.owner.runtime_state(), None);
    h.clock.0.set(4999);
    for _ in 0..16 { assert_eq!(h.owner.step(0, Limits::default()), Phase::Restarting { deadline: 5000 }); }
    assert_eq!(h.opens.get(), 1);
    h.clock.0.set(5000);
    assert_eq!(h.owner.step(0, Limits::default()), Phase::Running);
    for _ in 0..128 { h.turn(); }
    assert_eq!(h.opens.get(), 2);
    assert_eq!(h.owner.runtime_state(), Some(State::Multiplexing));
    assert_eq!(fs::read_dir(&h.record.directory).unwrap().count(), 13);
    assert_eq!(h.values(), startup_ready());
    h.owner.stop();
    assert_eq!(fs::read_dir(&h.record.directory).unwrap().count(), 0);
}

#[test]
fn ordinary_runtime_failure_cleans_before_two_timed_recovery_requests() {
    let mut h = Harness::new();
    h.boot(); h.malformed();
    assert_eq!(h.until(|phase| matches!(phase, Phase::Recovering { .. })), Phase::Recovering { deadline: 5000 });
    assert_eq!(h.owner.runtime_error(), Some(Error::Mcc(mcc::Error::TrailingBytes)));
    let mut expected = startup_ready();
    expected.extend([(Property::ReportCase, Value::One), (Property::Report, Value::One)]);
    assert_eq!(h.values(), expected);
    h.clock.0.set(4999);
    for _ in 0..16 { h.owner.step(0, Limits::default()); }
    assert_eq!(h.values(), expected);
    h.clock.0.set(5000);
    assert_eq!(h.owner.step(0, Limits::default()), Phase::Finished(Exit::RecoveryRequested));
    expected.extend([(Property::ReportCase, Value::Two), (Property::Report, Value::One)]);
    assert_eq!(h.values(), expected);
    for _ in 0..16 { h.owner.step(0, Limits::default()); }
    assert_eq!(h.values(), expected);
    assert_eq!(h.opens.get(), 1);
}

#[test]
fn explicit_stop_before_start_during_active_and_during_both_backoffs_is_final() {
    for mode in 0..4 {
        let mut h = Harness::new();
        if mode > 0 { h.boot(); }
        if mode == 2 { h.peer_close(); h.until(|phase| matches!(phase, Phase::Restarting { .. })); }
        if mode == 3 { h.malformed(); h.until(|phase| matches!(phase, Phase::Recovering { .. })); }
        let before = h.values();
        let opened = h.opens.get();
        h.owner.stop();
        h.clock.0.set(10000);
        for _ in 0..16 { assert_eq!(h.owner.step(0, Limits::default()), Phase::Finished(Exit::Stopped)); }
        assert_eq!(h.values(), before);
        assert_eq!(h.opens.get(), opened);
        assert_eq!(fs::read_dir(&h.record.directory).unwrap().count(), 0);
    }
}

#[test]
fn every_property_failure_stops_without_later_publication_or_retry() {
    for rejected in 0..8 {
        let mut h = Harness::new();
        h.record.fail_at.set(Some(rejected));
        if rejected < 4 {
            h.until(|phase| matches!(phase, Phase::Finished(_)));
        } else {
            h.boot(); h.malformed();
            h.until(|phase| matches!(phase, Phase::Finished(_) | Phase::Recovering { .. }));
            if rejected >= 6 { h.clock.0.set(5000); h.owner.step(0, Limits::default()); }
        }
        assert_eq!(h.owner.phase(), Phase::Finished(Exit::PropertyRejected));
        assert_eq!(h.values().len(), rejected + 1);
        assert_eq!(fs::read_dir(&h.record.directory).unwrap().count(), 0);
        let before = h.values();
        let opened = h.opens.get();
        h.clock.0.set(10000);
        for _ in 0..16 { h.owner.step(0, Limits::default()); }
        assert_eq!(h.values(), before);
        assert_eq!(h.opens.get(), opened);
    }
}

#[test]
fn initial_and_restart_open_failures_do_not_spin_or_reset_process_properties() {
    for restarted in [false, true] {
        let mut h = Harness::new();
        if restarted {
            h.boot(); h.peer_close();
            h.until(|phase| matches!(phase, Phase::Restarting { .. }));
            h.clock.0.set(5000);
        }
        h.fail_open.set(true);
        let phase = h.owner.step(0, Limits::default());
        assert_eq!(phase, Phase::Recovering { deadline: if restarted { 10000 } else { 5000 } });
        let opened = h.opens.get();
        for _ in 0..16 { h.owner.step(0, Limits::default()); }
        assert_eq!(h.opens.get(), opened);
        assert_eq!(h.values().iter().filter(|(property, _)| *property == Property::ReporterRunning).count(), 1);
        h.owner.stop();
    }
}

#[test]
fn invalid_wait_clock_regression_and_overflow_close_without_recovery() {
    for mode in 0..3 {
        let mut h = Harness::new();
        h.boot();
        let before = h.values();
        match mode {
            0 => { h.owner.step(MAX_WAIT_MS + 1, Limits::default()); }
            1 => {
                h.clock.0.set(1); h.turn(); h.clock.0.set(0); h.turn();
            }
            _ => {
                h.clock.0.set(u64::MAX); h.malformed();
                h.until(|phase| matches!(phase, Phase::Finished(_)));
            }
        }
        assert_eq!(h.owner.phase(), Phase::Finished(Exit::InternalFailure));
        assert_eq!(h.values(), before);
        assert_eq!(fs::read_dir(&h.record.directory).unwrap().count(), 0);
    }
}

#[test]
fn dropping_active_owner_cleans_without_recovery() {
    let mut h = Harness::new();
    h.boot();
    let record = h.record.clone();
    let before = h.values();
    drop(h);
    assert_eq!(*record.values.borrow(), before);
    assert_eq!(fs::read_dir(&record.directory).unwrap().count(), 0);
}

#[test]
fn startup_reset_read_requests_recovery_but_write_sets_exception_with_exact_origin() {
    use runtime_support::{Controlled, Endpoints, SharedPlan, Step};
    for writing in [false, true] {
        let mut h = Harness::new();
        assert!(h.owner.publication.initialize());
        let (serial, peer) = UnixStream::pair().unwrap();
        let plan = SharedPlan::default();
        let mut runtime = Runtime::new(Controlled::new(serial, plan.clone()), Endpoints(Rc::default()),
            h.owner.publication.clone(), &[ChannelConfig { channel: 1, atci: false }], h.owner.config.chat(), 0).unwrap();
        let now = if writing { 20000 } else { 0 };
        if writing { plan.borrow_mut().writes.push_back(Step::Fault(Fault::DeviceReset)); }
        else { plan.borrow_mut().reads.push_back(Step::Fault(Fault::DeviceReset)); }
        let token = runtime.interests()[0].token;
        let report = runtime.drive(now, &[Ready { token, readable: !writing, writable: writing, hangup: false }], Limits::default());
        assert_eq!(report.state, State::Failed);
        assert_eq!(report.error, Some(if writing { Error::Chat(chat::Error::DeviceReset) } else { Error::ChatRead(chat::Error::DeviceReset) }));
        assert!(runtime.interests().is_empty());
        let mut peer = peer;
        assert_eq!(std::io::Read::read(&mut peer, &mut [0; 1]).unwrap(), 0);
        h.owner.observed(0, report);
        assert_eq!(h.owner.phase(), if writing { Phase::Finished(Exit::ModemException) } else { Phase::Recovering { deadline: 5000 } });
        let mut expected = vec![(Property::ReporterRunning, Value::Zero), (Property::ModemException, Value::Zero)];
        if writing { expected.push((Property::ModemException, Value::One)); }
        else { expected.extend([(Property::ReportCase, Value::One), (Property::Report, Value::One)]); }
        assert_eq!(h.values(), expected);
        h.owner.stop();
    }
}

#[test]
fn mux_reset_keeps_actual_runtime_running_and_publication_failure_is_terminal() {
    use runtime_support::{Controlled, Endpoints, SharedPlan, Step, Store};
    let mut h = Harness::new();
    assert!(h.owner.publication.initialize());
    let (serial, peer) = UnixStream::pair().unwrap();
    let plan = SharedPlan::default();
    let store = Rc::new(RefCell::new(Store::default()));
    let mut runtime = Runtime::new(Controlled::new(serial, plan.clone()), Endpoints(store.clone()),
        h.owner.publication.clone(), &[ChannelConfig { channel: 1, atci: false }], h.owner.config.chat(), 0).unwrap();
    let mut modem = runtime_support::Modem::new(peer);
    modem.bytes(b"+EIND: 128\r\n");
    let mut now = 0;
    for _ in 0..4096 {
        modem.pump();
        let events: Vec<_> = runtime.interests().into_iter().map(|interest| Ready { token: interest.token,
            readable: interest.readable, writable: interest.writable, hangup: false }).collect();
        let report = runtime.drive(now, &events, Limits::default());
        assert!(report.error.is_none());
        modem.pump(); now += 1;
        if h.values().contains(&(Property::RilReady, Value::One)) { break; }
    }
    assert_eq!(h.values(), startup_ready());
    for reject in [false, true] {
        if reject { h.record.fail_at.set(Some(h.values().len())); }
        plan.borrow_mut().writes.push_back(Step::Fault(Fault::DeviceReset));
        std::io::Write::write_all(store.borrow_mut().peers.get_mut(&1).unwrap(), b"discard-on-reset").unwrap();
        let events: Vec<_> = runtime.interests().into_iter().map(|interest| Ready { token: interest.token,
            readable: interest.readable, writable: interest.writable, hangup: false }).collect();
        let report = runtime.drive(now, &events, Limits::default());
        now += 1;
        assert_eq!(report.state, if reject { State::Failed } else { State::Multiplexing });
        assert!(report.discarded >= b"discard-on-reset".len());
        assert_eq!(runtime.pending_tx(), 0);
        assert_eq!(h.values().last(), Some(&(Property::ModemException, Value::One)));
        if reject {
            assert_eq!(report.error, Some(Error::EffectRejected));
            h.owner.observed(0, report);
            assert_eq!(h.owner.phase(), Phase::Finished(Exit::PropertyRejected));
        }
    }
}

#[test]
fn unsupported_local_close_has_an_explicit_terminal_result_without_recovery() {
    use runtime_support::{Controlled, Endpoints, SharedPlan};
    let mut h = Harness::new();
    assert!(h.owner.publication.initialize());
    let before = h.values();
    let (serial, peer) = UnixStream::pair().unwrap();
    let mut runtime = Runtime::new(Controlled::new(serial, SharedPlan::default()), Endpoints(Rc::default()),
        h.owner.publication.clone(), &[ChannelConfig { channel: 1, atci: false }], h.owner.config.chat(), 0).unwrap();
    let mut modem = runtime_support::Modem::new(peer);
    modem.refuse = Some(0);
    modem.bytes(b"+EIND: 128\r\n");
    for now in 0..4096 {
        modem.pump();
        let events: Vec<_> = runtime.interests().into_iter().map(|interest| Ready { token: interest.token,
            readable: interest.readable, writable: interest.writable, hangup: false }).collect();
        let report = runtime.drive(now, &events, Limits::default());
        if report.state == State::Failed {
            assert_eq!(report.error, Some(Error::UnsupportedLocalClose));
            h.owner.observed(0, report);
            break;
        }
        modem.pump();
    }
    assert_eq!(h.owner.phase(), Phase::Finished(Exit::UnsupportedLocalClose));
    assert_eq!(h.owner.runtime_error(), Some(Error::UnsupportedLocalClose));
    assert_eq!(h.values(), before);
}

#[test]
fn recovery_deadline_starts_after_completed_property_publication() {
    let mut h = Harness::new();
    h.boot();
    h.clock.0.set(10);
    h.record.report_delay.set(37);
    h.malformed();
    assert_eq!(h.until(|phase| matches!(phase, Phase::Recovering { .. })), Phase::Recovering { deadline: 5047 });
    assert_eq!(h.clock.0.get(), 47);
    h.clock.0.set(5046);
    assert_eq!(h.owner.step(0, Limits::default()), Phase::Recovering { deadline: 5047 });
    h.clock.0.set(5047);
    assert_eq!(h.owner.step(0, Limits::default()), Phase::Finished(Exit::RecoveryRequested));
}
