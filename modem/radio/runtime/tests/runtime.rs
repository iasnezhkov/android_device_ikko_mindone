/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#![forbid(unsafe_code)]

use mindone_radio_runtime::*;
use mindone_radio_core::{Indication, RadioError, Request, Value};
use mindone_radio_modem::types as m;
use mindone_radio_network::types as n;
use mindone_radio_state::model::Radio;
use std::collections::{BTreeMap, BTreeSet, VecDeque};
use std::io;
use std::sync::{atomic::{AtomicU64, Ordering}, Arc, Condvar, Mutex, MutexGuard};
use std::time::{Duration, Instant};

const WAIT: Duration = Duration::from_secs(3);

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> { mutex.lock().unwrap() }

#[derive(Default)]
struct Pipe { incoming: VecDeque<u8>, outgoing: Vec<u8>, hung_up: bool }

struct State {
    pipes: BTreeMap<u8, Pipe>,
    opens: Vec<(u64, u8)>,
    closes: Vec<u8>,
    commands: Vec<(u8, String)>,
    unanswered: Vec<(u8, String)>,
    interests: Vec<Vec<Interest>>,
    short: usize,
    block: bool,
    blocked: BTreeSet<u8>,
    silent: bool,
    gated: bool,
    entered: bool,
    signaled: bool,
    panic_poll: bool,
    failures: usize,
}

struct Bus { state: Mutex<State>, changed: Condvar, now: AtomicU64 }

impl Bus {
    fn new() -> Arc<Self> {
        Arc::new(Self { state: Mutex::new(State { pipes: BTreeMap::new(), opens: Vec::new(), closes: Vec::new(),
            commands: Vec::new(), unanswered: Vec::new(), interests: Vec::new(), short: usize::MAX, block: false,
            blocked: BTreeSet::new(), silent: false, gated: false, entered: false, signaled: false,
            panic_poll: false, failures: 0 }), changed: Condvar::new(), now: AtomicU64::new(0) })
    }

    fn update(&self, action: impl FnOnce(&mut State)) {
        let mut state = lock(&self.state);
        action(&mut state);
        state.signaled = true;
        self.changed.notify_all();
    }

    fn wait(&self, condition: impl Fn(&State) -> bool) {
        let state = lock(&self.state);
        let (state, result) = self.changed.wait_timeout_while(state, WAIT, |state| !condition(state)).unwrap();
        assert!(!result.timed_out() || condition(&state), "Modem condition timed out; commands: {:?}", state.commands);
    }

    fn advance(&self, now: u64) {
        self.now.store(now, Ordering::Release);
        self.update(|_| ());
    }

    fn release_answers(&self) {
        self.update(|state| {
            state.silent = false;
            for (channel, command) in std::mem::take(&mut state.unanswered) { answer(state, channel, &command); }
        });
    }

    fn urc(&self, channel: u8, line: &str) {
        self.update(|state| { state.pipes.get_mut(&channel).unwrap().incoming.extend(format!("\r\n{line}\r\n").bytes()); });
    }
}

impl Clock for Bus { fn now_ms(&self) -> u64 { self.now.load(Ordering::Acquire) } }
impl Wake for Bus {
    fn wake(&self) -> io::Result<()> { self.update(|_| ()); Ok(()) }
}

struct MemoryEndpoint { bus: Arc<Bus>, channel: u8 }
impl Drop for MemoryEndpoint {
    fn drop(&mut self) {
        self.bus.update(|state| { state.pipes.remove(&self.channel); state.closes.push(self.channel); });
    }
}

fn answer(state: &mut State, channel: u8, command: &str) {
    let response = match command {
        "AT+ESIMS" => "+ESIMS: 0,0\r\nOK",
        "AT+CGMR" => "+CGMR: MOLY.RUNTIME\r\nOK",
        "AT+CGSN" => "490154203237518\r\nOK",
        "AT+EGMR=0,9" => "+EGMR: 78\r\nOK",
        "AT+ECSQ" => "+ECSQ: 0,99,-400,0,0,-40,-400,4096,80,3\r\nOK",
        _ => "OK",
    };
    state.pipes.get_mut(&channel).unwrap().incoming.extend(format!("\r\n{response}\r\n").bytes());
}

impl Endpoint for MemoryEndpoint {
    fn read(&mut self, bytes: &mut [u8]) -> io::Result<usize> {
        let mut state = lock(&self.bus.state);
        let pipe = state.pipes.get_mut(&self.channel).unwrap();
        if pipe.hung_up { return Ok(0); }
        if pipe.incoming.is_empty() { return Err(io::Error::from(io::ErrorKind::WouldBlock)); }
        let count = bytes.len().min(pipe.incoming.len());
        for byte in &mut bytes[..count] { *byte = pipe.incoming.pop_front().unwrap(); }
        Ok(count)
    }

    fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
        assert_ne!(self.channel, 0);
        let mut state = lock(&self.bus.state);
        if state.block && state.blocked.insert(self.channel) {
            return Err(io::Error::from(io::ErrorKind::WouldBlock));
        }
        state.blocked.remove(&self.channel);
        let count = bytes.len().min(state.short);
        for byte in &bytes[..count] {
            if *byte == b'\r' {
                let command = String::from_utf8(std::mem::take(&mut state.pipes.get_mut(&self.channel).unwrap().outgoing)).unwrap();
                state.commands.push((self.channel, command.clone()));
                if state.silent { state.unanswered.push((self.channel, command)); }
                else { answer(&mut state, self.channel, &command); }
            } else { state.pipes.get_mut(&self.channel).unwrap().outgoing.push(*byte); }
        }
        state.signaled = true;
        self.bus.changed.notify_all();
        Ok(count)
    }

    fn hung_up(&self) -> bool { lock(&self.bus.state).pipes[&self.channel].hung_up }
}

struct MemoryOpener(Arc<Bus>);
impl Opener for MemoryOpener {
    fn open(&mut self, directory: &str, name: &str) -> io::Result<Box<dyn Endpoint>> {
        assert_eq!(directory, DIRECTORY);
        let channel = TOPOLOGY.iter().find(|entry| entry.1 == name).unwrap().0;
        let mut state = lock(&self.0.state);
        state.opens.push((self.0.now_ms(), channel));
        if state.failures != 0 { state.failures -= 1; return Err(io::Error::from(io::ErrorKind::NotFound)); }
        assert!(state.pipes.insert(channel, Pipe::default()).is_none());
        self.0.changed.notify_all();
        Ok(Box::new(MemoryEndpoint { bus: self.0.clone(), channel }))
    }
}

struct MemoryPoller(Arc<Bus>);
impl Poller for MemoryPoller {
    fn poll(&mut self, interests: &[Interest], timeout: Option<Duration>) -> io::Result<Vec<Ready>> {
        let mut state = lock(&self.0.state);
        state.interests.push(interests.to_vec());
        state.entered = true;
        self.0.changed.notify_all();
        while state.gated { state = self.0.changed.wait(state).unwrap(); }
        let panic_poll = state.panic_poll;
        if panic_poll { drop(state); panic!("Scripted reactor panic"); }
        let events = |state: &State| interests.iter().filter_map(|interest| {
            let pipe = state.pipes.get(&interest.channel)?;
            let readable = interest.readable && !pipe.incoming.is_empty();
            (readable || interest.writable || pipe.hung_up).then_some(Ready {
                channel: interest.channel, readable, writable: interest.writable, hung_up: pipe.hung_up,
            })
        }).collect::<Vec<_>>();
        if !state.signaled && events(&state).is_empty() {
            let (next, _) = self.0.changed.wait_timeout(state, timeout.unwrap_or(Duration::MAX).min(Duration::from_millis(20))).unwrap();
            state = next;
        }
        state.signaled = false;
        Ok(events(&state))
    }
}

fn start(bus: &Arc<Bus>, capacity: usize) -> Runtime {
    Runtime::start(Config { directory: DIRECTORY.into(), opener: Box::new(MemoryOpener(bus.clone())),
        poller: Box::new(MemoryPoller(bus.clone())), wake: bus.clone(), clock: bus.clone(), output_capacity: capacity }).unwrap()
}

fn baseband() -> Request { Request::Modem(m::Request::GetBasebandVersion) }
fn imei() -> Request { Request::Modem(m::Request::GetImei) }

fn until(runtime: &Runtime, condition: impl Fn(&[Delivery]) -> bool) -> Vec<Delivery> {
    let end = Instant::now() + WAIT;
    let mut deliveries = Vec::new();
    while !condition(&deliveries) {
        assert!(Instant::now() < end, "Delivery condition timed out: {deliveries:?}");
        deliveries.extend(runtime.next_batch(Duration::from_millis(20)));
    }
    deliveries
}

fn responses(deliveries: &[Delivery]) -> Vec<(u64, RadioError)> {
    deliveries.iter().filter_map(|delivery| match delivery {
        Delivery::Response { invocation, error, .. } => Some((*invocation, *error)), _ => None,
    }).collect()
}

fn boot(bus: &Arc<Bus>, runtime: &Runtime) {
    runtime.submit(99, baseband()).unwrap();
    let deliveries = until(runtime, |deliveries| responses(deliveries).iter().any(|entry| entry.0 == 99));
    assert_eq!(responses(&deliveries), [(99, RadioError::None)]);
    bus.wait(|state| state.commands.iter().any(|entry| entry.1 == "AT+CGMR"));
}

#[test]
fn boot_topology_and_invocation_response() {
    let bus = Bus::new();
    let runtime = start(&bus, OUTPUT_CAPACITY);
    runtime.submit(u64::MAX, baseband()).unwrap();
    let deliveries = until(&runtime, |deliveries| !responses(deliveries).is_empty());
    assert!(deliveries.contains(&Delivery::Response { invocation: u64::MAX, error: RadioError::None,
        value: Some(Value::Modem(m::Value::Baseband("MOLY.RUNTIME".into()))) }));
    let state = lock(&bus.state);
    assert_eq!(state.opens.iter().map(|entry| entry.1).collect::<Vec<_>>(), [0, 1, 3, 6, 8]);
    assert_eq!(&state.commands[..11], &[
        (8,"ATE0Q0V1"), (8,"AT+CMEE=1"), (8,"AT+CMER=1,0,0,2,0"), (8,"AT+ICCID=1"),
        (8,"AT+ESIMS=1"), (8,"AT+ESIMS"), (8,"AT+EFUN=0"), (1,"AT+EREG=3"),
        (1,"AT+EGREG=3,1"), (1,"AT+PSBEARER=1"), (1,"AT+ECSQ=1"),
    ].map(|(channel, text)| (channel, text.into())));
    drop(state);
    runtime.join();
}

#[test]
fn simultaneous_invocations_keep_distinct_live_tokens() {
    let bus = Bus::new();
    let runtime = start(&bus, OUTPUT_CAPACITY);
    boot(&bus, &runtime);
    bus.update(|state| { state.silent = true; });
    runtime.submit(123, baseband()).unwrap();
    runtime.submit(456, imei()).unwrap();
    bus.wait(|state| !state.unanswered.is_empty());
    bus.release_answers();
    let deliveries = until(&runtime, |deliveries| responses(deliveries).len() == 2);
    assert_eq!(responses(&deliveries).into_iter().map(|(id, error)| (id, error as i32)).collect::<BTreeSet<_>>(),
        [(123, RadioError::None as i32), (456, RadioError::None as i32)].into_iter().collect());
    runtime.join();
}

#[test]
fn partial_writes_and_would_block_only_arm_the_affected_channel() {
    let bus = Bus::new();
    bus.update(|state| { state.short = 2; state.block = true; });
    let runtime = start(&bus, OUTPUT_CAPACITY);
    boot(&bus, &runtime);
    let state = lock(&bus.state);
    assert!(state.interests.iter().flatten().any(|interest| interest.channel == 8 && interest.writable));
    assert!(!state.interests.iter().flatten().any(|interest| [0, 6].contains(&interest.channel) && interest.writable));
    assert!(state.commands.iter().all(|entry| entry.1.starts_with("AT")));
    drop(state);
    runtime.join();
}

#[test]
fn hangup_reopens_every_endpoint_and_restores_filter() {
    let bus = Bus::new();
    let runtime = start(&bus, OUTPUT_CAPACITY);
    boot(&bus, &runtime);
    runtime.submit(1, Request::Network(n::Request::SetIndicationFilter(1))).unwrap();
    until(&runtime, |deliveries| responses(deliveries).iter().any(|entry| entry.0 == 1));
    bus.update(|state| { state.pipes.get_mut(&6).unwrap().hung_up = true; });
    let deliveries = until(&runtime, |deliveries| deliveries.contains(&Delivery::Indication(Indication::RadioStateChanged(Radio::Unavailable))));
    assert!(deliveries.contains(&Delivery::Indication(Indication::ModemReset { generation: 1 })));
    bus.wait(|state| state.closes.len() == 5);
    bus.advance(100);
    until(&runtime, |deliveries| deliveries.contains(&Delivery::Indication(Indication::RadioStateChanged(Radio::Off))));
    bus.wait(|state| state.commands.iter().filter(|entry| entry.1 == "AT+ESCREENSTATE=1").count() == 2);
    bus.wait(|state| state.commands.iter().filter(|entry| entry.1 == "AT+ECSQ").count() == 2);
    let state = lock(&bus.state);
    assert_eq!(state.opens.iter().map(|entry| entry.1).collect::<Vec<_>>(), [0,1,3,6,8,0,1,3,6,8]);
    assert_eq!(state.commands.iter().filter(|entry| entry.1 == "ATE0Q0V1").count(), 2);
    drop(state);
    runtime.join();
}

#[test]
fn deadline_expiry_resets_and_answers_the_invocation() {
    let bus = Bus::new();
    let runtime = start(&bus, OUTPUT_CAPACITY);
    boot(&bus, &runtime);
    bus.update(|state| { state.silent = true; });
    runtime.submit(71, imei()).unwrap();
    bus.wait(|state| !state.unanswered.is_empty());
    bus.advance(5000);
    let deliveries = until(&runtime, |deliveries| deliveries.contains(&Delivery::Indication(Indication::RadioStateChanged(Radio::Unavailable))));
    assert_eq!(responses(&deliveries), [(71, RadioError::RadioNotAvailable)]);
    assert!(deliveries.contains(&Delivery::Indication(Indication::ModemReset { generation: 1 })));
    bus.wait(|state| state.closes.len() == 5);
    runtime.join();
}

#[test]
fn full_submission_queue_fails_immediately_and_stop_answers_all() {
    let bus = Bus::new();
    bus.update(|state| { state.gated = true; state.silent = true; });
    let runtime = start(&bus, OUTPUT_CAPACITY);
    bus.wait(|state| state.entered);
    for invocation in 0..SUBMISSION_CAPACITY as u64 { runtime.submit(invocation, imei()).unwrap(); }
    assert_eq!(runtime.submit(1000, imei()), Err(SubmitError::Full));
    runtime.stop();
    bus.update(|state| { state.gated = false; });
    runtime.join();
    let deliveries = until(&runtime, |deliveries| responses(deliveries).len() == SUBMISSION_CAPACITY);
    assert_eq!(responses(&deliveries).into_iter().map(|(id, error)| (id, error as i32)).collect::<BTreeSet<_>>(),
        (0..SUBMISSION_CAPACITY as u64).map(|id| (id, RadioError::RadioNotAvailable as i32)).collect());
    assert_eq!(runtime.submit(1001, imei()), Err(SubmitError::Stopped));
}

#[test]
fn stop_with_inflight_and_queued_requests_answers_every_one_without_a_dispatcher() {
    let bus = Bus::new();
    let runtime = start(&bus, 1);
    boot(&bus, &runtime);
    bus.update(|state| { state.silent = true; });
    for id in 0..100 { runtime.submit(id, imei()).unwrap(); }
    bus.wait(|state| !state.unanswered.is_empty());
    runtime.join();
    let deliveries = until(&runtime, |deliveries| responses(deliveries).len() == 100);
    assert_eq!(responses(&deliveries).into_iter().map(|(id, error)| (id, error as i32)).collect::<BTreeSet<_>>(),
        (0..100).map(|id| (id, RadioError::RadioNotAvailable as i32)).collect());
    assert!(runtime.next_batch(Duration::ZERO).is_empty());
}

#[test]
fn output_backpressure_never_drops_responses() {
    let bus = Bus::new();
    let runtime = start(&bus, 1);
    boot(&bus, &runtime);
    for id in 0..200 { runtime.submit(id, Request::Modem(m::Request::GetCurrentCalls)).unwrap(); }
    let deliveries = until(&runtime, |deliveries| responses(deliveries).len() == 200);
    assert_eq!(responses(&deliveries).into_iter().map(|entry| entry.0).collect::<BTreeSet<_>>(), (0..200).collect());
    runtime.join();
}

#[test]
fn panic_boundary_answers_all_accepted_requests() {
    let bus = Bus::new();
    bus.update(|state| { state.gated = true; state.silent = true; });
    let runtime = start(&bus, 1);
    bus.wait(|state| state.entered);
    for id in 0..32 { runtime.submit(id, imei()).unwrap(); }
    bus.update(|state| { state.panic_poll = true; state.gated = false; });
    let deliveries = until(&runtime, |deliveries| responses(deliveries).len() == 32);
    assert!(responses(&deliveries).iter().all(|entry| entry.1 == RadioError::RadioNotAvailable));
    runtime.join();
}

#[test]
fn reconnect_backoff_is_bounded() {
    let bus = Bus::new();
    bus.update(|state| { state.failures = 12; });
    let runtime = start(&bus, OUTPUT_CAPACITY);
    until(&runtime, |deliveries| deliveries.contains(&Delivery::Indication(Indication::ModemReset { generation: 1 })));
    let mut now = 0;
    for delay in [100, 200, 400, 800, 1600, 3200, 5000, 5000] {
        let count = lock(&bus.state).opens.len();
        now += delay;
        bus.advance(now);
        bus.wait(|state| state.opens.len() > count);
    }
    let times: Vec<_> = lock(&bus.state).opens.iter().map(|entry| entry.0).collect();
    assert_eq!(times, [0,100,300,700,1500,3100,6300,11300,16300]);
    runtime.join();
}

#[test]
fn notification_channel_is_receive_only_and_fair_under_urc_load() {
    let bus = Bus::new();
    let runtime = start(&bus, OUTPUT_CAPACITY);
    boot(&bus, &runtime);
    bus.urc(0, &"+EREG: 0\r\n".repeat(1000));
    runtime.submit(1234, imei()).unwrap();
    let deliveries = until(&runtime, |deliveries| !responses(deliveries).is_empty());
    assert_eq!(responses(&deliveries), [(1234, RadioError::None)]);
    assert!(lock(&bus.state).commands.iter().all(|entry| entry.0 != 0));
    runtime.join();
}

#[test]
fn production_pipe_wake_and_shutdown_without_ptys() {
    let mut config = Config::production().unwrap();
    config.directory = "/nonexistent-mindone-runtime-test".into();
    let runtime = Runtime::start(config).unwrap();
    runtime.submit(1, imei()).unwrap();
    runtime.join();
    let deliveries = until(&runtime, |deliveries| !responses(deliveries).is_empty());
    assert_eq!(responses(&deliveries), [(1, RadioError::RadioNotAvailable)]);
}

#[test]
fn coalesced_completion_burst_survives_a_full_output_queue() {
    let bus = Bus::new();
    let runtime = start(&bus, 1);
    boot(&bus, &runtime);
    bus.update(|state| { state.gated = true; state.entered = false; state.silent = true; });
    bus.wait(|state| state.entered);
    for id in 0..128 { runtime.submit(id, baseband()).unwrap(); }
    let polls = lock(&bus.state).interests.len();
    bus.update(|state| { state.gated = false; });
    bus.wait(|state| state.interests.len() >= polls + 12);
    bus.release_answers();
    bus.wait(|state| state.interests.last().is_some_and(|interests| interests.iter().all(|interest| !interest.readable)));
    runtime.join();
    let deliveries = until(&runtime, |deliveries| responses(deliveries).len() == 128);
    assert_eq!(responses(&deliveries).into_iter().map(|(id, error)| (id, error as i32)).collect::<BTreeSet<_>>(),
        (0..128).map(|id| (id, RadioError::None as i32)).collect());
    assert_eq!(lock(&bus.state).commands.iter().filter(|entry| entry.1 == "AT+CGMR").count(), 2);
}
