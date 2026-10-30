/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use super::*;
use mindone_radio_modem::types as m;
use mindone_radio_network::types as n;
use mindone_radio_runtime::core::{Indication, Request, Value};

#[test]
fn guard_catches_panic() {
    assert_eq!(guard(|| panic!("boundary")), MR_PANIC);
}

#[test]
fn invalid_tags_and_presence() {
    let input = Input::default();
    assert!(Request::decode(MrRequest::default(), &input).is_err());
    assert!(Value::decode(MrValue::default(), &input).is_err());
    assert!(Indication::decode(MrIndication::default(), &input).is_err());
    assert!(Delivery::decode(MrOutput::default(), &input).is_err());
    assert!(bool::decode(2, &input).is_err());
    assert!(n::Rat::decode(999, &input).is_err());
    assert!(n::SignalStrength::decode(
        MrSignalStrength {
            gsm_present: 2,
            ..Default::default()
        },
        &input
    )
    .is_err());
    assert!(n::GsmSignal::decode(
        MrGsmSignal {
            signal_strength_present: 2,
            ..Default::default()
        },
        &input
    )
    .is_err());
    assert!(n::RegStateResult::decode(
        MrRegStateResult {
            cell_identity_present: 2,
            ..Default::default()
        },
        &input
    )
    .is_err());
    assert!(Delivery::decode(
        MrOutput {
            kind: 1,
            value_present: 2,
            ..Default::default()
        },
        &input
    )
    .is_err());
    assert!(m::RadioError::decode(500, &input).is_err());
}

#[test]
fn invalid_borrowed_memory_shapes() {
    let input = Input::default();
    assert!(input.array::<i32>(std::ptr::null(), 1).is_err());
    assert!(input
        .array::<i32>(std::ptr::dangling::<u8>().cast(), 1)
        .is_err());
    assert!(input.array::<u8>(std::ptr::dangling(), u32::MAX).is_err());
    assert!(input
        .array::<u64>((usize::MAX - 7) as *const u64, 4)
        .is_err());
    assert_eq!(
        input.array::<u8>(std::ptr::null(), 0).unwrap(),
        Vec::<u8>::new()
    );
    let bytes = [0xff];
    assert!(String::decode(
        MrString {
            data: bytes.as_ptr(),
            len: 1
        },
        &input
    )
    .is_err());
    let values = [0u64; 1];
    let input = Input {
        remaining: Cell::new(1),
    };
    assert!(input.array(values.as_ptr(), 1).is_err());
}

#[test]
fn invalid_submission_precedes_runtime_access() {
    let invalid = MrRequest {
        kind: 107,
        boolean: 2,
        ..Default::default()
    };
    assert_eq!(
        unsafe { mr_submit(u64::MAX, 1, &invalid) },
        MR_INVALID_ARGUMENT
    );
    let invalid = MrRequest {
        kind: 108,
        device_state_type: 9,
        ..Default::default()
    };
    assert_eq!(
        unsafe { mr_submit(u64::MAX, 1, &invalid) },
        MR_INVALID_ARGUMENT
    );
    assert_eq!(
        unsafe { mr_submit(u64::MAX, 1, std::ptr::null()) },
        MR_INVALID_ARGUMENT
    );
    let valid = MrRequest {
        kind: 1,
        ..Default::default()
    };
    assert_eq!(unsafe { mr_submit(u64::MAX, 1, &valid) }, MR_INVALID_HANDLE);
}

#[test]
fn request_copies_nested_arrays() {
    let mut thresholds = vec![1, 7, 9];
    let criteria = MrSignalCriteria {
        thresholds_data: thresholds.as_ptr(),
        thresholds_len: 3,
        ..Default::default()
    };
    let wire = MrRequest {
        kind: 9,
        signal_criteria_data: &criteria,
        signal_criteria_len: 1,
        ..Default::default()
    };
    let decoded = Request::decode(wire, &Input::default()).unwrap();
    thresholds.fill(0);
    drop(thresholds);
    assert!(
        matches!(decoded, Request::Network(n::Request::SetSignalStrengthReportingCriteria(v)) if v[0].thresholds == [1,7,9])
    );
}

#[test]
fn batch_owns_payload_until_release() {
    let deliveries = vec![Delivery::Response {
        invocation: u64::MAX,
        error: m::RadioError::None,
        value: Some(Value::Modem(m::Value::Baseband("日本語\0baseband".into()))),
    }];
    let batch = Box::new(Batch::new(&deliveries));
    drop(deliveries);
    let mut registry = registry().lock().unwrap();
    let mut id = registry.id().unwrap();
    registry.batches.insert(id, Box::into_raw(batch) as usize);
    drop(registry);
    let duplicate = id;
    let mut count = 0;
    assert_eq!(unsafe { mr_batch_count(id, &mut count) }, MR_OK);
    assert_eq!(count, 1);
    let mut item = std::ptr::null();
    assert_eq!(unsafe { mr_batch_at(id, 0, &mut item) }, MR_OK);
    let value = unsafe { Delivery::decode(*item, &Input::default()) }.unwrap();
    assert!(
        matches!(value,Delivery::Response {invocation:u64::MAX,value:Some(Value::Modem(m::Value::Baseband(ref s))),..} if s == "日本語\0baseband")
    );
    assert_eq!(
        unsafe { mr_batch_at(id, 1, &mut item) },
        MR_INVALID_ARGUMENT
    );
    assert!(item.is_null());
    assert_eq!(unsafe { mr_batch_free(&mut id) }, MR_OK);
    assert_eq!(id, 0);
    assert_eq!(unsafe { mr_batch_free(&mut id) }, MR_INVALID_HANDLE);
    let mut duplicate = duplicate;
    assert_eq!(unsafe { mr_batch_free(&mut duplicate) }, MR_INVALID_HANDLE);
    assert_eq!(
        unsafe { mr_batch_count(duplicate, &mut count) },
        MR_INVALID_HANDLE
    );
}

#[test]
fn null_outputs_and_invalid_handles() {
    assert_eq!(
        unsafe { mr_create(std::ptr::null_mut()) },
        MR_INVALID_ARGUMENT
    );
    assert_eq!(
        unsafe { mr_next_batch(0, 0, std::ptr::null_mut()) },
        MR_INVALID_ARGUMENT
    );
    assert_eq!(
        unsafe { mr_batch_count(0, std::ptr::null_mut()) },
        MR_INVALID_ARGUMENT
    );
    assert_eq!(
        unsafe { mr_batch_at(0, 0, std::ptr::null_mut()) },
        MR_INVALID_ARGUMENT
    );
    assert_eq!(
        unsafe { mr_batch_free(std::ptr::null_mut()) },
        MR_INVALID_ARGUMENT
    );
    assert_eq!(
        unsafe { mr_destroy(std::ptr::null_mut()) },
        MR_INVALID_ARGUMENT
    );
    assert_eq!(
        unsafe { mr_layout_at(0, std::ptr::null_mut()) },
        MR_INVALID_ARGUMENT
    );
    assert_eq!(mr_stop(0), MR_INVALID_HANDLE);
    assert_eq!(mr_abi_version(), 2);
    let mut output = 999;
    assert_eq!(
        unsafe { mr_next_batch(0, 0, &mut output) },
        MR_INVALID_HANDLE
    );
    assert_eq!(output, 0);
    let mut layout = MrLayout::default();
    assert_eq!(
        unsafe { mr_layout_at(u32::MAX, &mut layout) },
        MR_INVALID_ARGUMENT
    );
}

struct MissingEndpoints;
impl mindone_radio_runtime::Opener for MissingEndpoints {
    fn open(
        &mut self,
        _: &str,
        _: &str,
    ) -> std::io::Result<Box<dyn mindone_radio_runtime::Endpoint>> {
        Err(std::io::Error::from(std::io::ErrorKind::NotFound))
    }
}
struct IdlePoller;
impl mindone_radio_runtime::Poller for IdlePoller {
    fn poll(
        &mut self,
        _: &[mindone_radio_runtime::Interest],
        _: Option<Duration>,
    ) -> std::io::Result<Vec<mindone_radio_runtime::Ready>> {
        std::thread::sleep(Duration::from_millis(1));
        Ok(Vec::new())
    }
}
struct IdleWake;
impl mindone_radio_runtime::Wake for IdleWake {
    fn wake(&self) -> std::io::Result<()> {
        Ok(())
    }
}
fn isolated_runtime() -> u64 {
    let config = Config {
        directory: String::new(),
        opener: Box::new(MissingEndpoints),
        poller: Box::new(IdlePoller),
        wake: Arc::new(IdleWake),
        clock: Arc::new(mindone_radio_runtime::MonotonicClock::default()),
        output_capacity: 4,
    };
    let runtime = Arc::new(Runtime::start(config).unwrap());
    let mut registry = registry().lock().unwrap();
    let handle = registry.id().unwrap();
    registry.runtimes.insert(handle, runtime);
    handle
}

#[test]
fn runtime_lifecycle_without_device_io() {
    let mut handle = isolated_runtime();
    let duplicate = handle;
    let request = MrRequest {
        kind: 1,
        ..Default::default()
    };
    assert_eq!(unsafe { mr_submit(handle, u64::MAX, &request) }, MR_OK);
    assert_eq!(unsafe { mr_submit(handle, u64::MAX, &request) }, MR_OK);
    assert_eq!(mr_stop(handle), MR_OK);
    assert_eq!(unsafe { mr_submit(handle, 8, &request) }, MR_STOPPED);
    assert_eq!(unsafe { mr_destroy(&mut handle) }, MR_OK);
    assert_eq!(handle, 0);
    assert_eq!(mr_stop(duplicate), MR_INVALID_HANDLE);
    assert_eq!(unsafe { mr_destroy(&mut handle) }, MR_INVALID_HANDLE);
}

#[test]
fn runtime_batch_transfer_and_shutdown() {
    let mut handle = isolated_runtime();
    let request = MrRequest {
        kind: 1,
        ..Default::default()
    };
    assert_eq!(unsafe { mr_submit(handle, 123, &request) }, MR_OK);
    runtime(handle).unwrap().join();
    let mut responses = 0;
    for _ in 0..4 {
        let mut batch = 0;
        let status = unsafe { mr_next_batch(handle, 10, &mut batch) };
        if status == MR_TIMEOUT {
            break;
        }
        assert_eq!(status, MR_OK);
        let mut count = 0;
        assert_eq!(unsafe { mr_batch_count(batch, &mut count) }, MR_OK);
        for index in 0..count {
            let mut item = std::ptr::null();
            assert_eq!(unsafe { mr_batch_at(batch, index, &mut item) }, MR_OK);
            if let Delivery::Response {
                invocation,
                error,
                value,
            } = unsafe { Delivery::decode(*item, &Input::default()) }.unwrap()
            {
                assert_eq!(invocation, 123);
                assert_eq!(error, m::RadioError::RadioNotAvailable);
                assert_eq!(value, None);
                responses += 1;
            }
        }
        assert_eq!(unsafe { mr_batch_free(&mut batch) }, MR_OK);
    }
    assert_eq!(responses, 1);
    assert_eq!(unsafe { mr_destroy(&mut handle) }, MR_OK);
}

#[test]
fn absent_and_present_empty_values_stay_distinct() {
    let signals = n::SignalStrength {
        gsm: Some(n::GsmSignal {
            signal_strength: None,
            bit_error_rate: Some(0),
        }),
        lte: Some(n::LteSignal {
            rsrp: Some(0),
            rsrq: None,
            rssnr: Some(i32::MIN),
        }),
        wcdma: None,
    };
    let mut arena = Arena::default();
    let wire = signals.encode(&mut arena);
    assert_eq!(
        n::SignalStrength::decode(wire, &Input::default()).unwrap(),
        signals
    );
    let wire = Delivery::Response {
        invocation: 0,
        error: m::RadioError::None,
        value: Some(Value::Modem(m::Value::Baseband(String::new()))),
    }
    .encode(&mut arena);
    assert_eq!(wire.value_present, 1);
    assert_eq!(wire.value.text.len, 0);
    assert!(wire.value.text.data.is_null());
    assert!(
        matches!(Delivery::decode(wire, &Input::default()).unwrap(), Delivery::Response { value: Some(Value::Modem(m::Value::Baseband(s))), .. } if s.is_empty())
    );
}

#[test]
fn boolean_false_and_empty_vectors_round_trip() {
    for request in [
        Request::Modem(m::Request::EnableModem(false)),
        Request::Modem(m::Request::SetGsmBroadcastConfig(Vec::new())),
        Request::Network(n::Request::SetSignalStrengthReportingCriteria(Vec::new())),
    ] {
        let mut arena = Arena::default();
        let wire = request.encode(&mut arena);
        assert_eq!(Request::decode(wire, &Input::default()).unwrap(), request);
    }
    for value in [
        Value::Modem(m::Value::ReducedFeatureSet1(false)),
        Value::Network(n::Value::Cells(Vec::new())),
        Value::Modem(m::Value::Slots(Vec::new())),
        Value::Modem(m::Value::CurrentCalls(Vec::new())),
    ] {
        let mut arena = Arena::default();
        let wire = value.encode(&mut arena);
        assert_eq!(Value::decode(wire, &Input::default()).unwrap(), value);
    }
}

#[test]
fn submit_error_statuses() {
    assert_eq!(submit_error(SubmitError::Full), MR_FULL);
    assert_eq!(submit_error(SubmitError::Stopped), MR_STOPPED);
    assert_eq!(submit_error(SubmitError::IdExhausted), MR_ID_EXHAUSTED);
}

#[test]
fn batch_handle_crosses_dispatcher_threads() {
    let deliveries = vec![Delivery::Indication(Indication::NetworkStateChanged)];
    let batch = Box::new(Batch::new(&deliveries));
    let mut registry = registry().lock().unwrap();
    let handle = registry.id().unwrap();
    registry
        .batches
        .insert(handle, Box::into_raw(batch) as usize);
    drop(registry);
    std::thread::spawn(move || {
        let mut handle = handle;
        let mut item = std::ptr::null();
        assert_eq!(unsafe { mr_batch_at(handle, 0, &mut item) }, MR_OK);
        assert_eq!(unsafe { (*item).indication.kind }, 1);
        assert_eq!(unsafe { mr_batch_free(&mut handle) }, MR_OK);
    })
    .join()
    .unwrap();
}

#[test]
fn outstanding_batch_survives_runtime_destruction() {
    let mut handle = isolated_runtime();
    let request = MrRequest {
        kind: 1,
        ..Default::default()
    };
    assert_eq!(unsafe { mr_submit(handle, 99, &request) }, MR_OK);
    runtime(handle).unwrap().join();
    let mut batch = 0;
    assert_eq!(unsafe { mr_next_batch(handle, 10, &mut batch) }, MR_OK);
    assert_eq!(unsafe { mr_destroy(&mut handle) }, MR_OK);
    let mut count = 0;
    assert_eq!(unsafe { mr_batch_count(batch, &mut count) }, MR_OK);
    assert!(count > 0);
    let mut item = std::ptr::null();
    assert_eq!(unsafe { mr_batch_at(batch, 0, &mut item) }, MR_OK);
    assert!(unsafe { Delivery::decode(*item, &Input::default()) }.is_ok());
    assert_eq!(unsafe { mr_batch_free(&mut batch) }, MR_OK);
}

#[test]
fn handle_exhaustion_never_wraps() {
    let mut registry = Registry {
        next: u64::MAX,
        ..Default::default()
    };
    assert_eq!(registry.id(), Err(MR_ID_EXHAUSTED));
    assert_eq!(registry.next, u64::MAX);
}

#[derive(Default)]
struct EventWake {
    generation: Mutex<u64>,
    changed: std::sync::Condvar,
}
impl mindone_radio_runtime::Wake for EventWake {
    fn wake(&self) -> std::io::Result<()> {
        *self.generation.lock().unwrap() += 1;
        self.changed.notify_one();
        Ok(())
    }
}
struct EventPoller {
    wake: Arc<EventWake>,
    observed: u64,
}
impl mindone_radio_runtime::Poller for EventPoller {
    fn poll(
        &mut self,
        _: &[mindone_radio_runtime::Interest],
        _: Option<Duration>,
    ) -> std::io::Result<Vec<mindone_radio_runtime::Ready>> {
        let generation = self.wake.generation.lock().unwrap();
        let generation = self
            .wake
            .changed
            .wait_while(generation, |generation| *generation == self.observed)
            .unwrap();
        self.observed = *generation;
        Ok(Vec::new())
    }
}
struct FrozenClock;
impl mindone_radio_runtime::Clock for FrozenClock {
    fn now_ms(&self) -> u64 {
        0
    }
}
fn idle_event_runtime() -> u64 {
    let wake = Arc::new(EventWake::default());
    let config = Config {
        directory: String::new(),
        opener: Box::new(MissingEndpoints),
        poller: Box::new(EventPoller {
            wake: wake.clone(),
            observed: 0,
        }),
        wake,
        clock: Arc::new(FrozenClock),
        output_capacity: 4,
    };
    let runtime = Arc::new(Runtime::start(config).unwrap());
    let mut registry = registry().lock().unwrap();
    let handle = registry.id().unwrap();
    registry.runtimes.insert(handle, runtime);
    handle
}

#[test]
fn infinite_wait_stops_promptly_and_drains() {
    let mut handle = idle_event_runtime();
    let waiter_handle = handle;
    let (sender, receiver) = std::sync::mpsc::channel();
    let waiter = std::thread::spawn(move || {
        let mut responses = 0;
        loop {
            let mut batch = 0;
            let status = unsafe { mr_next_batch(waiter_handle, MR_WAIT_FOREVER, &mut batch) };
            if status == MR_STOPPED {
                assert_eq!(batch, 0);
                sender.send(responses).unwrap();
                return;
            }
            assert_eq!(status, MR_OK);
            let mut count = 0;
            assert_eq!(unsafe { mr_batch_count(batch, &mut count) }, MR_OK);
            for index in 0..count {
                let mut item = std::ptr::null();
                assert_eq!(unsafe { mr_batch_at(batch, index, &mut item) }, MR_OK);
                if unsafe { (*item).kind } == MR_OUTPUT_RESPONSE as u32 {
                    responses += 1;
                }
            }
            assert_eq!(unsafe { mr_batch_free(&mut batch) }, MR_OK);
        }
    });
    let request = MrRequest {
        kind: MR_REQUEST_NETWORK_GET_SIGNAL_STRENGTH as u32,
        ..Default::default()
    };
    assert_eq!(unsafe { mr_submit(handle, 77, &request) }, MR_OK);
    std::thread::sleep(Duration::from_millis(30));
    assert_eq!(mr_stop(handle), MR_OK);
    assert_eq!(receiver.recv_timeout(Duration::from_secs(1)).unwrap(), 1);
    waiter.join().unwrap();
    assert_eq!(unsafe { mr_destroy(&mut handle) }, MR_OK);
}

#[test]
fn infinite_idle_wait_enters_next_batch_once() {
    use std::sync::atomic::{AtomicUsize, Ordering};
    let mut handle = idle_event_runtime();
    loop {
        let mut batch = 0;
        let status = unsafe { mr_next_batch(handle, 10, &mut batch) };
        if status == MR_TIMEOUT {
            break;
        }
        assert_eq!(status, MR_OK);
        assert_eq!(unsafe { mr_batch_free(&mut batch) }, MR_OK);
    }
    let owner = runtime(handle).unwrap();
    let count = Arc::new(AtomicUsize::new(0));
    let observed = count.clone();
    let (entered, entered_receiver) = std::sync::mpsc::channel();
    let (done, done_receiver) = std::sync::mpsc::channel();
    let waiter = std::thread::spawn(move || {
        let result = wait_deliveries(MR_WAIT_FOREVER, |timeout| {
            assert_eq!(timeout, None);
            observed.fetch_add(1, Ordering::SeqCst);
            entered.send(()).unwrap();
            owner.next_batch_wait(timeout)
        });
        done.send(result.err()).unwrap();
    });
    entered_receiver
        .recv_timeout(Duration::from_secs(1))
        .unwrap();
    assert!(done_receiver
        .recv_timeout(Duration::from_millis(120))
        .is_err());
    assert_eq!(count.load(Ordering::SeqCst), 1);
    assert_eq!(mr_stop(handle), MR_OK);
    assert_eq!(
        done_receiver.recv_timeout(Duration::from_secs(1)).unwrap(),
        Some(MR_STOPPED)
    );
    waiter.join().unwrap();
    assert_eq!(count.load(Ordering::SeqCst), 1);
    assert_eq!(unsafe { mr_destroy(&mut handle) }, MR_OK);
}

#[test]
fn finite_wait_preserves_timeout_semantics() {
    let mut handle = isolated_runtime();
    let mut batch = 999;
    assert_eq!(unsafe { mr_next_batch(handle, 0, &mut batch) }, MR_TIMEOUT);
    assert_eq!(batch, 0);
    assert_eq!(mr_stop(handle), MR_OK);
    runtime(handle).unwrap().join();
    assert_eq!(unsafe { mr_next_batch(handle, 0, &mut batch) }, MR_TIMEOUT);
    assert_eq!(
        unsafe { mr_next_batch(handle, MR_WAIT_FOREVER, &mut batch) },
        MR_STOPPED
    );
    assert_eq!(unsafe { mr_destroy(&mut handle) }, MR_OK);
}
