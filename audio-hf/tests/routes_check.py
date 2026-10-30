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
import xml.etree.ElementTree as ET


INPUTS = ("Android.bp", "audio_hw.c", "mixer_paths.xml", "AudioRustRoutes.h",
          "rust/lib.rs", "rust/ffi.rs", "tests/core.rs", "tests/routes.c", "tests/callers.c", "tests/routes_check.py")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(source, signature):
    if signature == "static int out_set_route(" and signature not in source:
        return ""
    if source.count(signature) != 1:
        raise ValueError(f"Expected one production definition: {signature}")
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError(f"Expected one mutation site: {old}")
    return source.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser(description="Check exact audio endpoint methods and Rust FFI against actual mixer XML")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC", "rustc"))
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--fault-controls", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    repository = source.parents[3]
    fixture_path = repository / "tools/rigs/audio-call-routing/endpoints.c"
    inputs = [source / name for name in INPUTS] + [fixture_path]
    before = {str(path.relative_to(repository)): digest(path) for path in inputs}
    production = (source / "audio_hw.c").read_text()
    core = (source / "rust/lib.rs").read_text()
    template = (source / "tests/routes.c").read_text()
    baseline = fixture_path.read_text()
    baseline = replace_once(baseline, "int main(void)", "static int baseline_cases(void)")
    baseline = replace_once(baseline, "static void audio_route_apply_path(struct audio_route *route, const char *path)\n{",
                            "static void audio_route_apply_path(struct audio_route *route, const char *path)\n{\n    record_action(path);")
    baseline = replace_once(baseline, "static void audio_route_update_mixer(struct audio_route *route)\n{",
                            "static void audio_route_update_mixer(struct audio_route *route)\n{\n    record_action(\"update\");")
    enum = re.search(r"enum output_device \{.*?\n\};", production, re.S).group()
    count = re.search(r"^#define OUT_DEVICE_COUNT .+$", production, re.M).group()
    owner_field = re.search(r"    int out_ep_users\[OUT_DEVICE_COUNT\];", production).group()
    if owner_field not in baseline:
        raise ValueError("Production owner count layout differs from fixture")
    rows = []
    paths = ET.parse(source / "mixer_paths.xml").getroot()
    for name in ("speaker", "speaker-off", "earpiece", "earpiece-off", "headphone", "headphone-off"):
        path = paths.find(f"./path[@name='{name}']")
        if path is None:
            raise ValueError(f"Missing mixer path {name}")
        for control in path:
            if control.tag != "ctl":
                raise ValueError(f"Unsupported mixer element {control.tag}")
            row = [name, control.attrib["name"], control.attrib["value"]]
            rows.append("    {" + ", ".join(json.dumps(value) for value in row) + "},")

    def fixture(implementation):
        methods = [extract(implementation, signature) for signature in (
            "static const char *output_path_name(", "static const char *output_path_off_name(")]
        methods.append("#if defined(MINDONE_AUDIO_RUST_ROUTES)\n" + "\n".join(
            extract(implementation, signature) for signature in (
                "static int plan_endpoint_transition(", "static struct mindone_audio_route_state endpoint_snapshot(",
                "static int check_endpoint_transition(", "static int apply_endpoint_transition(")) + "\n#endif")
        methods.extend(extract(implementation, signature) for signature in (
            "static int endpoint_acquire(", "static int endpoint_release(",
            "static int apply_output_route(", "static int teardown_output_route("))
        body = baseline.replace("PRODUCTION_ENUMS", enum + "\n" + count)
        body = body.replace("PRODUCTION_PATHS", "\n".join(rows))
        body = body.replace("PRODUCTION_FUNCTIONS", "\n\n".join(methods))
        return template.replace("PRODUCTION_FIXTURE", body)

    def run(command, **kwargs):
        return subprocess.run(command, check=True, timeout=60, **kwargs)

    def caller_fixture(implementation):
        enums = enum + "\n" + count + "\n"
        enums += re.search(r"enum input_device \{.*?\n\};", implementation, re.S).group() + "\n"
        for name in ("IN_DEVICE_COUNT", "OUT_CHANNEL_COUNT"):
            enums += re.search(r"^#define " + name + r" .+$", implementation, re.M).group() + "\n"
        structures = []
        for name in ("mindone_audio_device", "mindone_stream_out", "mindone_stream_in"):
            structures.append(re.search(r"struct " + name + r" \{.*?\n\};", implementation, re.S).group())
        methods = [extract(implementation, signature) for signature in (
            "static enum output_device output_device_from_mask(", "static enum input_device input_device_from_mask(",
            "static const char *output_path_name(", "static const char *output_path_off_name(",
            "static int plan_endpoint_transition(", "static struct mindone_audio_route_state endpoint_snapshot(",
            "static int check_endpoint_transition(", "static int apply_endpoint_transition(",
            "static int endpoint_acquire(", "static int endpoint_release(",
            "static int apply_output_route(", "static int teardown_output_route(",
            "static int voice_route_disable(", "static int voice_route_enable(",
            "static int do_out_standby(", "static int out_standby(",
            "static int out_set_route(", "static int start_output_stream(",
            "static ssize_t out_write(",
            "static int set_mode(", "int mindone_audio_set_mode(", "static int check_output_patch(", "static int create_audio_patch(", "int mindone_audio_create_patch(")]
        methods.append("#define free checked_free\n" +
                       extract(implementation, "static void close_output_stream(") + "\n#undef free")
        return (source / "tests/callers.c").read_text().replace("PRODUCTION_ENUMS", enums).replace(
            "PRODUCTION_STRUCTS", "\n\n".join(structures)).replace("PRODUCTION_FUNCTIONS", "\n\n".join(methods))

    with tempfile.TemporaryDirectory(prefix="mindone-audio-routes-") as directory:
        root = Path(directory)
        library = root / "libmindone_audio_routes.rlib"
        archive = root / "libmindone_audio_routes_ffi.a"
        dependency = ["--extern", f"mindone_audio_routes={library}"]

        def build_rust(core_source):
            core_file = root / "lib.rs"
            core_file.write_text(core_source)
            run([args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib",
                 "--crate-name=mindone_audio_routes", str(core_file), "-o", str(library)])
            linked = run([args.rustc, "--edition=2021", "-Dwarnings", "-Dunsafe_op_in_unsafe_fn",
                          "--crate-type=staticlib", "--crate-name=mindone_audio_routes_ffi", "-C", "panic=abort",
                          "--print=native-static-libs", *dependency, str(source / "rust/ffi.rs"),
                          "-o", str(archive)], capture_output=True, text=True)
            matches = re.findall(r"native-static-libs: (.+)", linked.stderr)
            if len(matches) != 1:
                raise ValueError("Expected compiler-provided native link input list")
            return list(dict.fromkeys(shlex.split(matches[0])))

        native = build_rust(core)
        run([args.rustc, "--edition=2021", "-Dwarnings", "--test", str(source / "tests/core.rs"),
             *dependency, "-o", str(root / "core-test")])
        core_checked = run([str(root / "core-test")], capture_output=True, text=True)
        print(core_checked.stdout, end="")
        c_file = root / "routes.c"
        common = [args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                  "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(source), str(c_file)]
        variants = {}
        traces = {}
        for name, extra in (("default", []), ("rust", ["-DMINDONE_AUDIO_RUST_ROUTES", str(archive), *native])):
            binary = root / name
            c_file.write_text(fixture(production))
            run([*common, *extra, "-o", str(binary)])
            checked = run([str(binary)], capture_output=True, text=True)
            if "10000 exact ownership replays and shared mixer ordering PASS" not in checked.stdout:
                raise ValueError(f"Missing acceptance marker: {name}")
            variants[name] = checked.stdout.strip()
            print(f"{name}: {checked.stdout}", end="")
            traces[name] = run([str(binary), "trace"], capture_output=True).stdout
        if traces["default"] != traces["rust"]:
            raise ValueError("Default and Rust count/action/mixer traces differ")
        print(f"count/action/mixer trace parity: {len(traces['rust'])} bytes identical")
        controls = []
        if args.fault_controls:
            mutants = [
                ("early-power-off", production, replace_once(core, "if *users == 0 && old != Endpoint::Sco {",
                 "if old != Endpoint::Sco {"), "route.updates == updates"),
                ("missing-shared-restore", production, replace_once(core, "transition.push(Action::Enable(shared));",
                 "let _ = shared;"), "strcmp(route->pending[0].value"),
                ("double-owner-increment", production, replace_once(core, "users.checked_add(1)",
                 "users.checked_add(2)"), "speaker || receiver"),
                ("acquire-before-release", production, replace_once(core,
                 "transition.push(Action::Enable(new));\n                transition.push(Action::Update);",
                 "let previous = transition.actions;\n                let count = transition.action_count;\n"
                 "                transition.action_count = 0;\n                transition.push(Action::Enable(new));\n"
                 "                transition.push(Action::Update);\n                for action in previous.into_iter().take(count) {\n"
                 "                    transition.push(action);\n                }"), "release restore update precedes acquire update"),
                ("saturating-overflow", production, replace_once(core, "users.checked_add(1).ok_or(Error::Overflow)?",
                 "users.saturating_add(1)"), "overflow reroute rejected"),
                ("wrong-old-owner", replace_once(production, "apply_endpoint_transition(adev, old_device, new_device);",
                 "apply_endpoint_transition(adev, new_device, old_device);"), core, "shared release and acquire action count"),
            ]
            for name, implementation, mutated_core, expected in mutants:
                native = build_rust(mutated_core)
                c_file.write_text(fixture(implementation))
                binary = root / "mutant"
                run([*common, "-DMINDONE_AUDIO_RUST_ROUTES", str(archive), *native, "-o", str(binary)])
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
                if result.returncode == 0 or expected not in result.stderr:
                    raise ValueError(f"Mutation {name} not rejected as expected: {result.returncode} {result.stdout}{result.stderr}")
                controls.append(name)
                print(f"fault-control {name}: rejected")
        native = build_rust(core)
        c_file.write_text(caller_fixture(production))
        caller_binary = root / "callers"
        run([*common, "-pthread", "-DMINDONE_AUDIO_RUST_ROUTES", str(archive), *native, "-o", str(caller_binary)])
        caller_checked = run([str(caller_binary)], capture_output=True, text=True)
        print(caller_checked.stdout, end="")
        if "exact outer caller error propagation and cleanup PASS" not in caller_checked.stdout:
            raise ValueError("Missing outer caller acceptance marker")
        if args.fault_controls:
            start = extract(production, "static int start_output_stream(")
            disable = extract(production, "static int voice_route_disable(")
            close = extract(production, "static void close_output_stream(")
            write = extract(production, "static ssize_t out_write(")
            mutants = [
                ("swallow-parameter-error", replace_once(production,
                 "rc = apply_output_route(adev, out->routed_device, new_dev);",
                 "rc = (apply_output_route(adev, out->routed_device, new_dev), 0);"), "parameter planning error propagates"),
                ("noncumulative-preflight", replace_once(production, "        shadow = plan.next;",
                 "        (void)plan;"), "cumulative patch rejection preserves first stream and counts"),
                ("ignore-start-preflight", replace_once(production, start,
                 replace_once(start, "        if (rc) {", "        if (rc && 0) {")), "start planning error propagates"),
                ("ignore-voice-disable-preflight", replace_once(production, disable,
                 replace_once(disable, "        if (rc)", "        if (rc && 0)")), "voice disable error propagates"),
                ("skip-failed-close-cleanup", replace_once(production, close,
                 replace_once(close, "    if (rc) {", "    if (rc && 0) {")), "failed standby close frees owned resources"),
                ("write-ignores-standby-error", replace_once(production, write,
                 replace_once(write, "ret = do_out_standby(out);\n        if (ret)",
                              "ret = do_out_standby(out);\n        if (ret && 0)")), "write propagates standby rejection"),
            ]
            for name, implementation, expected in mutants:
                c_file.write_text(caller_fixture(implementation))
                run([*common, "-pthread", "-DMINDONE_AUDIO_RUST_ROUTES", str(archive), *native, "-o", str(caller_binary)])
                result = subprocess.run([str(caller_binary)], capture_output=True, text=True, timeout=20)
                if result.returncode != 1 or expected not in result.stderr:
                    raise ValueError(f"Caller mutation {name} not rejected: {result.returncode} {result.stdout}{result.stderr}")
                controls.append(name)
                print(f"fault-control {name}: rejected")
        if before != {str(path.relative_to(repository)): digest(path) for path in inputs}:
            raise ValueError("Source inputs changed during acceptance")
        report = {
            "status": "pass", "mode": "host_exact_endpoint_methods_actual_Rust_FFI_XML_mocked_mixer_not_Android",
            "source_sha256": before, "variants": variants, "fault_controls": controls,
            "outer_callers": caller_checked.stdout.strip(),
            "parity_bytes": len(traces["rust"]), "parity_sha256": hashlib.sha256(traces["rust"]).hexdigest(),
            "host_rust_version": run([args.rustc, "--version"], capture_output=True, text=True).stdout.strip(),
            "input_hashes_unchanged": True,
            "limits": [
                "Production libmindone_audio_backend uses the typed C API; only libaudio.mindone.rust.impl opts in.",
                "Actual endpoint methods and XML are replayed; the audio_route boundary is a fake mixer, not hardware.",
                "Invalid IDs, negative snapshots, underflow reroutes and overflow reject the whole opt-in transition; invalid default behavior is not a parity promise.",
                "Callers retain the existing device-lock ownership; this gate does not replace whole-service concurrency or lifecycle acceptance.",
                "The mixer API is void; no hardware failure detection or rollback is claimed.",
                "Exact outer callers reject software planning errors before accepted route metadata or side effects; void close force-cleans owned resources while preserving untrusted shared endpoint state.",
                "No target Soong/service/AIDL/FMQ/PCM/speech/SELinux/device acceptance; SCO routing actions preserve the existing no-mixer helper behavior only.",
            ],
        }
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report))


if __name__ == "__main__":
    main()
