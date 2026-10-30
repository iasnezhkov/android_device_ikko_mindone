# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile



def verify_errors(root, aidl):
    text = aidl.read_text()
    values = dict((name, int(value)) for name, value in re.findall(r"\b(\w+)\s*=\s*(\d+)", text))
    header = (root / "mindone_radio.h").read_text()
    actual = dict((name, int(value)) for name, value in re.findall(r"#define MR_RADIO_ERROR_(\w+) (\d+)", header))
    names = {"NONE": "NONE", "RADIO_NOT_AVAILABLE": "RADIO_NOT_AVAILABLE", "GENERIC_FAILURE": "GENERIC_FAILURE", "REQUEST_NOT_SUPPORTED": "REQUEST_NOT_SUPPORTED", "INTERNAL_ERR": "INTERNAL_ERR", "MODEM_ERR": "MODEM_ERR", "SIM_ERR": "SIM_ERR", "INVALID_ARGUMENTS": "INVALID_ARGUMENTS", "SIM_BUSY": "SIM_BUSY", "OPERATION_NOT_ALLOWED": "OPERATION_NOT_ALLOWED", "EMPTY_RECORD": "EMPTY_RECORD", "INVALID_RESPONSE": "INVALID_RESPONSE", "OEM_ERROR1": "OEM_ERROR_1"}
    assert actual == {name: values[aidl_name] for name, aidl_name in names.items()}


def main():
    parser = argparse.ArgumentParser(description="Host-only radio FFI checks")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC") or shutil.which("rustc") or "rustc")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--aidl", type=Path, help="RadioError.aidl; default from ANDROID_BUILD_TOP")
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    root = tests.parent

    def run(command):
        return subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)

    aidl = args.aidl
    if aidl is None and os.environ.get("ANDROID_BUILD_TOP"):
        aidl = Path(os.environ["ANDROID_BUILD_TOP"]) / "hardware/interfaces/radio/aidl/android/hardware/radio/RadioError.aidl"
    aidl_verified = aidl is not None and aidl.is_file()

    try:
        if aidl_verified:
            verify_errors(root, aidl)
        run(["python3", str(root / "generate.py"), "--check"])
        with tempfile.TemporaryDirectory(prefix=".host-check-", dir=tests) as directory:
            output = Path(directory)
            libraries = {}
            for name in ["at", "state", "network", "modem", "core", "runtime"]:
                library = output / f"libmindone_radio_{name}.rlib"
                command = [args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib", str(root.parent / name / "lib.rs"), "-L", f"dependency={output}", "-o", str(library)]
                if name in ["network", "modem", "core", "runtime"]:
                    dependencies = libraries if name in ["core", "runtime"] else {key: libraries[key] for key in ["at", "state"]}
                    for dependency, path in dependencies.items():
                        command.extend(["--extern", f"mindone_radio_{dependency}={path}"])
                run(command)
                libraries[name] = library
            shim = output / "posix.o"
            run([args.cc, "-Wall", "-Werror", "-std=c11", "-c", str(root.parent / "runtime" / "posix.c"), "-o", str(shim)])
            run(["ar", "rcs", str(output / "libmindone_radio_posix.a"), str(shim)])
            common = [args.rustc, "--edition=2021", "-Dwarnings", str(root / "lib.rs"), "-L", f"dependency={output}", "-L", f"native={output}", "-l", "static=mindone_radio_posix"]
            for name, path in libraries.items():
                common.extend(["--extern", f"mindone_radio_{name}={path}"])
            archive = output / "libmindone_radio_ffi.a"
            run(common + ["--crate-type=staticlib", "-o", str(archive)])
            binary = output / "ffi-tests"
            run(common + ["--test", "-o", str(binary)])
            result = run([str(binary)])
            print(result.stdout, end="")
            count = int(re.search(r"test result: ok\. (\d+) passed", result.stdout).group(1))
            c_binary = output / "layout-test"
            link = ["-lpthread", "-ldl", "-lm"] if os.uname().sysname == "Linux" else ["-framework", "Security", "-framework", "CoreFoundation", "-liconv", "-lresolv"]
            run([args.cc, "-Wall", "-Werror", "-std=c11", str(tests / "layout.c"), str(archive), *link, "-o", str(c_binary)])
            print(run([str(c_binary)]).stdout, end="")
            print(json.dumps({"status": "pass", "unit_tests": count, "c_tests": 1, "mode": "host_only", "warnings_denied": True, "aidl_verified": aidl_verified}))
    except subprocess.CalledProcessError as error:
        print(error.stdout, end="")
        print(error.stderr, end="")
        print(json.dumps({"status": "fail", "mode": "host_only", "returncode": error.returncode}))
        raise SystemExit(1)


if __name__ == "__main__":
    main()
