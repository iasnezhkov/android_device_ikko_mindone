# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project

import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import re
import struct
import subprocess
import tempfile


DEFINES = {"MUX_FLAG", "ADDR_EA", "ADDR_CR", "CTRL_PF", "CTRL_UIH", "CTRL_UI", "CMD_CR", "CMD_MSC", "MAX_FRAME", "TXQ_CAP", "RX_CAP"}
FUNCTIONS = ("crc_init", "crc_update", "txq_room", "tx_frame", "rx_parse")


def source_function(source, name):
    match = re.search(r"^static [^\n]+\b" + name + r"\([^\n]*\)\n\{\n.*?^\}", source, re.M | re.S)
    if not match:
        raise ValueError(f"Missing production function: {name}")
    return match.group()


def wire(channel, control, data, command=False):
    length = len(data)
    encoded_length = bytes([(length << 1) | 1]) if length < 128 else bytes([(length << 1) & 0xfe, length >> 7])
    header = bytes([channel << 2 | 1 | (2 if command else 0), control]) + encoded_length
    crc = 0xff
    for byte in header + (data if control & ~0x10 == 3 else b""):
        crc ^= byte
        for _ in range(8):
            crc = crc >> 1 ^ (0xe0 if crc & 1 else 0)
    return bytes([0xf9]) + header + data + bytes([crc ^ 0xff, 0xf9])


def replay():
    rng = random.Random(729)
    output = bytearray()
    operations = 0

    def append(data):
        nonlocal operations
        output.extend(data)
        operations += 1

    def limit(value):
        append(bytes([2]) + struct.pack("<I", value))

    def receive(data):
        append(bytes([3]) + struct.pack("<I", len(data)) + data)

    controls = [0xef, 0xff, 3, 0x13, 0x3f, 0x73, 0x1f, 0x53, 0, 0x81]
    for channel in range(64):
        for control in controls:
            for length in [0, 1, 126, 127, 128, 129, 511, 512, 513]:
                data = rng.randbytes(length)
                append(bytes([1, channel, control]) + struct.pack("<II", 512, length) + data)
    for control in controls:
        for length in [32766, 32767, 32768]:
            append(bytes([1, 63, control]) + struct.pack("<II", 32767, length) + rng.randbytes(length))
    limit(512)
    for control in controls:
        for length in [0, 1, 2, 127, 128, 129, 512]:
            data = wire(3, control, rng.randbytes(length))
            for split in list(range(min(8, len(data)))) + [len(data) - 2, len(data) - 1]:
                append(bytes([4]))
                receive(data[:split])
                receive(data[split:])
                receive(data[1:])
    for _ in range(10000):
        choice = rng.randrange(10)
        if choice == 0:
            append(bytes([4]))
        elif choice == 1:
            receive(rng.randbytes(rng.randrange(192)))
        else:
            data = bytearray(wire(rng.randrange(64), rng.choice(controls), rng.randbytes(rng.randrange(513)), bool(rng.randrange(2))))
            if choice == 2:
                data[-2] ^= 0x55
            elif choice == 3:
                data[-1] = 0
            elif choice == 4:
                data[3:5] = bytes([0xfe, 0xff])
            elif choice == 5:
                data = bytearray(b"noise\xf9\xf9") + data
            split = rng.randrange(len(data) + 1)
            receive(data[:split])
            receive(data[split:])
    for maximum in [1, 127, 128, 512, 32767]:
        limit(maximum)
        data = wire(63, 3, rng.randbytes(maximum))
        for start in range(0, len(data), 97):
            receive(data[start:start + 97])
    return bytes(output), operations


def main():
    parser = argparse.ArgumentParser(description="Build the safe mux framing library and compare deterministic replay against the exact production C encoder and parser; no modem or Android service is accessed")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC", "rustc"))
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source_root = tests.parent
    source = (source_root / "mindone_mux.c").read_text()
    defines = [line for line in source.splitlines() if re.match(r"#define\s+(" + "|".join(DEFINES) + r")\s", line)]
    if len(defines) != len(DEFINES):
        raise ValueError("Expected all production framing constants exactly once")
    fixture = (tests / "replay.c").read_text().replace("@DEFINES@", "\n".join(defines))
    fixture = fixture.replace("@FUNCTIONS@", "\n\n".join(source_function(source, name) for name in FUNCTIONS))
    stream, operations = replay()

    def run(command, **kwargs):
        return subprocess.run(command, check=True, timeout=60, **kwargs)

    with tempfile.TemporaryDirectory(prefix="mindone-mux-core-") as directory:
        output = Path(directory)
        (output / "replay.c").write_text(fixture)
        library = output / "libmindone_mux_core.rlib"
        run([args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib", "--crate-name=mindone_mux_core", str(source_root / "lib.rs"), "-o", str(library)])
        dependency = ["--extern", f"mindone_mux_core={library}"]
        run([args.rustc, "--edition=2021", "-Dwarnings", "--test", str(tests / "framing.rs"), *dependency, "-o", str(output / "framing")])
        run([str(output / "framing")])
        run([args.rustc, "--edition=2021", "-Dwarnings", str(tests / "replay.rs"), *dependency, "-o", str(output / "rust-replay")])
        run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(output / "replay.c"), "-o", str(output / "c-replay")])
        c = run([str(output / "c-replay")], input=stream, capture_output=True)
        rust = run([str(output / "rust-replay")], input=stream, capture_output=True)
        if c.stdout != rust.stdout:
            left, right = c.stdout.splitlines(), rust.stdout.splitlines()
            mismatch = next((index for index, pair in enumerate(zip(left, right)) if pair[0] != pair[1]), min(len(left), len(right)))
            raise AssertionError(f"C/Rust replay mismatch at output {mismatch}: {left[mismatch:mismatch + 1]} != {right[mismatch:mismatch + 1]}")
        frames = sum(line.startswith(b"FRAME ") for line in rust.stdout.splitlines())
        print(json.dumps({"status": "pass", "seed": 729, "operations": operations, "frames": frames, "replay_sha256": hashlib.sha256(stream).hexdigest(), "identical_output_sha256": hashlib.sha256(rust.stdout).hexdigest(), "production_c_sha256": hashlib.sha256(source.encode()).hexdigest(), "scope": "Source framing only; target Soong build, fd I/O, modem state, init, SELinux and hardware behavior remain untested"}))


if __name__ == "__main__":
    main()
