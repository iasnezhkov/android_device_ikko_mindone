# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


CORE = ("lib.rs", "session.rs", "transport.rs", "mcc.rs", "chat.rs")
INPUTS = (*CORE, "Android.bp", "mindone_mux.c", "tests/core_api.rs", "tests/core_api_check.py",
          "tests/session_check.py", "tests/transport_check.py", "tests/mcc_check.py")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description="Check the pure mux peer-close transition and caller-owned turn-budget accounting with anonymous host UnixStream controls; no modem or Android runtime is accessed")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC", "rustc"))
    parser.add_argument("--fault-controls", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source_root = tests.parent
    before = {name: digest(source_root / name) for name in INPUTS}

    def run(command, **kwargs):
        return subprocess.run(command, check=True, timeout=60, **kwargs)

    with tempfile.TemporaryDirectory(prefix="mindone-mux-core-api-") as directory:
        output = Path(directory)
        core = output / "core"
        core.mkdir()
        originals = {name: (source_root / name).read_text() for name in CORE}
        for name, content in originals.items():
            (core / name).write_text(content)
        library = output / "libmindone_mux_core.rlib"
        dependency = ["--extern", f"mindone_mux_core={library}"]

        def compile_library():
            run([args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib", "--crate-name=mindone_mux_core", str(core / "lib.rs"), "-o", str(library)])

        def compile_units():
            run([args.rustc, "--edition=2021", "-Dwarnings", "--test", str(tests / "core_api.rs"), *dependency, "-o", str(output / "core-api-test")])

        compile_library()
        compile_units()
        units = subprocess.run([str(output / "core-api-test")], capture_output=True, text=True, timeout=60)
        print(units.stdout, end="")
        print(units.stderr, end="")
        units.check_returncode()
        controls = []
        prefix = "use mindone_mux_core::transport::TurnBudget;\nfn main(){\nlet mut budget=TurnBudget::new(2,16).unwrap();\nlet permit=budget.permit().unwrap();\n"
        variants = (
            ("consumed-permit", "E0382", "permit.complete(1).unwrap();\nprintln!(\"{}\",budget.bytes());", "permit.complete(1).unwrap();\npermit.complete(1).unwrap();"),
            ("exclusive-budget-permit", "E0499", "let maximum=permit.max_bytes();\npermit.complete(0).unwrap();\nlet second=budget.permit().unwrap();\nsecond.complete(maximum.min(1)).unwrap();", "let second=budget.permit().unwrap();\npermit.complete(1).unwrap();\nsecond.complete(1).unwrap();"),
            ("budget-pinned-by-permit", "E0502", "permit.complete(0).unwrap();\nprintln!(\"{}\",budget.bytes());", "println!(\"{}\",budget.bytes());\npermit.complete(0).unwrap();"),
            ("permit-not-clone", "E0599", "let maximum=permit.max_bytes();\npermit.complete(maximum.min(1)).unwrap();", "let duplicate=permit.clone();\npermit.complete(1).unwrap();\nduplicate.complete(1).unwrap();"),
        )
        for name, error, positive, negative in variants:
            path = output / (name + ".rs")
            command = [args.rustc, "--edition=2021", "-Dwarnings", str(path), *dependency, "-o", str(output / name)]
            path.write_text(prefix + positive + "\n}\n")
            run(command)
            run([str(output / name)], capture_output=True)
            path.write_text(prefix + negative + "\n}\n")
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            if result.returncode == 0 or error not in result.stderr:
                raise ValueError(f"Compile-enforced permit ownership control failed: {name}")
            controls.append(name)
            print(f"type-control {name}: rejected")
        prefix = "use mindone_mux_core::transport::TurnBudget;\nfn main(){\nlet mut budget=TurnBudget::new(2,16).unwrap();\nbudget.permit().unwrap().complete(1).unwrap();\n"
        for name, error, positive, negative in (
            ("budget-not-copy", "E0382", "let snapshot=(budget.calls(),budget.bytes());\nprintln!(\"{:?}\",snapshot);", "let duplicate=budget;\nprintln!(\"{} {}\",budget.bytes(),duplicate.bytes());"),
            ("budget-not-clone", "E0599", "println!(\"{}\",budget.bytes());", "let duplicate=budget.clone();\nprintln!(\"{}\",duplicate.bytes());"),
        ):
            path = output / (name + ".rs")
            command = [args.rustc, "--edition=2021", "-Dwarnings", str(path), *dependency, "-o", str(output / name)]
            path.write_text(prefix + positive + "\n}\n")
            run(command)
            run([str(output / name)], capture_output=True)
            path.write_text(prefix + negative + "\n}\n")
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            if result.returncode == 0 or error not in result.stderr:
                raise ValueError(f"Compile-enforced budget ownership control failed: {name}")
            controls.append(name)
            print(f"type-control {name}: rejected")
        mutations = []
        if args.fault_controls:
            method = "pub fn peer_close(&mut self) -> Result<(), Error> {\n        self.active()?;\n        self.phase = Phase::PeerClosing;\n        Ok(())\n    }"
            variants = (
                ("peer-close-no-transition", "session.rs", method, method.replace("self.phase = Phase::PeerClosing;", "let _ = Phase::PeerClosing;")),
                ("peer-close-clears-ready-latch", "session.rs", method, method.replace("Ok(())", "self.ready_started = false;\n        Ok(())")),
                ("peer-close-clears-pf", "session.rs", method, method.replace("Ok(())", "self.poll_final_echo = false;\n        Ok(())")),
                ("peer-close-ignores-active", "session.rs", method, method.replace("self.active()?;", "")),
                ("late-frame-revives-closed-session", "session.rs", "if matches!(self.phase, Phase::Closing | Phase::PeerClosing) {\n            if self.recovery", "if false && matches!(self.phase, Phase::Closing | Phase::PeerClosing) {\n            if self.recovery"),
                ("unreserved-call", "transport.rs", "if !self.take_call() {\n            return None;", "if self.calls == self.call_limit || self.bytes == self.byte_limit {\n            return None;"),
                ("accepted-invalid-progress", "transport.rs", "if accepted > self.max_bytes {", "if false && accepted > self.max_bytes {"),
                ("undercharged-prefix", "transport.rs", "self.budget.bytes -= self.max_bytes - accepted;", "self.budget.bytes -= self.max_bytes - accepted.saturating_sub(1);"),
                ("wrong-byte-allowance", "transport.rs", "let max_bytes = self.available_bytes();", "let max_bytes = self.available_bytes().saturating_sub(1);"),
                ("unreserved-bytes", "transport.rs", "self.bytes += max_bytes;", "self.bytes += 0 * max_bytes;"),
            )
            for name, filename, old, new in variants:
                if originals[filename].count(old) != 1:
                    raise ValueError(f"Expected one exact API mutation site: {name}")
                for item, content in originals.items():
                    (core / item).write_text(content)
                (core / filename).write_text(originals[filename].replace(old, new, 1))
                compile_library()
                compile_units()
                result = subprocess.run([str(output / "core-api-test")], capture_output=True, text=True, timeout=10)
                if result.returncode == 0 or "test result: FAILED" not in result.stdout:
                    raise ValueError(f"Compiled API behavior mutation was not rejected: {name}")
                mutations.append(name)
                print(f"fault-control {name}: rejected")
        if before != {name: digest(source_root / name) for name in INPUTS}:
            raise ValueError("Canonical API inputs changed during the proof")
        report = {
            "status": "pass", "mode": "host_pure_core_API_not_runtime_or_Android",
            "source_sha256": before, "unit_tests": int(re.search(r"test result: ok\. (\d+) passed", units.stdout).group(1)),
            "permit_model_operations": 65000, "type_controls": controls, "fault_controls": mutations,
            "input_hashes_unchanged": True,
            "limits": [
                "The caller owns one non-Copy/non-Clone turn budget. Internal reports snapshot only scalar counters; no separate runtime budget is constructed by these APIs.",
                "A permit reserves one call and the full remaining byte allowance immediately. Drop, forgotten permits and invalid completion retain the entire reservation.",
                "Consuming valid completion refunds only unused bytes; complete(0) releases bytes after a known zero-transfer fault while retaining the call. Unconfirmed reservations are conservative charges, not measured transferred bytes.",
                "The caller must also validate progress against the actual offered slice; the permit only enforces its full captured allowance.",
                "Peer close only changes an active session phase. The owner must first admit any CLD echo to its transport queue; physical write completion is not required.",
                "Closing phases ignore later incoming frames without channel, PF, readiness, handoff or endpoint effects until explicit transport-close reset; this deliberately strengthens the C same-read behavior.",
                "Failed echo admission leaves the existing Session EffectRejected/PF behavior; this is not a whole-session atomic transaction.",
                "The transfer-abandonment regression writes one actual byte over anonymous nonblocking host UnixStream pairs; it does not establish a runtime fd/PTY ownership contract.",
                "No PTY lifecycle, native target/Soong, CCCI, init, SELinux or hardware acceptance is claimed.",
            ],
        }
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report))


if __name__ == "__main__": main()
