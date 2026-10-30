# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time


FILES = {
    "rustc", "clang", "readelf", "linker", "runtime_libc", "runtime_libdl",
    "crtbegin", "crtend", "link_flags", "link_libc", "link_libm", "link_libdl",
    "link_liblog", "unwind", "libc_bp",
}
SOURCES = {"main.rs", "protocol.rs", "Android.bp", "tests/android_mock.rs", "tests/android_check.py"}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1048576), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tree_sha256(path):
    entries = {str(entry.relative_to(path)): sha256(entry)
               for entry in sorted(path.rglob("*")) if entry.is_file()}
    if not entries:
        raise ValueError(f"Empty input tree: {path}")
    return hashlib.sha256(json.dumps(entries, sort_keys=True).encode()).hexdigest()


def verify_inputs(config, android_root, source_root):
    if config.get("schema_version") != 1 or set(config["files"]) != FILES:
        raise ValueError("Expected schema 1 and the complete explicit file input set")
    if set(config["source_sha256"]) != SOURCES:
        raise ValueError("Expected hashes for every pilot, mock and rig source input")
    if set(config["trees"]) != {"android_std", "libc_source"}:
        raise ValueError("Expected Android std and libc source tree hashes")
    paths = {}
    for kind in ("files", "trees"):
        for name, entry in config[kind].items():
            path = Path(entry["path"])
            path = path if path.is_absolute() else android_root / path
            actual = sha256(path) if kind == "files" else tree_sha256(path)
            if actual != entry["sha256"]:
                raise ValueError(f"Input hash mismatch: {name}: {path}")
            paths[name] = path.resolve()
    for name, expected in config["source_sha256"].items():
        if sha256(source_root / name) != expected:
            raise ValueError(f"Source hash mismatch: {name}")
    sysroot = Path(config["rust_sysroot"])
    paths["rust_sysroot"] = (sysroot if sysroot.is_absolute() else android_root / sysroot).resolve()
    if paths["android_std"] != paths["rust_sysroot"] / "lib/rustlib/aarch64-linux-android/lib":
        raise ValueError("Android std tree must belong to the explicit Rust sysroot")
    return paths


def live_group(group):
    proc = Path("/proc")
    if proc.is_dir():
        members = set()
        for entry in proc.iterdir():
            if not entry.name.isdecimal():
                continue
            try:
                fields = (entry / "stat").read_bytes().rsplit(b")", 1)[1].split()
                if int(fields[2]) == group and fields[0] not in (b"Z", b"X"):
                    members.add(int(entry.name))
            except (OSError, IndexError, ValueError):
                continue
        return members
    result = subprocess.run(["ps", "-axo", "pid=,pgid=,state="], check=True, stdout=subprocess.PIPE, timeout=1)
    return {int(pid) for pid, pgid, state in (line.split() for line in result.stdout.splitlines())
            if int(pgid) == group and not state.startswith((b"Z", b"X"))}


def stop_process(child):
    child.poll()
    if not live_group(child.pid):
        return False
    try:
        os.killpg(child.pid, signal.SIGTERM)
    except ProcessLookupError:
        return True
    deadline = time.monotonic() + 8
    grace = time.monotonic() + 1
    try:
        while time.monotonic() < deadline:
            child.poll()
            members = live_group(child.pid)
            if not members:
                child.wait(timeout=1)
                return True
            if time.monotonic() >= grace:
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            time.sleep(0.05)
        raise TimeoutError(f"Process group cleanup was not proved: {child.pid}")
    except BaseException:
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        child.wait(timeout=1)
        raise


def execute_step(command, cwd, environment, log, seconds):
    started = time.monotonic()
    with log.open("wb") as stream:
        child = subprocess.Popen(command, cwd=cwd, env=environment, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            result = child.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            result = 124
        except BaseException:
            stop_process(child)
            raise
        survivors = stop_process(child)
    if result == 0 and survivors:
        result = 125
    return {"argv": command, "rc": result, "process_group": child.pid, "descendants_stopped": survivors, "elapsed_seconds": round(time.monotonic() - started, 3)}


def private_directory(output):
    temporary = Path(tempfile.mkdtemp(prefix="mock-", dir=output))
    if stat.S_IMODE(temporary.stat().st_mode) != 0o700:
        raise ValueError(f"Expected a private mode 0700 directory: {temporary}")
    return temporary


def main():
    parser = argparse.ArgumentParser(description="Build and run an isolated Android ABI mock with explicitly hashed toolchain and Bionic inputs; this is not a Soong or hardware gate")
    parser.add_argument("--inputs", default=os.environ.get("MINDONE_ANDROID_PROOF_INPUTS"))
    parser.add_argument("--android-root", default=os.environ.get("MINDONE_ANDROID_PROOF_ROOT"))
    parser.add_argument("--output", required=True)
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args()
    if not args.inputs or not args.android_root or not 1 <= args.timeout <= 300:
        parser.error("Explicit hashed inputs, Android root and a timeout in 1..300 seconds are required")
    source = Path(__file__).resolve().parents[1]
    android_root = Path(args.android_root).resolve()
    output = Path(args.output).resolve()
    if output == source or source in output.parents:
        parser.error("Output must be outside the canonical source directory")
    config = json.loads(Path(args.inputs).read_text())
    inputs = verify_inputs(config, android_root, source)
    output.mkdir(parents=True, exist_ok=True)
    temporary = private_directory(output)
    if Path(args.inputs).resolve() != output / "inputs.json":
        shutil.copyfile(args.inputs, output / "inputs.json")
    report = {"mode": "direct_android_toolchain_not_soong", "steps": [], "artifacts": {}}
    environment = dict(os.environ, ROSETTA_DISABLE_AOT="1", RUSTC_BOOTSTRAP="1", TMPDIR=str(temporary))
    environment.pop("LD_PRELOAD", None)
    environment.pop("LD_AUDIT", None)
    environment["LD_LIBRARY_PATH"] = ":".join(str(inputs[key].parent) for key in ("runtime_libc", "runtime_libdl"))
    environment["CARGO_PKG_VERSION"] = config["libc_package_version"]

    def run(name, command, seconds=None):
        step = execute_step(command, android_root, environment, output / f"{name}.log", seconds or args.timeout)
        report["steps"].append({"name": name, **step})
        (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        if step["rc"]:
            raise RuntimeError(f"{name} failed with {step['rc']}; see {output / (name + '.log')}")

    run("rust-version", [str(inputs["rustc"]), "--version"])
    run("clang-version", [str(inputs["clang"]), "--version"])
    libc = output / "liblibc.rlib"
    libc_command = [str(inputs["rustc"]), "--crate-name", "libc", "--crate-type=rlib", "--edition=2021", "--target=aarch64-linux-android", "--sysroot", str(inputs["rust_sysroot"])]
    for value in config["libc_cfgs"]:
        libc_command += ["--cfg", value]
    run("libc", libc_command + [str(inputs["libc_source"] / "lib.rs"), "-o", str(libc)])
    common = [str(inputs["rustc"]), "--edition=2021", "--target=aarch64-linux-android", "--sysroot", str(inputs["rust_sysroot"]), "--extern", f"libc={libc}", "-C", f"linker={inputs['clang']}", "-C", "opt-level=2", "-C", "force-frame-pointers=y", "-C", "relocation-model=pic", "--cfg", "soong", "--cfg", "android_vndk"]
    for key in ("link_libc", "link_libm", "link_libdl", "link_liblog", "unwind"):
        common += ["-L", f"native={inputs[key].parent}"]
    flags = shlex.split(inputs["link_flags"].read_text())
    flags += [str(inputs["crtbegin"]), str(inputs["crtend"]), "-Wl,--dynamic-linker=/system/bin/linker64"]
    for flag in flags:
        common += ["-C", f"link-arg={flag}"]
    production = output / "volte_md_status_rust"
    mock = output / "android_mock"
    run("production-build", common + ["-C", "panic=abort", str(source / "main.rs"), "-l", "log", "-o", str(production)])
    run("mock-build", common + ["-C", "panic=abort", "-Z", "panic-abort-tests", "--test", str(source / "tests/android_mock.rs"), "-l", "log", "-o", str(mock)])
    for path in (production, mock):
        run(path.name + "-elf", [str(inputs["readelf"]), "-h", "-l", "-d", "-s", str(path)], 30)
        header = path.read_bytes()[:20]
        if header[:6] != b"\x7fELF\x02\x01" or int.from_bytes(header[18:20], "little") != 183:
            raise ValueError(f"Expected a little-endian ELF64 AArch64 artifact: {path}")
        report["artifacts"][path.name] = {"sha256": sha256(path), "size": path.stat().st_size}
    run("mock-needed", [str(inputs["linker"]), "--list", str(mock)], 30)
    run("mock-run", [str(inputs["linker"]), str(mock), "-Z", "unstable-options", "--force-run-in-process"], 30)
    verify_inputs(config, android_root, source)
    report["input_hashes_unchanged"] = True
    report["limitations"] = ["Direct compilation uses static Rust std and rebuilt libc; it does not prove the configured Soong installed variant.", "Production entrypoint was not executed; init, SELinux, device ownership and hardware behavior remain untested."]
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": "pass", "mode": report["mode"], "artifacts": report["artifacts"]}))


def interrupt(signum, _frame):
    raise SystemExit(128 + signum)


if __name__ == "__main__":
    for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(signum, interrupt)
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(f"Android ABI proof failed: {error}", file=sys.stderr)
        raise SystemExit(1)
