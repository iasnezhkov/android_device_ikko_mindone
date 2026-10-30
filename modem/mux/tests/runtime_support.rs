/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use mindone_mux_core::chat;
use mindone_mux_core::mcc;
use mindone_mux_core::session::ChannelConfig;
use mindone_mux_core::transport::{EndpointId, Fault, IoResult};
use mindone_mux_core::{encode, Decoder, Frame, POLL_FINAL, UIH};
use mindone_mux_runtime::{Effects, Endpoint, Event, Factory, Limits, Ready, Report, Runtime, State};
use std::cell::RefCell;
use std::collections::{BTreeMap, VecDeque};
use std::io::{self, Read, Write};
use std::os::fd::{AsFd, AsRawFd, BorrowedFd};
use std::os::unix::net::UnixStream;
use std::rc::Rc;
use std::sync::{Mutex, MutexGuard};

static LOCK: Mutex<()> = Mutex::new(());
pub fn lock() -> MutexGuard<'static, ()> { LOCK.lock().unwrap_or_else(|error| error.into_inner()) }

#[derive(Clone, Copy)]
pub enum Step { Limit(usize), Fault(Fault), Invalid(usize) }

#[derive(Default)]
pub struct Plan {
    pub reads: VecDeque<Step>,
    pub writes: VecDeque<Step>,
    pub read_limit: Option<usize>,
    pub write_limit: Option<usize>,
    pub read_calls: usize,
    pub write_calls: usize,
    pub read_bytes: usize,
    pub write_bytes: usize,
    pub prepare_fails: bool,
}

pub type SharedPlan = Rc<RefCell<Plan>>;

pub struct Controlled {
    stream: UnixStream,
    pub plan: SharedPlan,
}

impl Controlled {
    pub fn new(stream: UnixStream, plan: SharedPlan) -> Self { Self { stream, plan } }
}

impl AsFd for Controlled {
    fn as_fd(&self) -> BorrowedFd<'_> { self.stream.as_fd() }
}

fn result(value: io::Result<usize>) -> IoResult {
    match value {
        Ok(bytes) => IoResult::Transferred(bytes),
        Err(error) => IoResult::Fault(match error.kind() {
            io::ErrorKind::WouldBlock => Fault::WouldBlock,
            io::ErrorKind::Interrupted => Fault::Interrupted,
            _ => Fault::Failure,
        }),
    }
}

impl Endpoint for Controlled {
    fn prepare(&self) -> io::Result<()> {
        if self.plan.borrow().prepare_fails { return Err(io::Error::other("Injected prepare failure")); }
        self.stream.set_nonblocking(true)
    }
    fn read(&mut self, output: &mut [u8]) -> IoResult {
        let (step, limit) = {
            let mut plan = self.plan.borrow_mut();
            plan.read_calls += 1;
            (plan.reads.pop_front(), plan.read_limit)
        };
        let offered = match step {
            Some(Step::Fault(fault)) => return IoResult::Fault(fault),
            Some(Step::Invalid(bytes)) => return IoResult::Transferred(bytes),
            Some(Step::Limit(bytes)) => bytes.min(output.len()),
            None => limit.unwrap_or(output.len()).min(output.len()),
        };
        let value = result(Read::read(&mut self.stream, &mut output[..offered]));
        if let IoResult::Transferred(bytes) = value { self.plan.borrow_mut().read_bytes += bytes; }
        value
    }
    fn write(&mut self, input: &[u8]) -> IoResult {
        let (step, limit) = {
            let mut plan = self.plan.borrow_mut();
            plan.write_calls += 1;
            (plan.writes.pop_front(), plan.write_limit)
        };
        let offered = match step {
            Some(Step::Fault(fault)) => return IoResult::Fault(fault),
            Some(Step::Invalid(bytes)) => return IoResult::Transferred(bytes),
            Some(Step::Limit(bytes)) => bytes.min(input.len()),
            None => limit.unwrap_or(input.len()).min(input.len()),
        };
        let value = result(Write::write(&mut self.stream, &input[..offered]));
        if let IoResult::Transferred(bytes) = value { self.plan.borrow_mut().write_bytes += bytes; }
        value
    }
}

#[derive(Default)]
pub struct Store {
    pub peers: BTreeMap<u8, UnixStream>,
    pub plans: BTreeMap<u8, SharedPlan>,
    pub opens: BTreeMap<u8, usize>,
    pub descriptors: BTreeMap<u8, i32>,
    pub fail_channel: Option<u8>,
}

#[derive(Clone)]
pub struct Endpoints(pub Rc<RefCell<Store>>);

impl Factory<Controlled> for Endpoints {
    fn open(&mut self, channel: u8) -> io::Result<Controlled> {
        let mut store = self.0.borrow_mut();
        if store.fail_channel == Some(channel) { return Err(io::Error::other("Injected factory failure")); }
        let (endpoint, peer) = UnixStream::pair()?;
        peer.set_nonblocking(true)?;
        let plan = store.plans.entry(channel).or_default().clone();
        *store.opens.entry(channel).or_default() += 1;
        store.descriptors.insert(channel, endpoint.as_raw_fd());
        store.peers.insert(channel, peer);
        Ok(Controlled::new(endpoint, plan))
    }
}

#[derive(Default)]
pub struct Publications { pub events: Vec<Event>, pub reject: Option<Event> }

#[derive(Clone, Default)]
pub struct Publish(pub Rc<RefCell<Publications>>);

impl Effects for Publish {
    fn apply(&mut self, event: Event) -> bool {
        let mut state = self.0.borrow_mut();
        state.events.push(event);
        state.reject != Some(event)
    }
}

pub struct Modem {
    pub peer: UnixStream,
    pub outgoing: VecDeque<u8>,
    pub commands: Vec<Vec<u8>>,
    pub frames: Vec<(u8, u8, Vec<u8>)>,
    pub received: BTreeMap<u8, Vec<u8>>,
    pub mode_mux: bool,
    pub auto: bool,
    pub cmux_ready: bool,
    pub cmux_tail: Vec<u8>,
    pub refuse: Option<u8>,
    pub read_limit: usize,
    pub write_limit: usize,
    record: Vec<u8>,
    decoder: Decoder<65536>,
}

impl Modem {
    pub fn new(peer: UnixStream) -> Self {
        peer.set_nonblocking(true).unwrap();
        Self { peer, outgoing: VecDeque::new(), commands: Vec::new(), frames: Vec::new(), received: BTreeMap::new(),
            mode_mux: false, auto: true, cmux_ready: true, cmux_tail: Vec::new(), refuse: None,
            read_limit: 4096, write_limit: 4096, record: Vec::new(), decoder: Decoder::new(512).unwrap() }
    }
    pub fn bytes(&mut self, bytes: &[u8]) {
        assert!(self.outgoing.len() + bytes.len() <= 2 * 65536);
        self.outgoing.extend(bytes);
    }
    pub fn frame(&mut self, channel: u8, control: u8, payload: &[u8]) {
        let mut wire = [0; 519];
        let count = encode(Frame::command(channel, control, payload).unwrap(), 512, &mut wire).unwrap();
        self.bytes(&wire[..count]);
    }
    fn command(&mut self) {
        let value = std::mem::take(&mut self.record);
        self.commands.push(value.clone());
        if !self.auto { return; }
        if value.starts_with(b"AT+CMUX=") {
            assert_eq!(value, b"AT+CMUX=0,0,5,512");
            if self.cmux_ready { self.bytes(b"+CMUX: READY\r\n"); }
            self.bytes(b"OK\r\n");
            let tail = self.cmux_tail.clone();
            self.bytes(&tail);
            self.mode_mux = true;
        } else {
            assert!(matches!(value.as_slice(), b"AT" | b"ATZ" | b"ATE0") || value.starts_with(b"AT+CPIN="));
            self.bytes(b"OK\r\n");
        }
    }
    fn mux_input(&mut self, bytes: &[u8]) {
        self.decoder.push(bytes).unwrap();
        let mut frames = Vec::new();
        while let Some(frame) = self.decoder.next_frame() {
            frames.push((frame.channel(), frame.control, frame.payload.to_vec()));
        }
        for (channel, control, payload) in frames {
            self.frames.push((channel, control, payload.clone()));
            if control & !POLL_FINAL == 0x2f {
                if self.auto { self.frame(channel, if self.refuse == Some(channel) { 0x1f } else { 0x73 }, &[]); }
            } else if control & !POLL_FINAL == UIH {
                if channel != 0 { self.received.entry(channel).or_default().extend(payload); }
                else if self.auto {
                    let mut output = [0; 512];
                    if let Some(reply) = mcc::prepare(&payload, 512, &mut output).unwrap().reply {
                        self.frame(0, reply.control, reply.payload);
                    }
                }
            }
        }
    }
    pub fn pump(&mut self) {
        for _ in 0..64 {
            if self.outgoing.is_empty() { break; }
            let count = self.outgoing.len().min(self.write_limit);
            let contiguous = self.outgoing.make_contiguous();
            match Write::write(&mut self.peer, &contiguous[..count]) {
                Ok(0) => break,
                Ok(bytes) => { self.outgoing.drain(..bytes); }
                Err(error) if error.kind() == io::ErrorKind::WouldBlock => break,
                Err(error) if error.kind() == io::ErrorKind::BrokenPipe || error.kind() == io::ErrorKind::ConnectionReset => { self.outgoing.clear(); break; }
                Err(error) => panic!("Modem fixture write failed: {error}"),
            }
        }
        for _ in 0..64 {
            let mut input = [0; 4096];
            let count = self.read_limit.min(input.len());
            match Read::read(&mut self.peer, &mut input[..count]) {
                Ok(0) => break,
                Ok(bytes) if self.mode_mux => self.mux_input(&input[..bytes]),
                Ok(bytes) => {
                    for byte in &input[..bytes] {
                        if *byte == b'\r' { self.command(); }
                        else { self.record.push(*byte); assert!(self.record.len() <= 128); }
                    }
                }
                Err(error) if error.kind() == io::ErrorKind::WouldBlock => break,
                Err(error) if error.kind() == io::ErrorKind::ConnectionReset => break,
                Err(error) => panic!("Modem fixture read failed: {error}"),
            }
        }
    }
}

pub type Owner = Runtime<Controlled, Endpoints, Publish>;

pub struct Harness {
    pub runtime: Owner,
    pub modem: Modem,
    pub store: Rc<RefCell<Store>>,
    pub serial: SharedPlan,
    pub published: Publish,
    pub now: u64,
    pub limits: Limits,
    pub reports: Vec<Report>,
    pub serial_writable: bool,
}

impl Harness {
    pub fn new() -> Self {
        Self::new_at(0)
    }
    pub fn new_at(now: u64) -> Self {
        let (serial, peer) = UnixStream::pair().unwrap();
        let plan = SharedPlan::default();
        let store = Rc::new(RefCell::new(Store::default()));
        let factory = Endpoints(store.clone());
        let published = Publish::default();
        let config = chat::Config { frame_limit: 512, baud_index: 5, user_build: false, pin: None };
        let runtime = Owner::new(Controlled::new(serial, plan.clone()), factory, published.clone(),
            &[ChannelConfig { channel: 1, atci: false }, ChannelConfig { channel: 26, atci: true }], config, now).unwrap();
        let mut modem = Modem::new(peer);
        modem.bytes(b"+EIND: 128\r\n");
        Self { runtime, modem, store, serial: plan, published, now, limits: Limits::default(), reports: Vec::new(), serial_writable: true }
    }
    pub fn ready(&self) -> Vec<Ready> {
        self.runtime.interests().into_iter().filter(|value| value.readable || value.writable)
            .map(|value| Ready { token: value.token, readable: value.readable, writable: value.writable && (self.serial_writable || value.token.endpoint() != EndpointId::Serial), hangup: false }).collect()
    }
    pub fn drive(&mut self, ready: &[Ready]) -> Report {
        let report = self.runtime.drive(self.now, ready, self.limits);
        assert!(report.calls <= self.limits.calls && report.bytes <= self.limits.bytes);
        self.now += 1;
        self.reports.push(report);
        report
    }
    pub fn step(&mut self) -> Report {
        self.modem.pump();
        let ready = self.ready();
        let report = self.drive(&ready);
        self.modem.pump();
        report
    }
    pub fn steps(&mut self, count: usize) {
        for _ in 0..count { self.step(); }
    }
    pub fn boot(&mut self) {
        for _ in 0..4000 {
            let report = self.step();
            assert_ne!(report.state, State::Failed, "Runtime startup failed: {report:?}");
            if self.published.0.borrow().events.iter().any(|event| matches!(event, Event::Ready { .. })) { return; }
        }
        panic!("Bounded host startup did not publish readiness");
    }
    pub fn plan(&self, channel: u8) -> SharedPlan { self.store.borrow().plans[&channel].clone() }
    pub fn send(&self, channel: u8, bytes: &[u8]) {
        self.store.borrow_mut().peers.get_mut(&channel).unwrap().write_all(bytes).unwrap();
    }
    pub fn received(&self, channel: u8) -> Vec<u8> {
        let mut store = self.store.borrow_mut();
        drain(store.peers.get_mut(&channel).unwrap())
    }
    pub fn token(&self, endpoint: EndpointId) -> mindone_mux_core::transport::PollToken {
        self.runtime.interests().into_iter().find(|value| value.token.endpoint() == endpoint).unwrap().token
    }
}

pub fn drain(stream: &mut UnixStream) -> Vec<u8> {
    let mut output = Vec::new();
    for _ in 0..512 {
        let mut input = [0; 4096];
        match Read::read(stream, &mut input) {
            Ok(0) => break,
            Ok(bytes) => output.extend_from_slice(&input[..bytes]),
            Err(error) if error.kind() == io::ErrorKind::WouldBlock => break,
            Err(error) => panic!("Fixture drain failed: {error}"),
        }
    }
    assert!(output.len() <= 65536);
    output
}
