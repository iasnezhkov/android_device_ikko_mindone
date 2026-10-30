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
    "Android.bp", "HfManagerClient.cpp", "HfRustCore.h", "hf_manager_uapi.h",
    "rust/lib.rs", "rust/calibration.rs", "rust/ffi.rs", "tests/core.rs", "tests/ffi.cpp", "tests/kernel_abi.c",
    "tests/check.py",
)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def function(source, signature):
    if source.count(signature) != 1:
        raise ValueError(f"Expected one production definition: {signature}")
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


def main():
    parser = argparse.ArgumentParser(description="Check the safe Rust sensor wire core, its C ABI and the exact production hf_manager command/read methods without Android services or sensor devices")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC", "rustc"))
    parser.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--kernel-source", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--fault-controls", action="store_true")
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    before = {name: sha256(source / name) for name in INPUTS}
    kernel = args.kernel_source.resolve()
    kernel_inputs = (
        "mindone/modules/hf_manager/hf_sensor_io.h",
        "mindone/modules/hf_manager/hf_sensor_type.h",
        "include/uapi/asm-generic/ioctl.h",
    )
    kernel_before = {name: sha256(kernel / name) for name in kernel_inputs}
    production = (source / "HfManagerClient.cpp").read_text()
    fixture_source = (source / "tests/ffi.cpp").read_text()
    signatures = (
        "bool HfManagerClient::setEnable(", "bool HfManagerClient::requestFlush(",
        "bool HfManagerClient::configCalibration(", "bool HfManagerClient::waitAndReadEvents(",
    )

    def fixture(implementation):
        functions = "\n\n".join(function(implementation, signature) for signature in signatures)
        for original, replacement in (("::write(", "::fixture_write("),
                                      ("::poll(", "::fixture_poll("),
                                      ("::read(", "::fixture_read(")):
            functions = functions.replace(original, replacement)
        return fixture_source.replace("PRODUCTION_FUNCTIONS", functions)

    def run(command, **kwargs):
        return subprocess.run(command, check=True, timeout=60, **kwargs)

    with tempfile.TemporaryDirectory(prefix="mindone-sensors-core-") as directory:
        output = Path(directory)
        (output / "linux").mkdir()
        shutil.copyfile(kernel / "include/uapi/asm-generic/ioctl.h", output / "linux/ioctl.h")
        (output / "linux/types.h").write_text("#include <stdint.h>\n")
        for name in ("hf_sensor_io.h", "hf_sensor_type.h"):
            shutil.copyfile(kernel / "mindone/modules/hf_manager" / name, output / name)
        run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(output),
             str(source / "tests/kernel_abi.c"), "-o", str(output / "kernel-abi")])
        kernel_checked = run([str(output / "kernel-abi")], capture_output=True, text=True)
        print(kernel_checked.stdout, end="")
        library = output / "libmindone_sensors_core.rlib"
        static = output / "libmindone_sensors_wire_ffi.a"
        dependency = ["--extern", f"mindone_sensors_core={library}"]
        run([args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib",
             "--crate-name=mindone_sensors_core", str(source / "rust/lib.rs"), "-o", str(library)])
        run([args.rustc, "--edition=2021", "-Dwarnings", "--test", str(source / "tests/core.rs"),
             *dependency, "-o", str(output / "core-test")])
        run([str(output / "core-test")])
        ffi_command = [args.rustc, "--edition=2021", "-Dwarnings", "-Dunsafe_op_in_unsafe_fn",
                       "--crate-type=staticlib", "--crate-name=mindone_sensors_wire_ffi",
                       "-C", "panic=abort", "--print=native-static-libs", *dependency,
                       str(source / "rust/ffi.rs"), "-o", str(static)]
        linked = run(ffi_command, capture_output=True, text=True)
        matches = re.findall(r"native-static-libs: (.+)", linked.stderr)
        if len(matches) != 1:
            raise ValueError("Expected exactly one compiler-provided native static link input list")
        native = list(dict.fromkeys(shlex.split(matches[0])))
        cpp = output / "fixture.cpp"
        binary = output / "ffi-check"
        command = [args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                   "-I", str(source), "-I", str(output), str(cpp), str(static),
                   *native, "-o", str(binary)]
        cpp.write_text(fixture(production))
        run(command)
        checked = run([str(binary)], capture_output=True, text=True)
        print(checked.stdout, end="")
        controls = []
        if args.fault_controls:
            mutants = (
                ("missing-framing-check", "static_cast<size_t>(n) % sizeof(hf_manager_event) != 0",
                 "false", "invalid read framing"),
                ("oversized-drain-budget", "constexpr size_t kMaxEvents = 128;",
                 "constexpr size_t kMaxEvents = 4096;", "continuous producer"),
                ("unbounded-interrupt-retries", "constexpr size_t kMaxReads = 16;",
                 "constexpr size_t kMaxReads = 1024;", "persistent EINTR"),
                ("partial-prefix-on-read-failure",
                 'ALOGE("read(hf_manager) failed: %s", strerror(errno));\n            out->clear();',
                 'ALOGE("read(hf_manager) failed: %s", strerror(errno));',
                 "later read failure"),
            )
            for name, old, new, expected in mutants:
                if production.count(old) != 1:
                    raise ValueError(f"Expected one mutation site: {name}")
                cpp.write_text(fixture(production.replace(old, new, 1)))
                run(command)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
                if result.returncode != 1 or expected not in result.stderr:
                    raise ValueError(f"Mutation {name} was not rejected by its behavioral control: {result.stderr}")
                controls.append(name)
                print(f"fault-control {name}: rejected")
        after = {name: sha256(source / name) for name in INPUTS}
        kernel_after = {name: sha256(kernel / name) for name in kernel_inputs}
        if before != after or kernel_before != kernel_after:
            raise ValueError("Source inputs changed during the host acceptance run")
        report = {
            "status": "pass", "mode": "host_source_and_static_C_ABI_not_Android",
            "source_sha256": before,
            "kernel_source": str(kernel), "kernel_uapi_sha256": kernel_before,
            "kernel_abi": kernel_checked.stdout.strip(),
            "host_rust_version": run([args.rustc, "--version"], capture_output=True, text=True).stdout.strip(),
            "host_cxx_version": run([args.cxx, "--version"], capture_output=True, text=True).stdout.splitlines()[0],
            "cpp_assertions": checked.stdout.strip(), "fault_controls": controls,
            "input_hashes_unchanged": True, "production_default_Rust_enabled": False,
            "limits": [
                "Rust wire core and narrow static C ABI are separately buildable opt-in targets; the selected AIDL service is not linked to them yet.",
                "Target Soong/ELF/AIDL/FMQ/SELinux, real hf_manager I/O, SCP reset and calibration durability remain untested.",
                "The host C++ fixture uses the unchanged userspace wire structs and exact production command/read function bodies with only OS call boundaries substituted.",
            ],
        }
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report))


if __name__ == "__main__":
    main()
