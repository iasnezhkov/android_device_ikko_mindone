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
    "MUX_FLAG", "ADDR_EA", "ADDR_CR", "CTRL_PF", "CTRL_UIH", "CTRL_UI", "CMD_CR",
    "CMD_MSC", "MAX_FRAME", "TXQ_CAP", "RX_CAP", "PENDING_CAP", "PTY_CHUNK", "FRAME_OVERHEAD",
}
FUNCTIONS = ("crc_init", "crc_update", "txq_room", "tx_frame", "tx_flush", "deliver", "can_take_pty_data")
INPUTS = (
    "Android.bp", "mindone_mux.c", "lib.rs", "session.rs", "transport.rs", "mcc.rs", "chat.rs",
    "tests/transport.rs", "tests/transport_replay.c", "tests/transport_replay.rs",
    "tests/transport_controls.c", "tests/transport_check.py", "tests/check.py",
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replay():
    rng = random.Random(731)
    stream = bytearray()
    operations = 0

    def add(kind, data=b""):
        nonlocal operations
        stream.extend(bytes([kind]) + data)
        operations += 1

    def reset(limit):
        add(1, struct.pack("<I", limit))

    def frame(length, limit):
        add(2, bytes([rng.randrange(64), rng.choice([0xef, 0xff, 3, 0x13, 0x3f, 0x73])]) + struct.pack("<I", length) + rng.randbytes(length))

    def drain(steps):
        add(3, bytes([len(steps)]) + b"".join(bytes([kind]) + struct.pack("<I", count) for kind, count in steps))

    for limit in [1, 127, 128, 512, 32767]:
        reset(limit)
        for length in sorted({0, 1, min(limit, 127), min(limit, 128), limit, limit + 1}):
            frame(length, limit)
            add(5)
        drain([(0, 3), (1, 0), (0, 65536)])
        for count in [1, 7, 4096, min(limit, 1024)]:
            if count <= limit:
                add(4, struct.pack("<I", count) + rng.randbytes(count))
    reset(512)
    for _ in range(140):
        frame(512, 512)
        add(5)
    drain([(0, 65536)])
    limit = 512
    for _ in range(5000):
        choice = rng.randrange(10)
        if choice == 0:
            limit = rng.choice([1, 127, 128, 512, 32767])
            reset(limit)
        elif choice < 5:
            length = rng.choice([0, 1, min(limit, 127), min(limit, 128), min(limit, 1024), limit + 1])
            frame(length, limit)
        elif choice < 8:
            steps = [(rng.choice([0, 0, 1]), rng.randrange(1, 1025)) for _ in range(rng.randrange(8))]
            steps.append((rng.choice([0, 2, 3, 4, 5, 6]), 65536))
            drain(steps)
        elif choice == 8:
            length = rng.choice([0, 1, min(limit, 512), limit])
            add(4, struct.pack("<I", length) + rng.randbytes(length))
        else:
            add(5)
    return bytes(stream), operations


def main():
    parser = argparse.ArgumentParser(description="Check bounded safe transport decisions against exact C source with host-only substituted I/O; no modem, fd or Android service is accessed")
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

    def defines(names):
        found = [line for line in source.splitlines() if re.match(r"#define\s+(" + "|".join(names) + r")\s", line)]
        if len(found) != len(names):
            raise ValueError("Expected every exact production constant once")
        return "\n".join(found)

    channel = re.search(r"struct chan \{.*?^\};", source, re.M | re.S).group()
    replacements = {
        "@DEFINES@": defines(CONSTANTS), "@CHANNEL@": channel,
        "@PHASE@": re.search(r"enum mux_state \{[^}]+\};", source).group(),
        "@FUNCTIONS@": "\n\n".join(extract(source, name).replace("write(", "fixture_write(") for name in FUNCTIONS),
        "@CHAT_DEFINES@": defines({"CHAT_TIMEOUT_MS"}),
        "@POLL_DEFINES@": defines({"MAX_SIMS", "KINDS", "MAX_CHANS", "TX_RETRY_MS"}),
    }
    for name in ("chat", "tx_flush", "chan_reopen", "chan_drain", "run"):
        replacements["@" + name.upper() + "@"] = re.sub(r"\bpoll\(", "fixture_poll(", re.sub(r"\bwrite\(", "fixture_write(", extract(source, name)))

    def render(name):
        text = (tests / name).read_text()
        for marker, value in replacements.items():
            text = text.replace(marker, value)
        if re.search(r"@[A-Z_]+@", text):
            raise ValueError("Unresolved exact-source fixture marker")
        return text

    def run(command, **kwargs):
        return subprocess.run(command, check=True, timeout=60, **kwargs)

    stream, operations = replay()
    with tempfile.TemporaryDirectory(prefix="mindone-mux-transport-") as directory:
        output = Path(directory)
        library = output / "libmindone_mux_core.rlib"
        dependency = ["--extern", f"mindone_mux_core={library}"]

        def compile_library(root):
            run([args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib", "--crate-name=mindone_mux_core", str(root / "lib.rs"), "-o", str(library)])

        def compile_units():
            run([args.rustc, "--edition=2021", "-Dwarnings", "--test", str(tests / "transport.rs"), *dependency, "-o", str(output / "transport-test")])

        def compile_c(name, extra=()):
            run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", *extra, str(output / (name + ".c")), "-o", str(output / name)])

        compile_library(source_root)
        compile_units()
        units = run([str(output / "transport-test")], capture_output=True, text=True)
        print(units.stdout, end="")
        (output / "replay.c").write_text(render("transport_replay.c"))
        compile_c("replay")
        run([args.rustc, "--edition=2021", "-Dwarnings", str(tests / "transport_replay.rs"), *dependency, "-o", str(output / "rust-replay")])
        c = run([str(output / "replay")], input=stream, capture_output=True)
        rust = run([str(output / "rust-replay")], input=stream, capture_output=True)
        if c.stdout != rust.stdout:
            left, right = c.stdout.splitlines(), rust.stdout.splitlines()
            mismatch = next((index for index, pair in enumerate(zip(left, right)) if pair[0] != pair[1]), min(len(left), len(right)))
            raise AssertionError(f"C/Rust transport mismatch at output {mismatch}: {[line[:160] for line in left[mismatch:mismatch + 1]]} != {[line[:160] for line in right[mismatch:mismatch + 1]]}")
        legacy = []
        for mode in ("CHAT", "REUSED", "INTERRUPT"):
            name = "control-" + mode.lower()
            (output / (name + ".c")).write_text(render("transport_controls.c"))
            compile_c(name, ["-DCONTROL_" + mode])
            result = run([str(output / name)], capture_output=True, text=True)
            print(result.stdout, end="")
            legacy.append({"control": mode.lower(), "output": result.stdout.strip()})
        mutations = []
        if args.fault_controls:
            original = (source_root / "transport.rs").read_text()
            variants = (
                ("replayed-accepted-byte", "self.start += bytes;", "self.start += bytes.saturating_sub(1);"),
                ("non-atomic-capacity", "if input.len() > self.available() {", "if false && input.len() > self.available() {"),
                ("partial-pty-admission", "if required > self.available() {", "if false && required > self.available() {"),
                ("extra-interrupted-syscall", "if self.calls == self.call_limit ||", "if self.calls > self.call_limit ||"),
                ("lost-read-error-reset", "else { self.reset() };", "else { 0 };"),
                ("refreshed-at-deadline", "self.expires_at.checked_sub(now_ms).filter", "self.expires_at.checked_sub(now_ms.min(self.expires_at.saturating_sub(1))).filter"),
                ("reused-fd-token", "self.entries[slot] == Some(token)", "self.entries[slot].is_some_and(|current| current.descriptor == token.descriptor)"),
            )
            mutated = output / "mutated"
            mutated.mkdir()
            for name in ("lib.rs", "session.rs", "mcc.rs", "chat.rs"):
                (mutated / name).write_bytes((source_root / name).read_bytes())
            for name, old, new in variants:
                if original.count(old) != 1:
                    raise ValueError(f"Expected one exact mutation site: {name}")
                (mutated / "transport.rs").write_text(original.replace(old, new, 1))
                compile_library(mutated)
                compile_units()
                result = subprocess.run([str(output / "transport-test")], capture_output=True, text=True, timeout=10)
                if result.returncode == 0 or "test result: FAILED" not in result.stdout:
                    raise ValueError(f"Compiled behavioral mutation was not rejected by test assertions: {name}")
                mutations.append(name)
                print(f"fault-control {name}: rejected")
        if before != {name: digest(source_root / name) for name in INPUTS}:
            raise ValueError("Canonical source changed during the transport proof")
        report = {
            "status": "pass", "mode": "host_source_transport_with_substituted_io_not_Android",
            "source_sha256": before, "seed": 731, "operations": operations,
            "unit_tests": int(re.search(r"test result: ok\. (\d+) passed", units.stdout).group(1)),
            "replay_sha256": hashlib.sha256(stream).hexdigest(),
            "identical_output_sha256": hashlib.sha256(c.stdout).hexdigest(),
            "identical_output_bytes": len(c.stdout), "output_lines": len(c.stdout.splitlines()),
            "exact_c_legacy_controls": legacy, "fault_controls": mutations,
            "input_hashes_unchanged": True,
            "limits": [
                "C differential covers atomic frame admission, PTY read reservation, accepted partial serial writes and blocked channel remainder admission within finite valid C turn limits.",
                "Exact C legacy hazard controls substitute clock/poll/write and endpoint helpers; they are simulated source conditions, not observed phone failures.",
                "The Rust core owns bytes and generation tickets, not file descriptors; the caller must invalidate tokens before close and check each event before use.",
                "Receiver callbacks accept complete frames synchronously; a later read failure clears unpublished fragments and cannot undo frames already accepted by the sink.",
                "MCC, POSIX daemon, fd/PTMX lifetime, real poll scheduling, AOSP native/Soong, modem, init, SELinux and hardware remain untested for this candidate.",
            ],
        }
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report))


if __name__ == "__main__":
    main()
