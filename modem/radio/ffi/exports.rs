/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

use crate::{
    arena::Arena,
    convert::{Codec, Reader, Result},
    wire::*,
};
use mindone_radio_runtime::{Config, Delivery, Runtime, SubmitError};
use std::cell::Cell;
use std::collections::BTreeMap;
use std::mem::{align_of, size_of};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::Duration;

struct Input {
    remaining: Cell<usize>,
}
impl Default for Input {
    fn default() -> Self {
        Self {
            remaining: Cell::new(MR_MAX_INPUT_BYTES as usize),
        }
    }
}

fn address<T>(pointer: *const T) -> Result<()> {
    let start = pointer as usize;
    if start == 0 || start % align_of::<T>() != 0 || start.checked_add(size_of::<T>()).is_none() {
        return Err(MR_INVALID_ARGUMENT);
    }
    Ok(())
}

impl Reader for Input {
    fn array<T: Copy>(&self, data: *const T, len: u32) -> Result<Vec<T>> {
        if len == 0 {
            return Ok(Vec::new());
        }
        address(data)?;
        let bytes = size_of::<T>()
            .checked_mul(len as usize)
            .ok_or(MR_INVALID_ARGUMENT)?;
        if len > MR_MAX_ARRAY_ELEMENTS as u32
            || bytes > isize::MAX as usize
            || (data as usize).checked_add(bytes).is_none()
            || bytes > self.remaining.get()
        {
            return Err(MR_INVALID_ARGUMENT);
        }
        self.remaining.set(self.remaining.get() - bytes);
        Ok(unsafe { std::slice::from_raw_parts(data, len as usize) }.to_vec())
    }
}

struct Batch {
    outputs: Vec<MrOutput>,
    _arena: Arena,
}
impl Batch {
    fn new(deliveries: &[Delivery]) -> Self {
        let mut arena = Arena::default();
        let outputs = deliveries.iter().map(|v| v.encode(&mut arena)).collect();
        Self {
            outputs,
            _arena: arena,
        }
    }
}

#[derive(Default)]
struct Registry {
    next: u64,
    runtimes: BTreeMap<u64, Arc<Runtime>>,
    batches: BTreeMap<u64, usize>,
}
impl Registry {
    fn id(&mut self) -> Result<u64> {
        self.next = self.next.checked_add(1).ok_or(MR_ID_EXHAUSTED)?;
        Ok(self.next)
    }
}
fn registry() -> &'static Mutex<Registry> {
    static REGISTRY: OnceLock<Mutex<Registry>> = OnceLock::new();
    REGISTRY.get_or_init(|| Mutex::new(Registry::default()))
}
fn runtime(handle: u64) -> Result<Arc<Runtime>> {
    registry()
        .lock()
        .map_err(|_| MR_PANIC)?
        .runtimes
        .get(&handle)
        .cloned()
        .ok_or(MR_INVALID_HANDLE)
}
fn guard(action: impl FnOnce() -> Result<()>) -> i32 {
    match catch_unwind(AssertUnwindSafe(action)) {
        Ok(Ok(())) => MR_OK,
        Ok(Err(error)) => error,
        Err(_) => MR_PANIC,
    }
}
unsafe fn write<T>(out: *mut T, value: T) -> Result<()> {
    address(out)?;
    unsafe {
        out.write(value);
    }
    Ok(())
}
fn submit_error(error: SubmitError) -> i32 {
    match error {
        SubmitError::Full => MR_FULL,
        SubmitError::Stopped => MR_STOPPED,
        SubmitError::IdExhausted => MR_ID_EXHAUSTED,
    }
}

#[no_mangle]
pub extern "C" fn mr_abi_version() -> u32 {
    catch_unwind(|| MR_ABI_VERSION as u32).unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn mr_create(out: *mut u64) -> i32 {
    guard(|| {
        unsafe {
            write(out, 0)?;
        }
        let config = Config::production().map_err(|_| MR_IO_ERROR)?;
        let runtime = Arc::new(Runtime::start(config).map_err(|_| MR_IO_ERROR)?);
        let mut registry = registry().lock().map_err(|_| MR_PANIC)?;
        let id = registry.id()?;
        registry.runtimes.insert(id, runtime);
        unsafe { write(out, id) }
    })
}

#[no_mangle]
pub unsafe extern "C" fn mr_submit(
    handle: u64,
    invocation_id: u64,
    request: *const MrRequest,
) -> i32 {
    guard(|| {
        let reader = Input::default();
        let request = reader.array(request, 1)?.pop().ok_or(MR_INVALID_ARGUMENT)?;
        let request = mindone_radio_runtime::core::Request::decode(request, &reader)?;
        runtime(handle)?
            .submit(invocation_id, request)
            .map_err(submit_error)
    })
}

fn wait_deliveries(
    timeout_ms: u32,
    mut next: impl FnMut(Option<Duration>) -> Option<Vec<Delivery>>,
) -> Result<Vec<Delivery>> {
    if timeout_ms != MR_WAIT_FOREVER {
        let deliveries = next(Some(Duration::from_millis(timeout_ms.into()))).unwrap_or_default();
        return if deliveries.is_empty() {
            Err(MR_TIMEOUT)
        } else {
            Ok(deliveries)
        };
    }
    loop {
        match next(None) {
            Some(deliveries) if !deliveries.is_empty() => return Ok(deliveries),
            Some(_) => (),
            None => return Err(MR_STOPPED),
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn mr_next_batch(handle: u64, timeout_ms: u32, out: *mut u64) -> i32 {
    guard(|| {
        unsafe {
            write(out, 0)?;
        }
        let runtime = runtime(handle)?;
        let deliveries = wait_deliveries(timeout_ms, |timeout| runtime.next_batch_wait(timeout))?;
        let batch = Box::new(Batch::new(&deliveries));
        let mut registry = registry().lock().map_err(|_| MR_PANIC)?;
        let id = registry.id()?;
        registry.batches.insert(id, Box::into_raw(batch) as usize);
        unsafe { write(out, id) }
    })
}

#[no_mangle]
pub unsafe extern "C" fn mr_batch_count(handle: u64, out: *mut u32) -> i32 {
    guard(|| {
        unsafe {
            write(out, 0)?;
        }
        let registry = registry().lock().map_err(|_| MR_PANIC)?;
        let pointer = *registry.batches.get(&handle).ok_or(MR_INVALID_HANDLE)?;
        let batch = unsafe { &*(pointer as *const Batch) };
        unsafe {
            write(
                out,
                u32::try_from(batch.outputs.len()).map_err(|_| MR_ID_EXHAUSTED)?,
            )
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn mr_batch_at(handle: u64, index: u32, out: *mut *const MrOutput) -> i32 {
    guard(|| {
        unsafe {
            write(out, std::ptr::null())?;
        }
        let registry = registry().lock().map_err(|_| MR_PANIC)?;
        let pointer = *registry.batches.get(&handle).ok_or(MR_INVALID_HANDLE)?;
        let batch = unsafe { &*(pointer as *const Batch) };
        let item = batch
            .outputs
            .get(index as usize)
            .ok_or(MR_INVALID_ARGUMENT)?;
        unsafe { write(out, item as *const MrOutput) }
    })
}

#[no_mangle]
pub unsafe extern "C" fn mr_batch_free(handle: *mut u64) -> i32 {
    guard(|| {
        address(handle)?;
        let mut registry = registry().lock().map_err(|_| MR_PANIC)?;
        let id = unsafe { *handle };
        let pointer = registry.batches.remove(&id).ok_or(MR_INVALID_HANDLE)?;
        unsafe {
            write(handle, 0)?;
            drop(Box::from_raw(pointer as *mut Batch));
        }
        Ok(())
    })
}

#[no_mangle]
pub extern "C" fn mr_stop(handle: u64) -> i32 {
    guard(|| {
        runtime(handle)?.stop();
        Ok(())
    })
}

#[no_mangle]
pub unsafe extern "C" fn mr_destroy(handle: *mut u64) -> i32 {
    guard(|| {
        address(handle)?;
        let id = unsafe { *handle };
        let runtime = registry()
            .lock()
            .map_err(|_| MR_PANIC)?
            .runtimes
            .remove(&id)
            .ok_or(MR_INVALID_HANDLE)?;
        unsafe {
            write(handle, 0)?;
        }
        runtime.join();
        Ok(())
    })
}

#[no_mangle]
pub extern "C" fn mr_layout_count() -> u32 {
    catch_unwind(|| u32::try_from(layouts().len()).unwrap_or(0)).unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn mr_layout_at(index: u32, out: *mut MrLayout) -> i32 {
    guard(|| {
        address(out)?;
        let layout = layouts()
            .get(index as usize)
            .copied()
            .ok_or(MR_INVALID_ARGUMENT)?;
        unsafe { write(out, layout) }
    })
}

#[cfg(test)]
#[path = "tests/generated.rs"]
mod generated;
#[cfg(test)]
#[path = "tests/safety.rs"]
mod safety;
