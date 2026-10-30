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
          "platform.rs", "posix.c", "Android.bp", "mindone_mux.c", "tests/platform.rs",
          "tests/platform_support.c", "tests/platform_check.py", "tests/namespace_check.py",
          "tests/namespace_controls.c", "tests/namespace_bridge.c", "tests/namespace.rs",
          "../../volte_md_status/tests/android_check.py", "../../rootdir/etc/init.mt8781.rc")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description="Build and exercise the optional mux POSIX adapter using private host PTYs, no device or modem access")
    parser.add_argument("--rustc", default=os.environ.get("RUSTC", "rustc"))
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--ar", default=os.environ.get("AR", "ar"))
    parser.add_argument("--fault-controls", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    before = {name: digest(root / name) for name in INPUTS}
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
            raise ValueError("A completed check left live descendants")
        result = subprocess.CompletedProcess(command, child.returncode, stdout, stderr)
        results.append({"command": [str(value) for value in command], "exit_code": child.returncode,
                        "stdout": stdout, "stderr": stderr})
        if check and result.returncode:
            print(stdout, end="")
            print(stderr, end="")
            result.check_returncode()
        return result

    with tempfile.TemporaryDirectory(prefix="mindone-mux-posix-") as temporary:
        build = Path(temporary)
        libraries = {name: build / f"libmindone_mux_{name}.rlib" for name in ("core", "runtime", "platform")}
        dependencies = []

        def rust_library(name, source, deps):
            execute([args.rustc, "--edition=2021", "-Dwarnings", "-C", "opt-level=1", "--crate-type=rlib",
                     f"--crate-name=mindone_mux_{name}", str(source), *deps, "-L", f"dependency={build}", "-o", str(libraries[name])])

        for name, source in (("core", "lib.rs"), ("runtime", "runtime.rs"), ("platform", "platform.rs")):
            rust_library(name, root / source, dependencies)
            dependencies.extend(["--extern", f"mindone_mux_{name}={libraries[name]}"])

        def c_object(source, output):
            execute([args.cc, "-Wall", "-Wextra", "-Werror", "-std=gnu11", "-c", str(source), "-o", str(output)])

        native = build / "posix.o"
        support = build / "support.o"
        c_object(root / "posix.c", native)
        c_object(root / "tests/platform_support.c", support)
        executable = build / "platform-test"
        archive = build / "libmindone_mux_platform_support.a"
        link = ["-L", f"native={build}", "-l", "static:+whole-archive=mindone_mux_platform_support"]

        def build_tests():
            execute([args.ar, "rcs", str(archive), str(native), str(support)])
            execute([args.rustc, "--edition=2021", "-Dwarnings", "-C", "opt-level=1", "--test",
                     str(root / "tests/platform.rs"), *dependencies, "-L", f"dependency={build}", *link, "-o", str(executable)])

        def run_tests(check=True, test=None):
            with tempfile.TemporaryDirectory(prefix="ptys-", dir=temporary) as fixtures:
                env = {**os.environ, "MINDONE_PTY_TEST_ROOT": fixtures, "RUST_MIN_STACK": "8388608"}
                command = [str(executable), "--test-threads=1", "--nocapture"]
                if test:
                    command.extend(["--exact", test])
                return execute(command, env=env, check=check)

        build_tests()
        units = run_tests()
        print(units.stdout, end="")
        if "test result: ok." not in units.stdout:
            raise ValueError("Missing completed test evidence")
        c_source = (root / "mindone_mux.c").read_text()
        table = re.search(r"sim_dlci\[MAX_SIMS\]\[KINDS\] = \{(.*?)\n\};", c_source, re.S).group(1)
        dlcis = [[int(number) for number in re.findall(r"\d+", row)] for row in re.findall(r"\{([^}]+)\}", table)]
        kinds = re.findall(r'"([^"]*)"|(NULL)', re.search(r"kind_name\[KINDS\] = \{(.*?)\n\};", c_source, re.S).group(1))
        expected = []
        for sim, row in enumerate(dlcis):
            for channel, (kind, atci) in zip(row, kinds):
                name = f"atci{sim + 1}" if atci else f"ptty{sim + 1 if sim else ''}{kind}"
                expected.append((channel, name, str(bool(atci)).lower()))
        for name, macro in (("pttynwcmd", "DLCI_NWCMD"), ("pttynwurc", "DLCI_NWURC")):
            expected.append((int(re.search(r"^#define " + macro + r" (\d+)$", c_source, re.M).group(1)), name, "false"))
        actual = [(int(channel), name, atci) for channel, name, atci in re.findall(r"mapping (\d+) (\S+) (true|false)", units.stdout)]
        if actual != expected:
            raise ValueError("Rust board mapping differs from the exact production C source")
        type_controls = []
        prefix = 'use mindone_mux_platform::PosixEndpoint;\nuse std::os::fd::AsFd;\nfn main(){\nlet owner=PosixEndpoint::private_pty().unwrap();\n'
        for name, code, negative in (
            ("endpoint-not-copy", "E0382", 'let duplicate=owner; let _=owner.as_fd(); let _=duplicate.as_fd();'),
            ("endpoint-not-clone", "E0599", 'let _=owner.clone(); let _=owner.as_fd();'),
            ("descriptor-is-private", "E0616", 'let _=&owner.file; let _=owner.as_fd();'),
        ):
            source = build / (name + ".rs")
            command = [args.rustc, "--edition=2021", "-Dwarnings", str(source), *dependencies,
                       "-L", f"dependency={build}", *link, "-o", str(build / name)]
            source.write_text(prefix + 'let _=owner.as_fd();\n}\n')
            execute(command)
            execute([str(build / name)])
            source.write_text(prefix + negative + '\n}\n')
            rejected = execute(command, check=False)
            if rejected.returncode == 0 or code not in rejected.stderr:
                raise ValueError(f"Ownership type control failed: {name}")
            type_controls.append(name)
        mutations = []
        if args.fault_controls:
            variants = (
                ("same-target-replacement-removed", "posix.c", "identity->inode != (uint64_t)status.st_ino", "(0 && identity->inode != (uint64_t)status.st_ino)", "cleanup_preserves_a_replacement_symlink_to_the_same_slave"),
                ("text-busy-reset-lost", "posix.c", "error == ENODEV || error == ETXTBSY", "error == ENODEV", "errno_classes_preserve_modem_specific_faults"),
                ("serial-lock-disabled", "posix.c", "if (flock(fd, LOCK_EX | LOCK_NB))", "if (0 && flock(fd, LOCK_EX | LOCK_NB))", "serial_ownership_refuses_competing_open_and_releases_on_drop"),
                ("serial-follows-symlink", "posix.c", "O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW", "O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC", "serial_refuses_regular_file_symlink_and_nul"),
                ("master-without-cloexec", "posix.c", "posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC)", "posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK)", "real_pty_is_raw_nonblocking_cloexec_private_and_bidirectional"),
                ("serial-without-nonblock", "posix.c", "O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW", "O_RDWR | O_NOCTTY | O_CLOEXEC | O_NOFOLLOW", "real_pty_is_raw_nonblocking_cloexec_private_and_bidirectional"),
                ("untrusted-directory-accepted", "posix.c", "status.st_uid != geteuid() || status.st_mode & 0022", "status.st_uid != geteuid()", "factory_refuses_untrusted_directory_and_symlink"),
                ("hup-ignored", "posix.c", "((events & (POLLHUP | POLLERR)) ? 4U : 0U)", "((events & (POLLHUP | POLLERR)) ? 0U : 0U)", "poll_hangup_and_invalid_descriptor_are_observed"),
                ("invalid-fd-ignored", "platform.rs", "if descriptor.events & 8 != 0", "if false && descriptor.events & 8 != 0", "poll_hangup_and_invalid_descriptor_are_observed"),
                ("timer-deadline-ignored", "platform.rs", "maximum.min(end.saturating_sub(now))", "maximum.max(end.saturating_sub(now))", "poll_deadline_and_maximum_are_bounded_without_spin"),
                ("poll-eintr-fatal", "platform.rs", "if classify_error(&error) == Fault::Interrupted", "if false && classify_error(&error) == Fault::Interrupted", "poll_interruption_returns_control_after_one_signal"),
                ("driver-error-retains-endpoints", "platform.rs", "if result.is_err() { self.runtime.stop(); }", "if false && result.is_err() { self.runtime.stop(); }", "driver_error_drops_owned_descriptors_and_published_links"),
                ("terminal-driver-polls", "platform.rs", "if matches!(self.runtime.state(), State::Closed | State::Failed)", "if false && matches!(self.runtime.state(), State::Closed | State::Failed)", "terminal_driver_returns_without_poll_wait"),
                ("drop-retains-link", "platform.rs", "unsafe { mindone_mux_unpublish(self.directory.as_raw_fd(), self.name.as_ptr(), self.target.as_ptr(), &self.identity); }", "if false { unsafe { mindone_mux_unpublish(self.directory.as_raw_fd(), self.name.as_ptr(), self.target.as_ptr(), &self.identity); } }", "factory_publishes_cleans_and_reopens_all_board_channels"),
            )
            for name, source_name, old, new, test in variants:
                original = (root / source_name).read_text()
                if original.count(old) != 1:
                    raise ValueError(f"Expected one mutation site: {name}")
                mutated = build / source_name
                mutated.write_text(original.replace(old, new, 1))
                if source_name == "posix.c":
                    c_object(mutated, native)
                else:
                    rust_library("platform", mutated, dependencies[:4])
                build_tests()
                rejected = run_tests(check=False, test=test)
                if rejected.returncode == 0 or "test result: FAILED" not in rejected.stdout:
                    raise ValueError(f"Behavior mutation was not rejected by its control: {name}")
                mutations.append(name)
                print(f"fault-control {name}: rejected")
                if source_name == "posix.c":
                    c_object(root / source_name, native)
                else:
                    rust_library("platform", root / source_name, dependencies[:4])
        namespace_output = args.output / "namespace" if args.output else build / "namespace"
        namespace_args = ["--rustc", args.rustc, "--cc", args.cc, "--ar", args.ar, "--output", str(namespace_output)]
        if args.fault_controls:
            namespace_args.append("--fault-controls")
        namespace = runpy.run_path(str(root / "tests/namespace_check.py"))["main"](namespace_args)
        if before != {name: digest(root / name) for name in INPUTS}:
            raise ValueError("Source inputs changed during the proof")
        report = {
            "status": "pass", "mode": "host_actual_private_PTY_POSIX_adapter_not_modem_or_Android",
            "source_sha256": before, "input_hashes_unchanged": True,
            "tests": int(re.search(r"test result: ok\. (\d+) passed", units.stdout).group(1)),
            "exact_c_board_mapping": True, "mapped_channels": len(expected),
            "runtime_pty_replay": dict(zip(("uplink_bytes", "downlink_bytes", "channels"), map(int, re.search(r"pty-runtime uplink_bytes=(\d+) downlink_bytes=(\d+) channels=(\d+)", units.stdout).groups()))),
            "actual_pty_backpressure_bytes": int(re.search(r"pty-backpressure accepted=(\d+)", units.stdout).group(1)),
            "type_controls": type_controls, "fault_controls": mutations, "owned_process_groups": groups,
            "namespace": namespace,
            "limits": [
                "The adapter and libraries are optional build targets; production C selection is unchanged. No daemon, init, SELinux, Android properties, signals/restart policy or modem hardware acceptance is claimed.",
                "Serial ownership uses a nonblocking advisory flock; uncooperative processes and duplicated descriptors remain an external startup/exclusive-owner prerequisite.",
                "Private publication requires an euid-owned directory without group/world write. Explicit shared-radio construction pins /dev/radio with exact uid/gid1001 and0770 under euid1001. Both paths retain the directory descriptor, refuse existing entries and check symlink device/inode/target before removal. Shared radio group writers are trusted; concurrent namespace replacement can race unlink.",
                "PTY permission failure is fatal; private host mode is0600, explicit radio mode requires uid/gid1001 and0660. Radio ownership and SELinux require target verification.",
                "Darwin leaves a new master unopened and applies raw mode when its slave is opened through open_serial; opening/closing a bootstrap slave would cause initial HUP and its termios would reset. Linux/Android configure the master and preserve serial settings; that target branch is not executed by a Darwin host proof.",
                "Each driver step performs at most one poll with an Instant-derived deadline and a maximum1000ms; EINTR returns to the caller, ERR/HUP are handed to the runtime and NVAL fails closed. No background loop is started.",
                "Host PTYs cover framing, actual short writes/EAGAIN, lifecycle, setup and close. Existing runtime injected-fault gates cover bounded EINTR/short-I/O/reset/recreated-token handling; actual CCCI errors remain untested.",
                "Optional Soong targets still require exact target compilation and ELF/ABI checks; this host gate is not Android17 integration acceptance.",
            ],
        }
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
            (args.output / "commands.json").write_text(json.dumps(results, indent=2) + "\n")
        print(json.dumps({key: report[key] for key in ("status", "tests", "mapped_channels", "runtime_pty_replay", "type_controls", "fault_controls")}))


if __name__ == "__main__":
    main()
