# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import subprocess
import tempfile


INPUTS = ("lib.rs", "session.rs", "transport.rs", "chat.rs", "mcc.rs", "runtime.rs", "platform.rs", "posix.c",
          "service.rs", "android_properties.rs", "properties.c", "Android.bp", "mindone_mux.c",
          "tests/service.rs", "tests/service_pty.rs", "tests/service_effects.c.in", "tests/service_check.py",
          "tests/property_backend.c", "tests/property_probe.c", "tests/property_replay.rs", "tests/check.py",
          "tests/runtime_support.rs", "tests/platform.rs", "tests/platform_support.c",
          "../../volte_md_status/tests/android_check.py")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description="Exercise the optional mux service owner, exact C effect traces and private PTY lifecycle without modem, device or Android property access")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC", "rustc"))
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--ar", default=os.environ.get("AR", "ar"))
    parser.add_argument("--fault-controls", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    before = {name: digest(root / name) for name in INPUTS}
    c_source = (root / "mindone_mux.c").read_text()
    service_source = (root / "service.rs").read_text()
    for name in ("RESTART_DELAY_MS", "REPORT_WAIT_MS"):
        c = int(re.search(r"^#define " + name + r" (\d+)$", c_source, re.M).group(1))
        rust = int(re.search(r"^pub const " + name + r": u64 = (\d+);$", service_source, re.M).group(1))
        if c != rust:
            raise ValueError("Service timer differs from the exact production C source: " + name)
    platform_fixture = (root / "tests/platform.rs").read_text()
    modem = platform_fixture[platform_fixture.index("struct Modem {"):platform_fixture.index("struct Harness {")]
    if (root / "tests/service_pty.rs").read_text().split("struct Modem {", 1)[1].rstrip("\n") != modem.split("struct Modem {", 1)[1].rstrip("\n"):
        raise ValueError("The shared PTY modem fixture differs from its existing platform proof")
    source_function = runpy.run_path(str(root / "tests/check.py"))["source_function"]
    functions = ("prop_set", "on_signal", "report_and_exit", "modem_exception_exit", "setup_complete",
                 "graceful_exit", "chat_read", "chat_write", "tx_flush")
    definitions = ("REPORT_WAIT_MS", "CHAT_TIMEOUT_MS", "TX_RETRY_MS", "CHAT_CAP")
    defines = [re.search(r"^#define " + name + r" .*$", c_source, re.M).group() for name in definitions]
    fixture = (root / "tests/service_effects.c.in").read_text().replace("@DEFINES@", "\n".join(defines))
    fixture = fixture.replace("@STATE@", re.search(r"^enum mux_state \{[^\n]+", c_source, re.M).group())
    fixture = fixture.replace("@FUNCTIONS@", "\n\n".join(source_function(c_source, name) for name in functions))
    startup = re.search(r'\tprop_set\("vendor.ril.muxreport.run", "0"\);\n\tprop_set\("vendor.ril.mux.ee.md1", "0"\);', c_source)
    if not startup:
        raise ValueError("The C process startup publications changed")
    fixture = fixture.replace("@STARTUP@", startup.group())
    stop_process = runpy.run_path(str(root / "../../volte_md_status/tests/android_check.py"))["stop_process"]
    commands, groups = [], []

    def execute(command, env=None, seconds=60, check=True):
        child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env, start_new_session=True)
        try:
            stdout, stderr = child.communicate(timeout=seconds)
        except BaseException:
            stop_process(child)
            child.communicate(timeout=1)
            raise
        survivors = stop_process(child)
        if survivors:
            raise ValueError("A completed lifecycle check left live descendants")
        groups.append({"group": child.pid, "exit_code": child.returncode, "empty_after_cleanup": True})
        commands.append({"argv": [str(value) for value in command], "exit_code": child.returncode, "stdout": stdout, "stderr": stderr})
        result = subprocess.CompletedProcess(command, child.returncode, stdout, stderr)
        if check and result.returncode:
            print(stdout, end=""); print(stderr, end="")
            result.check_returncode()
        if "Sanitizer" in stderr or "runtime error:" in stderr:
            raise ValueError("A lifecycle control triggered a sanitizer diagnostic")
        return result

    with tempfile.TemporaryDirectory(prefix="mindone-mux-service-") as temporary:
        build = Path(temporary)
        c_file = build / "c-effects.c"
        c_file.write_text(fixture)
        execute([args.cc, "-Wall", "-Wextra", "-Werror", "-std=gnu11", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                 str(c_file), "-o", str(build / "c-effects")])
        ready = ["property vendor.ril.muxreport.run=0", "property vendor.ril.mux.ee.md1=0",
                 "property vendor.ril.mux.start=1", "property vendor.ril.mtk=1"]
        report = ["close ptys", "close serial", "property vendor.ril.mux.report.case=1", "property vendor.ril.muxreport=1",
                  "wait 5000", "property vendor.ril.mux.report.case=2", "property vendor.ril.muxreport=1", "exit 0"]
        exception = ["property vendor.ril.mux.ee.md1=1", "close ptys", "close serial", "exit 0"]
        expected = {
            "ready-twice": ready + ["drop root boundary", "property vendor.ril.mtk.restart=1", "ready state=1 setup=1 retained=1"],
            "report": report,
            "report-stop": report[:5] + ["exit 0"],
            "chat-write-enodev": exception, "chat-write-etxtbsy": exception,
            "chat-read-enodev": report, "chat-read-etxtbsy": report,
            "mux-reset-enodev": ["property vendor.ril.mux.ee.md1=1", "tx result=0 pending=0 blocked=0"],
            "mux-reset-etxtbsy": ["property vendor.ril.mux.ee.md1=1", "tx result=0 pending=0 blocked=0"],
            "stop": ["close ptys", "close serial", "exit 0"],
            "ready-first-property-rejected": ["property vendor.ril.mux.start=1 rejected", "property vendor.ril.mtk=1",
                                               "drop root boundary", "ready retained=1 writes=2"],
        }
        traces = {}
        for name, lines in expected.items():
            result = execute([str(build / "c-effects"), name], seconds=10)
            if result.stdout.splitlines() != lines:
                raise ValueError("The actual C effect trace changed: " + name + "\n" + result.stdout)
            traces[name] = result.stdout.splitlines()
        header = build / "include/sys/system_properties.h"
        header.parent.mkdir(parents=True)
        header.write_text("int __system_property_set(const char *key, const char *value);\n")
        flags = ["-Wall", "-Wextra", "-Werror", "-std=gnu11"]
        native = build / "posix.o"
        execute([args.cc, *flags, "-c", str(root / "posix.c"), "-o", str(native)])
        archive = build / "libmindone_mux_service_native.a"
        execute([args.ar, "rcs", str(archive), str(native)])
        dependencies = []
        libraries = {}

        def library(name, source, deps):
            path = build / f"libmindone_mux_{name}.rlib"
            execute([args.rustc, "--edition=2021", "-Dwarnings", "-C", "opt-level=1", "--crate-type=rlib", f"--crate-name=mindone_mux_{name}",
                     str(source), *deps, "-L", f"dependency={build}", "-o", str(path)])
            libraries[name] = path
            return path

        for name, source in (("core", "lib.rs"), ("runtime", "runtime.rs"), ("platform", "platform.rs"), ("service", "service.rs"),
                             ("android_properties", "android_properties.rs")):
            path = library(name, root / source, dependencies)
            dependencies.extend(["--extern", f"mindone_mux_{name}={path}"])
        executable = build / "service-test"
        service_dependencies = dependencies[:6]
        link = ["-L", f"dependency={build}", "-L", f"native={build}", "-l", "static:+whole-archive=mindone_mux_service_native"]

        def build_service_tests(source=root / "service.rs"):
            execute([args.rustc, "--edition=2021", "-Dwarnings", "-C", "opt-level=1", "--test", str(source),
                     *service_dependencies, *link, "-o", str(executable)])

        def run_service_tests(test=None, check=True):
            with tempfile.TemporaryDirectory(prefix="ptys-", dir=temporary) as directory:
                command = [str(executable), "--test-threads=1", "--nocapture"]
                if test:
                    command.extend(["--exact", "tests::" + test])
                return execute(command, env={**os.environ, "MINDONE_SERVICE_TEST_ROOT": directory, "RUST_MIN_STACK": "8388608"}, check=check)

        build_service_tests()
        units = run_service_tests()
        print(units.stdout, end="")
        if "test result: ok. 11 passed" not in units.stdout:
            raise ValueError("Missing completed service controls")
        for android in (False, True):
            probe = build / ("property-android" if android else "property-unsupported")
            command = [args.cc, *flags, str(root / "properties.c"), str(root / "tests/property_probe.c")]
            command.extend(["-D__ANDROID__", "-I", str(header.parent.parent), str(root / "tests/property_backend.c")] if android else ["-DEXPECT_UNSUPPORTED"])
            execute([*command, "-o", str(probe)])
            output = execute([str(probe)], env={key: value for key, value in os.environ.items() if key != "MINDONE_PROPERTY_FAILURE"})
            if "PROPERTY_MATRIX_PASS allowed=10 rejected=28" not in output.stdout:
                raise ValueError("Missing exact property key/value controls")
        prop_object, backend_object = build / "properties.o", build / "property-backend.o"
        execute([args.cc, *flags, "-D__ANDROID__", "-I", str(header.parent.parent), "-c", str(root / "properties.c"), "-o", str(prop_object)])
        execute([args.cc, *flags, "-c", str(root / "tests/property_backend.c"), "-o", str(backend_object)])
        prop_archive = build / "libmindone_mux_property_fixture.a"
        execute([args.ar, "rcs", str(prop_archive), str(prop_object), str(backend_object)])
        prop_replay = build / "property-replay"
        execute([args.rustc, "--edition=2021", "-Dwarnings", str(root / "tests/property_replay.rs"), *dependencies, *link,
                 "-l", "static:+whole-archive=mindone_mux_property_fixture", "-o", str(prop_replay)])
        success = execute([str(prop_replay)], env={**os.environ, "MINDONE_PROPERTY_FAILURE": "0"})
        rejected = execute([str(prop_replay)], env={**os.environ, "MINDONE_PROPERTY_FAILURE": "3"})
        if success.stdout.splitlines() != ready + ["property vendor.lackof.atci.channel=1", "property vendor.ril.mux.ee.md1=1"] + report[2:4] + report[5:7] + ["PROPERTY_REPLAY_PASS ready=true"]:
            raise ValueError("The Rust effects and actual Android shim differ from the C property order")
        if rejected.stdout.splitlines() != ready[:2] + ["property vendor.ril.mux.start=1 rejected", "PROPERTY_REPLAY_PASS ready=false"]:
            raise ValueError("Property rejection allowed later effects")
        mutations = []
        if args.fault_controls:
            (build / "tests").symlink_to(root / "tests", target_is_directory=True)
            reset_test = "startup_reset_read_requests_recovery_but_write_sets_exception_with_exact_origin"
            variants = (
                ("ril_republished_after_restart", "service.rs", "if state.ril_started {", "if false && state.ril_started {", "private_pty_ready_peer_close_and_bounded_restart_keep_process_publications"),
                ("property_rejection_ignored", "service.rs", "if !self.properties.set(property, value) {", "if !self.properties.set(property, value) && false {", "every_property_failure_stops_without_later_publication_or_retry"),
                ("recovery_property_order_reversed", "service.rs", "state.set(Property::ReportCase, stage) && state.set(Property::Report, Value::One)", "state.set(Property::Report, Value::One) && state.set(Property::ReportCase, stage)", "ordinary_runtime_failure_cleans_before_two_timed_recovery_requests"),
                ("terminal_cleanup_omitted", "service.rs", "self.close();\n        self.phase = Phase::Finished(exit);", "self.phase = Phase::Finished(exit);", "explicit_stop_before_start_during_active_and_during_both_backoffs_is_final"),
                ("restart_backoff_removed", "service.rs", "pub const RESTART_DELAY_MS: u64 = 5000;", "pub const RESTART_DELAY_MS: u64 = 0;", "private_pty_ready_peer_close_and_bounded_restart_keep_process_publications"),
                ("report_wait_counts_property_latency", "service.rs", "if !self.publication.recover(Value::One) { self.finish(Exit::PropertyRejected); return; }\n        let Some(now) = self.now() else { return; };", "let Some(now) = self.now() else { return; };\n        if !self.publication.recover(Value::One) { self.finish(Exit::PropertyRejected); return; }", "recovery_deadline_starts_after_completed_property_publication"),
                ("chat_write_reset_requests_recovery", "service.rs", "} else if error == Error::Chat(chat::Error::DeviceReset) {", "} else if false && error == Error::Chat(chat::Error::DeviceReset) {", reset_test),
                ("stop_ignored_during_backoff", "service.rs", "if !matches!(self.phase, Phase::Finished(_)) { self.finish(Exit::Stopped); }", "if matches!(self.phase, Phase::Starting | Phase::Running) { self.finish(Exit::Stopped); }", "explicit_stop_before_start_during_active_and_during_both_backoffs_is_final"),
                ("unsupported_close_requests_recovery", "service.rs", "} else if error == Error::UnsupportedLocalClose {", "} else if false && error == Error::UnsupportedLocalClose {", "unsupported_local_close_has_an_explicit_terminal_result_without_recovery"),
                ("read_reset_origin_omitted", "runtime.rs", "if fault == Fault::DeviceReset { Error::ChatRead(error) } else { Error::Chat(error) }", "if false && fault == Fault::DeviceReset { Error::ChatRead(error) } else { Error::Chat(error) }", reset_test),
                ("write_reset_tagged_as_read", "runtime.rs", "self.chat.write_fault(now, token, fault).map_err(Error::Chat)?;", "self.chat.write_fault(now, token, fault).map_err(Error::ChatRead)?;", reset_test),
            )
            for name, filename, old, new, test in variants:
                original = (root / filename).read_text()
                if original.count(old) != 1:
                    raise ValueError("Expected one lifecycle mutation site: " + name)
                mutated = build / filename
                mutated.write_text(original.replace(old, new, 1))
                if filename == "runtime.rs":
                    library("runtime", mutated, dependencies[:2])
                    library("platform", root / "platform.rs", dependencies[:4])
                    build_service_tests()
                else:
                    build_service_tests(mutated)
                output = run_service_tests(test=test, check=False)
                if output.returncode != 101 or "test result: FAILED. 0 passed; 1 failed" not in output.stdout:
                    raise ValueError("Lifecycle mutant was not rejected by a compiled semantic control: " + name)
                mutations.append({"name": name, "test": test, "status": "semantic_rejection"})
                print("service-fault-control " + name + ": rejected")
                if filename == "runtime.rs":
                    library("runtime", root / "runtime.rs", dependencies[:2])
                    library("platform", root / "platform.rs", dependencies[:4])
            original = (root / "properties.c").read_text()
            old = "(property == 6 && value != 1)"
            if original.count(old) != 1:
                raise ValueError("Expected one property whitelist mutation site")
            mutated = build / "properties.c"
            mutated.write_text(original.replace(old, "(0 && property == 6 && value != 1)", 1))
            probe = build / "property-whitelist-mutant"
            execute([args.cc, *flags, "-D__ANDROID__", "-I", str(header.parent.parent), str(mutated),
                     str(root / "tests/property_probe.c"), str(root / "tests/property_backend.c"), "-o", str(probe)])
            output = execute([str(probe)], check=False, env={**os.environ, "MINDONE_PROPERTY_FAILURE": "0"})
            if output.returncode != 1 or "property vendor.ril.muxreport=0" not in output.stdout:
                raise ValueError("Property whitelist mutant escaped its semantic control")
            mutations.append({"name": "report_value_whitelist_widened", "status": "semantic_rejection", "exit_code": 1})
        if before != {name: digest(root / name) for name in INPUTS}:
            raise ValueError("Service source, fixture or helper inputs changed during the proof")
        result = {
            "status": "pass", "source_sha256": before, "input_hashes_unchanged": True,
            "actual_c_trace_controls": len(traces), "actual_c_function_sha256": {name: hashlib.sha256(source_function(c_source, name).encode()).hexdigest() for name in functions},
            "c_traces": traces, "service_unit_tests": 11, "property_pairs_allowed": 9, "property_pairs_rejected": 29,
            "fault_controls": mutations, "owned_process_groups": groups,
            "limits": [
                "Actual private host PTYs exercise the real Driver lifecycle. Startup read/write and mux DeviceReset use controlled UnixStream endpoints and the actual Runtime; no modem errno or reset was induced.",
                "The C fixture substitutes property, close, wait, exit and I/O boundaries in exact function bodies. The Android property shim calls a fixture symbol; it never contacts Android property services.",
                "Property rejection is deliberately terminal, unlike C which logs and continues. Startup read/write DeviceReset origins remain distinct. Runtime closes descriptors before the owner publishes the chat-write exception.",
                "This optional owner has no CLI, signal registration, MIPC/SIM property discovery, UID/capability transition, init/SELinux activation or exclusive CCCI ownership proof. Caller-supplied config/opener and explicit stop are required; local close is explicitly unsupported.",
                "C default product selection is unchanged. Target Soong/ABI and hardware acceptance remain separate.",
            ],
        }
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        (args.output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        print(json.dumps({key: result[key] for key in ("status", "actual_c_trace_controls", "service_unit_tests", "property_pairs_allowed", "property_pairs_rejected", "fault_controls")}))


if __name__ == "__main__":
    main()
