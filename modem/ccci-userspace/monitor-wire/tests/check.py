# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The LineageOS Project

import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import re
import runpy
import struct
import subprocess
import tempfile


KERNEL_INPUTS = (
    "mindone/modules/ccci_md_all/fsm/ccci_fsm_monitor.c",
    "mindone/modules/ccci_md_all/fsm/ccci_fsm_internal.h",
    "mindone/modules/ccci_md_all/inc/ccci_core.h",
    "mindone/modules/include/drivers/misc/mediatek/include/mt-plat/mtk_ccci_common.h",
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description="Check the fixed 16-byte virtual CCCI monitor decoder against the exact current kernel producer with synthetic inputs; no device, ioctl, lifecycle or backend action is performed")
    parser.add_argument("--kernel-root", required=True, type=Path)
    parser.add_argument("--rustc", default=os.environ.get("RUSTC", "rustc"))
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--fault-controls", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source_root = tests.parent
    kernel_root = args.kernel_root.resolve()
    inputs = [source_root / "wire.rs", tests / "wire.rs", tests / "producer.c", tests / "check.py"]
    fixed = [source_root.parent / "mdinit/ccci_mdinit.c", source_root.parent / "Android.bp",
             source_root / "../../../volte_md_status/tests/android_check.py"]
    before = {str(path): digest(path) for path in inputs + fixed}
    kernel_before = {name: digest(kernel_root / name) for name in KERNEL_INPUTS}
    source, internal, core, common = [(kernel_root / name).read_text() for name in KERNEL_INPUTS]
    producer = re.search(r"^int fsm_monitor_send_message\([^;{]+\)\s*\{.*?^\}", source, re.M | re.S)
    header = re.search(r"^struct ccci_header \{.*?^\} __packed;", common, re.M | re.S)
    messages = re.search(r"^enum CCCI_MD_MSG \{.*?^\};", internal, re.M | re.S)
    if not producer or not header or not messages:
        raise ValueError("Cannot find the exact monitor producer, header or message enum")
    magic = re.search(r"^#define CCCI_MAGIC_NUM (0x[0-9A-Fa-f]+)$", core, re.M)
    channel = re.search(r"^\s*CCCI_MONITOR_CH_ID = (0x[0-9A-Fa-f]+),", core, re.M)
    if not magic or not channel or (int(magic.group(1), 16), int(channel.group(1), 16)) != (0xffffffff, 0xf0000000):
        raise ValueError("Kernel monitor constants changed")
    expected_names = ["FORCE_STOP_REQUEST", "FLIGHT_STOP_REQUEST", "FORCE_START_REQUEST", "FLIGHT_START_REQUEST",
                      "RESET_REQUEST", "EXCEPTION", "SEND_BATTERY_INFO", "STORE_NVRAM_MD_TYPE", "CFG_UPDATE", "RANDOM_PATTERN"]
    if re.findall(r"CCCI_MD_MSG_([A-Z_]+)", messages.group(0)) != expected_names:
        raise ValueError("The kernel monitor message list changed")
    if not re.search(r"CCCI_MD_MSG_FORCE_STOP_REQUEST = 0xFAF50001,", messages.group(0)):
        raise ValueError("The kernel monitor message enum base changed")
    for required in ["ccci_alloc_skb(sizeof(struct ccci_header), 1, 1)", "ccci_h->data[0] = CCCI_MAGIC_NUM;",
                     "ccci_h->data[1] = msg;", "*(((u32 *) ccci_h) + 2) = CCCI_MONITOR_CH_ID;", "ccci_h->reserved = resv;"]:
        if required not in producer.group(0):
            raise ValueError(f"The exact producer contract changed: {required}")
    reader = re.search(r"^static ssize_t dev_char_read\([^;{]+\)\s*\{.*?^\}", source, re.M | re.S)
    if not reader:
        raise ValueError("Cannot find the exact monitor read contract")
    for required in ["read_len > count", "ret = -EFAULT;", "ccci_free_skb(skb);"]:
        if required not in reader.group(0):
            raise ValueError(f"The exact monitor short-buffer/drop contract changed: {required}")
    template = (tests / "producer.c").read_text()
    for name, value in {
        "CONSTANTS": f"#define CCCI_MAGIC_NUM {magic.group(1)}u\n#define CCCI_MONITOR_CH_ID {channel.group(1)}u",
        "HEADER": header.group(0), "MESSAGES": messages.group(0), "PRODUCER": producer.group(0),
    }.items():
        token = "@" + name + "@"
        if template.count(token) != 1:
            raise ValueError(f"Expected one exact producer substitution: {token}")
        template = template.replace(token, value)
    stop_process = runpy.run_path(str(fixed[2]))["stop_process"]
    groups = []

    def execute(command, data=None, check=True, environment=None):
        child = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 env=environment, start_new_session=True)
        try:
            stdout, stderr = child.communicate(input=data, timeout=60)
        except BaseException:
            stop_process(child)
            child.communicate(timeout=1)
            raise
        survivors = stop_process(child)
        groups.append({"group": child.pid, "exit_code": child.returncode, "empty_after_cleanup": not survivors})
        if survivors:
            raise ValueError(f"A completed monitor check left live descendants: {child.pid}")
        result = subprocess.CompletedProcess(command, child.returncode, stdout, stderr)
        if check and result.returncode:
            print(stdout.decode(errors="replace"), end="")
            print(stderr.decode(errors="replace"), end="")
            result.check_returncode()
        return result

    random_source = random.Random(72950001)
    cases = [(message, reserved) for message in [*range(0xfaf50001, 0xfaf5000b), 0, 1, 0xfaf50000, 0xfaf5000b, 0xffffffff]
             for reserved in [0, 1, 0x7fff, 0x80000000, 0xffffffff]]
    cases += [(random_source.randrange(0x100000000), random_source.randrange(0x100000000)) for _ in range(4096)]
    stream = b"".join(struct.pack("<II", *values) for values in cases)
    mutants = []
    with tempfile.TemporaryDirectory(prefix="mindone-monitor-wire-") as directory:
        output = Path(directory)
        c_source = output / "producer.c"
        c_source.write_text(template)
        execute([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(c_source), "-o", str(output / "producer")])
        produced = execute([str(output / "producer")], stream).stdout
        expected = b"".join(struct.pack("<IIII", 0xffffffff, message, 0xf0000000, reserved) for message, reserved in cases)
        if produced != expected:
            raise ValueError("The exact kernel producer does not match the fixed virtual monitor contract")
        fixture = b"".join(struct.pack("<II", *values) + produced[index * 16:(index + 1) * 16] for index, values in enumerate(cases))
        fixture_path = output / "producer-fixture.bin"
        fixture_path.write_bytes(fixture)
        environment = {**os.environ, "MINDONE_MONITOR_FIXTURE": str(fixture_path)}
        library = output / "libmindone_monitor_wire.rlib"
        units = output / "wire-test"

        def compile_units(source):
            execute([args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib", "--crate-name=mindone_monitor_wire", str(source), "-o", str(library)])
            execute([args.rustc, "--edition=2021", "-Dwarnings", "--test", str(tests / "wire.rs"), "--extern", f"mindone_monitor_wire={library}", "-o", str(units)])

        compile_units(source_root / "wire.rs")
        unit_output = execute([str(units), "--nocapture"], environment=environment).stdout.decode()
        print(unit_output, end="")
        if args.fault_controls:
            original = (source_root / "wire.rs").read_text()
            variants = (
                ("accepted-concatenated-record", "input.len() != RECORD_BYTES", "input.len() < RECORD_BYTES"),
                ("ignored-magic", "if word(input, 0) != MAGIC", "if false && word(input, 0) != MAGIC"),
                ("only-low-channel-half-checked", "word(input, 8) != MONITOR_WORD", "word(input, 8) as u16 != MONITOR_WORD as u16"),
                ("ignored-monitor-word", "if word(input, 8) != MONITOR_WORD", "if false && word(input, 8) != MONITOR_WORD"),
                ("wrong-field-byte-order", "u32::from_le_bytes", "u32::from_be_bytes"),
                ("lost-reserved-word", "reserved: word(input, 12)", "reserved: 0 * word(input, 12)"),
                ("unknown-event-becomes-reset", "value => Kind::Unsupported(value)", "_ => Kind::ResetRequest"),
                ("nvram-event-becomes-config-update", "0xfaf50008 => Kind::StoreNvramModemType", "0xfaf50008 => Kind::ConfigUpdate"),
            )
            for name, old, new in variants:
                if original.count(old) != 1:
                    raise ValueError(f"Expected one exact monitor mutation site: {name}")
                source_path = output / "wire.rs"
                source_path.write_text(original.replace(old, new, 1))
                compile_units(source_path)
                result = execute([str(units)], check=False, environment=environment)
                if result.returncode == 0 or b"test result: FAILED" not in result.stdout:
                    raise ValueError(f"Compiled monitor behavior mutant was not rejected: {name}")
                mutants.append(name)
                print(f"fault-control {name}: rejected")
    if before != {str(path): digest(path) for path in inputs + fixed} or kernel_before != {name: digest(kernel_root / name) for name in KERNEL_INPUTS}:
        raise ValueError("Canonical monitor, prototype, harness or kernel inputs changed during the proof")
    report = {
        "status": "pass", "mode": "synthetic_exact_kernel_monitor_producer_host_only", "source_sha256": before,
        "kernel_source_sha256": kernel_before, "kernel_producer_sha256": hashlib.sha256(producer.group(0).encode()).hexdigest(),
        "unit_tests": int(re.search(r"test result: ok\. (\d+) passed", unit_output).group(1)),
        "producer_records": len(cases), "producer_input_sha256": hashlib.sha256(stream).hexdigest(),
        "producer_output_sha256": hashlib.sha256(produced).hexdigest(), "producer_output_bytes": len(produced),
        "fixture_sha256": hashlib.sha256(fixture).hexdigest(), "arbitrary_fuzz_records": 20000,
        "valid_fuzz_records": 20000, "monitor_word_bit_faults": 32, "fault_controls": mutants,
        "input_hashes_unchanged": True, "owned_process_groups": groups,
        "limits": [
            "The actual fsm_monitor_send_message producer and CCCI header/message enum are extracted unchanged from hashed current kernel sources and run with substituted allocation/queue/wakeup/controller helpers under ASan/UBSan.",
            "The producer writes the complete u32 at offset8 to 0xf0000000. This is a virtual monitor record, not an ordinary u16 CCCI channel plus independently meaningful sequence/assert fields.",
            "The source-only monitor read audit proves short caller buffers or copy faults return EFAULT after the dequeued skb is freed. This proof does not run that read function or establish live delivery; callers must request the complete record and must not retry a missing tail as a byte stream.",
            "The parser requires exactly16 bytes, exact magic and full monitor word. Message id and reserved remain owned scalar values; known kind names are classification only and unknown ids are explicit Unsupported.",
            "All records are synthetic kernel-producer controls. No monitor fd, start/stop/reset/flight command, NVRAM/config/battery action, backend or hardware behavior is implemented or exercised.",
            "Existing mdinit/prototypes, Android.bp and production selection are unchanged. Native Android/Soong outputs, I/O ownership, init/SELinux and the modem lifecycle remain separate acceptance work.",
        ],
    }
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
