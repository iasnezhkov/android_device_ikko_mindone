// SPDX-FileCopyrightText: 2026 The LineageOS Project
// SPDX-License-Identifier: Apache-2.0
include!("../main.rs");

#[cfg(test)]
mod android_api_tests {
    use super::*;
    use std::ffi::CStr;
    use std::io::{Seek, SeekFrom, Write};
    use std::os::unix::fs::{MetadataExt, PermissionsExt};
    use std::path::{Path, PathBuf};
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::Mutex;

    struct State {
        published: Vec<String>,
        logs: Vec<(i32, String)>,
        requests: Vec<(i32, u64, i32)>,
        fail_property: bool,
    }

    static STATE: Mutex<State> = Mutex::new(State {
        published: Vec::new(),
        logs: Vec::new(),
        requests: Vec::new(),
        fail_property: false,
    });
    static NEXT: AtomicUsize = AtomicUsize::new(0);
    static SUITE: Mutex<()> = Mutex::new(());

    #[export_name = "__system_property_set"]
    unsafe extern "C" fn property_set(name: *const libc::c_char, value: *const libc::c_char) -> i32 {
        assert_eq!(unsafe { CStr::from_ptr(name) }.to_bytes(), b"vendor.volte_md_status");
        let mut state = STATE.lock().unwrap();
        state.published.push(unsafe { CStr::from_ptr(value) }.to_str().unwrap().into());
        if state.fail_property { -1 } else { 0 }
    }

    #[export_name = "__system_property_get"]
    unsafe extern "C" fn property_get(name: *const libc::c_char, target: *mut libc::c_char) -> i32 {
        if unsafe { CStr::from_ptr(name) }.to_bytes() != b"vendor.volte_md_status" {
            unsafe { target.write(0) };
            return 0;
        }
        let state = STATE.lock().unwrap();
        if state.fail_property { return -1; }
        let value = state.published.last().unwrap();
        assert!(value.len() < 92);
        unsafe {
            std::ptr::copy_nonoverlapping(value.as_ptr().cast(), target, value.len());
            target.add(value.len()).write(0);
        }
        value.len() as i32
    }

    #[export_name = "__android_log_write"]
    unsafe extern "C" fn log_write(priority: i32, tag: *const libc::c_char, text: *const libc::c_char) -> i32 {
        assert_eq!(unsafe { CStr::from_ptr(tag) }.to_bytes(), b"VOLTE MD STATUS");
        STATE.lock().unwrap().logs.push((priority, unsafe { CStr::from_ptr(text) }.to_str().unwrap().into()));
        0
    }

    #[export_name = "ioctl"]
    extern "C" fn ioctl_mock(fd: i32, request: u64, argument: i32) -> i32 {
        if request != 0x4200 && request != 0x4201 {
            #[cfg(target_os = "android")]
            unsafe { *libc::__errno() = libc::ENOTTY };
            return -1;
        }
        assert!(fd >= 0);
        assert_eq!(argument, -1);
        STATE.lock().unwrap().requests.push((fd, request, argument));
        0
    }

    fn reset() {
        let mut state = STATE.lock().unwrap();
        state.published.clear();
        state.logs.clear();
        state.requests.clear();
        state.fail_property = false;
    }

    fn next_path() -> PathBuf {
        std::env::temp_dir().join(format!("volte-api-mock-{}-{}", std::process::id(), NEXT.fetch_add(1, Ordering::Relaxed)))
    }

    fn device_at(path: &Path, data: &[u8]) -> io::Result<AndroidDevice> {
        let mut file = OpenOptions::new().read(true).write(true).create_new(true).mode(0o600).open(path)?;
        std::fs::remove_file(path)?;
        file.write_all(data)?;
        file.seek(SeekFrom::Start(0))?;
        Ok(AndroidDevice(file))
    }

    fn device(data: &[u8]) -> AndroidDevice {
        device_at(&next_path(), data).unwrap()
    }

    fn event(kind: i32) -> [u8; 56] {
        let mut data = [0xa5; 56];
        data[20..24].copy_from_slice(&kind.to_le_bytes());
        data
    }

    #[test]
    fn production_android_adapter_uses_only_intercepted_api_calls() {
        let _suite = SUITE.lock().unwrap();
        reset();
        let mut input = Vec::new();
        input.extend(event(10));
        input.extend(event(11));
        input.extend(event(12));
        let mut target = device(&input);
        protocol::run_session(&mut target);
        let state = STATE.lock().unwrap();
        assert_eq!(state.published, ["ready", "exception", "stop"]);
        assert_eq!(state.requests.len(), 2);
        assert_eq!(state.requests[0].1, 0x4200);
        assert_eq!(state.requests[1].1, 0x4201);
        assert_eq!(state.requests[0].0, state.requests[1].0);
        assert_eq!(state.logs.iter().filter(|(priority, _)| *priority == 3).count(), 3);
        assert!(state.logs.last().unwrap().1.contains("status read failed: count=0 errno=0"));
    }

    #[test]
    fn android_adapter_short_event_never_sets_a_property() {
        let _suite = SUITE.lock().unwrap();
        for size in 0..56 {
            reset();
            let mut target = device(&event(10)[..size]);
            protocol::run_session(&mut target);
            let state = STATE.lock().unwrap();
            assert!(state.published.is_empty());
            assert_eq!(state.requests.len(), 2);
            assert!(state.logs[0].1.contains("status read failed"));
        }
    }

    #[test]
    fn android_property_failures_are_reported_without_external_writes() {
        let _suite = SUITE.lock().unwrap();
        reset();
        STATE.lock().unwrap().fail_property = true;
        let mut target = device(&event(10));
        protocol::run_session(&mut target);
        let state = STATE.lock().unwrap();
        assert!(state.logs.iter().any(|(_, text)| text.contains("property set failed: result=-1")));
        assert!(state.logs.iter().any(|(_, text)| text.contains("property get failed: result=-1")));
        assert_eq!(state.requests.len(), 2);
    }
    #[test]
    fn android_file_owner_closes_the_descriptor_after_session() {
        let _suite = SUITE.lock().unwrap();
        reset();
        let mut target = device(&event(10));
        let fd = target.0.as_raw_fd();
        protocol::run_session(&mut target);
        drop(target);
        assert_eq!(unsafe { libc::fcntl(fd, libc::F_GETFD) }, -1);
        #[cfg(target_os = "android")]
        assert_eq!(unsafe { *libc::__errno() }, libc::EBADF);
        assert_eq!(STATE.lock().unwrap().requests.len(), 2);
    }

    #[test]
    fn fixture_uses_private_unlinked_storage_and_rewinds_the_same_descriptor() {
        let _suite = SUITE.lock().unwrap();
        let path = next_path();
        let mut target = device_at(&path, b"fixture payload").unwrap();
        assert!(!path.exists());
        let metadata = target.0.metadata().unwrap();
        assert_eq!(metadata.permissions().mode() & 0o777, 0o600);
        assert_eq!(metadata.nlink(), 0);
        let mut actual = Vec::new();
        target.read_to_end(&mut actual).unwrap();
        assert_eq!(actual, b"fixture payload");
    }

    #[test]
    fn fixture_rejects_an_existing_symlink_without_overwriting_its_target() {
        let _suite = SUITE.lock().unwrap();
        let target_path = next_path();
        let fixture_path = next_path();
        let mut protected = OpenOptions::new().read(true).write(true).create_new(true).mode(0o600).open(&target_path).unwrap();
        protected.write_all(b"protected payload").unwrap();
        std::os::unix::fs::symlink(&target_path, &fixture_path).unwrap();
        let error = device_at(&fixture_path, b"replacement").err().unwrap();
        assert_eq!(error.kind(), io::ErrorKind::AlreadyExists);
        assert!(std::fs::symlink_metadata(&fixture_path).unwrap().file_type().is_symlink());
        protected.seek(SeekFrom::Start(0)).unwrap();
        let mut actual = Vec::new();
        protected.read_to_end(&mut actual).unwrap();
        assert_eq!(actual, b"protected payload");
        std::fs::remove_file(fixture_path).unwrap();
        std::fs::remove_file(target_path).unwrap();
    }
}
