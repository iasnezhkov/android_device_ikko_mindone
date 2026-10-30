/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#![deny(unsafe_code)]
#![crate_name = "mindone_radio_runtime"]

#[allow(unsafe_code)]
pub mod sys;

pub use mindone_radio_core as core;
use core::{Core, Indication, Output, RadioError, Request, Value};
use std::collections::{BTreeMap, VecDeque};
use std::io;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::{atomic::{AtomicBool, Ordering}, Arc, Condvar, Mutex, MutexGuard};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

pub const SUBMISSION_CAPACITY: usize = 256;
pub const OUTPUT_CAPACITY: usize = 256;
pub const ACCEPTED_LIMIT: usize = 1024;
pub const READ_BYTES: usize = 512;
pub const IO_ITERATIONS: usize = 4;
pub const INITIAL_BACKOFF_MS: u64 = 100;
pub const MAX_BACKOFF_MS: u64 = 5000;
pub const SLOT: u8 = 0;
pub const DIRECTORY: &str = "/dev/radio";
pub const TOPOLOGY: [(u8, &str); 5] = [
    (0, "pttynoti"), (1, "pttycmd1"), (3, "pttycmd3"), (6, "pttycmd7"), (8, "pttycmd9"),
];

pub trait Endpoint: Send {
    fn read(&mut self, bytes: &mut [u8]) -> io::Result<usize>;
    fn write(&mut self, bytes: &[u8]) -> io::Result<usize>;
    fn hung_up(&self) -> bool;
}

pub trait Opener: Send {
    fn open(&mut self, directory: &str, name: &str) -> io::Result<Box<dyn Endpoint>>;
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Interest { pub channel: u8, pub readable: bool, pub writable: bool }

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Ready { pub channel: u8, pub readable: bool, pub writable: bool, pub hung_up: bool }

pub trait Wake: Send + Sync {
    fn wake(&self) -> io::Result<()>;
}

pub trait Poller: Send {
    fn poll(&mut self, interests: &[Interest], timeout: Option<Duration>) -> io::Result<Vec<Ready>>;
}

pub trait Clock: Send + Sync {
    fn now_ms(&self) -> u64;
}

pub struct MonotonicClock(Instant);
impl Default for MonotonicClock { fn default() -> Self { Self(Instant::now()) } }
impl Clock for MonotonicClock {
    fn now_ms(&self) -> u64 { self.0.elapsed().as_millis().min(u64::MAX as u128) as u64 }
}

pub struct Config {
    pub directory: String,
    pub opener: Box<dyn Opener>,
    pub poller: Box<dyn Poller>,
    pub wake: Arc<dyn Wake>,
    pub clock: Arc<dyn Clock>,
    pub output_capacity: usize,
}

impl Config {
    pub fn production() -> io::Result<Self> { sys::config() }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Delivery {
    Response { invocation: u64, error: RadioError, value: Option<Value> },
    Indication(Indication),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SubmitError { Full, Stopped, IdExhausted }

struct Submission { id: u64, request: Request }
struct Queues {
    submissions: VecDeque<Submission>,
    outputs: VecDeque<Delivery>,
    deferred: VecDeque<Delivery>,
    accepted: BTreeMap<u64, u64>,
    next_id: u64,
    outstanding: usize,
    finished: bool,
}

struct Shared {
    queues: Mutex<Queues>,
    changed: Condvar,
    stopped: AtomicBool,
    wake: Arc<dyn Wake>,
    capacity: usize,
}

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    mutex.lock().unwrap_or_else(|poison| poison.into_inner())
}

fn replaceable(left: &Delivery, right: &Delivery) -> bool {
    matches!((left, right),
        (Delivery::Indication(Indication::NetworkStateChanged), Delivery::Indication(Indication::NetworkStateChanged)) |
        (Delivery::Indication(Indication::CurrentSignalStrength(_)), Delivery::Indication(Indication::CurrentSignalStrength(_))))
}

impl Shared {
    fn flush(&self, queues: &mut Queues) {
        while queues.outputs.len() < self.capacity {
            let Some(delivery) = queues.deferred.pop_front() else { break; };
            queues.outputs.push_back(delivery);
        }
    }

    fn push(&self, queues: &mut Queues, delivery: Delivery) {
        if let Some(previous) = queues.deferred.back_mut().or_else(|| queues.outputs.back_mut()) {
            if replaceable(previous, &delivery) { *previous = delivery; return; }
        }
        queues.deferred.push_back(delivery);
        self.flush(queues);
        self.changed.notify_one();
    }

    fn response(&self, queues: &mut Queues, id: u64, error: RadioError, value: Option<Value>) {
        if let Some(invocation) = queues.accepted.get(&id).copied() {
            self.push(queues, Delivery::Response { invocation, error, value });
            queues.accepted.remove(&id);
        }
    }

    fn pressured(&self) -> bool {
        let queues = lock(&self.queues);
        queues.outputs.len() >= self.capacity || !queues.deferred.is_empty()
    }

    fn finish(&self) {
        self.stopped.store(true, Ordering::Release);
        let mut queues = lock(&self.queues);
        queues.submissions.clear();
        let ids: Vec<_> = queues.accepted.keys().copied().collect();
        for id in ids { self.response(&mut queues, id, RadioError::RadioNotAvailable, None); }
        queues.finished = true;
        self.changed.notify_all();
    }
}

pub struct Runtime { shared: Arc<Shared>, thread: Mutex<Option<JoinHandle<()>>> }

impl Runtime {
    pub fn start(config: Config) -> io::Result<Self> {
        if config.output_capacity == 0 || config.output_capacity > 4096 {
            return Err(io::Error::new(io::ErrorKind::InvalidInput, "Invalid output capacity"));
        }
        let shared = Arc::new(Shared {
            queues: Mutex::new(Queues { submissions: VecDeque::new(), outputs: VecDeque::new(),
                deferred: VecDeque::new(), accepted: BTreeMap::new(), next_id: 0, outstanding: 0, finished: false }),
            changed: Condvar::new(), stopped: AtomicBool::new(false), wake: config.wake.clone(),
            capacity: config.output_capacity,
        });
        let owner = shared.clone();
        let thread = thread::Builder::new().name("radio-reactor".into()).spawn(move || {
            let _ = catch_unwind(AssertUnwindSafe(|| {
                if let Ok(mut reactor) = Reactor::new(config, owner.clone()) { reactor.run(); }
            }));
            owner.finish();
        })?;
        Ok(Self { shared, thread: Mutex::new(Some(thread)) })
    }

    pub fn submit(&self, invocation: u64, request: Request) -> Result<(), SubmitError> {
        let mut queues = lock(&self.shared.queues);
        if self.shared.stopped.load(Ordering::Acquire) { return Err(SubmitError::Stopped); }
        if queues.submissions.len() == SUBMISSION_CAPACITY || queues.outstanding == ACCEPTED_LIMIT {
            return Err(SubmitError::Full);
        }
        let id = queues.next_id.checked_add(1).ok_or(SubmitError::IdExhausted)?;
        queues.next_id = id;
        queues.accepted.insert(id, invocation);
        queues.submissions.push_back(Submission { id, request });
        queues.outstanding += 1;
        drop(queues);
        if self.shared.wake.wake().is_err() { self.shared.stopped.store(true, Ordering::Release); }
        Ok(())
    }

    pub fn next_batch(&self, timeout: Duration) -> Vec<Delivery> {
        self.next_batch_wait(Some(timeout)).unwrap_or_default()
    }

    pub fn next_batch_wait(&self, timeout: Option<Duration>) -> Option<Vec<Delivery>> {
        let queues = lock(&self.shared.queues);
        let mut queues = match timeout {
            Some(timeout) => self.shared.changed.wait_timeout_while(queues, timeout,
                |queues| queues.outputs.is_empty() && !queues.finished)
                .unwrap_or_else(|poison| poison.into_inner()).0,
            None => self.shared.changed.wait_while(queues,
                |queues| queues.outputs.is_empty() && !queues.finished)
                .unwrap_or_else(|poison| poison.into_inner()),
        };
        if queues.outputs.is_empty() && queues.finished { return None; }
        let batch: Vec<_> = queues.outputs.drain(..).collect();
        queues.outstanding -= batch.iter().filter(|delivery| matches!(delivery, Delivery::Response { .. })).count();
        self.shared.flush(&mut queues);
        drop(queues);
        let _ = self.shared.wake.wake();
        Some(batch)
    }

    pub fn stop(&self) {
        self.shared.stopped.store(true, Ordering::Release);
        let _ = self.shared.wake.wake();
    }

    pub fn join(&self) {
        self.stop();
        if let Some(thread) = lock(&self.thread).take() { let _ = thread.join(); }
    }
}

impl Drop for Runtime { fn drop(&mut self) { self.join(); } }

struct Reactor {
    config: Config,
    shared: Arc<Shared>,
    core: Core,
    endpoints: BTreeMap<u8, Box<dyn Endpoint>>,
    tokens: BTreeMap<i32, u64>,
    next_token: i32,
    retry_at: u64,
    backoff: u64,
    disconnected: bool,
    blocked: [bool; 9],
}

impl Reactor {
    fn new(config: Config, shared: Arc<Shared>) -> Result<Self, ()> {
        let now = config.clock.now_ms();
        let core = Core::new(now).map_err(|_| ())?;
        Ok(Self { config, shared, core, endpoints: BTreeMap::new(), tokens: BTreeMap::new(),
            next_token: 0, retry_at: now, backoff: INITIAL_BACKOFF_MS, disconnected: false, blocked: [false; 9] })
    }

    fn now(&self) -> u64 { self.config.clock.now_ms() }

    fn collect(&mut self) {
        let outputs = self.core.drain();
        let reset = outputs.iter().any(|output| matches!(output, Output::Indication(Indication::ModemReset { .. })));
        let mut queues = lock(&self.shared.queues);
        for output in outputs {
            match output {
                Output::Response { serial, error, value } => {
                    if let Some(id) = self.tokens.remove(&serial) { self.shared.response(&mut queues, id, error, value); }
                }
                Output::Indication(indication) => self.shared.push(&mut queues, Delivery::Indication(indication)),
            }
        }
        drop(queues);
        if reset { self.disconnect(); }
    }

    fn disconnect(&mut self) {
        self.endpoints.clear();
        self.blocked.fill(false);
        self.disconnected = true;
        self.retry_at = self.now().saturating_add(self.backoff);
        self.backoff = self.backoff.saturating_mul(2).min(MAX_BACKOFF_MS);
    }

    fn reset(&mut self) {
        let _ = self.core.on_reset(self.now());
        self.collect();
        if !self.endpoints.is_empty() { self.disconnect(); }
    }

    fn open(&mut self) {
        let mut endpoints = BTreeMap::new();
        for (channel, name) in TOPOLOGY {
            match self.config.opener.open(&self.config.directory, name) {
                Ok(endpoint) => { endpoints.insert(channel, endpoint); }
                Err(_) => {
                    drop(endpoints);
                    let was_disconnected = self.disconnected;
                    self.reset();
                    if was_disconnected {
                        self.retry_at = self.now().saturating_add(self.backoff);
                        self.backoff = self.backoff.saturating_mul(2).min(MAX_BACKOFF_MS);
                    }
                    return;
                }
            }
        }
        self.endpoints = endpoints;
        if self.disconnected && self.core.on_reconnect(self.now()).is_err() { self.reset(); return; }
        self.disconnected = false;
        self.backoff = INITIAL_BACKOFF_MS;
        self.collect();
    }

    fn submit(&mut self) {
        for _ in 0..16 {
            if self.shared.pressured() || self.shared.stopped.load(Ordering::Acquire) { break; }
            let submission = lock(&self.shared.queues).submissions.pop_front();
            let Some(submission) = submission else { break; };
            loop {
                self.next_token = if self.next_token == i32::MAX { 1 } else { self.next_token + 1 };
                if !self.tokens.contains_key(&self.next_token) { break; }
            }
            self.tokens.insert(self.next_token, submission.id);
            self.core.submit(submission.request, self.next_token, self.now());
            self.collect();
        }
    }

    fn write(&mut self, ready: &[Ready]) {
        for event in ready {
            if event.writable {
                if let Some(blocked) = self.blocked.get_mut(event.channel as usize) { *blocked = false; }
            }
        }
        for _ in 0..IO_ITERATIONS {
            if self.shared.pressured() || self.endpoints.is_empty() { return; }
            let mut progress = false;
            for (channel, token, bytes) in self.core.pending_writes() {
                if self.shared.pressured() { return; }
                if self.blocked[channel as usize] { continue; }
                let Some(endpoint) = self.endpoints.get_mut(&channel) else { return; };
                match endpoint.write(&bytes[..bytes.len().min(READ_BYTES)]) {
                    Ok(0) => { self.reset(); return; }
                    Ok(count) if count <= bytes.len().min(READ_BYTES) => {
                        self.blocked[channel as usize] = false;
                        if self.core.on_written(channel, token, count, self.now()).is_err() { self.reset(); return; }
                        self.collect();
                        if self.endpoints.is_empty() { return; }
                        progress = true;
                    }
                    Err(error) if error.kind() == io::ErrorKind::WouldBlock => { self.blocked[channel as usize] = true; }
                    Err(error) if error.kind() == io::ErrorKind::Interrupted => (),
                    _ => { self.reset(); return; }
                }
            }
            if !progress { break; }
        }
    }

    fn read(&mut self, ready: &[Ready]) {
        let mut bytes = [0u8; READ_BYTES];
        for event in ready {
            for _ in 0..IO_ITERATIONS {
                if self.shared.pressured() { return; }
                let Some(endpoint) = self.endpoints.get_mut(&event.channel) else { break; };
                if event.hung_up || endpoint.hung_up() { self.reset(); return; }
                if !event.readable { break; }
                match endpoint.read(&mut bytes) {
                    Ok(0) => { self.reset(); return; }
                    Ok(count) if count <= bytes.len() => {
                        if self.core.on_readable(event.channel, &bytes[..count], self.now()).is_err() { self.reset(); return; }
                        self.collect();
                        if self.endpoints.is_empty() { return; }
                    }
                    Err(error) if error.kind() == io::ErrorKind::WouldBlock => break,
                    Err(error) if error.kind() == io::ErrorKind::Interrupted => (),
                    _ => { self.reset(); return; }
                }
            }
        }
    }

    fn run(&mut self) {
        while !self.shared.stopped.load(Ordering::Acquire) {
            if self.core.tick(self.now()).is_err() { self.reset(); }
            self.collect();
            let pressured = self.shared.pressured();
            if !pressured {
                if self.endpoints.is_empty() && self.now() >= self.retry_at { self.open(); }
                self.submit();
                self.write(&[]);
            }
            let pressured = self.shared.pressured();
            let pending = self.core.pending_writes();
            let interests: Vec<_> = self.endpoints.keys().map(|channel| Interest {
                channel: *channel, readable: !pressured,
                writable: !pressured && self.blocked[*channel as usize] && pending.iter().any(|entry| entry.0 == *channel),
            }).collect();
            let now = self.now();
            let mut wait = self.core.next_deadline().map(|deadline| deadline.saturating_sub(now));
            if !pressured && self.endpoints.is_empty() {
                let retry = self.retry_at.saturating_sub(now);
                wait = Some(wait.map_or(retry, |wait| wait.min(retry)));
            }
            if !pressured && (!lock(&self.shared.queues).submissions.is_empty() ||
                (!self.endpoints.is_empty() && pending.iter().any(|entry| !self.blocked[entry.0 as usize]))) { wait = Some(0); }
            match self.config.poller.poll(&interests, wait.map(Duration::from_millis)) {
                Ok(ready) => {
                    if ready.iter().any(|event| event.hung_up) { self.reset(); }
                    else if !self.shared.stopped.load(Ordering::Acquire) {
                        self.read(&ready);
                        self.write(&ready);
                    }
                }
                Err(error) if error.kind() == io::ErrorKind::Interrupted => (),
                Err(_) => self.reset(),
            }
        }
    }
}
