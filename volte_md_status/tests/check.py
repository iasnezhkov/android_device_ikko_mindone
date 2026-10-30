# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
import argparse
import errno
import os
from pathlib import Path
import random
import re
import struct
import subprocess
import tempfile


def run(command, **kwargs):
    return subprocess.run(command, check=True, timeout=60, **kwargs)


def main():
    parser = argparse.ArgumentParser(description="Compare the production C and Rust modem status sessions using deterministic binary replay")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC", "rustc"))
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source = tests.parent
    c_source = re.sub(r"^#include[^\n]*\n", "", (source / "volte_md_status.c").read_text(), flags=re.M)
    c_source = c_source.replace("int main(void)", "int status_main(void)")
    fixture = (tests / "replay.c").read_text().replace("@SOURCE@", c_source)
    rng = random.Random(886)
    stream = bytearray()
    publications = 0

    def record(data):
        stream.extend(struct.pack("<i", len(data)))
        stream.extend(data)

    def error(code):
        stream.extend(struct.pack("<i", -code))

    for kind in list(range(16)) + [-1, -2147483648, 16, 2147483647]:
        payload = bytearray(rng.randbytes(56))
        struct.pack_into("<i", payload, 20, kind)
        record(payload)
        publications += 1
    record(b"")
    for length in range(56):
        error(errno.EINTR)
        record(rng.randbytes(length))
    for _ in range(10000):
        kind = rng.randrange(8)
        if kind == 0:
            error(errno.EINTR)
        elif kind == 1:
            error(rng.choice([errno.EIO, errno.EAGAIN, errno.ENODEV]))
        else:
            length = 56 if kind >= 4 else rng.randrange(56)
            record(rng.randbytes(length))
            publications += length == 56
    record(b"")
    with tempfile.TemporaryDirectory(prefix="volte-rust-check-") as directory:
        output = Path(directory)
        (output / "replay.c").write_text(fixture)
        run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(output / "replay.c"), "-o", str(output / "c-replay")])
        run([args.rustc, "--edition=2021", "-Dwarnings", "--test", str(tests / "session.rs"), "-o", str(output / "session")])
        run([str(output / "session")])
        run([args.rustc, "--edition=2021", "-Dwarnings", str(tests / "replay.rs"), "-o", str(output / "rust-replay")])
        c = run([str(output / "c-replay")], input=stream, capture_output=True)
        rust = run([str(output / "rust-replay")], input=stream, capture_output=True)
        if c.stdout != rust.stdout:
            c_lines = c.stdout.splitlines()
            rust_lines = rust.stdout.splitlines()
            mismatch = next((i for i, pair in enumerate(zip(c_lines, rust_lines)) if pair[0] != pair[1]), min(len(c_lines), len(rust_lines)))
            raise AssertionError(f"C/Rust replay mismatch at operation {mismatch}: {c_lines[mismatch:mismatch+3]} != {rust_lines[mismatch:mismatch+3]}")
        actual = sum(line.startswith(b"publish ") for line in rust.stdout.splitlines())
        if actual != publications:
            raise AssertionError(f"Expected {publications} complete-event publications, got {actual}")
        print(f"PASS: C/Rust identical for {len(rust.stdout.splitlines())} operations, {actual} complete events, all 56 short-read sizes, seed 886")


if __name__ == "__main__":
    main()
