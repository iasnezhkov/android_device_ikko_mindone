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


def main():
    parser = argparse.ArgumentParser(description="Host-only radio network checks")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC") or shutil.which("rustc") or "rustc")
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    root = tests.parent

    def run(command):
        return subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)

    try:
        with tempfile.TemporaryDirectory(prefix=".host-check-", dir=tests) as directory:
            output = Path(directory)
            libraries = {}
            for name in ["at", "state", "network"]:
                library = output / f"libmindone_radio_{name}.rlib"
                source = root / "lib.rs" if name == "network" else root.parent / name / "lib.rs"
                command = [args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib", str(source), "-o", str(library)]
                if name == "network":
                    for dependency, path in libraries.items():
                        command.extend(["--extern", f"mindone_radio_{dependency}={path}"])
                run(command)
                libraries[name] = library
            binary = output / "network-tests"
            command = [args.rustc, "--edition=2021", "-Dwarnings", "--test", str(tests / "network.rs"), "-L", f"dependency={output}", "-o", str(binary)]
            for name, library in libraries.items():
                command.extend(["--extern", f"mindone_radio_{name}={library}"])
            run(command)
            result = run([str(binary)])
            print(result.stdout, end="")
            count = int(re.search(r"test result: ok\. (\d+) passed", result.stdout).group(1))
            print(json.dumps({"status": "pass", "unit_tests": count, "mode": "host_only", "warnings_denied": True}))
    except subprocess.CalledProcessError as error:
        print(error.stdout, end="")
        print(error.stderr, end="")
        print(json.dumps({"status": "fail", "mode": "host_only", "returncode": error.returncode}))
        raise SystemExit(1)


if __name__ == "__main__":
    main()
