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
    "mindone/modules/ccci_md_all/port/port_rpc.c",
    "mindone/modules/ccci_md_all/port/port_rpc.h",
    "mindone/modules/ccci_md_all/port/port_proxy.c",
    "mindone/modules/ccci_md_all/port/port_cfg.c",
    "mindone/modules/ccci_md_all/inc/ccci_core.h",
    "mindone/modules/include/drivers/misc/mediatek/include/mt-plat/mtk_ccci_common.h",
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract_function(source, name):
    match = re.search(r"^static void " + re.escape(name) + r"\([^;{]+\)\s*\{.*?^\}", source, re.M | re.S)
    if not match:
        raise ValueError(f"Cannot find exact kernel function: {name}")
    return match.group(0)


def extract_struct(source, name):
    match = re.search(r"^struct " + re.escape(name) + r" \{.*?^\} __packed;", source, re.M | re.S)
    if not match:
        raise ValueError(f"Cannot find exact kernel struct: {name}")
    return match.group(0)


def packet(opcode, arguments, data0=0x12345678, data1=0, sequence=0x9234, index=0, tail=b""):
    output = bytearray(struct.pack("<IIHHIII", data0, data1, 32, sequence, index, opcode, len(arguments)))
    for argument in arguments:
        output.extend(struct.pack("<I", len(argument)))
        output.extend(argument)
        output.extend(bytes((-len(argument)) % 4))
    output.extend(tail)
    return bytes(output)


def records(values):
    return b"".join(struct.pack("<I", len(value)) + value for value in values)


def split_records(output):
    result = []
    offset = 0
    while offset < len(output):
        if len(output) - offset < 4:
            raise ValueError("Truncated replay output prefix")
        length, = struct.unpack_from("<I", output, offset)
        offset += 4
        if length > len(output) - offset:
            raise ValueError("Truncated replay output record")
        result.append(output[offset:offset + length])
        offset += length
    return result


def main():
    parser = argparse.ArgumentParser(description="Check a safe complete-record CCCI RPC wire core against exact current kernel serialization with synthetic requests and substituted operation/I/O helpers; no device or backend is accessed")
    parser.add_argument("--kernel-root", required=True, type=Path)
    parser.add_argument("--rustc", default=os.environ.get("RUSTC", "rustc"))
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--fault-controls", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source_root = tests.parent
    kernel_root = args.kernel_root.resolve()
    inputs = [source_root / "wire.rs", tests / "wire.rs", tests / "replay.rs", tests / "replay.c", tests / "check.py"]
    fixed = [source_root.parent / "rpcd/ccci_rpcd.c", source_root.parent / "Android.bp",
             source_root / "../../../volte_md_status/tests/android_check.py"]
    before = {str(path): digest(path) for path in inputs + fixed}
    kernel_before = {name: digest(kernel_root / name) for name in KERNEL_INPUTS}
    rpc = (kernel_root / KERNEL_INPUTS[0]).read_text()
    header = (kernel_root / KERNEL_INPUTS[1]).read_text()
    proxy = (kernel_root / KERNEL_INPUTS[2]).read_text()
    port_cfg = (kernel_root / KERNEL_INPUTS[3]).read_text()
    core = (kernel_root / KERNEL_INPUTS[4]).read_text()
    common = (kernel_root / KERNEL_INPUTS[5]).read_text()
    handler = extract_function(rpc, "rpc_msg_handler")
    if "if (skb->len > RPC_MAX_BUF_SIZE)" not in handler:
        raise ValueError("Cannot prove the full-record input bound from the exact kernel handler")
    if not re.search(r"^\s*CCCI_RPC_RX = 32,\n\s*CCCI_RPC_TX = 33,", core, re.M):
        raise ValueError("Kernel RPC channels changed")
    if "do NOT assume data[1] is data length in Rx" not in common:
        raise ValueError("Kernel RX length contract changed")
    if not re.search(r"\{CCCI_RPC_TX, CCCI_RPC_RX,[^{}]+PORT_F_USER_HEADER \| PORT_F_WITH_CHAR_NODE,\s*&rpc_port_ops, 20, \"ccci_rpc\"", port_cfg):
        raise ValueError("Kernel userspace RPC port header ownership changed")
    for required in ["port->flags |= PORT_F_ADJUST_HEADER;", ".read = &port_dev_read", ".write = &port_dev_write"]:
        if required not in rpc:
            raise ValueError(f"Kernel userspace RPC contract changed: {required}")
    for required in ["if (port->flags & PORT_F_USER_HEADER)", "ccci_h->data[1] = actual_count;", "ccci_h->channel = port->tx_ch;", "read_len = count;", "skb_pull(skb, read_len);"]:
        if required not in proxy:
            raise ValueError(f"Kernel character-port contract changed: {required}")
    constants = {}
    for name in ["RPC_REQ_BUFFER_NUM", "RPC_MAX_ARG_NUM", "RPC_MAX_BUF_SIZE", "RPC_API_RESP_ID"]:
        match = re.search(r"^#define\s+" + name + r"\s+(0x[0-9a-fA-F]+|\d+)\b", header, re.M)
        if not match:
            raise ValueError(f"Cannot find kernel RPC constant: {name}")
        constants[name] = int(match.group(1), 0)
    if list(constants.values()) != [2, 6, 2048, 0xffff0000]:
        raise ValueError("Kernel RPC bounds changed")
    userspace_operations = {"IPC_RPC_QUERY_AP_SYS_PROPERTY": 0x400f, "IPC_RPC_SAR_TABLE_IDX_QUERY_OP": 0x4010,
                            "IPC_RPC_AMMS_DRDI_CONTROL": 0x4014, "IPC_RPC_SAVE_MD_CAPID": 0x4015}
    for name, value in userspace_operations.items():
        if not re.search(r"^\s*" + name + r" = 0x" + f"{value:X}" + r",$", header, re.M):
            raise ValueError(f"Kernel standard userspace operation changed: {name}")
    route = rpc.split("int port_rpc_recv_match(", 1)[1].split("struct port_ops rpc_port_ops", 1)[0]
    standard_route = re.sub(r"#ifdef CONFIG_MTK_TC1_FEATURE.*?#endif", "", route, flags=re.S)
    if set(re.findall(r"case (IPC_RPC_[A-Z0-9_]+):", standard_route)) != set(userspace_operations):
        raise ValueError("The kernel standard userspace routing switch changed")
    template = (tests / "replay.c").read_text()
    substitutions = {
        "CONSTANTS": "\n".join(f"#define {name} {value}u" for name, value in constants.items()) + "\n#define CCCI_RPC_TX 33",
        "HEADER": extract_struct(common, "ccci_header"),
        "RPC_PACKET": extract_struct(header, "rpc_pkt"),
        "RPC_BUFFER": extract_struct(header, "rpc_buffer"),
        "HANDLER": handler,
    }
    for name, value in substitutions.items():
        token = "@" + name + "@"
        if template.count(token) != 1:
            raise ValueError(f"Expected one exact replay substitution: {token}")
        template = template.replace(token, value)
    stop_process = runpy.run_path(str(fixed[2]))["stop_process"]
    groups = []

    def execute(command, data=None, seconds=60, check=True):
        child = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
        try:
            stdout, stderr = child.communicate(input=data, timeout=seconds)
        except BaseException:
            stop_process(child)
            child.communicate(timeout=1)
            raise
        survivors = stop_process(child)
        groups.append({"group": child.pid, "exit_code": child.returncode, "empty_after_cleanup": not survivors})
        if survivors:
            raise ValueError(f"A completed RPC check left live descendants: {child.pid}")
        result = subprocess.CompletedProcess(command, child.returncode, stdout, stderr)
        if check and result.returncode:
            print(stdout.decode(errors="replace"), end="")
            print(stderr.decode(errors="replace"), end="")
            result.check_returncode()
        return result

    random_source = random.Random(7294015)
    valid = [packet(0x400f, [], data1=value, index=index) for value in [0, 1, 24, 2048, 0xffffffff] for index in range(3)]
    valid += [packet(0x4015, [bytes(length)], data1=0xffffffff) for length in range(2021)]
    valid += [packet(0x4010, [b"", b"a", b"bc", b"def", b"ghij", b"klmno"], index=2)]
    for _ in range(4096):
        arguments = [random_source.randbytes(random_source.randrange(97)) for _ in range(random_source.randrange(7))]
        valid.append(packet(random_source.choice([0x2001, 0x400f, 0x4010, 0x4014, 0x4015, 0x4321, 0xffff]), arguments,
                            data0=random_source.randrange(0x100000000), data1=random_source.randrange(0x100000000),
                            sequence=random_source.randrange(65536), index=random_source.randrange(3)))
    stream = records(valid)
    controls = []
    type_controls = []
    mutations = []
    with tempfile.TemporaryDirectory(prefix="mindone-rpc-wire-") as directory:
        output = Path(directory)
        library = output / "libmindone_rpc_wire.rlib"
        units = output / "wire-test"
        dependency = ["--extern", f"mindone_rpc_wire={library}"]

        def compile_library(source):
            execute([args.rustc, "--edition=2021", "-Dwarnings", "--crate-type=rlib", "--crate-name=mindone_rpc_wire", str(source), "-o", str(library)])

        def compile_units():
            execute([args.rustc, "--edition=2021", "-Dwarnings", "--test", str(tests / "wire.rs"), *dependency, "-o", str(units)])

        compile_library(source_root / "wire.rs")
        compile_units()
        unit_output = execute([str(units), "--nocapture"]).stdout.decode()
        print(unit_output, end="")
        replay = output / "kernel-replay.c"
        replay.write_text(template)
        execute([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-pointer-sign", "-Wno-sign-compare", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(replay), "-o", str(output / "kernel-replay")])
        execute([args.rustc, "--edition=2021", "-Dwarnings", str(tests / "replay.rs"), *dependency, "-o", str(output / "rust-replay")])
        previous = execute([str(output / "kernel-replay")], stream).stdout
        candidate = execute([str(output / "rust-replay")], stream).stdout
        if previous != candidate:
            left, right = split_records(previous), split_records(candidate)
            mismatch = next((index for index, pair in enumerate(zip(left, right)) if pair[0] != pair[1]), min(len(left), len(right)))
            raise ValueError(f"Exact kernel/Rust generic wire mismatch at record {mismatch}")
        cases = [
            ("trailing-byte-is-ignored-by-kernel", packet(0x400f, [], tail=b"x"), 24),
            ("missing-final-pad-is-extended-by-kernel", packet(0x4010, [b"x"])[:-3], 32),
            ("full-record-over-2048-is-rejected-by-kernel", packet(0x4014, [bytes(2021)]), 0),
            ("kernel-accepts-six-zero-length-arguments", packet(0x4015, [b""] * 6, index=2), 48),
        ]
        for name, data, expected in cases:
            result = execute([str(output / "kernel-replay")], records([data])).stdout
            replies = split_records(result)
            if len(replies) != 1 or len(replies[0]) != expected:
                raise ValueError(f"Exact kernel serialization control changed: {name}")
            controls.append({"name": name, "request_bytes": len(data), "response_bytes": expected, "output_sha256": hashlib.sha256(result).hexdigest()})
        for name, error, positive, negative in [
            ("request-pins-record", "E0506", "let count=request.argument_count();\nprintln!(\"{}\",count);\nbytes[0]=1;", "bytes[0]=1;\nprintln!(\"{}\",request.argument_count());"),
            ("borrowed-argument-pins-record", "E0506", "let argument=request.argument(0).unwrap();\nprintln!(\"{}\",argument.len());\nbytes[0]=1;", "let argument=request.argument(0).unwrap();\nbytes[0]=1;\nprintln!(\"{}\",argument.len());"),
            ("request-has-no-payload-debug", "E0277", "println!(\"{:?}\",request.header());", "println!(\"{:?}\",request);"),
        ]:
            path = output / (name + ".rs")
            prefix = 'use mindone_rpc_wire::Request;\nfn main(){\nlet mut bytes=[0u8;28];\nbytes[8]=32;\nbytes[20]=1;\nlet request=Request::parse(&bytes).unwrap();\n'
            suffix = '\nprintln!("{}",bytes[0]);\n}\n'
            path.write_text(prefix + positive + suffix)
            command = [args.rustc, "--edition=2021", "-Dwarnings", str(path), *dependency, "-o", str(output / name)]
            execute(command)
            execute([str(output / name)])
            path.write_text(prefix + negative + suffix)
            rejected = execute(command, check=False)
            if rejected.returncode == 0 or error not in rejected.stderr.decode():
                raise ValueError(f"Compile-enforced RPC borrow control failed: {name}")
            type_controls.append(name)
        if args.fault_controls:
            original = (source_root / "wire.rs").read_text()
            variants = (
                ("rx-data1-treated-as-record-length", "if header.channel != RX_CHANNEL", "if header.data1 as usize != input.len() { return Err(Error::Truncated); }\n        if header.channel != RX_CHANNEL"),
                ("wrong-input-byte-order", "u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]])", "u32::from_be_bytes([bytes[0], bytes[1], bytes[2], bytes[3]])"),
                ("accepted-seventh-argument", "if count > MAX_ARGUMENTS", "if false && count > MAX_ARGUMENTS"),
                ("rejected-source-index-two", "header.request_index > MAX_REQUEST_INDEX", "header.request_index >= MAX_REQUEST_INDEX"),
                ("accepted-wrong-channel", "if header.channel != RX_CHANNEL", "if false && header.channel != RX_CHANNEL"),
                ("accepted-response-as-request", "if opcode & RESPONSE_MASK != 0", "if false && opcode & RESPONSE_MASK != 0"),
                ("lost-argument-alignment", "offset += aligned(length);", "offset += length;"),
                ("ignored-record-tail", "if offset != input.len()", "if false && offset != input.len()"),
                ("incomplete-response-opcode", "request.opcode | RESPONSE_MASK", "request.opcode"),
                ("lost-response-sequence", "u32::from(request.header.sequence) << 16", "0 * u32::from(request.header.sequence) << 16"),
                ("uninitialized-response-padding", "output[..length].fill(0);", "let _ = output.len();"),
                ("output-mutation-before-validation", "if arguments.len() > MAX_ARGUMENTS", "if !output.is_empty() { output[0] = 0; }\n    if arguments.len() > MAX_ARGUMENTS"),
            )
            for name, old, new in variants:
                if original.count(old) != 1:
                    raise ValueError(f"Expected one exact RPC mutation site: {name}")
                source = output / "wire.rs"
                source.write_text(original.replace(old, new, 1))
                compile_library(source)
                compile_units()
                result = execute([str(units)], check=False, seconds=30)
                if result.returncode == 0 or b"test result: FAILED" not in result.stdout:
                    raise ValueError(f"Compiled RPC behavior mutant was not rejected: {name}")
                mutations.append(name)
                print(f"fault-control {name}: rejected")
    if before != {str(path): digest(path) for path in inputs + fixed} or kernel_before != {name: digest(kernel_root / name) for name in KERNEL_INPUTS}:
        raise ValueError("Canonical RPC, prototype, harness or kernel inputs changed during the proof")
    report = {
        "status": "pass", "mode": "synthetic_exact_kernel_generic_RPC_wire_host_only", "source_sha256": before,
        "kernel_source_sha256": kernel_before, "exact_kernel_constants": constants,
        "standard_userspace_operation_ids": userspace_operations,
        "extracted_kernel_warning_exceptions": ["-Wno-pointer-sign", "-Wno-sign-compare"],
        "unit_tests": int(re.search(r"test result: ok\. (\d+) passed", unit_output).group(1)),
        "valid_differential_records": len(valid), "input_sha256": hashlib.sha256(stream).hexdigest(),
        "identical_output_sha256": hashlib.sha256(previous).hexdigest(), "identical_output_bytes": len(previous),
        "arbitrary_fuzz_records": 20000, "valid_fuzz_roundtrips": 20000, "payload_length_boundaries": 2021,
        "exact_kernel_controls": controls, "type_controls": type_controls, "fault_controls": mutations,
        "input_hashes_unchanged": True, "owned_process_groups": groups,
        "limits": [
            "Synthetic complete RPC records exercise the exact current rpc_msg_handler and packed header definitions with substituted skb/allocation/send/operation helpers; these are not live firmware fixtures or backend semantics.",
            "The kernel input guard bounds skb->len to 2048 including CCCI16 and RPC8. The Rust parser and builder also bound the complete record and require every argument and its four-byte padding to fit it.",
            "That 2048 guard belongs to the in-kernel RPC sibling handler. The userspace char port bypasses it and has a CCCI_MTU-plus-header write bound; this library adopts the shared bounded profile, not a proven userspace firmware record-size contract.",
            "The raw kernel handler accepts trailing bytes and missing final padding under these substitutions. Rust deliberately rejects both; only complete fully padded records participate in differential parity.",
            "RX data[1] is opaque per the actual kernel header; no stream framing or accumulation by that word is provided. The caller must preserve complete CCCI record boundaries.",
            "Response serialization preserves data[0], sequence/assert bits and request index, sets channel33 and total data[1], and ORs the opcode with 0xffff0000. Rust zeroes padding rather than preserving arbitrary incoming padding.",
            "The four standard userspace opcode names are classification only. Other opcodes are explicitly Unsupported; CONFIG_MTK_TC1_FEATURE selection and conditional LGE operation semantics are not established by this proof.",
            "Property/CAPID/SAR/AMMS payload contracts and all NVRAM/libsysenv/filesystem/backend operations remain unimplemented; no response success or hardware behavior is inferred.",
            "Existing C prototypes, Android.bp and production selection are unchanged. There is no fd I/O owner, native target/Soong, init/SELinux, CCCI access, daemon or hardware acceptance.",
        ],
    }
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
