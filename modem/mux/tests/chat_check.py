# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project

import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import runpy
import struct
import subprocess
import tempfile


CONSTANTS = {"CHAT_CAP", "CHAT_TIMEOUT_MS", "TX_RETRY_MS", "EIND_WAIT_MS", "EIND_WAIT_USER_MS", "MAX_FRAME"}
FUNCTIONS = ("wait_ms", "serial_wait", "chat_read", "chat_clear", "chat_log", "wait_modem_ready", "chat_write", "chat", "chat_cmux", "modem_handshake")
INPUTS = (
    "Android.bp", "mindone_mux.c", "lib.rs", "session.rs", "transport.rs", "mcc.rs", "chat.rs",
    "tests/chat.rs", "tests/chat_replay.c", "tests/chat_replay.rs", "tests/chat_check.py", "tests/check.py",
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def case(frame=512, baud=5, user=0, pin=-1, fault=0, chunk=128, indication=1, cmux=1, retry=0, mode=0):
    return struct.pack("<IBBiBBBBBB", frame, baud, user, pin, fault, chunk, indication, cmux, retry, mode)


def replay():
    output = bytearray()
    operations = 0
    for values in itertools.product([1, 127, 512, 32767], [0, 5, 255], [0, 1], [-1, 7, 12345, 12345678],
                                    [0, 1, 2], [1, 7, 128], [0, 1], [0, 1], [0, 1]):
        output.extend(case(*values))
        operations += 1
    return bytes(output), operations


def main():
    parser = argparse.ArgumentParser(description="Check the pure bounded AT/startup source controller against exact C fixture transcripts; no modem, Android properties or fd are accessed and PINs are synthetic fixtures")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC", "rustc"))
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--fault-controls", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source_root = tests.parent
    before = {name: digest(source_root / name) for name in INPUTS}
    source = (source_root / "mindone_mux.c").read_text()
    extract = runpy.run_path(str(tests / "check.py"), run_name="framing_helpers")["source_function"]
    if "char line[128];" not in extract(source, "chat_write"):
        raise ValueError("Unexpected production command scratch bound")
    defines = [line for line in source.splitlines() if re.match(r"#define\s+(" + "|".join(CONSTANTS) + r")\s", line)]
    if len(defines) != len(CONSTANTS): raise ValueError("Expected every exact production chat constant once")
    bodies = []
    for name in FUNCTIONS:
        body = extract(source, name)
        for operation in ["read", "write", "poll"]:
            body = re.sub(r"\b" + operation + r"\(", "fixture_" + operation + "(", body)
        bodies.append(body)
    fixture = (tests / "chat_replay.c").read_text().replace("@DEFINES@", "\n".join(defines)).replace("@FUNCTIONS@", "\n\n".join(bodies))

    def run(command, **kwargs): return subprocess.run(command, check=True, timeout=60, **kwargs)

    stream, cases = replay()
    with tempfile.TemporaryDirectory(prefix="mindone-mux-chat-") as directory:
        output = Path(directory)
        core = output / "core"
        core.mkdir()
        for name in ("lib.rs", "session.rs", "transport.rs", "mcc.rs", "chat.rs"):
            (core / name).write_bytes((source_root / name).read_bytes())
        temporary_declaration = not re.search(r"^pub mod chat;$", (core / "lib.rs").read_text(), re.M)
        if temporary_declaration:
            with (core / "lib.rs").open("a") as file: file.write("\npub mod chat;\n")
        library = output / "libmindone_mux_core.rlib"
        dependency = ["--extern", f"mindone_mux_core={library}"]

        def compile_library():
            run([args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib", "--crate-name=mindone_mux_core", str(core / "lib.rs"), "-o", str(library)])

        def compile_units():
            run([args.rustc, "--edition=2021", "-Dwarnings", "--test", str(tests / "chat.rs"), *dependency, "-o", str(output / "chat-test")])

        compile_library()
        compile_units()
        units = run([str(output / "chat-test")], capture_output=True, text=True)
        print(units.stdout, end="")
        (output / "replay.c").write_text(fixture)
        run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(output / "replay.c"), "-o", str(output / "c-replay")])
        run([args.rustc, "--edition=2021", "-Dwarnings", str(tests / "chat_replay.rs"), *dependency, "-o", str(output / "rust-replay")])
        c = run([str(output / "c-replay")], input=stream, capture_output=True)
        rust = run([str(output / "rust-replay")], input=stream, capture_output=True)
        if c.stdout != rust.stdout:
            left, right = c.stdout.splitlines(), rust.stdout.splitlines()
            mismatch = next((index for index, pair in enumerate(zip(left, right)) if pair[0] != pair[1]), min(len(left), len(right)))
            raise AssertionError(f"C/Rust chat mismatch at output {mismatch}: {left[mismatch:mismatch + 2]} != {right[mismatch:mismatch + 2]}")
        legacy = []
        for mode, name, old_end, new_end in [
            (1, "substring-not-final-result", b"RESULT 0 0 4 0", b"RESULT 0 5000 5 0"),
            (2, "unterminated-final-result", b"RESULT 0 0 4 0", b"RESULT 0 5000 5 0"),
            (3, "refreshed-response-deadline", b"RESULT 0 65000 5 0", b"RESULT 0 5000 5 0"),
            (4, "discarded-mux-tail", b"RESULT 0 0 4 0", b"TAIL f90003"),
        ]:
            data = case(mode=mode)
            previous = run([str(output / "c-replay")], input=data, capture_output=True)
            candidate = run([str(output / "rust-replay")], input=data, capture_output=True)
            if old_end not in previous.stdout or new_end not in candidate.stdout or previous.stdout == candidate.stdout:
                raise ValueError(f"Exact C source divergence control failed: {name}")
            legacy.append({"control": name, "c_last_line": previous.stdout.splitlines()[-1].decode(),
                           "rust_last_lines": [line.decode() for line in candidate.stdout.splitlines()[-2:]]})
            print(f"exact-C divergence-control {name}: confirmed")
        controls = []
        prefix = '''use mindone_mux_core::chat::*;
fn main(){
let pin=Pin::new(b"00001234").unwrap();
let config=Config{frame_limit:512,baud_index:5,user_build:false,pin:Some(pin)};
let mut controller=Controller::new(config,0).unwrap();
controller.tick(20000).unwrap();
let request=controller.pending_write().unwrap();
println!("{}",request.bytes.len());
}
'''
        probes = [
            ("pin-no-debug", "E0277", 'println!("{}",request.bytes.len());', 'println!("{:?}",Pin::new(b"00001234").unwrap());'),
            ("request-no-debug", "E0277", 'println!("{}",request.bytes.len());', 'println!("{:?}",request);'),
            ("controller-no-debug", "E0277", 'println!("{}",request.bytes.len());', 'println!("{:?}",controller);'),
            ("config-no-debug", "E0277", 'println!("{}",request.bytes.len());', 'let _ = request; println!("{:?}",Config{frame_limit:512,baud_index:5,user_build:false,pin:Some(Pin::new(b"00001234").unwrap())});'),
            ("request-pins-write-buffer", "E0502", 'println!("{}",request.bytes.len());', 'controller.accept_written(20000,request.token,1).unwrap();\nprintln!("{}",request.bytes.len());'),
        ]
        for name, code, old, new in probes:
            path = output / (name + ".rs")
            path.write_text(prefix)
            command = [args.rustc, "--edition=2021", "-Dwarnings", str(path), *dependency, "-o", str(output / name)]
            run(command)
            run([str(output / name)], capture_output=True)
            path.write_text(prefix.replace(old, new, 1))
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            if result.returncode == 0 or code not in result.stderr:
                raise ValueError(f"Compile-enforced confidentiality/borrow control failed: {name}")
            controls.append(name)
            print(f"type-control {name}: rejected")
        tail_source = '''use mindone_mux_core::chat::*;
fn main(){
let mut controller=Controller::new(Config{frame_limit:512,baud_index:5,user_build:false,pin:None},0).unwrap();
let input=Vec::new();
let result=controller.feed(0,&input).unwrap();
println!("{}",result.consumed);
drop(input);
}
'''
        path = output / "tail-pins-input.rs"
        command = [args.rustc, "--edition=2021", "-Dwarnings", str(path), *dependency, "-o", str(output / "tail-pins-input")]
        path.write_text(tail_source)
        run(command)
        run([str(output / "tail-pins-input")], capture_output=True)
        path.write_text(tail_source.replace('println!("{}",result.consumed);\ndrop(input);', 'drop(input);\nprintln!("{}",result.consumed);'))
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        if result.returncode == 0 or "E0505" not in result.stderr:
            raise ValueError("Borrowed mux tail did not pin the caller input")
        controls.append("tail-pins-input")
        print("type-control tail-pins-input: rejected")
        mutations = []
        if args.fault_controls:
            original = (source_root / "chat.rs").read_text()
            variants = (
                ("substring-ack", 'b"OK" => Record::Ok,', 'value if value.windows(2).any(|bytes| bytes == b"OK") => Record::Ok,'),
                ("replayed-write-prefix", "self.accepted += bytes;", "self.accepted += bytes.saturating_sub(1);"),
                ("discarded-mux-tail", "Some(&input[consumed..]) });\n                }", "Some(&input[input.len()..]) });\n                }"),
                ("shortened-settle", "Deadline::after(now_ms, SETTLE_MS)", "Deadline::after(now_ms, SETTLE_MS - 1)"),
                ("truncated-pin", "self.append(&pin.digits[..pin.length])?;", "let _ = pin.length; self.append(&pin.digits[..4])?;"),
                ("accepted-late-write", "if self.deadline.remaining(now_ms).is_none() { return self.fail(Error::LateProgress); }", "if false && self.deadline.remaining(now_ms).is_none() { return self.fail(Error::LateProgress); }"),
                ("ignored-stale-write", "if Some(token) != self.token { return self.fail(Error::StaleProgress); }\n        if bytes == 0", "let _ = token;\n        if bytes == 0"),
                ("unbounded-record", "if self.record_length == CHAT_BYTES {", "if false && self.record_length == CHAT_BYTES {"),
                ("retry-partial-at", "Phase::Reading | Phase::Writing => self.fail(Error::Timeout),", "Phase::Writing if self.command == Some(Command::At) => self.start(Command::AtRetry, now_ms),\n            Phase::Reading | Phase::Writing => self.fail(Error::Timeout),"),
                ("refreshed-command-deadline", "pub fn tick(&mut self, now_ms: u64) -> Result<(), Error> {\n        self.clock(now_ms)?;", "pub fn tick(&mut self, now_ms: u64) -> Result<(), Error> {\n        self.clock(now_ms)?;\n        if matches!(self.phase, Phase::Writing | Phase::Reading) { self.deadline = Deadline::after(now_ms, COMMAND_TIMEOUT_MS).unwrap(); }"),
            )
            for name, old, new in variants:
                if original.count(old) != 1: raise ValueError(f"Expected one exact chat mutation site: {name}")
                (core / "chat.rs").write_text(original.replace(old, new, 1))
                compile_library()
                compile_units()
                result = subprocess.run([str(output / "chat-test")], capture_output=True, text=True, timeout=10)
                if result.returncode == 0 or "test result: FAILED" not in result.stdout:
                    raise ValueError(f"Compiled chat behavior mutation was not rejected: {name}")
                mutations.append(name)
                print(f"fault-control {name}: rejected")
        if before != {name: digest(source_root / name) for name in INPUTS}:
            raise ValueError("Canonical source or frozen native wiring changed during chat proof")
        report = {
            "status": "pass", "mode": "host_pure_AT_startup_with_fixture_io_not_Android",
            "source_sha256": before, "unit_tests": int(re.search(r"test result: ok\. (\d+) passed", units.stdout).group(1)),
            "cases": cases, "replay_sha256": hashlib.sha256(stream).hexdigest(),
            "identical_output_sha256": hashlib.sha256(c.stdout).hexdigest(),
            "identical_output_bytes": len(c.stdout), "output_lines": len(c.stdout.splitlines()),
            "exact_c_divergence_controls": legacy, "type_controls": controls, "fault_controls": mutations,
            "input_hashes_unchanged": True, "temporary_module_declaration_added": temporary_declaration,
            "limits": [
                "The core is a pure one-shot controller; accepted write counts and a monotonic clock are supplied by a single external owner, with no fd, syscalls, properties or logs.",
                "Only exact CR/LF-terminated printable ASCII records are recognized; non-printable noise is dropped. Substring, unterminated, oversized and stale input acceptance intentionally differs from permissive C.",
                "Each command uses one absolute deadline starting before writing; only a fully written first AT retries once on ERROR or response timeout. Partial write timeout, late progress and transport faults fail closed.",
                "PINs are explicit four-to-eight ASCII digits with leading zeros preserved; C numeric-converts and only pads to a minimum of four. All transcript PINs are synthetic fixtures.",
                "No Debug is implemented for secret-bearing types or requests; raw outgoing bytes are deliberately exposed to the writer and external caller logging is outside the core contract.",
                "Mux tails borrow caller input exactly, including during the bounded settle phase; the caller must retain and budget them and wait for Ready before mux effects.",
                "The temporary test crate appends chat wiring only when absent; canonical lib.rs and Android.bp are never changed by this gate.",
                "Target native/Soong, OS/fd ownership, signal/reset supervision, CCCI, actual modem startup, init, SELinux and hardware remain untested.",
            ],
        }
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report))


if __name__ == "__main__": main()
