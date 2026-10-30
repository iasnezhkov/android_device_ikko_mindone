/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */
use mindone_mux_android_properties::AndroidProperties;
use mindone_mux_platform::{PosixEndpoint, PtyFactory, LINK_DIRECTORY};
use mindone_mux_runtime::Limits;
use mindone_mux_service::{Clock, Config, Exit, MonotonicClock, Opener, Phase, Properties, Property, Service, Value};
use std::ffi::{c_char, c_int, CString, OsString};
use std::io;
use std::os::unix::ffi::OsStrExt;
use std::path::{Path, PathBuf};

extern "C" {
    fn getopt(argc: c_int, argv: *const *mut c_char, options: *const c_char) -> c_int;
    static mut optarg: *mut c_char;
    fn atoi(value: *const c_char) -> c_int;
    fn mindone_mux_initialize() -> c_int;
    fn mindone_mux_signal() -> c_int;
    fn mindone_mux_drop_root() -> c_int;
    fn mindone_mux_get_property(key: *const c_char, value: *mut c_char) -> c_int;
    fn mindone_mux_log(priority: c_int, message: *const c_char);
}

const USAGE: &str = "usage: gsm0710muxd-rs [-s port] [-f frame_size] [-n ports] [-m basic] [-b baud_index] [-P pin] [-r link_dir] [-v]\nGSM 07.10 basic-mode multiplexer for the MediaTek modem AT port. Channels follow\nro.boot.opt_sim_count or persist.radio.multisim.config; -n, -d and -o are ignored.\nSets vendor.ril.mux.start=1 then vendor.ril.mtk=1 after setup; session rebuilds\nset vendor.ril.mtk.restart=1. SIGTERM and SIGINT close endpoints and exit zero.\n-r uses an existing private directory; /dev/radio requires radio:radio 0770.\nSerial termios is unchanged on Android; -b selects the CMUX baud index.\n";

struct Arguments { serial: PathBuf, directory: PathBuf, frame: usize, baud: i32, pin: i32, verbose: bool }

enum ArgumentError { Usage, Mode }

fn arguments(args: Vec<OsString>) -> Result<Arguments, ArgumentError> {
    let strings: Vec<CString> = args.iter().map(|arg| CString::new(arg.as_bytes()).map_err(|_| ArgumentError::Usage)).collect::<Result<_, _>>()?;
    let mut pointers: Vec<_> = strings.iter().map(|arg| arg.as_ptr() as *mut c_char).collect();
    pointers.push(std::ptr::null_mut());
    let mut result = Arguments { serial: "/dev/ttyC0".into(), directory: LINK_DIRECTORY.into(), frame: 512, baud: 5, pin: -1, verbose: false };
    let mut frame = 512;
    loop {
        let option = unsafe { getopt(strings.len() as c_int, pointers.as_ptr(), c"s:f:n:m:b:P:r:vdo".as_ptr()) };
        if option == -1 { break; }
        let bytes = || unsafe { std::ffi::CStr::from_ptr(optarg).to_bytes() };
        match option as u8 {
            b's' => result.serial = std::ffi::OsStr::from_bytes(bytes()).into(),
            b'r' => result.directory = std::ffi::OsStr::from_bytes(bytes()).into(),
            b'f' => frame = unsafe { atoi(optarg) },
            b'b' => result.baud = unsafe { atoi(optarg) },
            b'P' => result.pin = unsafe { atoi(optarg) },
            b'm' if bytes() == b"basic" => {},
            b'm' => return Err(ArgumentError::Mode),
            b'n' | b'd' | b'o' => {},
            b'v' => result.verbose = true,
            _ => return Err(ArgumentError::Usage),
        }
    }
    if !(1..=32767).contains(&frame) { return Err(ArgumentError::Usage); }
    result.frame = frame as usize;
    Ok(result)
}

fn log(priority: i32, message: &str) {
    if let Ok(message) = CString::new(message) { unsafe { mindone_mux_log(priority, message.as_ptr()); } }
}

fn property(key: &str) -> String {
    let key = CString::new(key).unwrap();
    let mut value = [0u8; 92];
    let size = unsafe { mindone_mux_get_property(key.as_ptr(), value.as_mut_ptr().cast()) };
    String::from_utf8_lossy(&value[..(size.max(0) as usize).min(91)]).into_owned()
}

fn sims(boot: &str, config: &str) -> usize {
    match boot.as_bytes().first() {
        Some(value @ b'1'..=b'4') => usize::from(value - b'0'),
        _ => match config { "dsds" | "dsda" => 2, "tsts" => 3, "qsqs" => 4, _ => 1 },
    }
}

struct LoggedProperties;
impl Properties for LoggedProperties {
    fn set(&mut self, property: Property, value: Value) -> bool {
        let accepted = AndroidProperties.set(property, value);
        if accepted { log(4, &format!("mux property {property:?}={value:?}")); }
        else { log(6, &format!("mux property {property:?} rejected")); }
        accepted
    }
}

struct OpenBoard { serial: PathBuf, directory: PathBuf, sims: usize }
impl Opener for OpenBoard {
    fn open(&mut self) -> io::Result<(PosixEndpoint, PtyFactory)> {
        let serial = PosixEndpoint::open_serial(&self.serial)?;
        if unsafe { mindone_mux_drop_root() } != 0 { return Err(io::Error::other("Cannot switch to radio")); }
        let factory = if self.directory == Path::new(LINK_DIRECTORY) { PtyFactory::radio_shared(self.sims)? }
            else { PtyFactory::new(&self.directory, self.sims, cfg!(target_os = "android"))? };
        Ok((serial, factory))
    }
}

fn run<P: Properties, O: Opener, C: Clock>(mut service: Service<P, O, C>, mut turn: impl FnMut(), stopped: impl Fn() -> bool) -> Exit {
    let mut previous = service.phase();
    loop {
        if stopped() { service.stop(); }
        turn();
        let phase = service.step(1000, Limits::default());
        if phase != previous {
            log(4, &format!("mux lifecycle {phase:?}"));
            if let Some(error) = service.runtime_error() { log(6, &format!("mux runtime {error:?}")); }
            previous = phase;
        }
        if let Phase::Finished(exit) = phase { return exit; }
    }
}

fn main() {
    let argv: Vec<_> = std::env::args_os().collect();
    let program = argv.first().cloned().unwrap_or_else(|| OsString::from("gsm0710muxd-rs"));
    let args = match arguments(argv) {
        Ok(args) => args,
        Err(ArgumentError::Mode) => { eprintln!("only -m basic is supported"); std::process::exit(2); }
        Err(ArgumentError::Usage) => { eprint!("{}", USAGE.replace("gsm0710muxd-rs", &program.to_string_lossy())); std::process::exit(2); }
    };
    if property("ro.vendor.mtk_mipc_support").starts_with('1') { log(4, "MIPC is in use, the AT multiplexer is not needed"); return; }
    if unsafe { mindone_mux_initialize() } != 0 { std::process::exit(1); }
    let sims = sims(&property("ro.boot.opt_sim_count"), &property("persist.radio.multisim.config"));
    log(4, &format!("gsm0710muxd-rs on {}: {} channels, frame size {}", args.serial.display(), 11 * sims + 2, args.frame));
    if args.verbose { log(3, "verbose mux lifecycle logging enabled"); }
    let opener = OpenBoard { serial: args.serial, directory: args.directory, sims };
    let config = Config { frame_limit: args.frame, baud_index: 5, user_build: property("vendor.ril.emulation.userload").starts_with('1') || property("ro.build.type") == "user" };
    let mut service = Service::new(LoggedProperties, opener, MonotonicClock::default(), config).unwrap();
    service.daemon_mode(args.baud, args.pin);
    let exit = run(service, || {}, || unsafe { mindone_mux_signal() != 0 });
    if matches!(exit, Exit::InternalFailure | Exit::PropertyRejected | Exit::UnsupportedLocalClose) { std::process::exit(1); }
}

#[cfg(test)]
#[path = "tests/daemon.rs"]
mod tests;
