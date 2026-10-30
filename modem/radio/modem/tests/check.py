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


def verify_aidl(root, aidl):
    def values(relative, name):
        text = (aidl / relative).read_text()
        text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
        body = re.search(r"enum\s+" + name + r"\s*\{([^}]+)\}", text).group(1)
        result = {}
        value = -1
        for entry in body.split(","):
            match = re.fullmatch(r"\s*(\w+)\s*(?:=\s*(\d+))?\s*", entry)
            if match:
                value = int(match[2]) if match[2] else value + 1
                result[match[1]] = value
        return result

    error = values("RadioError.aidl", "RadioError")
    expected = {"None": "NONE", "RadioNotAvailable": "RADIO_NOT_AVAILABLE", "GenericFailure": "GENERIC_FAILURE",
                "RequestNotSupported": "REQUEST_NOT_SUPPORTED", "InternalErr": "INTERNAL_ERR", "ModemErr": "MODEM_ERR",
                "SimErr": "SIM_ERR", "InvalidArguments": "INVALID_ARGUMENTS", "SimBusy": "SIM_BUSY",
                "OperationNotAllowed": "OPERATION_NOT_ALLOWED", "EmptyRecord": "EMPTY_RECORD",
                "InvalidResponse": "INVALID_RESPONSE", "OemError1": "OEM_ERROR_1"}
    source = (root / "types.rs").read_text()
    body = re.search(r"pub enum RadioError\s*\{([^}]+)\}", source).group(1)
    actual = dict((name, int(value)) for name, value in re.findall(r"(\w+)\s*=\s*(\d+)", body))
    assert actual == {name: error[aidl_name] for name, aidl_name in expected.items()}
    for relative, enum, mapping in [
        ("modem/DeviceStateType.aidl", "DeviceStateType", {"POWER_SAVE_MODE": 0, "CHARGING_STATE": 1, "LOW_DATA_EXPECTED": 2}),
        ("modem/ImeiInfo.aidl", "ImeiType", {"INVALID": 0, "PRIMARY": 1, "SECONDARY": 2}),
        ("voice/TtyMode.aidl", "TtyMode", {"OFF": 0, "FULL": 1, "HCO": 2, "VCO": 3}),
        ("sim/PinState.aidl", "PinState", {"UNKNOWN": 0}),
        ("config/MultipleEnabledProfilesMode.aidl", "MultipleEnabledProfilesMode", {"NONE": 0}),
        ("AccessNetwork.aidl", "AccessNetwork", {"UNKNOWN": 0}),
        ("RadioTechnology.aidl", "RadioTechnology", {"GPRS": 1}),
    ]:
        found = values(relative, enum)
        assert all(found[key] == value for key, value in mapping.items()), relative
    for relative, mapping in [
        ("sim/CardStatus.aidl", {"STATE_ABSENT": 0, "STATE_PRESENT": 1, "STATE_ERROR": 2, "STATE_RESTRICTED": 3}),
        ("modem/RadioCapability.aidl", {"PHASE_UNSOL_RSP": 3, "STATUS_SUCCESS": 1}),
        ("modem/ActivityStatsTechSpecificInfo.aidl", {"FREQUENCY_RANGE_UNKNOWN": 0}),
        ("config/PhoneCapability.aidl", {"UNKNOWN": -1}),
    ]:
        text = (aidl / relative).read_text()
        for name, value in mapping.items():
            assert re.search(r"\b" + name + r"\s*=\s*" + str(value) + r"\s*;", text), (relative, name)


def main():
    parser = argparse.ArgumentParser(description="Host-only radio modem checks")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC") or shutil.which("rustc") or "rustc")
    parser.add_argument("--aidl", type=Path, help="android/hardware/radio AIDL directory; default from ANDROID_BUILD_TOP")
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    root = tests.parent
    aidl = args.aidl
    if aidl is None and os.environ.get("ANDROID_BUILD_TOP"):
        aidl = Path(os.environ["ANDROID_BUILD_TOP"]) / "hardware/interfaces/radio/aidl/android/hardware/radio"
    aidl_verified = aidl is not None and aidl.is_dir()
    if aidl_verified:
        verify_aidl(root, aidl)

    def run(command):
        return subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)

    try:
        with tempfile.TemporaryDirectory(prefix=".host-check-", dir=tests) as directory:
            output = Path(directory)
            libraries = {}
            for name in ["at", "state", "modem"]:
                library = output / f"libmindone_radio_{name}.rlib"
                source = root / "lib.rs" if name == "modem" else root.parent / name / "lib.rs"
                command = [args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib", str(source), "-o", str(library)]
                if name == "modem":
                    for dependency, path in libraries.items():
                        command.extend(["--extern", f"mindone_radio_{dependency}={path}"])
                run(command)
                libraries[name] = library
            binary = output / "modem-tests"
            command = [args.rustc, "--edition=2021", "-Dwarnings", "--test", str(tests / "modem.rs"), "-L", f"dependency={output}", "-o", str(binary)]
            for name, library in libraries.items():
                command.extend(["--extern", f"mindone_radio_{name}={library}"])
            run(command)
            result = run([str(binary)])
            print(result.stdout, end="")
            count = int(re.search(r"test result: ok\. (\d+) passed", result.stdout).group(1))
            print(json.dumps({"status": "pass", "unit_tests": count, "mode": "host_only", "warnings_denied": True, "aidl_verified": aidl_verified}))
    except subprocess.CalledProcessError as error:
        print(error.stdout, end="")
        print(error.stderr, end="")
        print(json.dumps({"status": "fail", "mode": "host_only", "returncode": error.returncode}))
        raise SystemExit(1)


if __name__ == "__main__":
    main()
