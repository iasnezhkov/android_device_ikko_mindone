// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0
mod protocol;

#[cfg(not(all(target_pointer_width = "64", target_endian = "little")))]
compile_error!("volte_md_status requires the 64-bit little-endian CCCI ABI");

use protocol::{Device, EVENT_SIZE};
use std::ffi::{CStr, CString};
use std::fs::{File, OpenOptions};
use std::io::{self, Read};
use std::os::fd::AsRawFd;
use std::os::unix::fs::OpenOptionsExt;
use std::time::Duration;

const PROPERTY: &[u8] = b"vendor.volte_md_status\0";
const TAG: &[u8] = b"VOLTE MD STATUS\0";
const PROP_VALUE_MAX: usize = 92;

extern "C" {
    fn __system_property_set(name: *const libc::c_char, value: *const libc::c_char) -> libc::c_int;
    fn __system_property_get(name: *const libc::c_char, value: *mut libc::c_char) -> libc::c_int;
    fn __android_log_write(
        priority: libc::c_int,
        tag: *const libc::c_char,
        text: *const libc::c_char,
    ) -> libc::c_int;
}

fn log(priority: i32, message: String) {
    if let Ok(message) = CString::new(message) {
        unsafe {
            __android_log_write(priority, TAG.as_ptr().cast(), message.as_ptr());
        }
    }
}

struct AndroidDevice(File);

impl Read for AndroidDevice {
    fn read(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        self.0.read(buffer)
    }
}

impl Device for AndroidDevice {
    fn reset_lock(&mut self, request: u64) -> io::Result<()> {
        let result = unsafe { libc::ioctl(self.0.as_raw_fd(), request as _, -1 as libc::c_int) };
        if result < 0 {
            Err(io::Error::last_os_error())
        } else {
            Ok(())
        }
    }

    fn publish(&mut self, event: i32, value: &'static str) {
        let value = CString::new(value).expect("fixed status strings contain no NUL");
        let mut property = [0 as libc::c_char; PROP_VALUE_MAX];
        let result = unsafe { __system_property_set(PROPERTY.as_ptr().cast(), value.as_ptr()) };
        if result < 0 {
            log(
                6,
                format!("modem status property set failed: result={result}"),
            );
        }
        let length =
            unsafe { __system_property_get(PROPERTY.as_ptr().cast(), property.as_mut_ptr()) };
        property[PROP_VALUE_MAX - 1] = 0;
        if length < 0 {
            log(
                6,
                format!("modem status property get failed: result={length}"),
            );
        }
        let actual = unsafe { CStr::from_ptr(property.as_ptr()) }.to_string_lossy();
        log(3, format!("volte_ims_md_status_read_looper {event} {} count = {EVENT_SIZE} property = {actual}", value.to_string_lossy()));
    }

    fn fault(&mut self, operation: &str, count: i64, error: i32) {
        log(
            6,
            format!("modem {operation} failed: count={count} errno={error}"),
        );
    }
}

fn main() {
    loop {
        if let Ok(file) = OpenOptions::new()
            .read(true)
            .write(true)
            .custom_flags(libc::O_NOCTTY)
            .open("/dev/ccci_mdx_sta")
        {
            protocol::run_session(&mut AndroidDevice(file));
        }
        std::thread::sleep(Duration::from_secs(1));
    }
}
