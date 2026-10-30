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


INPUTS = ("lib.rs", "session.rs", "transport.rs", "chat.rs", "mcc.rs", "runtime.rs",
          "platform.rs", "posix.c", "tests/namespace.rs", "tests/namespace_bridge.c",
          "tests/namespace_controls.c", "tests/namespace_check.py",
          "../../volte_md_status/tests/android_check.py", "../../rootdir/etc/init.mt8781.rc")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser(description="Check private and explicit shared-radio mux directory contracts using private host fixtures and injected identity, without opening /dev/radio")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC", "rustc"))
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--ar", default=os.environ.get("AR", "ar"))
    parser.add_argument("--fault-controls", action="store_true")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parent.parent
    before = {name: digest(root / name) for name in INPUTS}
    init = (root / "../../rootdir/etc/init.mt8781.rc").read_text()
    if len(re.findall(r"^\s*mkdir /dev/radio 0770 radio radio\s*$", init, re.M)) != 1:
        raise ValueError("Expected the exact current radio directory init contract")
    stop_process = runpy.run_path(str(root / "../../volte_md_status/tests/android_check.py"))["stop_process"]
    groups = []
    results = []

    def execute(command, seconds=45, env=None, check=True):
        child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 text=True, env=env, start_new_session=True)
        try:
            stdout, stderr = child.communicate(timeout=seconds)
        except BaseException:
            stop_process(child)
            child.communicate(timeout=1)
            raise
        survivors = stop_process(child)
        groups.append({"group": child.pid, "exit_code": child.returncode, "empty_after_cleanup": not bool(survivors)})
        if survivors:
            raise ValueError("A completed namespace check left live descendants")
        result = subprocess.CompletedProcess(command, child.returncode, stdout, stderr)
        results.append({"command": [str(value) for value in command], "exit_code": child.returncode,
                        "stdout": stdout, "stderr": stderr})
        if check and result.returncode:
            print(stdout, end="")
            print(stderr, end="")
            result.check_returncode()
        if "Sanitizer" in stderr or "runtime error:" in stderr:
            raise ValueError("A namespace control triggered a sanitizer diagnostic")
        return result

    with tempfile.TemporaryDirectory(prefix="mindone-mux-namespace-") as temporary:
        build = Path(temporary)
        flags = ["-Wall", "-Wextra", "-Werror", "-std=gnu11"]
        sanitizer = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else []

        def c_controls(source, name, baseline=False):
            executable = build / name
            execute([args.cc, *flags, *sanitizer, '-DPOSIX_SOURCE="' + str(source) + '"',
                     *(["-DBASELINE"] if baseline else []), str(root / "tests/namespace_controls.c"),
                     "-o", str(executable)])
            return executable

        def run_controls(executable, check=True):
            with tempfile.TemporaryDirectory(prefix="directory-", dir=temporary) as fixture:
                return execute([str(executable), fixture], seconds=15, check=check)

        baseline = run_controls(c_controls(root / "posix.c", "private-baseline", baseline=True))
        if "BASELINE_MISMATCH_PROVEN tests=2" not in baseline.stdout:
            raise ValueError("The strict private API did not prove refusal of the real init directory mode")
        controls = run_controls(c_controls(root / "posix.c", "namespace-controls"))
        if "NAMESPACE_CONTROLS_PASS tests=19" not in controls.stdout:
            raise ValueError("Missing completed C namespace controls")
        dependencies = []
        for name, source in (("core", "lib.rs"), ("runtime", "runtime.rs"), ("platform", "platform.rs")):
            library = build / f"libmindone_mux_{name}.rlib"
            execute([args.rustc, "--edition=2021", "-Dwarnings", "-C", "opt-level=1", "--crate-type=rlib",
                     f"--crate-name=mindone_mux_{name}", str(root / source), *dependencies,
                     "-L", f"dependency={build}", "-o", str(library)])
            dependencies.extend(["--extern", f"mindone_mux_{name}={library}"])
        bridge = build / "namespace-bridge.o"
        execute([args.cc, *flags, '-DPOSIX_SOURCE="' + str(root / "posix.c") + '"', "-c",
                 str(root / "tests/namespace_bridge.c"), "-o", str(bridge)])
        archive = build / "libmindone_mux_namespace.a"
        execute([args.ar, "rcs", str(archive), str(bridge)])
        constructor = build / "namespace-constructor"
        execute([args.rustc, "--edition=2021", "-Dwarnings", "-C", "opt-level=1", str(root / "tests/namespace.rs"),
                 *dependencies, "-L", f"dependency={build}", "-L", f"native={build}",
                 "-l", "static:+whole-archive=mindone_mux_namespace", "-o", str(constructor)])
        with tempfile.TemporaryDirectory(prefix="constructor-", dir=temporary) as fixture:
            Path(fixture).chmod(0o770)
            constructors = execute([str(constructor)], seconds=15,
                                   env={**os.environ, "MINDONE_NAMESPACE_TEST_DIRECTORY": fixture})
        if "RADIO_CONSTRUCTOR_PASS private_modes=2 invalid_bounds=2 valid_counts=4 board_path_verified=true" not in constructors.stdout:
            raise ValueError("Missing completed constructor controls")
        mutations = []
        if args.fault_controls:
            source = (root / "posix.c").read_text()
            variants = (
                ("caller_identity_removed", "geteuid() != 1001 || ", "", 1, "shared root caller rejected"),
                ("directory_owner_removed", "status.st_uid != 1001 || ", "", 1, "shared other owner rejected"),
                ("directory_group_removed", "status.st_gid != 1001 ||", "", 1, "shared other group rejected"),
                ("permission_contract_widened", "(status.st_mode & 07777) != 0770", "(status.st_mode & 0002)", 1, "shared unsupported mode 0700 rejected"),
                ("symlink_followed", "O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW", "O_RDONLY | O_DIRECTORY | O_CLOEXEC", 70, "shared symlink unexpectedly accepted"),
                ("descriptor_cloexec_removed", "O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW", "O_RDONLY | O_DIRECTORY | O_NOFOLLOW", 2, "directory descriptor is missing CLOEXEC"),
                ("stat_failure_leaks_fd", "if (fstat(fd, &status)) return fail_fd(fd, errno);\n    if (shared_radio)", "if (fstat(fd, &status)) return -errno;\n    if (shared_radio)", 5, "shared fstat failure closes descriptor leaked directory descriptor"),
                ("shared_mode_routes_private", "return open_directory(path, 1);", "return open_directory(path, 0);", 1, "shared exact radio identity mode accepted"),
            )
            for name, old, new, expected, evidence in variants:
                if source.count(old) != 1:
                    raise ValueError(f"Expected one namespace mutation site: {name}")
                mutated = build / (name + ".c")
                mutated.write_text(source.replace(old, new, 1))
                rejected = run_controls(c_controls(mutated, name), check=False)
                if rejected.returncode != expected or evidence not in rejected.stderr:
                    raise ValueError(f"Namespace semantic control did not reject its compiled mutant: {name}")
                mutations.append({"name": name, "exit_code": rejected.returncode, "semantic_evidence": evidence})
                print(f"namespace-fault-control {name}: rejected")
        if before != {name: digest(root / name) for name in INPUTS}:
            raise ValueError("Namespace inputs changed during the proof")
        report = {
            "status": "pass", "mode": "private_host_directories_injected_radio_identity_not_device",
            "source_sha256": before, "input_hashes_unchanged": True,
            "strict_private_baseline_controls": 2, "actual_init_mode_0770_refused_by_private_api": True,
            "c_controls": 19, "constructor_controls": 8, "board_path_verified": True,
            "fault_controls": mutations, "c_controls_sanitized": args.sanitize,
            "owned_process_groups": groups,
            "limits": [
                "Directory descriptors, permissions, symlinks, regular files, CLOEXEC and cleanup use real private host filesystem operations. EUID and stat UID/GID are injected; this does not prove real Android radio identity or SELinux access.",
                "The baseline is the unchanged strict private API applied to the actual init mode 0770; it is not a historical source checkout.",
                "The constructor bridge validates the exact /dev/radio argument before redirecting to a private temporary directory. It creates no PTY and never opens /dev/radio or modem endpoints.",
                "Shared radio policy trusts UID1001 and GID1001 namespace writers. Concurrent same-UID/group replacement can race unlink; atomic cleanup and exclusive modem ownership are not claimed.",
                "Optional sanitizers cover the C directory controls and their C mutants; the Rust constructor bridge is not sanitizer-instrumented.",
            ],
        }
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
            (args.output / "commands.json").write_text(json.dumps(results, indent=2) + "\n")
        print(json.dumps({key: report[key] for key in ("status", "strict_private_baseline_controls", "c_controls", "constructor_controls", "fault_controls")}))
        return report


if __name__ == "__main__":
    main()
