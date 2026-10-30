# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile


INPUTS = (
    "Android.bp", "HfManagerClient.cpp", "HfManagerClient.h", "hf_manager_uapi.h",
    "HfRustCore.h", "rust/lib.rs", "rust/calibration.rs", "rust/ffi.rs", "tests/client.cpp", "tests/client_check.py",
)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description="Check both real HfManagerClient variants with actual Rust FFI and substituted OS call sites; no sensor devices or Android services are used")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC", "rustc"))
    parser.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
    parser.add_argument("--nm", default="nm")
    parser.add_argument("--kernel-source", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--fault-controls", action="store_true")
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    before = {name: sha256(source / name) for name in INPUTS}
    ioctl = args.kernel_source.resolve() / "include/uapi/asm-generic/ioctl.h"
    kernel_before = sha256(ioctl)
    production = (source / "HfManagerClient.cpp").read_text()
    template = (source / "tests/client.cpp").read_text()
    if template.count("PRODUCTION_CLIENT") != 1:
        raise ValueError("Expected one full production source substitution point")
    substitutions = {}

    def fixture(implementation):
        found = {}

        def replace(match):
            name = match[1]
            found[name] = found.get(name, 0) + 1
            return f"::fixture_{name}("

        replaced = re.sub(r"(?<![A-Za-z0-9_])::(open|close|ioctl|write|poll|read)\(", replace, implementation)
        replaced, sleeps = re.subn(r"\busleep\(", "fixture_usleep(", replaced)
        expected = {"open": 1, "close": 1, "ioctl": 4, "write": 3, "poll": 1, "read": 1}
        if found != expected or sleeps != 2:
            raise ValueError(f"Unexpected production OS boundaries: {found}, sleeps={sleeps}")
        substitutions.update(found, usleep=sleeps)
        return template.replace("PRODUCTION_CLIENT", replaced)

    def run(command, **kwargs):
        return subprocess.run(command, check=True, timeout=60, **kwargs)

    with tempfile.TemporaryDirectory(prefix="mindone-sensors-client-") as directory:
        output = Path(directory)
        (output / "linux").mkdir()
        shutil.copyfile(ioctl, output / "linux/ioctl.h")
        (output / "sys").mkdir()
        (output / "sys/ioctl.h").write_text("#include <linux/ioctl.h>\n")
        (output / "log").mkdir()
        (output / "log/log.h").write_text("inline void fixture_log(const char*, ...) {}\n#define ALOGE(...) fixture_log(__VA_ARGS__)\n#define ALOGI(...) fixture_log(__VA_ARGS__)\n")
        library = output / "libmindone_sensors_core.rlib"
        static = output / "libmindone_sensors_wire_ffi.a"
        run([args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib",
             "--crate-name=mindone_sensors_core", str(source / "rust/lib.rs"), "-o", str(library)])
        linked = run([args.rustc, "--edition=2021", "-Dwarnings", "-Dunsafe_op_in_unsafe_fn",
                      "--crate-type=staticlib", "--crate-name=mindone_sensors_wire_ffi", "-C", "panic=abort",
                      "--extern", f"mindone_sensors_core={library}", "--print=native-static-libs",
                      str(source / "rust/ffi.rs"), "-o", str(static)], capture_output=True, text=True)
        matches = re.findall(r"native-static-libs: (.+)", linked.stderr)
        if len(matches) != 1:
            raise ValueError("Expected exactly one compiler-provided native link input list")
        native = list(dict.fromkeys(shlex.split(matches[0])))
        cpp = output / "client.cpp"
        variants = {}
        traces = {}
        common = [args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                  "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(source),
                  "-I", str(output), str(cpp)]
        for name, extra in (("default", []), ("rust", ["-DMINDONE_HF_RUST_WIRE", str(static), *native])):
            binary = output / f"client-{name}"
            cpp.write_text(fixture(production))
            command = [*common, *extra, "-o", str(binary)]
            run(command)
            checked = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            if checked.returncode or "real client lifecycle" not in checked.stdout:
                raise ValueError(f"{name} client check failed ({checked.returncode}): {checked.stdout}{checked.stderr}")
            trace = run([str(binary), "valid"], capture_output=True, text=True)
            if not re.fullmatch(r"[0-9a-f]+\n", trace.stdout):
                raise ValueError(f"Expected exactly one hex wire/semantic trace: {name}")
            traces[name] = bytes.fromhex(trace.stdout)
            symbols = run([args.nm, str(binary)], capture_output=True, text=True).stdout
            exported = set(re.findall(r"\b[Tt] _?(mindone_hf_\w+)$", symbols, re.MULTILINE))
            expected = {"mindone_hf_decode_event", "mindone_hf_decode_info", "mindone_hf_enable_command",
                        "mindone_hf_flush_command", "mindone_hf_calibration_command"}
            if (name == "default" and "mindone_hf_" in symbols) or (name == "rust" and not expected <= exported):
                raise ValueError(f"Unexpected Rust wire symbol ownership: {name}: {exported}")
            variants[name] = {"assertions": checked.stdout.strip(), "Rust_archive_linked": name == "rust",
                              "defined_Rust_wire_symbols": sorted(exported)}
            print(name + ": " + checked.stdout.strip())
            if args.output:
                args.output.mkdir(parents=True, exist_ok=True)
                (args.output / f"{name}-valid.trace").write_bytes(traces[name])
        if traces["default"] != traces["rust"]:
            raise ValueError("Valid full-cycle wire effects and semantic event/info outputs differ")
        print(f"valid full-cycle parity: {len(traces['rust'])} bytes identical")
        controls = []
        if args.fault_controls:
            mutants = (
                ("ignore-command-error", "    if (encoded != 0) return false;",
                 "    if (encoded == INT32_MAX) return false;", "invalid Rust command is rejected before write", 3),
                ("ignore-event-error", "            if (status != 0) {",
                 "            if (status == INT32_MAX) {", "failed Rust decode clears the entire earlier prefix", 1),
                ("retain-prefix-on-decode-error",
                 'ALOGE("hf_manager returned an invalid event: %d", status);\n                out->clear();',
                 'ALOGE("hf_manager returned an invalid event: %d", status);',
                 "failed Rust decode clears the entire earlier prefix", 1),
                ("ignore-info-identity", "status != 0 || decoded.sensor_type != sensorType",
                 "status != 0", "invalid or mismatched info rejected without output", 1),
                ("leak-owned-fd", "    if (fd_ >= 0) ::close(fd_);",
                 "    if (fd_ == INT32_MAX) ::close(fd_);", "owned fd closed exactly once", 1),
                ("oversized-drain", "constexpr size_t kMaxEvents = 128;",
                 "constexpr size_t kMaxEvents = 4096;", "continuous producer has bounded output and reads", 1),
            )
            for name, old, new, expected, count in mutants:
                if production.count(old) != count:
                    raise ValueError(f"Expected {count} mutation sites for {name}")
                mutant = production.replace(old, new, 1)
                cpp.write_text(fixture(mutant))
                binary = output / "mutant"
                run([*common, "-DMINDONE_HF_RUST_WIRE", str(static), *native, "-o", str(binary)])
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
                if result.returncode != 1 or expected not in result.stderr:
                    raise ValueError(f"Mutation {name} was not rejected by its behavioral control: {result.returncode} {result.stdout}{result.stderr}")
                controls.append(name)
                print(f"fault-control {name}: rejected")
        if before != {name: sha256(source / name) for name in INPUTS} or kernel_before != sha256(ioctl):
            raise ValueError("Source inputs changed during the host acceptance run")
        report = {
            "status": "pass", "mode": "host_real_client_with_actual_Rust_FFI_and_mocked_OS_not_Android",
            "source_sha256": before, "kernel_ioctl_sha256": kernel_before,
            "variants": variants, "valid_parity_bytes": len(traces["rust"]),
            "valid_parity_sha256": hashlib.sha256(traces["rust"]).hexdigest(),
            "full_production_client_and_header": True, "OS_call_substitutions": substitutions,
            "fault_controls": controls, "input_hashes_unchanged": True,
            "host_rust_version": run([args.rustc, "--version"], capture_output=True, text=True).stdout.strip(),
            "host_cxx_version": run([args.cxx, "--version"], capture_output=True, text=True).stdout.splitlines()[0],
            "limits": [
                "Only the extra libsensors.mindone.rust.impl target enables Rust; the selected implementation, service, product, init and VINTF remain unchanged.",
                "Invalid sensor IDs, negative timing, malformed events and mismatched metadata are stricter opt-in errors; valid kernel wire effects and semantic outputs retain parity.",
                "The host uses the entire production client source and real header with only OS call sites and Android logging substituted. Driver and hardware scenarios remain untested.",
                "Soong/installed target variant, FMQ, SELinux, real hf_manager I/O, SCP and calibration durability remain unaccepted.",
            ],
        }
        if args.output:
            (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report))


if __name__ == "__main__":
    main()
