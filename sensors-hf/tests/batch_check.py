# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


INPUTS = ("Sensors.cpp", "Sensors.h", "tests/batch.cpp", "tests/batch_check.py")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(source, signature, end):
    if source.count(signature) != 1:
        raise ValueError(f"Expected one production definition: {signature}")
    start = source.index(signature)
    return source[start:source.index(end, start) + len(end)]


def main():
    parser = argparse.ArgumentParser(description="Check the exact production Sensors batch/activate state transaction without Android services or sensor devices")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--fault-controls", action="store_true")
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    before = {name: sha256(source / name) for name in INPUTS}
    production = (source / "Sensors.cpp").read_text()
    header = (source / "Sensors.h").read_text()
    template = (source / "tests/batch.cpp").read_text()
    state = extract(header, "    struct SensorState {", "\n    };")
    activate = extract(production, "::ndk::ScopedAStatus Sensors::activate(", "\n}")
    batch = extract(production, "::ndk::ScopedAStatus Sensors::batch(", "\n}")

    def fixture(batch_source):
        if template.count("PRODUCTION_STATE") != 1 or template.count("PRODUCTION_FUNCTIONS") != 1:
            raise ValueError("Expected unique production substitution points")
        return template.replace("PRODUCTION_STATE", state).replace("PRODUCTION_FUNCTIONS", activate + "\n\n" + batch_source)

    def run(command, **kwargs):
        return subprocess.run(command, check=True, timeout=45, **kwargs)

    with tempfile.TemporaryDirectory(prefix="mindone-sensors-batch-") as directory:
        output = Path(directory)
        cpp = output / "fixture.cpp"
        binary = output / "batch-check"
        command = [args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(cpp), "-o", str(binary)]
        cpp.write_text(fixture(batch))
        run(command)
        checked = subprocess.run([str(binary)], capture_output=True, text=True, timeout=45)
        print(checked.stdout, end="")
        print(checked.stderr, end="")
        checked.check_returncode()
        controls = []
        if args.fault_controls:
            assignments = "    st.samplingPeriodNs = in_samplingPeriodNs;\n    st.maxReportLatencyNs = in_maxReportLatencyNs;\n"
            if batch.count(assignments) != 1 or batch.count("    if (st.active) {\n") != 1:
                raise ValueError("Expected unique transaction mutation points")
            early = batch.replace(assignments, "", 1).replace("    if (st.active) {\n", assignments + "    if (st.active) {\n", 1)
            old_values = batch.replace("true, in_samplingPeriodNs,\n                            in_maxReportLatencyNs", "true, st.samplingPeriodNs,\n                            st.maxReportLatencyNs", 1)
            inactive = batch.replace("    if (st.active) {\n", "    if (true) {\n", 1)
            for name, mutant, expected in (
                ("cache-before-backend", early, "failed active batch must preserve accepted cache"),
                ("active-write-uses-old-cache", old_values, "active batch must send requested values"),
                ("inactive-batch-writes-backend", inactive, "inactive batch must not write backend"),
            ):
                if mutant == batch:
                    raise ValueError(f"Mutation site missing: {name}")
                cpp.write_text(fixture(mutant))
                run(command)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
                if result.returncode != 1 or expected not in result.stderr:
                    raise ValueError(f"Mutation {name} was not rejected by its behavioral control: {result.stderr}")
                controls.append(name)
                print(f"fault-control {name}: rejected")
        after = {name: sha256(source / name) for name in INPUTS}
        if before != after:
            raise ValueError("Source inputs changed during the host acceptance run")
        report = {
            "status": "pass", "mode": "host_exact_production_batch_activate_not_Android",
            "source_sha256": before, "input_hashes_unchanged": True,
            "host_cxx_version": run([args.cxx, "--version"], capture_output=True, text=True).stdout.splitlines()[0],
            "cpp_assertions": checked.stdout.strip(), "fault_controls": controls,
            "limits": [
                "The exact production batch/activate function bodies and SensorState definition run against a deterministic backend boundary and minimal status representation.",
                "Target Soong/ELF/AIDL/FMQ/SELinux, real hf_manager write failures and hardware timing remain untested.",
            ],
        }
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report))


if __name__ == "__main__":
    main()
