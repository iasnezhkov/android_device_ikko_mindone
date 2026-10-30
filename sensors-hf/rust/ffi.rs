/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
use mindone_sensors_core::{self as core, Error};
use std::mem::align_of;
use std::slice;

#[cfg(not(all(target_pointer_width = "64", target_endian = "little")))]
compile_error!("The sensor FFI requires a 64-bit little-endian target");

#[repr(C)]
pub struct DecodedEvent {
    pub timestamp_ns: i64,
    pub words: [i32; 16],
    pub sensor_type: u32,
    pub accuracy: u32,
    pub action: u32,
}

#[repr(C)]
pub struct DecodedInfo {
    pub sensor_type: u32,
    pub gain: u32,
    pub name: [u8; 16],
    pub vendor: [u8; 16],
}

#[repr(C)]
pub struct CalibrationLayout {
    pub length: u32,
    pub bias_offset: u32,
    pub bias_length: u32,
    pub cali_offset: u32,
    pub cali_length: u32,
    pub temp_offset: u32,
    pub temp_length: u32,
}

#[repr(C)]
pub struct CalibrationPayload {
    pub bytes: [u8; 48],
    pub layout: CalibrationLayout,
    pub combined: u32,
}

#[repr(C)]
pub struct CalibrationEvent {
    pub words: [i32; 6],
    pub count: u32,
    pub region: u32,
}

const _: () = {
    assert!(std::mem::size_of::<CalibrationLayout>() == 28);
    assert!(std::mem::align_of::<CalibrationLayout>() == 4);
    assert!(std::mem::size_of::<CalibrationPayload>() == 80);
    assert!(std::mem::align_of::<CalibrationPayload>() == 4);
    assert!(std::mem::offset_of!(CalibrationPayload, layout) == 48);
    assert!(std::mem::offset_of!(CalibrationPayload, combined) == 76);
    assert!(std::mem::size_of::<CalibrationEvent>() == 32);
    assert!(std::mem::align_of::<CalibrationEvent>() == 4);
    assert!(std::mem::offset_of!(CalibrationEvent, count) == 24);
    assert!(std::mem::offset_of!(CalibrationEvent, region) == 28);
};

unsafe fn calibration_words<'a>(input: *const i32, count: usize) -> Result<&'a [i32], i32> {
    if count == 0 {
        return Ok(&[]);
    }
    let bytes = count.checked_mul(std::mem::size_of::<i32>()).ok_or(-1)?;
    if input.is_null() || input as usize % align_of::<i32>() != 0
        || bytes > isize::MAX as usize || (input as usize).checked_add(bytes).is_none()
    {
        return Err(-1);
    }
    Ok(unsafe { slice::from_raw_parts(input, count) })
}

#[no_mangle]
pub unsafe extern "C" fn mindone_hf_calibration_payload(sensor_type: u8,
    bias: *const i32, bias_count: usize, cali: *const i32, cali_count: usize,
    temp: *const i32, temp_count: usize, output: *mut CalibrationPayload) -> i32 {
    if output.is_null() || output as usize % align_of::<CalibrationPayload>() != 0 {
        return -1;
    }
    let inputs = unsafe {
        (calibration_words(bias, bias_count), calibration_words(cali, cali_count),
         calibration_words(temp, temp_count))
    };
    let (bias, cali, temp) = match inputs {
        (Ok(bias), Ok(cali), Ok(temp)) => (bias, cali, temp),
        _ => return -1,
    };
    let payload = match core::calibration::payload(sensor_type, bias, cali, temp) {
        Ok(payload) => payload,
        Err(error) => return status(error),
    };
    let layout = payload.layout;
    unsafe {
        output.write(CalibrationPayload {
            bytes: payload.bytes,
            layout: CalibrationLayout {
                length: layout.length as u32,
                bias_offset: layout.bias.offset as u32,
                bias_length: layout.bias.length as u32,
                cali_offset: layout.cali.offset as u32,
                cali_length: layout.cali.length as u32,
                temp_offset: layout.temp.offset as u32,
                temp_length: layout.temp.length as u32,
            },
            combined: payload.combined.into(),
        });
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn mindone_hf_calibration_event(sensor_type: u8, action: u8,
    words: *const i32, count: usize, output: *mut CalibrationEvent) -> i32 {
    if count != 16 || output.is_null() || output as usize % align_of::<CalibrationEvent>() != 0 {
        return -1;
    }
    let words = match unsafe { calibration_words(words, count) } {
        Ok(words) => words,
        Err(error) => return error,
    };
    let event = match core::calibration::event(sensor_type, action, words) {
        Ok(event) => event,
        Err(error) => return status(error),
    };
    unsafe {
        output.write(CalibrationEvent {
            words: event.words, count: event.count as u32, region: event.kind as u32,
        });
    }
    0
}

fn status(error: Error) -> i32 {
    match error {
        Error::Length => -1,
        Error::Sensor => -2,
        Error::Action => -3,
        Error::Accuracy => -4,
        Error::Timing => -5,
        Error::Capacity => -6,
        Error::State => -7,
    }
}

#[derive(Clone, Copy)]
#[repr(C)]
pub struct SensorState {
    pub period_ns: i64,
    pub latency_ns: i64,
    pub active: u32,
    pub reserved: u32,
}

#[repr(C)]
pub struct StateTransition {
    pub next: SensorState,
    pub command: u32,
    pub reserved: u32,
}

const _: () = {
    assert!(std::mem::size_of::<SensorState>() == 24);
    assert!(std::mem::align_of::<SensorState>() == 8);
    assert!(std::mem::offset_of!(SensorState, period_ns) == 0);
    assert!(std::mem::offset_of!(SensorState, latency_ns) == 8);
    assert!(std::mem::offset_of!(SensorState, active) == 16);
    assert!(std::mem::offset_of!(SensorState, reserved) == 20);
    assert!(std::mem::size_of::<StateTransition>() == 32);
    assert!(std::mem::align_of::<StateTransition>() == 8);
    assert!(std::mem::offset_of!(StateTransition, command) == 24);
    assert!(std::mem::offset_of!(StateTransition, reserved) == 28);
};

unsafe fn load_state(current: *const SensorState, output: *mut StateTransition) -> Result<core::SensorState, i32> {
    if current.is_null() || output.is_null()
        || current as usize % align_of::<SensorState>() != 0
        || output as usize % align_of::<StateTransition>() != 0
    {
        return Err(-1);
    }
    let current = unsafe { current.read() };
    if current.active > 1 || current.reserved != 0 {
        return Err(-1);
    }
    Ok(core::SensorState {
        active: current.active != 0,
        period_ns: current.period_ns,
        latency_ns: current.latency_ns,
    })
}

unsafe fn store_transition(transition: Result<core::StateTransition, Error>, output: *mut StateTransition) -> i32 {
    let transition = match transition {
        Ok(transition) => transition,
        Err(error) => return status(error),
    };
    let command = match transition.command {
        core::StateCommand::None => 0,
        core::StateCommand::Enable => 1,
        core::StateCommand::Flush => 2,
    };
    unsafe {
        output.write(StateTransition {
            next: SensorState {
                period_ns: transition.next.period_ns,
                latency_ns: transition.next.latency_ns,
                active: transition.next.active.into(),
                reserved: 0,
            },
            command,
            reserved: 0,
        });
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn mindone_hf_state_activate(current: *const SensorState, sensor_type: i32,
    enabled: u32, output: *mut StateTransition) -> i32 {
    if enabled > 1 {
        return -1;
    }
    let state = match unsafe { load_state(current, output) } {
        Ok(state) => state,
        Err(error) => return error,
    };
    unsafe { store_transition(state.activate(sensor_type, enabled != 0), output) }
}

#[no_mangle]
pub unsafe extern "C" fn mindone_hf_state_batch(current: *const SensorState, sensor_type: i32,
    period_ns: i64, latency_ns: i64, output: *mut StateTransition) -> i32 {
    let state = match unsafe { load_state(current, output) } {
        Ok(state) => state,
        Err(error) => return error,
    };
    unsafe { store_transition(state.batch(sensor_type, period_ns, latency_ns), output) }
}

#[no_mangle]
pub unsafe extern "C" fn mindone_hf_state_flush(current: *const SensorState, sensor_type: i32,
    one_shot: u32, output: *mut StateTransition) -> i32 {
    if one_shot > 1 {
        return -1;
    }
    let state = match unsafe { load_state(current, output) } {
        Ok(state) => state,
        Err(error) => return error,
    };
    unsafe { store_transition(state.flush(sensor_type, one_shot != 0), output) }
}

#[no_mangle]
pub unsafe extern "C" fn mindone_hf_decode_event(
    input: *const u8,
    length: usize,
    output: *mut DecodedEvent,
) -> i32 {
    if length != core::EVENT_BYTES
        || input.is_null()
        || output.is_null()
        || output as usize % align_of::<DecodedEvent>() != 0
    {
        return -1;
    }
    let event = match core::decode_event(unsafe { slice::from_raw_parts(input, length) }) {
        Ok(event) => event,
        Err(error) => return status(error),
    };
    unsafe {
        output.write(DecodedEvent {
            timestamp_ns: event.timestamp_ns,
            words: event.words,
            sensor_type: event.sensor_type.into(),
            accuracy: event.accuracy.into(),
            action: event.action as u32,
        });
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn mindone_hf_decode_info(
    input: *const u8,
    length: usize,
    output: *mut DecodedInfo,
) -> i32 {
    if length != core::INFO_BYTES
        || input.is_null()
        || output.is_null()
        || output as usize % align_of::<DecodedInfo>() != 0
    {
        return -1;
    }
    let info = match core::decode_info(unsafe { slice::from_raw_parts(input, length) }) {
        Ok(info) => info,
        Err(error) => return status(error),
    };
    unsafe {
        output.write(DecodedInfo {
            sensor_type: info.sensor_type.into(),
            gain: info.gain,
            name: info.name,
            vendor: info.vendor,
        });
    }
    0
}

unsafe fn store_command(
    command: Result<[u8; core::COMMAND_BYTES], Error>,
    output: *mut u8,
    capacity: usize,
) -> i32 {
    if output.is_null() || capacity < core::COMMAND_BYTES {
        return -1;
    }
    match command {
        Ok(command) => {
            unsafe { std::ptr::copy_nonoverlapping(command.as_ptr(), output, command.len()) };
            0
        }
        Err(error) => status(error),
    }
}

#[no_mangle]
pub unsafe extern "C" fn mindone_hf_enable_command(
    sensor_type: u8,
    enabled: u8,
    delay_ns: i64,
    latency_ns: i64,
    output: *mut u8,
    capacity: usize,
) -> i32 {
    if enabled > 1 {
        return -1;
    }
    unsafe {
        store_command(
            core::enable_command(sensor_type, enabled != 0, delay_ns, latency_ns),
            output,
            capacity,
        )
    }
}

#[no_mangle]
pub unsafe extern "C" fn mindone_hf_flush_command(
    sensor_type: u8,
    output: *mut u8,
    capacity: usize,
) -> i32 {
    unsafe { store_command(core::flush_command(sensor_type), output, capacity) }
}

#[no_mangle]
pub unsafe extern "C" fn mindone_hf_calibration_command(
    sensor_type: u8,
    input: *const u8,
    length: usize,
    output: *mut u8,
    capacity: usize,
) -> i32 {
    if length > core::CALIBRATION_BYTES || (input.is_null() && length != 0) {
        return -1;
    }
    let data = if length == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(input, length) }
    };
    let command = core::calibration_command(sensor_type, data);
    unsafe { store_command(command, output, capacity) }
}
