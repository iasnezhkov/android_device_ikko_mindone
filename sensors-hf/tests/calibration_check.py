# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project

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


INPUTS = ("Android.bp", "CalibrationStore.cpp", "CalibrationStore.h", "NvcfgJson.cpp",
          "NvcfgJson.h", "HfManagerClient.h", "hf_manager_uapi.h", "HfRustCore.h",
          "rust/lib.rs", "rust/calibration.rs", "rust/ffi.rs", "tests/core.rs",
          "tests/calibration.cpp", "tests/calibration_oracle.c", "tests/calibration_check.py")
RIGS = ("sensor-calibration-check.cpp", "sensor-calibration-json-check.cpp")
KERNEL = ("include/uapi/asm-generic/ioctl.h",)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description="Check the complete CalibrationStore with actual Rust FFI and an independent calibration contract")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC", "rustc"))
    parser.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--kernel-source", type=Path, required=True)
    parser.add_argument("--fault-controls", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    rigs = source.parents[3] / "tools/rigs"
    kernel = args.kernel_source.resolve()
    before = {name: digest(source / name) for name in INPUTS}
    rig_before = {name: digest(rigs / name) for name in RIGS}
    kernel_before = {name: digest(kernel / name) for name in KERNEL}

    def run(command, **kwargs):
        return subprocess.run(command, check=True, timeout=60, **kwargs)

    with tempfile.TemporaryDirectory(prefix="mindone-calibration-") as directory:
        root = Path(directory)
        for name in INPUTS:
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / name, target)
        (root / "log").mkdir()
        (root / "log/log.h").write_text("#pragma once\nstatic inline void fixtureLog(const char*, ...) {}\n#define ALOGI(...) fixtureLog(__VA_ARGS__)\n#define ALOGW(...) fixtureLog(__VA_ARGS__)\n#define ALOGE(...) fixtureLog(__VA_ARGS__)\n")
        (root / "linux").mkdir()
        shutil.copyfile(kernel / KERNEL[0], root / "linux/ioctl.h")
        (root / "linux/types.h").write_text("#include <stdint.h>\n")
        shutil.copyfile(source / "tests/calibration_oracle.c", root / "oracle.c")
        sanitizer = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", "-g", *sanitizer,
             "-I", str(root), "-c", str(root / "oracle.c"), "-o", str(root / "oracle.o")])
        library = root / "libmindone_sensors_core.rlib"
        archive = root / "libmindone_sensors_wire_ffi.a"
        dependency = ["--extern", f"mindone_sensors_core={library}"]

        def build_rust():
            run([args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib",
                 "--crate-name=mindone_sensors_core", str(root / "rust/lib.rs"), "-o", str(library)])
            linked = run([args.rustc, "--edition=2021", "-Dwarnings", "-Dunsafe_op_in_unsafe_fn",
                          "--crate-type=staticlib", "--crate-name=mindone_sensors_wire_ffi", "-C", "panic=abort",
                          "--print=native-static-libs", *dependency, str(root / "rust/ffi.rs"), "-o", str(archive)],
                         capture_output=True, text=True)
            matches = re.findall(r"native-static-libs: (.+)", linked.stderr)
            if len(matches) != 1:
                raise ValueError("Expected one compiler-provided native link input list")
            return list(dict.fromkeys(shlex.split(matches[0])))

        native = build_rust()
        run([args.rustc, "--edition=2021", "-Dwarnings", "--test", str(root / "tests/core.rs"),
             *dependency, "-o", str(root / "core-test")])
        core_result = run([str(root / "core-test")], capture_output=True, text=True).stdout
        print(core_result, end="", flush=True)
        common = [args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                  "-O1", "-g", "-pthread", *sanitizer, "-I", str(root)]
        variants = {}
        traces = {}
        for name, flags in (("default", []), ("rust", ["-DMINDONE_HF_RUST_WIRE", str(archive), *native])):
            binary = root / (name + "-check")
            run([*common, str(root / "tests/calibration.cpp"), str(root / "oracle.o"), *flags, "-o", str(binary)])
            variants[name] = run([str(binary)], capture_output=True, text=True).stdout.strip()
            traces[name] = run([str(binary), "trace"], capture_output=True).stdout
            print(f"{name}: {variants[name]}", flush=True)
            run([*common, "-DHAVE_DEFERRED=1", str(rigs / RIGS[0]), *flags, "-o", str(root / "lifecycle")])
            variants[name + "_lifecycle"] = run([str(root / "lifecycle")], capture_output=True, text=True).stdout.strip()
            run([*common, str(rigs / RIGS[1]), str(root / "CalibrationStore.cpp"), str(root / "NvcfgJson.cpp"),
                 *flags, "-o", str(root / "json-check")])
            disk = root / (name + "-disk")
            disk.mkdir()
            variants[name + "_json"] = run([str(root / "json-check"), str(disk)], capture_output=True, text=True).stdout.strip()
            print(f"{name}: {variants[name + '_lifecycle']} {variants[name + '_json']}", flush=True)
        if traces["default"] != traces["rust"]:
            raise ValueError("Whole-store payload/cache/backend traces differ")
        controls = []
        if args.fault_controls:
            mutants = (
                ("wrong-temperature-offset", "rust/calibration.rs", "(12, 12), (24, 24)", "(12, 12), (20, 24)", "payload matches independent calibration contract"),
                ("ignore-combined-file", "rust/calibration.rs", "bias_count > 0 &&", "bias_count == 0 &&", "payload matches independent calibration contract"),
                ("short-temperature", "rust/calibration.rs", "let count = region.length / 4;", "let count = region.length / 4 - 1;", "outer dispatch stores exactly"),
                ("drop-temperature-dispatch", "CalibrationStore.cpp", "case 2: region = Region::Temp;", "case 2: return; region = Region::Temp;", "outer dispatch stores exactly"),
                ("ignore-payload-error", "CalibrationStore.cpp", "status != 0 || actual.length", "false || actual.length", "FFI rejection cannot seed partial caches"),
                ("accept-oversized-combined", "CalibrationStore.cpp", "cali->size() != (actual.bias_length + actual.cali_length)", "cali->size() < (actual.bias_length + actual.cali_length)", "invalid combined selector rejected before cache seeding"),
                ("ignore-event-error", "CalibrationStore.cpp", "sizeof(ke.word) / sizeof(ke.word[0]), &event) != 0)", "sizeof(ke.word) / sizeof(ke.word[0]), &event) == 123)", "event FFI rejection preserves outer cache state"),
                ("seed-before-validation", "CalibrationStore.cpp", "    const auto& actual = result.layout;", "    if (bias) seedCache(&biasCache_[layout.sensorType], *bias);\n    const auto& actual = result.layout;", "FFI rejection cannot seed partial caches"),
                ("reverse-seed-order", "CalibrationStore.cpp", "    if (bias) seedCache(&biasCache_[layout.sensorType], *bias);", "", "combined file overrides clean split seed"),
                ("overwrite-dirty-seed", "CalibrationStore.cpp", "    if (cache->haveValue && (!cache->havePersistedValue ||\n                            cache->values != cache->persistedValues)) return;", "", "reseed preserves dirty cache"),
                ("subscribe-after-failed-config", "CalibrationStore.cpp", "layout.name, payload.size());\n            continue;", "layout.name, payload.size());", "failed configuration never subscribes"),
                ("accept-empty-payload", "CalibrationStore.cpp", "        if (payload.size() != layout.totalLength) continue;", "", "FFI rejection stops outer configuration"),
            )
            for name, path, old, new, expected in mutants:
                target = root / path
                original = (source / path).read_text()
                if original.count(old) != 1:
                    raise ValueError(f"Expected one mutation site: {name}")
                if name == "reverse-seed-order":
                    mutated = original.replace(old, new, 1).replace(
                        "    if (temp) seedCache(&tempCache_[layout.sensorType], *temp);",
                        old + "\n    if (temp) seedCache(&tempCache_[layout.sensorType], *temp);", 1)
                else:
                    mutated = original.replace(old, new, 1)
                target.write_text(mutated)
                if path.endswith(".rs"):
                    build_rust()
                binary = root / "mutant"
                run([*common, str(root / "tests/calibration.cpp"), str(root / "oracle.o"),
                     "-DMINDONE_HF_RUST_WIRE", str(archive), *native, "-o", str(binary)])
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                if result.returncode != 1 or expected not in result.stderr:
                    raise ValueError(f"Mutation {name} was not rejected as expected: {result.returncode}\n{result.stdout}\n{result.stderr}")
                controls.append(name)
                print(f"fault-control {name}: rejected", flush=True)
                target.write_text(original)
                if path.endswith(".rs"):
                    build_rust()
        if before != {name: digest(source / name) for name in INPUTS} or rig_before != {name: digest(rigs / name) for name in RIGS} or kernel_before != {name: digest(kernel / name) for name in KERNEL}:
            raise ValueError("Source inputs changed during acceptance")
        report = {
            "status": "pass", "mode": "host_whole_CalibrationStore_actual_Rust_FFI_mocked_disk_and_hf_manager",
            "source_sha256": before, "rig_sha256": rig_before, "kernel_sha256": kernel_before,
            "core_tests": core_result.strip(), "variants": variants, "fault_controls": controls,
            "parity_bytes": len(traces["rust"]), "parity_sha256": hashlib.sha256(traces["rust"]).hexdigest(),
            "host_rust_version": run([args.rustc, "--version"], capture_output=True, text=True).stdout.strip(),
            "input_hashes_unchanged": True, "production_default_Rust_enabled": False,
            "limits": ["Disk I/O, cache and deferred writer remain C++; real JSON roundtrip runs separately.",
                       "The independent oracle checks the eight declared calibration regions; no kernel implementation is compiled or executed.",
                       "TEMP six-word transport parity is not temperature compensation evidence.",
                       "No target Soong/service link/FMQ/SELinux/runtime, SCP reset or nvcfg power-loss acceptance."],
        }
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report))


if __name__ == "__main__":
    main()
