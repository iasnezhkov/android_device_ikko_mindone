# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
rustc = os.environ.get("RUSTC", "rustc")
with tempfile.TemporaryDirectory(prefix="mux-daemon-") as temp:
    build = Path(temp)
    def run(args, **kwargs):
        subprocess.run([str(arg) for arg in args], check=True, timeout=60, **kwargs)
    objects = []
    for source in ("posix.c", "properties.c", "daemon.c"):
        obj = build / (source + ".o")
        run(["cc", "-Wall", "-Wextra", "-Werror", "-std=gnu11", "-c", root / source, "-o", obj])
        objects.append(obj)
    run(["ar", "rcs", build / "libmux_native.a", *objects])
    deps = []
    for name, source in (("core", "lib.rs"), ("runtime", "runtime.rs"), ("platform", "platform.rs"), ("service", "service.rs"), ("android_properties", "android_properties.rs")):
        output = build / ("libmindone_mux_" + name + ".rlib")
        run([rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib", "--crate-name=mindone_mux_" + name, root / source, "-L", "dependency=" + temp, *deps, "-o", output])
        deps += ["--extern", "mindone_mux_" + name + "=" + str(output)]
    link = ["-L", "dependency=" + temp, "-L", "native=" + temp, "-l", "static:+whole-archive=mux_native"]
    binary = build / "gsm0710muxd-rs"
    run([rustc, "--edition=2021", "-Dwarnings", root / "main.rs", *deps, *link, "-o", binary])
    for args in (["-m", "advanced"], ["-f", "0"], ["-f", "32768"], ["-s"], ["-z"], ["-h"]):
        result = subprocess.run([binary, *args], capture_output=True, timeout=5)
        assert result.returncode == 2, (args, result.returncode)
    tests = build / "daemon-tests"
    run([rustc, "--edition=2021", "-Dwarnings", "--test", root / "main.rs", *deps, *link, "-o", tests])
    for name, source in (("framing", "tests/framing.rs"), ("session", "tests/session.rs"), ("transport", "tests/transport.rs"), ("chat", "tests/chat.rs"), ("mcc", "tests/mcc.rs"), ("core_api", "tests/core_api.rs"), ("runtime", "tests/runtime.rs"), ("platform", "tests/platform.rs"), ("service", "service.rs")):
        executable = build / (name + "-test")
        support = build / "platform_support.o"
        run(["cc", "-Wall", "-Wextra", "-Werror", "-std=gnu11", "-c", root / "tests/platform_support.c", "-o", support])
        run(["ar", "rcs", build / "libmux_native.a", support])
        run([rustc, "--edition=2021", "-Dwarnings", "--test", root / source, *deps, *link, "-o", executable])
        test_root = build / (name + "-ptys")
        test_root.mkdir(mode=0o700)
        run([executable, "--test-threads=1"], env={**os.environ, "MINDONE_PTY_TEST_ROOT": str(test_root), "MINDONE_SERVICE_TEST_ROOT": str(test_root), "RUST_MIN_STACK": "8388608"})
    header = build / "include/sys/system_properties.h"
    header.parent.mkdir(parents=True)
    header.write_text("int __system_property_set(const char *key, const char *value);\n")
    for android in (False, True):
        executable = build / ("property-android" if android else "property-host")
        flags = ["-D__ANDROID__", "-I", str(header.parent.parent), str(root / "tests/property_backend.c")] if android else ["-DEXPECT_UNSUPPORTED"]
        run(["cc", "-Wall", "-Wextra", "-Werror", "-std=gnu11", root / "properties.c", root / "tests/property_probe.c", *flags, "-o", executable])
        run([executable], env={key: value for key, value in os.environ.items() if key != "MINDONE_PROPERTY_FAILURE"})
    fixture = build / "namespace"
    fixture.mkdir(mode=0o700)
    bridge = build / "namespace-bridge.o"
    run(["cc", "-Wall", "-Wextra", "-Werror", "-std=gnu11", '-DPOSIX_SOURCE="' + str(root / "posix.c") + '"', "-c", root / "tests/namespace_bridge.c", "-o", bridge])
    run(["ar", "rcs", build / "libnamespace.a", bridge])
    constructor = build / "namespace-constructor"
    run([rustc, "--edition=2021", "-Dwarnings", root / "tests/namespace.rs", *deps, "-L", "dependency=" + temp, "-L", "native=" + temp, "-l", "static:+whole-archive=namespace", "-o", constructor])
    fixture.chmod(0o770)
    run([constructor], env={**os.environ, "MINDONE_NAMESPACE_TEST_DIRECTORY": str(fixture)})
    directory = build / "ptys"
    directory.mkdir(mode=0o700)
    run([tests, "--test-threads=1", "--nocapture"], env={**os.environ, "MINDONE_DAEMON_TEST_ROOT": str(directory), "RUST_MIN_STACK": "8388608"})
