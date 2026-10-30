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
    "CMD_PN", "CMD_PSC", "CMD_CLD", "CMD_TEST", "CMD_MSC", "CMD_NSC", "MSC_FC",
    "MAX_DLCI", "MAX_FRAME", "TXQ_CAP",
}
INPUTS = (
    "Android.bp", "mindone_mux.c", "lib.rs", "session.rs", "transport.rs", "mcc.rs", "chat.rs",
    "tests/mcc.rs", "tests/mcc_replay.c", "tests/mcc_replay.rs", "tests/mcc_check.py",
    "tests/check.py",
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def message(kind, value):
    length = len(value)
    encoded = bytearray()
    while True:
        byte = (length & 127) << 1
        length >>= 7
        encoded.append(byte | (0 if length else 1))
        if not length:
            return bytes([kind]) + encoded + value


def replay():
    rng = random.Random(732)
    stream = bytearray()
    operations = 0

    def add(kind, data):
        nonlocal operations
        stream.extend(bytes([kind]) + data)
        operations += 1

    def reset(limit):
        add(1, struct.pack("<I", limit))

    def command(data, control=0xef):
        add(2, bytes([control]) + struct.pack("<I", len(data)) + data)

    reset(32767)
    for control in [0xef, 0xff, 3, 0x13]:
        for length in [0, 1, 127, 128, 16383, 16384, 32763]:
            command(message(0x23, rng.randbytes(length)), control)
        for type_length in [1, 2, 8, 9, 128, 2048]:
            prefix = bytes([0x52]) * (type_length - 1) + bytes([0x53])
            command(prefix + bytes([1]), control)
    reset(512)
    for _ in range(10000):
        kind = rng.choice([0x23, 0x43, 0x83, 0xc3, 0xe3, 0x53, 0x21, 0x41, 0x81, 0xc1, 0xe1, 0x11])
        if kind == 0xe3:
            channel, signals = rng.randrange(64) << 2 | 3, rng.randrange(256)
            value = bytes([channel, signals | 1])
            if rng.randrange(2): value = bytes([channel, signals & 0xfe, rng.randrange(256) | 1])
        elif kind == 0x83:
            value = rng.randbytes(8)
        elif kind in [0x43, 0xc3]:
            value = b""
        else:
            value = rng.randbytes(rng.randrange(129))
        command(message(kind, value), rng.choice([0xef, 0xff, 3, 0x13]))
    return bytes(stream), operations


def main():
    parser = argparse.ArgumentParser(description="Check the single-message MCC source profile with exact C wire replay and safe Session handoffs; canonical module wiring is not changed and no modem is accessed")
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
    defines = [line for line in source.splitlines() if re.match(r"#define\s+(" + "|".join(CONSTANTS) + r")\s", line)]
    if len(defines) != len(CONSTANTS):
        raise ValueError("Expected every exact production MCC constant once")
    encoder = "\n\n".join(extract(source, name) for name in ("crc_init", "crc_update", "txq_room", "tx_frame"))
    encoder = encoder.replace("static bool tx_frame(", "static bool source_tx_frame(", 1)
    fixture = (tests / "mcc_replay.c").read_text()
    for marker, value in {
        "@DEFINES@": "\n".join(defines),
        "@CHANNEL@": re.search(r"struct chan \{.*?^\};", source, re.M | re.S).group(),
        "@PHASE@": re.search(r"enum mux_state \{[^}]+\};", source).group(),
        "@ENCODER@": encoder,
        "@HANDLERS@": "\n\n".join(extract(source, name) for name in ("reply_echo", "apply_msc", "control_command")),
    }.items():
        fixture = fixture.replace(marker, value)

    def run(command, **kwargs):
        return subprocess.run(command, check=True, timeout=60, **kwargs)

    stream, operations = replay()
    with tempfile.TemporaryDirectory(prefix="mindone-mux-mcc-") as directory:
        output = Path(directory)
        core = output / "core"
        core.mkdir()
        for name in ("lib.rs", "session.rs", "transport.rs", "mcc.rs", "chat.rs"):
            (core / name).write_bytes((source_root / name).read_bytes())
        temporary_declaration = not re.search(r"^pub mod mcc;$", (core / "lib.rs").read_text(), re.M)
        if temporary_declaration:
            with (core / "lib.rs").open("a") as file:
                file.write("\npub mod mcc;\n")
        library = output / "libmindone_mux_core.rlib"
        dependency = ["--extern", f"mindone_mux_core={library}"]

        def compile_library():
            run([args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib", "--crate-name=mindone_mux_core", str(core / "lib.rs"), "-o", str(library)])

        def compile_units():
            run([args.rustc, "--edition=2021", "-Dwarnings", "--test", str(tests / "mcc.rs"), *dependency, "-o", str(output / "mcc-test")])

        compile_library()
        compile_units()
        units = run([str(output / "mcc-test")], capture_output=True, text=True)
        print(units.stdout, end="")
        (output / "replay.c").write_text(fixture)
        run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(output / "replay.c"), "-o", str(output / "c-replay")])
        run([args.rustc, "--edition=2021", "-Dwarnings", str(tests / "mcc_replay.rs"), *dependency, "-o", str(output / "rust-replay")])
        c = run([str(output / "c-replay")], input=stream, capture_output=True)
        rust = run([str(output / "rust-replay")], input=stream, capture_output=True)
        if c.stdout != rust.stdout:
            left, right = c.stdout.splitlines(), rust.stdout.splitlines()
            mismatch = next((index for index, pair in enumerate(zip(left, right)) if pair[0] != pair[1]), min(len(left), len(right)))
            raise AssertionError(f"C/Rust MCC mismatch at output {mismatch}: {[line[:160] for line in left[mismatch:mismatch + 1]]} != {[line[:160] for line in right[mismatch:mismatch + 1]]}")
        controls = [
            ("overlapping-input-output", "E0502", '''use mindone_mux_core::mcc::prepare;
fn main(){
let mut bytes=[0x23,1,0,0];
let (input,output)=bytes.split_at_mut(2);
println!("{:?}",prepare(input,512,output).unwrap());
}
''', 'let (input,output)=bytes.split_at_mut(2);', 'let input=&bytes[..2]; let output=&mut bytes[..2];'),
            ("live-session-handoff", "E0499", '''use mindone_mux_core::{Frame,UIH,session::*};
struct Noop;
impl Sink for Noop{
fn open_endpoint(&mut self,_:u8)->bool{true}
fn apply(&mut self,_:Action<'_>)->bool{true}
}
fn main(){
let mut session=Session::new(&[],512).unwrap();
let mut sink=Noop;
session.begin(0,&mut sink).unwrap();
let frame=Frame::command(0,UIH,&[0x23,1]).unwrap();
let handoff=session.receive(frame,&mut sink).unwrap();
println!("{:?}",handoff);
session.send_control(UIH,&[0x21,1],&mut sink).unwrap();
}
''', 'println!("{:?}",handoff);\nsession.send_control(UIH,&[0x21,1],&mut sink).unwrap();', 'session.send_control(UIH,&[0x21,1],&mut sink).unwrap();\nprintln!("{:?}",handoff);'),
            ("reply-pins-output", "E0506", '''use mindone_mux_core::mcc::prepare;
fn main(){
let mut output=[0;2];
let result=prepare(&[0x23,1],512,&mut output).unwrap();
println!("{:?}",result);
output[0]=0x55;
println!("{:?}",output);
}
''', 'println!("{:?}",result);\noutput[0]=0x55;', 'output[0]=0x55;\nprintln!("{:?}",result);'),
        ]
        borrowed = []
        for name, code, original, old, new in controls:
            path = output / (name + ".rs")
            path.write_text(original)
            command = [args.rustc, "--edition=2021", "-Dwarnings", str(path), *dependency, "-o", str(output / name)]
            run(command)
            run([str(output / name)], capture_output=True)
            path.write_text(original.replace(old, new, 1))
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            if result.returncode == 0 or code not in result.stderr:
                raise ValueError(f"Safe borrow control failed to reject {name} with {code}")
            borrowed.append(name)
            print(f"borrow-control {name}: rejected")
        mutations = []
        if args.fault_controls:
            original = (source_root / "mcc.rs").read_text()
            variants = (
                ("ignored-type-ea", "position(|byte| byte & 1 != 0)", "position(|_| true)"),
                ("ignored-length-ea", "if byte & 1 != 0 {", "if true || byte & 1 != 0 {"),
                ("ignored-declared-length", "if declared > actual {", "if false && declared > actual {"),
                ("ignored-trailing-message", "if declared < actual {", "if false && declared < actual {"),
                ("echoed-response", "if input[0] & COMMAND == 0 {", "if false && input[0] & COMMAND == 0 {"),
                ("oversized-nsc-prefix", "type_bytes.min(MAX_NSC_TYPE_BYTES)", "type_bytes.min(MAX_NSC_TYPE_BYTES + 1)"),
                ("wrong-msc-channel", "channel: value[0] >> 2", "channel: value[0] >> 3"),
                ("ignored-msc-ea", "MSC if !valid_msc(value)", "MSC if false && !valid_msc(value)"),
                ("missing-peer-close", "CLD => Effect::PeerClosing,", "CLD => Effect::None,"),
                ("early-output-mutation", "if length > output.len() {", "output[0] = 0;\n    if length > output.len() {"),
                ("unsolicited-final-bit", "control: UIH,", "control: UIH | 0x10,"),
            )
            for name, old, new in variants:
                if original.count(old) != 1:
                    raise ValueError(f"Expected one exact MCC mutation site: {name}")
                (core / "mcc.rs").write_text(original.replace(old, new, 1))
                compile_library()
                compile_units()
                result = subprocess.run([str(output / "mcc-test")], capture_output=True, text=True, timeout=10)
                if result.returncode == 0 or "test result: FAILED" not in result.stdout:
                    raise ValueError(f"Compiled behavioral mutation was not rejected: {name}")
                mutations.append(name)
                print(f"fault-control {name}: rejected")
        if before != {name: digest(source_root / name) for name in INPUTS}:
            raise ValueError("Canonical sources or frozen native wiring changed during MCC proof")
        report = {
            "status": "pass", "mode": "host_single_message_MCC_source_not_Android",
            "source_sha256": before, "seed": 732, "operations": operations,
            "unit_tests": int(re.search(r"test result: ok\. (\d+) passed", units.stdout).group(1)),
            "replay_sha256": hashlib.sha256(stream).hexdigest(),
            "identical_output_sha256": hashlib.sha256(c.stdout).hexdigest(),
            "identical_output_bytes": len(c.stdout), "output_lines": len(c.stdout.splitlines()),
            "borrow_controls": borrowed, "fault_controls": mutations, "input_hashes_unchanged": True,
            "temporary_module_declaration_added": temporary_declaration,
            "message_format_reference": "https://www.etsi.org/deliver/etsi_TS/127000_127099/127010/18.00.00_60/ts_127010v180000p.pdf",
            "limits": [
                "The check never changes canonical wiring; a temporary source copy appends the MCC declaration only when the canonical module is not yet wired.",
                "Only one complete MCC message per payload is supported; malformed EA/length, trailing bytes and concatenated messages reject before effects or output mutation.",
                "Commands use a strict body profile: PSC/CLD empty, PN eight bytes, MSC two or three bytes with DLCI low bits11 and terminated signal/break EA chains; replies have outer framing validated and are ignored.",
                "C wire parity covers supported single messages with accepted reply admission; stricter malformed/unsupported input rejection is an intentional separate control.",
                "PeerClosing is a returned effect, not an integrated Session transition; the replay compares that effect and does not run a closing daemon.",
                "The mock applier sends a prepared reply before peer effects; rejected Session send_control is terminal. No production effect applier or retry policy is implemented.",
                "AT, real fd/PTMX/poll, properties, AOSP native/Soong, modem, init, SELinux and hardware remain untested for MCC.",
            ],
        }
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report))


if __name__ == "__main__":
    main()
