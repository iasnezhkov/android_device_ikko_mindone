# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project

import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import re
import runpy
import struct
import subprocess
import tempfile


CONSTANTS = {
    "MUX_FLAG", "ADDR_EA", "ADDR_CR", "CTRL_PF", "CTRL_SABM", "CTRL_UA", "CTRL_DM",
    "CTRL_DISC", "CTRL_UIH", "CTRL_UI", "CMD_CR", "CMD_MSC", "CMD_CLD", "MSC_FC",
    "CMD_TEST", "CMD_PSC", "CMD_PN", "CMD_NSC",
    "MSC_SIGNALS", "MAX_DLCI", "MAX_SIMS", "KINDS", "MAX_CHANS", "MAX_FRAME",
    "TXQ_CAP", "SETUP_TIMEOUT_MS", "CLOSE_WAIT_MS", "EXIT_FLUSH_MS",
    "REOPEN_RETRY_MS", "REOPEN_ATTEMPTS", "FRAME_OVERHEAD", "RECOVERY_ESCALATION_MS",
}
FUNCTIONS = (
    "send_msc", "setup_complete", "resolve", "mux_open", "chan_reopen", "chan_recover", "recovery_timeout", "on_ua",
    "on_dm", "on_disc", "handle_frame", "apply_msc", "close_all_ptys", "close_serial",
    "send_close_down", "close_mux",
)
INPUTS = (
    "Android.bp", "mindone_mux.c", "lib.rs", "session.rs", "transport.rs", "mcc.rs", "chat.rs", "tests/session.rs",
    "tests/session_replay.c", "tests/session_replay.rs", "tests/session_check.py",
    "tests/check.py",
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replay(shared=False):
    random_source = random.Random(730)
    output = bytearray()
    operations = 0

    def add(operation, data=b""):
        nonlocal operations
        output.extend(bytes([operation]) + data)
        operations += 1

    def begin(now):
        add(6, bytes([255]))
        add(1, struct.pack("<Q", now))

    def frame(channel, control, payload=b""):
        if shared and channel in (1, 26) and control & ~0x10 in (0x0f, 0x43, 0xef, 0x03):
            control = 0x81
        add(2, bytes([channel, control]) + struct.pack("<I", len(payload)) + payload)

    def control(kind, payload):
        add(7, bytes([kind]) + struct.pack("<I", len(payload)) + payload)

    begin(100)
    add(10, struct.pack("<QB", 101, 0))
    add(10, struct.pack("<QB", 30099, 1))
    frame(0, 0x73)
    frame(0, 0x73)
    frame(1, 0x73)
    frame(26, 0x1f)
    add(8, bytes([26]))
    add(8, bytes([26]))
    frame(26, 0x3f)
    add(3, bytes([1, 2]))
    add(3, bytes([1, 0]))
    add(4, bytes([1, 1]))
    add(5, bytes([1]))
    frame(1, 0x53)
    add(6, bytes([1]))
    frame(1, 0x3f)
    add(6, bytes([255]))
    frame(1, 0x3f)
    frame(63, 0xff, b"unconfigured")
    control(0xef, bytes([0xe1, 5, 7, 0x8d]))
    add(9)
    frame(1, 0x73)
    add(3, bytes([0, 2]))
    begin(100000)
    now = 100000
    controls = [0x3f, 0x73, 0x1f, 0x53, 0xef, 0xff, 3, 0x13, 0, 0x81]
    for _ in range(10000):
        choice = random_source.randrange(10)
        channel = random_source.choice([0, 1, 26, 2, 63])
        if choice == 0:
            now += 100000
            begin(now)
        elif choice == 1:
            frame(channel, random_source.choice(controls), random_source.randbytes(random_source.randrange(49)))
        elif choice == 2:
            add(3, bytes([random_source.randrange(256), random_source.randrange(256)]))
        elif choice == 3:
            add(4, bytes([channel, random_source.randrange(2)]))
        elif choice == 4:
            add(6, bytes([255]))
            add(5, bytes([random_source.choice([1, 26])]))
        elif choice == 5:
            data = bytes([random_source.choice([0xe1, 0xe3, 0x81, 0])]) + random_source.randbytes(random_source.randrange(17))
            control(random_source.choice([0xef, 0xff, 3, 0x13]), data)
        elif choice == 6:
            add(8, bytes([random_source.choice([1, 26])]))
        elif choice == 7:
            add(6, bytes([random_source.choice([1, 26, 255])]))
        elif choice == 8:
            add(9)
        else:
            add(10, struct.pack("<QB", now + random_source.randrange(30000), random_source.randrange(2)))
    return bytes(output), operations


def main():
    parser = argparse.ArgumentParser(description="Check bounded mux session decisions and exact C handler replay with substituted endpoint/transport effects; no modem, fd or Android service is accessed")
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
    constants = [line for line in source.splitlines() if re.match(r"#define\s+(" + "|".join(CONSTANTS) + r")\s", line)]
    if len(constants) != len(CONSTANTS):
        raise ValueError("Expected every exact production session constant once")
    encoder = "\n\n".join(extract(source, name) for name in ("crc_init", "crc_update", "txq_room", "tx_frame"))
    encoder = encoder.replace("static bool tx_frame(", "static bool source_tx_frame(", 1)
    fixture = (tests / "session_replay.c").read_text()
    replacements = {
        "@DEFINES@": "\n".join(constants),
        "@CHANNEL@": re.search(r"struct chan \{.*?^\};", source, re.M | re.S).group(),
        "@PHASE@": re.search(r"enum mux_state \{[^}]+\};", source).group(),
        "@ENCODER@": encoder,
        "@FUNCTIONS@": "\n\n".join(extract(source, name) for name in FUNCTIONS),
    }
    for marker, value in replacements.items():
        fixture = fixture.replace(marker, value)
    stream, operations = replay()

    def run(command, **kwargs):
        return subprocess.run(command, check=True, timeout=60, **kwargs)

    with tempfile.TemporaryDirectory(prefix="mindone-mux-session-") as directory:
        output = Path(directory)
        library = output / "libmindone_mux_core.rlib"
        dependency = ["--extern", f"mindone_mux_core={library}"]

        def compile_library(root):
            run([args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib", "--crate-name=mindone_mux_core", str(root / "lib.rs"), "-o", str(library)])

        def compile_replay():
            run([args.rustc, "--edition=2021", "-Dwarnings", str(tests / "session_replay.rs"), *dependency, "-o", str(output / "rust-replay")])

        compile_library(source_root)
        run([args.rustc, "--edition=2021", "-Dwarnings", "--test", str(tests / "session.rs"), *dependency, "-o", str(output / "session-test")])
        unit = run([str(output / "session-test")], capture_output=True, text=True)
        print(unit.stdout, end="")
        raw_control = fixture.replace(extract(fixture, "control_command"),
            "static void apply_msc(uint8_t dlci, uint8_t signals);\n\n" +
            extract(source, "reply_echo") + "\n\n" + extract(source, "control_command"), 1)
        (output / "raw-close-control.c").write_text(raw_control)
        run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(output / "raw-close-control.c"), "-o", str(output / "raw-close-control")])
        recovery = run([str(output / "raw-close-control"), "--recovery"], capture_output=True, text=True)
        if "RECOVERY_PASS" not in recovery.stdout:
            raise ValueError("Production C recovery checks did not finish")
        print(recovery.stdout, end="")
        close_stream = bytes([1]) + struct.pack("<Q", 100)
        for channel, control, payload in [(0, 0xef, bytes([0xc3, 1])), (0, 0x73, b""), (1, 0x73, b""), (26, 0x73, b"")]:
            close_stream += bytes([2, channel, control]) + struct.pack("<I", len(payload)) + payload
        raw = run([str(output / "raw-close-control")], input=close_stream, capture_output=True)
        states = [line for line in raw.stdout.decode().splitlines() if line.startswith("STATE ")]
        if len(states) != 5 or not states[1].startswith("STATE 3 ") or not states[-1].startswith("STATE 3 ") or raw.stdout.count(b"READY 0\n") != 0:
            raise ValueError("Exact C close-then-late-UA control revived setup")
        legacy = {"control": "same-read-close-then-late-UA", "closing_state": states[1], "final_state": states[-1],
                  "output_sha256": hashlib.sha256(raw.stdout).hexdigest(), "ready_effects_after_close": 0,
                  "scope": "Exact C source simulation with substituted endpoints; no observed modem fault"}
        dispatch = "            handle_frame(channel, control, payload, length);"
        if fixture.count(dispatch) != 1:
            raise ValueError("Expected one owner frame-dispatch site in the C replay fixture")
        owner_fixture = fixture.replace(dispatch, "            if (state != ST_CLOSING && state != ST_PEER_CLOSING)\n    " + dispatch, 1)
        (output / "replay.c").write_text(owner_fixture)
        run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(output / "replay.c"), "-o", str(output / "c-replay")])
        compile_replay()
        c = run([str(output / "c-replay")], input=stream, capture_output=True)
        rust = run([str(output / "rust-replay")], input=stream, capture_output=True)
        shared_stream, _ = replay(shared=True)
        shared_c = run([str(output / "c-replay")], input=shared_stream, capture_output=True)
        shared_rust = run([str(output / "rust-replay")], input=shared_stream, capture_output=True)
        if shared_c.stdout != shared_rust.stdout:
            raise AssertionError("C/Rust shared setup, flow, endpoint recreation and control behavior differ")
        borrowed = '''use mindone_mux_core::{Frame,session::*};
struct Noop;
impl Sink for Noop {
    fn open_endpoint(&mut self,_:u8)->bool{true}
    fn apply(&mut self,_:Action<'_>)->bool{true}
}
fn main(){
    let mut session=Session::new(&[],512).unwrap();
    let mut sink=Noop;
    session.begin(0,&mut sink).unwrap();
    let handoff=session.receive(Frame::command(0,0xef,b"owned").unwrap(),&mut sink).unwrap();
    println!("{:?}",handoff);
    session.reset_after_transport_close();
}
'''
        control = output / "borrowed.rs"
        control.write_text(borrowed)
        command = [args.rustc, "--edition=2021", "-Dwarnings", str(control), *dependency, "-o", str(output / "borrowed")]
        run(command)
        control.write_text(borrowed.replace('    println!("{:?}",handoff);\n    session.reset_after_transport_close();', '    session.reset_after_transport_close();\n    println!("{:?}",handoff);'))
        rejected = subprocess.run(command, capture_output=True, text=True, timeout=30)
        if rejected.returncode == 0 or "E0499" not in rejected.stderr:
            raise ValueError("The live session handoff did not reject a concurrent mutable session borrow")
        controls = []
        if args.fault_controls:
            original = (source_root / "session.rs").read_text()
            mutants = (
                ("stale-open-ack", "state.opening = false;\n                    state.open = true;", "state.opening = true;\n                    state.open = true;"),
                ("ignored-peer-flow", "value.peer_paused = signals & 2 != 0;", "let _ = signals; value.peer_paused = false;"),
                ("missing-endpoint-close", "self.apply(Action::CloseEndpoint(channel), sink)?;", "let _ = sink;"),
                ("missing-ready-latch", "self.ready_started = true;", "self.ready_started = false;"),
                ("stale-poll-final", "            self.poll_final_echo = false;", "            self.poll_final_echo = true;"),
            )
            mutated = output / "mutated"
            mutated.mkdir()
            (mutated / "lib.rs").write_text((source_root / "lib.rs").read_text())
            (mutated / "transport.rs").write_text((source_root / "transport.rs").read_text())
            for name in ("mcc.rs", "chat.rs"):
                (mutated / name).write_bytes((source_root / name).read_bytes())
            for name, old, new in mutants:
                if original.count(old) != 1:
                    raise ValueError(f"Expected one exact mutation site: {name}")
                (mutated / "session.rs").write_text(original.replace(old, new, 1))
                compile_library(mutated)
                compile_replay()
                result = run([str(output / "rust-replay")], input=stream, capture_output=True)
                if result.stdout == rust.stdout:
                    raise ValueError(f"Behavioral replay failed to reject mutation: {name}")
                controls.append(name)
                print(f"fault-control {name}: rejected")
        after = {name: digest(source_root / name) for name in INPUTS}
        if before != after:
            raise ValueError("Canonical source changed during the session proof")
        report = {
            "status": "pass", "mode": "host_source_session_with_mocked_effects_not_Android",
            "source_sha256": before, "seed": 730, "operations": operations,
            "unit_tests": int(re.search(r"test result: ok\. (\d+) passed", unit.stdout).group(1)),
            "replay_sha256": hashlib.sha256(stream).hexdigest(),
            "production_c_output_sha256": hashlib.sha256(c.stdout).hexdigest(),
            "legacy_rust_output_sha256": hashlib.sha256(rust.stdout).hexdigest(),
            "production_c_recovery": "pass",
            "production_c_escalation_interval_ms": int(re.search(r"^#define RECOVERY_ESCALATION_MS (\d+)$", source, re.M).group(1)),
            "shared_policy_parity_sha256": hashlib.sha256(shared_c.stdout).hexdigest(),
            "output_lines": len(c.stdout.splitlines()), "handoff_mutation_rejected": True,
            "fault_controls": controls, "input_hashes_unchanged": True,
            "exact_c_lifecycle_control": legacy,
            "differential_profile": "Shared setup/control/flow/recreation replay retains parity; nonzero DLCI DM/DISC and downlink delivery policies are checked independently",
            "limits": [
                "Endpoint creation, close, readiness, data delivery and transport effects are synchronous host mocks; real fd lifetime and partial I/O are not exercised.",
                "Rust DLCI0 payloads are handed off without parsing MCC commands; production C uses its own parser.",
                "The optional Rust model is checked independently against its own mutation controls; parity with production C recovery is not asserted.",
                "Production C retains PTYs and retries closed DLCIs; the optional Rust implementation retains its previous close policy and is not the shipped daemon.",
                "Target Soong, CCCI, init, SELinux and hardware behavior remain untested.",
            ],
        }
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report))


if __name__ == "__main__":
    main()
