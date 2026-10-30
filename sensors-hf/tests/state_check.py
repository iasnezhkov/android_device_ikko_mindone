# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


INPUTS = ("Android.bp", "Sensors.cpp", "Sensors.h", "HfRustCore.h", "rust/lib.rs", "rust/calibration.rs",
          "rust/ffi.rs", "tests/core.rs", "tests/state.cpp", "tests/state_check.py")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(source, signature, end="\n}"):
    if source.count(signature) != 1:
        raise ValueError(f"Expected one production definition: {signature}")
    start = source.index(signature)
    return source[start:source.index(end, start) + len(end)]


def main():
    parser = argparse.ArgumentParser(description="Check actual Sensors state methods with the real opt-in Rust core and substituted Android/backend boundaries")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC", "rustc"))
    parser.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
    parser.add_argument("--fault-controls", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    before = {name: digest(source / name) for name in INPUTS}
    production = (source / "Sensors.cpp").read_text()
    state = extract((source / "Sensors.h").read_text(), "    struct SensorState {", "\n    };")
    template = (source / "tests/state.cpp").read_text()

    def fixture(implementation):
        methods = "\n\n".join(extract(implementation, signature) for signature in (
            "::ndk::ScopedAStatus Sensors::activate(", "::ndk::ScopedAStatus Sensors::batch(",
            "::ndk::ScopedAStatus Sensors::flush("))
        return template.replace("PRODUCTION_STATE", state).replace("PRODUCTION_FUNCTIONS", methods)

    def run(command, **kwargs):
        return subprocess.run(command, check=True, timeout=60, **kwargs)

    with tempfile.TemporaryDirectory(prefix="mindone-sensors-state-") as directory:
        root = Path(directory)
        library = root / "libmindone_sensors_core.rlib"
        archive = root / "libmindone_sensors_wire_ffi.a"
        run([args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib",
             "--crate-name=mindone_sensors_core", str(source / "rust/lib.rs"), "-o", str(library)])
        dependency = ["--extern", f"mindone_sensors_core={library}"]
        run([args.rustc, "--edition=2021", "-Dwarnings", "--test", str(source / "tests/core.rs"),
             *dependency, "-o", str(root / "core-test")])
        core_checked = run([str(root / "core-test")], capture_output=True, text=True)
        print(core_checked.stdout, end="")
        linked = run([args.rustc, "--edition=2021", "-Dwarnings", "-Dunsafe_op_in_unsafe_fn",
                      "--crate-type=staticlib", "--crate-name=mindone_sensors_wire_ffi", "-C", "panic=abort",
                      "--print=native-static-libs", *dependency, str(source / "rust/ffi.rs"),
                      "-o", str(archive)], capture_output=True, text=True)
        matches = re.findall(r"native-static-libs: (.+)", linked.stderr)
        if len(matches) != 1:
            raise ValueError("Expected one compiler-provided native link input list")
        native = list(dict.fromkeys(shlex.split(matches[0])))
        cpp = root / "state.cpp"
        common = [args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1", "-g", "-pthread",
                  "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(source), str(cpp)]
        variants = {}
        traces = {}
        for name, extra in (("default", []), ("rust", ["-DMINDONE_HF_RUST_WIRE", str(archive), *native])):
            binary = root / name
            cpp.write_text(fixture(production))
            run([*common, *extra, "-o", str(binary)])
            checked = run([str(binary)], capture_output=True, text=True)
            if "10000 exact state replays and lifecycle checks PASS" not in checked.stdout:
                raise ValueError(f"Missing acceptance marker: {name}")
            variants[name] = checked.stdout.strip()
            print(f"{name}: {checked.stdout}", end="")
            traces[name] = run([str(binary), "trace"], capture_output=True).stdout
        if traces["default"] != traces["rust"]:
            raise ValueError("Default and Rust backend/status/state traces differ")
        print(f"valid/error state trace parity: {len(traces['rust'])} bytes identical")
        controls = []
        if args.fault_controls:
            commit = "    state_[in_sensorHandle] = {transition.next.active != 0, transition.next.period_ns,\n                               transition.next.latency_ns};\n"
            batch = extract(production, "::ndk::ScopedAStatus Sensors::batch(")
            early = batch.replace(commit, "", 1).replace(
                "    if (transition.command == MINDONE_HF_STATE_ENABLE &&", commit +
                "    if (transition.command == MINDONE_HF_STATE_ENABLE &&", 1)
            if early == batch or batch.count(commit) != 1:
                raise ValueError("Missing unique batch commit mutation site")
            mutants = [
                ("commit-before-backend", production.replace(batch, early, 1), "transaction commits only after backend success"),
                ("inactive-batch-writes", production.replace("transition.command == MINDONE_HF_STATE_ENABLE &&", "true &&", 1),
                 "expected backend action count"),
                ("ignore-one-shot", production.replace("oneShot ? 1u : 0u, &transition", "oneShot ? 0u : 0u, &transition", 1),
                 "inactive or one-shot flush rejected"),
                ("stale-batch-timing", production.replace("in_samplingPeriodNs,\n                               in_maxReportLatencyNs, &transition", "(static_cast<void>(in_samplingPeriodNs), st.samplingPeriodNs),\n                               (static_cast<void>(in_maxReportLatencyNs), st.maxReportLatencyNs), &transition", 1),
                 "transaction commits only after backend success"),
                ("release-state-lock", production.replace(batch, batch.replace("    std::lock_guard<std::mutex> lock(mutex_);\n", "", 1), 1),
                 "backend action remains under lifecycle mutex"),
            ]
            for name, mutant, expected in mutants:
                if mutant == production:
                    raise ValueError(f"Missing mutation site: {name}")
                cpp.write_text(fixture(mutant))
                binary = root / "mutant"
                run([*common, "-DMINDONE_HF_RUST_WIRE", str(archive), *native, "-o", str(binary)])
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
                if result.returncode != 1 or expected not in result.stderr:
                    raise ValueError(f"Mutation {name} not rejected: {result.returncode} {result.stdout}{result.stderr}")
                controls.append(name)
                print(f"fault-control {name}: rejected")
        if before != {name: digest(source / name) for name in INPUTS}:
            raise ValueError("Source inputs changed during acceptance")
        report = {
            "status": "pass", "mode": "host_exact_state_methods_actual_Rust_FFI_mocked_backend_not_Android",
            "source_sha256": before, "variants": variants, "fault_controls": controls,
            "parity_bytes": len(traces["rust"]), "parity_sha256": hashlib.sha256(traces["rust"]).hexdigest(),
            "host_rust_version": run([args.rustc, "--version"], capture_output=True, text=True).stdout.strip(),
            "input_hashes_unchanged": True,
            "limits": [
                "Only libsensors.mindone.rust.impl opts in; production service selection is unchanged.",
                "Actual activate/batch/flush bodies run with real std::mutex and static Rust FFI; Android types, enumeration and hf_manager are substituted.",
                "Negative timing is rejected before any opt-in side effect; failed initial activation also preserves an absent cache entry, unlike the default implementation's default entry.",
                "No target Soong/ELF/AIDL/FMQ/SELinux or hardware acceptance; SCP one-shot re-arm and full initialize/shutdown remain separate gates.",
            ],
        }
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report))


if __name__ == "__main__":
    main()
