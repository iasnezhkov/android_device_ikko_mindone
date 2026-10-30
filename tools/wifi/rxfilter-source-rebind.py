#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

UNITS = ("policy", "query", "firmware", "legacy", "mcast", "table", "config")
NAMES = [f"tools/wifi/rxfilter-{unit}.{extension}" for unit in UNITS for extension in ("c", "h")]
TOKEN = re.compile(r"^(\S+) '(.*)'(?:\t.*)?\tLoc=<.*>$")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def tokens(clang, directory, unit):
    result = subprocess.run([clang, "-std=c11", "-ffreestanding", "-fsyntax-only", "-Xclang", "-dump-tokens",
                             "-I", str(directory), str(directory / f"rxfilter-{unit}.c")],
                            capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        raise ValueError(f"Token dump failed for {unit}: {result.stderr[-2000:]}")
    stream = []
    for line in result.stderr.splitlines():
        match = TOKEN.match(line)
        if not match:
            raise ValueError(f"Unexpected token dump line for {unit}: {line}")
        stream.append(match.group(1) + " " + match.group(2))
    if not stream or stream[-1] != "eof ":
        raise ValueError(f"Incomplete token stream for {unit}")
    return digest("\n".join(stream).encode())


def main():
    parser = argparse.ArgumentParser(description="Rebind an RXFILTER firmware receipt to edited sources only when every "
                                     "translation unit preprocesses to the same token stream as the sources the "
                                     "receipt was built from (taken from git at --old-rev and checked against the "
                                     "receipt); writes the receipt in place and records the proof in source_rebinds")
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--tree", type=Path, required=True, help="Directory that holds tools/wifi (the device tree)")
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--git-prefix", required=True, help="Path of --tree inside the repository")
    parser.add_argument("--old-rev", required=True)
    parser.add_argument("--clang", default=shutil.which("clang"))
    args = parser.parse_args()
    receipt = json.loads(args.receipt.read_bytes())
    if set(receipt["sources"]) != set(NAMES):
        raise ValueError("Receipt source set differs")
    with tempfile.TemporaryDirectory(prefix="rxfilter-rebind-") as temporary:
        old_dir, new_dir = Path(temporary) / "old", Path(temporary) / "new"
        old_dir.mkdir()
        new_dir.mkdir()
        old_sources, new_sources = {}, {}
        for name in NAMES:
            old = subprocess.run(["git", "-C", str(args.repository), "show", f"{args.old_rev}:{args.git_prefix}/{name}"],
                                 capture_output=True, check=True).stdout
            if digest(old) != receipt["sources"][name]:
                raise ValueError(f"Old revision does not hold the receipt's source: {name}")
            new = (args.tree / name).read_bytes()
            (old_dir / Path(name).name).write_bytes(old)
            (new_dir / Path(name).name).write_bytes(new)
            old_sources[name], new_sources[name] = digest(old), digest(new)
        streams = {}
        for unit in UNITS:
            before, after = tokens(args.clang, old_dir, unit), tokens(args.clang, new_dir, unit)
            if before != after:
                raise ValueError(f"Preprocessed token stream differs: rxfilter-{unit}.c")
            streams[f"rxfilter-{unit}.c"] = after
    if new_sources == old_sources:
        print(json.dumps({"status": "UNCHANGED"}))
        return
    version = subprocess.run([args.clang, "--version"], capture_output=True, text=True, check=True).stdout.splitlines()[0]
    receipt.setdefault("source_rebinds", []).append({
        "old_revision": args.old_rev, "old_sources": old_sources, "new_sources": new_sources,
        "token_stream_sha256": streams, "clang": version,
        "basis": "Equal post-preprocessor token streams for every translation unit; the build compiles exactly these "
                 "units with fixed flags and no __LINE__, __FILE__ or __COUNTER__ use, so the firmware is unchanged"})
    receipt["sources"] = new_sources
    args.receipt.write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"status": "REBOUND", "units": len(streams), "changed_sources": sorted(
        name for name in NAMES if old_sources[name] != new_sources[name])}))


if __name__ == "__main__":
    main()
