# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import subprocess
import tempfile


INPUTS = ("lib.rs", "session.rs", "transport.rs", "mcc.rs", "chat.rs", "runtime.rs",
          "Android.bp", "mindone_mux.c", "tests/runtime.rs", "tests/runtime_support.rs", "tests/runtime_check.py",
          "../../volte_md_status/tests/android_check.py")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description="Check the optional owned-endpoint Rust mux runtime with bounded anonymous host UnixStream fixtures and injected faults; no device, CCCI or Android services are accessed")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC", "rustc"))
    parser.add_argument("--fault-controls", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source_root = tests.parent
    before = {name: digest(source_root / name) for name in INPUTS}
    c_source = (source_root / "mindone_mux.c").read_text()
    runtime_source = (source_root / "runtime.rs").read_text()
    transport_source = (source_root / "transport.rs").read_text()

    def c_constant(name):
        return int(re.search(r"^#define\s+" + name + r"\s+(\d+)\s*$", c_source, re.M).group(1))

    def rust_constant(source, name):
        return int(re.search(r"^pub const " + name + r": (?:usize|u64) = (\d+);$", source, re.M).group(1))

    if not re.search(r"^#define\s+MAX_CHANS\s+\(MAX_SIMS \* KINDS \+ 2\)$", c_source, re.M):
        raise ValueError("The production C channel-bound expression changed")
    source_bounds = {
        "maximum_channels": c_constant("MAX_SIMS") * c_constant("KINDS") + 2,
        "tx_bytes": c_constant("TXQ_CAP"), "rx_bytes": c_constant("RX_CAP"),
        "pending_bytes": c_constant("PENDING_CAP"), "application_read_bytes": c_constant("PTY_CHUNK"),
        "peer_close_flush_ms": c_constant("EXIT_FLUSH_MS"),
    }
    rust_bounds = {
        "maximum_channels": rust_constant(runtime_source, "MAX_CHANNELS"),
        "tx_bytes": rust_constant(transport_source, "TX_BYTES"), "rx_bytes": rust_constant(transport_source, "RX_BYTES"),
        "pending_bytes": rust_constant(transport_source, "PENDING_BYTES"), "application_read_bytes": rust_constant(transport_source, "PTY_READ_BYTES"),
        "peer_close_flush_ms": rust_constant(runtime_source, "CLOSE_FLUSH_MS"),
    }
    if source_bounds != rust_bounds: raise ValueError("Runtime bounds differ from the exact production C source")
    stop_process = runpy.run_path(str(source_root / "../../volte_md_status/tests/android_check.py"))["stop_process"]
    groups = []

    def execute(command, seconds=60, text=False, check=False):
        child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=text, start_new_session=True)
        try:
            stdout, stderr = child.communicate(timeout=seconds)
        except BaseException:
            stop_process(child)
            child.communicate(timeout=1)
            raise
        survivors = stop_process(child)
        groups.append({"group": child.pid, "exit_code": child.returncode, "empty_after_cleanup": True})
        if survivors: raise ValueError(f"A completed runtime check left live descendants: {child.pid}")
        result = subprocess.CompletedProcess(command, child.returncode, stdout, stderr)
        if check:
            if result.returncode:
                print(stdout if text else stdout.decode(errors="replace"), end="")
                print(stderr if text else stderr.decode(errors="replace"), end="")
            result.check_returncode()
        return result

    def run(command, **kwargs):
        return execute(command, text=kwargs.get("text", False), check=True)

    with tempfile.TemporaryDirectory(prefix="mindone-mux-runtime-") as directory:
        output = Path(directory)
        core = output / "libmindone_mux_core.rlib"
        runtime = output / "libmindone_mux_runtime.rlib"
        dependencies = ["--extern", f"mindone_mux_core={core}", "--extern", f"mindone_mux_runtime={runtime}", "-L", "dependency=" + str(output)]
        run([args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib", "--crate-name=mindone_mux_core", str(source_root / "lib.rs"), "-o", str(core)])

        def compile_runtime(path):
            run([args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib", "--crate-name=mindone_mux_runtime", str(path), "--extern", f"mindone_mux_core={core}", "-o", str(runtime)])

        def compile_units():
            run([args.rustc, "--edition=2021", "-Dwarnings", "--test", str(tests / "runtime.rs"), *dependencies, "-o", str(output / "runtime-test")])

        compile_runtime(source_root / "runtime.rs")
        compile_units()
        units = execute([str(output / "runtime-test"), "--nocapture"], text=True)
        print(units.stdout, end="")
        print(units.stderr, end="")
        units.check_returncode()
        replay = re.search(r"socket-replay operations=(\d+) turns=(\d+) uplink_bytes=(\d+) downlink_bytes=(\d+)", units.stdout)
        if not replay: raise ValueError("The actual-socket replay did not report its completed operations")
        type_controls = []
        prefix = '''use mindone_mux_core::chat::Config;
use mindone_mux_runtime::{Effects,Event,Factory,Runtime};
use std::os::unix::net::UnixStream;
struct Empty;
impl Factory<UnixStream> for Empty {
fn open(&mut self,_:u8)->std::io::Result<UnixStream>{Err(std::io::Error::other("No endpoints"))}
}
impl Effects for Empty { fn apply(&mut self,_:Event)->bool{true} }
fn main(){
let (serial,peer)=UnixStream::pair().unwrap();
let config=Config{frame_limit:512,baud_index:5,user_build:false,pin:None};
let owner=Runtime::new(serial,Empty,Empty,&[],config,0).unwrap();
let _=peer.peer_addr().unwrap();
'''
        for name, code, negative in (
            ("owner-not-copy", "E0382", "let duplicate=owner;\nprintln!(\"{:?} {:?}\",owner.state(),duplicate.state());"),
            ("owner-not-clone", "E0599", "let duplicate=owner.clone();\nprintln!(\"{:?}\",duplicate.state());"),
            ("owner-not-debug", "E0277", "println!(\"{:?}\",owner);"),
            ("serial-is-private", "E0616", "let _=&owner.serial;"),
            ("supplied-serial-is-moved", "E0382", "let _=serial.peer_addr();\nprintln!(\"{:?}\",owner.state());"),
        ):
            path = output / (name + ".rs")
            command = [args.rustc, "--edition=2021", "-Dwarnings", str(path), *dependencies, "-o", str(output / name)]
            path.write_text(prefix + 'println!("{:?}",owner.state());\n}\n')
            run(command)
            run([str(output / name)], capture_output=True)
            path.write_text(prefix + negative + "\n}\n")
            rejected = execute(command, seconds=30, text=True)
            if rejected.returncode == 0 or code not in rejected.stderr:
                raise ValueError(f"Owned runtime type control failed: {name}")
            type_controls.append(name)
            print(f"type-control {name}: rejected")
        mutations = []
        if args.fault_controls:
            original = (source_root / "runtime.rs").read_text()
            variants = (
                ("missing-actual-progress-bound", "if accepted > offered { return Err(Error::InvalidProgress); }", "if false && accepted > offered { return Err(Error::InvalidProgress); }"),
                ("stale-recreated-channel-read", "if event.writable { self.drain_channel(node as u8, budget, report)?; }\n                if !self.epochs.is_current(event.token) { report.stale_events += 1; continue; }", "if event.writable { self.drain_channel(node as u8, budget, report)?; }"),
                ("serial-owner-retained-after-close", "self.serial = None;", "let _ = self.serial.as_ref();"),
                ("old-mode-readiness-writes-mux", "if !self.epochs.is_current(event.token) { report.stale_events += 1; continue; }\n                if event.writable && !early_write", "if event.writable && !early_write"),
                ("discarded-admitted-mux-tail", "self.decoder.push(&self.held_tail).map_err(|_| Error::Capacity)?;", "let _ = self.decoder.available_capacity();"),
                ("mux-reset-is-terminal", "if self.effects.apply(Event::ModemException) { Ok(()) } else { Err(Error::EffectRejected) }", "Err(Error::Io)"),
                ("discarded-pty-progress", "IoResult::Transferred(bytes) => { offset += bytes; }", "IoResult::Transferred(bytes) => { offset += bytes.saturating_sub(1); }"),
                ("disabled-close-deadline", "if self.state == State::PeerClosing && self.close_at.is_some_and(|end| now >= end) {", "if self.state == State::PeerClosing && self.close_at.is_some_and(|end| now > end.saturating_add(500)) {"),
                ("mux-writable-retry-disabled", "State::Multiplexing => (true, !self.tx.pending().is_empty() && self.write_wakeup && self.last_now < self.retry_at)", "State::Multiplexing => (true, false)"),
                ("peer-close-writable-retry-disabled", "State::PeerClosing => (false, !self.tx.pending().is_empty() && self.write_wakeup && self.last_now < self.retry_at)", "State::PeerClosing => (false, false)"),
                ("failed-pollout-rearms-pollout", "self.write_wakeup = !self.pollout_turn;\n                Ok(())", "self.write_wakeup = true;\n                Ok(())"),
                ("failed-startup-pollout-rearms-pollout", "self.write_wakeup = !self.pollout_turn;\n                        break;", "self.write_wakeup = true;\n                        break;"),
                ("missing-startup-deadline", "State::Startup => self.chat.expires_at(),", "State::Startup => None,"),
                ("missing-setup-deadline", "let setup = (!self.session.setup_done()).then_some(self.session.setup_deadline());", "let setup: Option<u64> = None;"),
                ("missing-peer-close-deadline", "State::PeerClosing => self.close_at,", "State::PeerClosing => None,"),
                ("missing-retry-deadline", "if retry_pending {", "if false && retry_pending {"),
                ("latest-instead-of-earliest-deadline", "end.min(retry)", "end.max(retry)"),
                ("budget-yield-misses-immediate-wakeup", "if retry_pending {", "if retry_pending && self.retry_at > self.last_now {"),
                ("terminal-runtime-retains-deadline", "State::Closed | State::Failed => return None,", "State::Closed | State::Failed => return Some(0),"),
            )
            for name, old, new in variants:
                if original.count(old) != 1: raise ValueError(f"Expected one exact runtime mutation site: {name}")
                mutated = output / "runtime.rs"
                mutated.write_text(original.replace(old, new, 1))
                compile_runtime(mutated)
                compile_units()
                result = execute([str(output / "runtime-test")], text=True)
                if result.returncode == 0 or "test result: FAILED" not in result.stdout:
                    raise ValueError(f"Compiled runtime behavior mutation was not rejected: {name}")
                mutations.append(name)
                print(f"fault-control {name}: rejected")
        if before != {name: digest(source_root / name) for name in INPUTS}:
            raise ValueError("Runtime or component inputs changed during the proof")
        report = {
            "status": "pass", "mode": "host_owned_UnixStream_runtime_not_PTY_or_Android",
            "source_sha256": before, "unit_tests": int(re.search(r"test result: ok\. (\d+) passed", units.stdout).group(1)),
            "socket_replay": dict(zip(("operations", "turns", "uplink_bytes", "downlink_bytes"), map(int, replay.groups()))),
            "blocked_retry_advertised_wakeups": {key: int(value) for key, value in zip(("multiplexing", "peer_closing"), re.search(r"retry-readiness baseline_or_fixed mux_wakeups=(\d+) peer_close_wakeups=(\d+)", units.stdout).groups())},
            "exact_c_source_bounds": source_bounds,
            "type_controls": type_controls, "fault_controls": mutations, "input_hashes_unchanged": True,
            "owned_process_groups": groups,
            "limits": [
                "Endpoints are supplied by value and one runtime owns their read/write operations. Exclusive kernel-channel access across other processes or duplicated descriptors remains a caller prerequisite.",
                "Actual nonblocking host UnixStream I/O connects the pure AT/startup, session, single-message MCC and bounded transport cores with injected endpoint factories and typed effect sinks.",
                "One non-Copy turn budget covers endpoint read/write attempts and charged bytes. Endpoint creation, fcntl and external effect syscalls are not counted as read/write attempts.",
                "Unconfirmed or invalid I/O may conservatively charge the full reserved byte allowance; reported charged bytes then differ from measured transferred bytes.",
                "Ready tokens include endpoint, descriptor and process-wide generation; the runtime rechecks them after recreation and ASCII-to-mux mode transition.",
                "The explicit peer-close path flushes an admitted reply for at most 500 supplied monotonic milliseconds and then drops endpoints. Malformed MCC and unsupported local closing fail closed, with no rollback of an already delivered prefix.",
                "The read-only next_deadline snapshot combines active startup/chat, unfinished setup, peer-close and future pending-write retry deadlines using the last accepted drive time. Eligible pending writes advertise an immediate deadline after a budget yield; terminal and idle-running states have no timer.",
                "Serial writable interests allow a retry before retry_at after blocking or partial progress; pending writes are attempted each turn and retry_at bounds retries when POLLOUT is absent. The timed host driver advances the supplied clock to the retry deadline and transfers actual UnixStream bytes without sleeping; this is not a poll syscall or production I/O adapter.",
                "The owner must call drive with a monotonic clock for timers and use a bounded external readiness wait. This library does not provide a poll syscall, signal loop, process restart or daemon.",
                "Local graceful close, PTY allocation, tty/CCCI configuration, default source selection, init, SELinux, Android native/Soong and real modem behavior remain untested.",
            ],
        }
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report))


if __name__ == "__main__": main()
