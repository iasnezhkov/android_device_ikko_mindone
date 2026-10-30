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


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(source, signature):
    if signature in ("static int out_set_route(", "static int out_set_parameters(") and signature not in source:
        return ""
    if source.count(signature) != 1:
        raise ValueError("Expected one production definition: " + signature)
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError("Expected one source anchor: " + old)
    return source.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser(description="Replay exact output open, close, route patch and SCO lifecycle without Android or hardware")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC", "rustc"))
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--fault-controls", action="store_true")
    parser.add_argument("--baseline-source", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    inputs = [source / name for name in ("audio_hw.c", "AudioRustRoutes.h", "rust/lib.rs", "rust/ffi.rs",
                                        "tests/callers.c", "tests/tracking.c", "tests/tracking_check.py")]
    if args.baseline_source:
        inputs.append(args.baseline_source.resolve())
    before = {str(path): digest(path) for path in inputs}
    production = inputs[0].read_text()
    template = (source / "tests/tracking.c").read_text()
    environment = (source / "tests/callers.c").read_text().split("PRODUCTION_FUNCTIONS", 1)[0]
    environment = replace_once(environment, "struct audio_stream { int unused; };", """struct audio_stream {
    void *get_sample_rate, *set_sample_rate, *get_buffer_size, *get_channels;
    void *get_format, *set_format, *standby, *dump, *set_parameters;
    void *get_parameters, *add_audio_effect, *remove_audio_effect;
};""")
    environment = replace_once(environment, "struct audio_stream_out { struct audio_stream common; };", """struct audio_stream_out {
    struct audio_stream common;
    void *get_latency, *set_volume, *write, *get_render_position;
    void *get_next_write_timestamp, *get_presentation_position;
};
struct audio_config { unsigned int sample_rate, channel_mask, format; };""")
    environment += "\n" + "\n".join("#define " + name + " " + str(value) for name, value in (
        ("AUDIO_OUTPUT_FLAG_DEEP_BUFFER", 8), ("AUDIO_OUTPUT_FLAG_FAST", 4),
        ("AUDIO_OUTPUT_FLAG_DIRECT", 1), ("AUDIO_OUTPUT_FLAG_MMAP_NOIRQ", 16384),
        ("AUDIO_FORMAT_PCM_16_BIT", 1), ("AUDIO_CHANNEL_OUT_STEREO", 3), ("PCM_FORMAT_S16_LE", 0))) + "\n"
    environment = replace_once(environment, "    (void)key;\n    return snprintf(value, (size_t)capacity, \"%s\", parms->value);", """    size_t length = strlen(key);
    if (strncmp(parms->value, key, length) || parms->value[length] != '=')
        return -ENOENT;
    return snprintf(value, (size_t)capacity, "%s", parms->value + length + 1);""")
    environment += "enum mindone_audio_sco_band { MINDONE_AUDIO_SCO_NB, MINDONE_AUDIO_SCO_WB };\nstruct mindone_audio_sco { enum mindone_audio_sco_band band; };\n"
    environment += """
static int speech_set_dl_gain(struct mindone_audio_device *device, const char *value)
{ (void)device; (void)value; return 0; }
static int speech_rx_pump(struct mindone_audio_device *device, const char *value)
{ (void)device; (void)value; return 0; }
"""

    def fixture(implementation, baseline=False):
        enums = "\n".join(re.search(r"enum " + name + r" \{.*?\n\};", implementation, re.S).group()
                          for name in ("output_device", "input_device"))
        for name in ("OUT_DEVICE_COUNT", "IN_DEVICE_COUNT", "OUT_CHANNEL_COUNT", "OUT_SAMPLING_RATE",
                     "OUT_PERIOD_SIZE", "OUT_PERIOD_COUNT", "OUT_DEEP_PERIOD_SIZE", "OUT_DEEP_PERIOD_COUNT",
                     "OUT_FAST_PERIOD_SIZE", "OUT_FAST_PERIOD_COUNT", "PCM_DEVICE_PRIMARY", "PCM_DEVICE_FAST",
                     "PCM_DEVICE_DEEP_BUFFER"):
            enums += "\n" + re.search(r"^#define " + name + r" .+$", implementation, re.M).group()
        structures = "\n".join(re.search(r"struct " + name + r" \{.*?\n\};", implementation, re.S).group()
                               for name in ("mindone_audio_device", "mindone_stream_out", "mindone_stream_in"))
        signatures = (
            "static enum output_device output_device_from_mask(", "static enum input_device input_device_from_mask(",
            "static const char *output_path_name(", "static const char *output_path_off_name(",
            "static int endpoint_acquire(", "static int endpoint_release(",
            "static int apply_output_route(", "static int teardown_output_route(",
            "static int voice_route_disable(", "static int voice_route_enable(",
            "static int do_out_standby(", "static int out_standby(", "static int out_set_route(", "static int out_set_parameters(",
            "static int start_output_stream(", "static ssize_t out_write(",
            "static int set_mode(", "int mindone_audio_set_mode(", "int mindone_audio_set_sco(",
            "static int create_audio_patch(", "int mindone_audio_create_patch(")
        if baseline:
            signatures = signatures[:-5] + ("static int adev_set_mode(", "static int adev_set_parameters(", "static int adev_create_audio_patch(")
        planning = "\n".join(extract(implementation, signature) for signature in (
            "static int plan_endpoint_transition(", "static struct mindone_audio_route_state endpoint_snapshot(",
            "static int check_endpoint_transition(", "static int apply_endpoint_transition(",
            "static int check_output_patch("))
        methods = [extract(implementation, signature) for signature in signatures[:4]]
        methods.append("#if defined(MINDONE_AUDIO_RUST_ROUTES)\n" + planning + "\n#endif")
        methods.extend(extract(implementation, signature) for signature in signatures[4:])
        native = "static int open_output_stream(" in implementation
        opening = extract(implementation, "static int open_output_stream(" if native else
                          "static int adev_open_output_stream(")
        closing = extract(implementation, "static void close_output_stream(" if native else
                          "static void adev_close_output_stream(")
        if baseline:
            methods += [opening, closing]
        else:
            methods.append("#define calloc stream_calloc\n#define pthread_mutex_init stream_mutex_init\n"
                           "#define free stream_free\n" + opening + "\n#undef pthread_mutex_init\n#undef calloc\n" +
                           closing + "\n#undef free")
            methods.extend(extract(implementation, signature) for signature in (
                "int mindone_audio_output_open(", "void mindone_audio_output_close("))
        callbacks = sorted(set(re.findall(r"out->stream\.(?:common\.)?\w+ = (out_\w+);", opening)))
        present = set(re.findall(r"\b(out_\w+)\(", "\n".join(methods)))
        callbacks = "\n".join("static void " + name + "(void) {}" for name in callbacks if name not in present)
        body = environment.replace("PRODUCTION_ENUMS", enums).replace("PRODUCTION_STRUCTS", structures)
        if baseline:
            body = "struct audio_hw_device { int unused; };\n" + body
            return body + "\n" + callbacks + "\n" + "\n\n".join(methods) + """
int main(void)
{
    struct audio_route route = {0};
    struct mindone_audio_device device = {.route = &route};
    struct audio_stream_out *streams[9];
    pthread_mutex_init(&device.lock, NULL);
    for (unsigned int i = 0; i < 9; i++) {
        struct audio_config config = {0};
        check(adev_open_output_stream(&device.hw_device, (int)i + 10, AUDIO_DEVICE_OUT_SPEAKER,
                                     0, &config, &streams[i], "") == 0, "baseline opens nine outputs");
    }
    struct audio_port_config input = {.type = AUDIO_PORT_TYPE_MIX, .ext.mix.handle = 18};
    struct audio_port_config output = {.type = AUDIO_PORT_TYPE_DEVICE,
                                      .ext.device.type = AUDIO_DEVICE_OUT_WIRED_HEADPHONE};
    audio_patch_handle_t handle = 0;
    check(adev_create_audio_patch(&device.hw_device, 1, &input, 1, &output, &handle) == 0,
          "baseline accepts ninth output patch");
    check(device.n_outs == 8 &&
          ((struct mindone_stream_out *)streams[8])->device == AUDIO_DEVICE_OUT_SPEAKER &&
          ((struct mindone_stream_out *)streams[0])->device == AUDIO_DEVICE_OUT_WIRED_HEADPHONE,
          "baseline reproduces omitted ninth output and wrong fallback reroute");
    for (unsigned int i = 0; i < 9; i++)
        adev_close_output_stream(&device.hw_device, streams[i]);
    pthread_mutex_destroy(&device.lock);
    puts("BASELINE reproduced: ninth output omitted; targeted patch reroutes other eight");
    return 0;
}
"""
        return template.replace("PRODUCTION_ENVIRONMENT", body).replace("PRODUCTION_CALLBACKS", callbacks).replace(
            "PRODUCTION_FUNCTIONS", "\n\n".join(methods))

    def run(command, **kwargs):
        return subprocess.run(command, check=True, timeout=60, **kwargs)

    args.output.mkdir(parents=True, exist_ok=False)
    report = {"source_sha256": before, "variants": {}, "fault_controls": []}
    with tempfile.TemporaryDirectory(prefix="mindone-audio-tracking-") as directory:
        work = Path(directory)
        library = work / "libmindone_audio_routes.rlib"
        archive = work / "libmindone_audio_routes_ffi.a"
        run([args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib", "--crate-name=mindone_audio_routes",
             str(source / "rust/lib.rs"), "-o", str(library)])
        linked = run([args.rustc, "--edition=2021", "-Dwarnings", "-Dunsafe_op_in_unsafe_fn",
                      "--crate-type=staticlib", "--crate-name=mindone_audio_routes_ffi", "-C", "panic=abort",
                      "--print=native-static-libs", "--extern", "mindone_audio_routes=" + str(library),
                      str(source / "rust/ffi.rs"), "-o", str(archive)], capture_output=True, text=True)
        matches = re.findall(r"native-static-libs: (.+)", linked.stderr)
        if len(matches) != 1:
            raise ValueError("Expected compiler-provided native link inputs")
        native = list(dict.fromkeys(shlex.split(matches[0])))
        traces = {}

        def compile_case(implementation, name, rust, baseline=False):
            path = args.output / (name + ".c")
            path.write_text(fixture(implementation, baseline))
            binary = work / name
            command = [args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                       "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-pthread",
                       "-I", str(source), str(path)]
            if rust:
                command += ["-DMINDONE_AUDIO_RUST_ROUTES", str(archive), *native]
            command += ["-o", str(binary)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=60)
            (args.output / (name + "-compile.log")).write_text(built.stdout + built.stderr)
            if built.returncode:
                raise ValueError("Fixture compile failed: " + name + "\n" + built.stderr)
            return binary

        if args.baseline_source:
            binary = compile_case(args.baseline_source.read_text(), "baseline", True, True)
            result = run([str(binary)], capture_output=True, text=True)
            (args.output / "baseline.log").write_text(result.stdout + result.stderr)
            report["baseline"] = result.stdout.strip()
            print(result.stdout, end="", flush=True)
        for name, rust in (("default", False), ("rust", True)):
            binary = compile_case(production, name, rust)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            (args.output / (name + ".log")).write_text(result.stdout + result.stderr)
            if result.returncode or "exact output tracking lifecycle PASS" not in result.stdout:
                raise ValueError("Lifecycle replay failed: " + name + "\n" + result.stdout + result.stderr)
            report["variants"][name] = result.stdout.strip()
            print(name + ": " + result.stdout, end="", flush=True)
            traces[name] = run([str(binary), "trace"], capture_output=True).stdout
            (args.output / (name + "-trace.txt")).write_bytes(traces[name])
        if traces["default"] != traces["rust"]:
            raise ValueError("Actual default and Rust lifecycle traces differ")
        report["parity_bytes"] = len(traces["default"])
        report["parity_sha256"] = hashlib.sha256(traces["default"]).hexdigest()
        if args.fault_controls:
            native_open = "static int open_output_stream(" in production
            opening = extract(production, "static int open_output_stream(" if native_open else
                              "static int adev_open_output_stream(")
            closing = extract(production, "static void close_output_stream(" if native_open else
                              "static void adev_close_output_stream(")
            parameters = extract(production, "int mindone_audio_set_sco(")
            patching = extract(production, "static int create_audio_patch(")
            mutants = [
                ("drop-output-after-eight", replace_once(production, opening, replace_once(opening,
                 "    *link = out;", "    unsigned int count = 0;\n    for (struct mindone_stream_out *it = adev->outs; it; it = it->next)\n        count++;\n    if (count < 8)\n        *link = out;")), "every successful open is tracked beyond eight"),
                ("skip-close-unlink", replace_once(production, closing, replace_once(closing,
                 "    if (*link == out) {", "    if (*link == out && false) {")), "freed output is detached from registry"),
                ("close-first-sibling-only", replace_once(production, closing, replace_once(closing,
                 "other; other = other->next)", "other; other = NULL)")), "failed close preserves far shared crossbar and rejected ownership snapshot"),
                ("hide-allocation-failure", replace_once(production, opening, replace_once(opening,
                 "    if (!out)\n        return -ENOMEM;", "    if (!out)\n        return 0;")), "failed open reports allocation initialization or argument error"),
                ("accept-unsupported-mmap-output", replace_once(production, opening, replace_once(opening,
                 "    if (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)",
                 "    if (false && (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ))")),
                 "unsupported MMAP output is rejected without publishing a stream"),
                ("ignore-mutex-init-failure", replace_once(production, opening, replace_once(opening,
                 "    if (rc) {", "    if (rc && false) {")), "failed open reports allocation initialization or argument error"),
                ("sco-first-output-only", replace_once(production, parameters, replace_once(parameters,
                 "out; out = out->next)", "out; out = NULL)")), "SCO band reaches every tracked output"),
                ("patch-first-output-only", replace_once(production, patching, replace_once(patching,
                 "            for (struct mindone_stream_out *o = adev->outs; o; o = o->next) {",
                 "            for (struct mindone_stream_out *o = adev->outs; o; o = NULL) {")),
                 "matched patch changes only selected output"),
                ("skip-missing-handle-rejection", replace_once(production, patching, replace_once(patching,
                 "                if (!found) {", "                if (!found && false) {")),
                 "unknown stale or malformed output patch rejected"),
                ("validate-first-source-only", replace_once(production, patching, replace_once(patching,
                 "            for (si = 0; si < num_sources; si++) {",
                 "            for (si = 0; si < num_sources && si < 1; si++) {")),
                 "unknown stale or malformed output patch rejected"),
                ("noncumulative-tail-preflight", replace_once(production, "        shadow = plan.next;",
                 "        (void)plan;"), "cumulative preflight preserves counts mixer and handle"),
            ]
            for name, implementation, expected in mutants:
                binary = compile_case(implementation, name, True)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                (args.output / (name + ".log")).write_text(result.stdout + result.stderr)
                if result.returncode != 1 or expected not in result.stderr:
                    raise ValueError("Mutation not rejected as expected: " + name + "\n" + result.stdout + result.stderr)
                report["fault_controls"].append(name)
                print("fault-control " + name + ": rejected", flush=True)
        after = {str(path): digest(path) for path in inputs}
        if before != after:
            raise ValueError("Source inputs changed during replay")
        report.update({"status": "pass", "input_hashes_unchanged": True,
                       "limits": ["Exact C methods and Rust FFI; Android structs and hardware dependencies are host fixtures.",
                                  "Stream cardinalities 1, 8, 9, 65 and 257; no production stream cap is introduced.",
                                  "No Soong, complete HAL link, runtime media, AIDL/FMQ or hardware acceptance."]})
        (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report))


if __name__ == "__main__":
    main()
