import argparse
import hashlib
import importlib.util
import json
import struct
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FLAGS = [
    "-std=c11", "-Os", "-ffreestanding", "-fno-builtin",
    "-fno-stack-protector", "-fno-pic", "-fno-common", "-fno-jump-tables",
    "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
    "-Wall", "-Wextra", "-Werror", "-mlittle-endian", "-march=v2", "-mabi=2",
    "-mno-fp-as-gp", "-mno-relax", "-mno-relax-hint", "-mno-v3push",
    "-mno-cmov", "-mno-hw-abs", "-mno-ext-perf", "-mno-ext-perf2",
    "-mno-ext-string", "-mno-ext-dsp", "-mno-unaligned-access",
    "-fno-tree-forwprop",
]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--gcc", required=True, type=Path)
    parser.add_argument("--binutils", required=True, type=Path)
    parser.add_argument("--simulator", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--suite", choices=["legacy", "multicast", "table", "policy"], default="legacy")
    args = parser.parse_args()
    if not 1 <= args.timeout <= 600:
        parser.error("timeout must be between 1 and 600 seconds")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location("target", ROOT / "tools/wifi/rxfilter-target.py")
    target_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(target_module)
    target = target_module.TargetImage(args.firmware.read_bytes())
    binary = args.binutils.resolve()
    assembler = binary / "gas/as-new"
    linker = binary / "ld/ld-new"
    objcopy = binary / "binutils/objcopy"
    nm = binary / "binutils/nm-new"
    compiler = args.gcc.resolve()
    commands = []
    receipt = {
        "suite": args.suite,
        "runner_sha256": digest(__file__),
        "firmware_sha256": digest(args.firmware),
        "compiler_sha256": digest(compiler),
        "simulator_sha256": digest(args.simulator),
        "commands": commands,
        "deployment_image_created": False,
        "limits": [
            "NDS32 instruction simulation, not hardware validation",
            "Debug output, address counting, multicast and reason hooks are explicit stubs",
            "Valid mapped descriptor, station, BSS and packet backing",
        ],
    }

    def run(argv, name, timeout=30):
        command = [str(part) for part in argv]
        result = subprocess.run(command, capture_output=True, timeout=timeout)
        (out / (name + ".stdout")).write_bytes(result.stdout)
        (out / (name + ".stderr")).write_bytes(result.stderr)
        commands.append({"argv": command, "exit_code": result.returncode})
        if result.returncode:
            raise RuntimeError(name + " failed: " + result.stderr.decode(errors="replace")[:2000])
        return result.stdout

    def symbols(path):
        lines = run([nm, path], path.stem + "-symbols").decode().splitlines()
        return {line.split()[2]: int(line.split()[0], 16)
                for line in lines if len(line.split()) == 3}

    try:
        sources = [ROOT / "tools/rigs/wifi-rxfilter-sim-check.c",
                   ROOT / "tools/wifi/rxfilter-legacy.c", ROOT / "tools/wifi/rxfilter-config.c"]
        bindings = [(0xF036B81C, 1244), (0xF032A398, 228),
                    (0xF036B6E0, 116), (0xF036B754, 84)]
        stub_targets = [(0xF02F120C, "sim_memcmp"), (0xF036BCF8, "sim_count"),
                        (0xF032A1B4, "sim_multicast"), (0xF0317CEC, "sim_reason"),
                        (0xF0319120, None), (0xF0318D74, None)]
        minimum_cases, result_words = 7098624, 8
        if args.suite == "multicast":
            sources = [ROOT / "tools/rigs/wifi-rxfilter-mcast-sim-check.c",
                       ROOT / "tools/wifi/rxfilter-mcast.c"]
            bindings = [(0xF032A1B4, 484), (0xF032AAE8, 44)]
            stub_targets = [(0xF02F120C, "sim_memcmp")]
            minimum_cases, result_words = 111104, 12
            receipt["limits"][1] = "memcmp is the only explicit stub; the control getter is original"
        if args.suite == "table":
            sources = [ROOT / "tools/rigs/wifi-rxfilter-table-check.c",
                       ROOT / "tools/wifi/rxfilter-table.c"]
            bindings = [(0xF032AA64, 132), (0xF032B564, 176)]
            stub_targets = [(0xF02F120C, "sim_memcmp"), (0xF02F1224, "sim_memcpy")]
            minimum_cases, result_words = 360584, 12
            receipt["limits"][1] = "memcmp and memcpy are the only explicit stubs"
        if args.suite == "policy":
            sources = [ROOT / "tools/rigs/wifi-rxfilter-policy-check.c",
                       ROOT / "tools/rigs/wifi-rxfilter-policy-sim-runtime.c",
                       ROOT / "tools/wifi/rxfilter-policy.c"]
            bindings, stub_targets = [], []
            minimum_cases, result_words = 34495, 8
            receipt["limits"] = [
                "New policy scenarios executed as NDS32 instructions, not a legacy oracle",
                "Fixture-only single-allocation heap and I/O runtime",
                "Truncation memory safety is checked separately by host ASan/UBSan",
            ]
            (out / "stdio.h").write_text("int printf(const char*, ...);\n")
            (out / "stdlib.h").write_text("#include <stddef.h>\nvoid* malloc(size_t);\nvoid free(void*);\n")
            (out / "string.h").write_text("#include <stddef.h>\n"
                "void* memcpy(void*, const void*, size_t);\n"
                "void* memmove(void*, const void*, size_t);\nvoid* memset(void*, int, size_t);\n")
        headers = [source.with_suffix(".h") for source in sources if source.parent.name == "wifi"]
        receipt["source_sha256"] = {str(source.relative_to(ROOT)): digest(source)
                                    for source in sources + headers}
        objects = []
        for source in sources:
            obj = out / (source.stem + ".o")
            run([compiler, "-B" + str(compiler.parent) + "/", *FLAGS,
                 "-I", out, "-c", source, "-o", obj], source.stem + "-compile")
            objects.append(obj)
        start = out / "start.s"
        start.write_text(".text\n.global _start\n_start:\n li $sp,0x200000\n"
                         " la $gp,_SDA_BASE_\n bal main\n syscall 1\n")
        run([assembler, "-EL", "-march=v2", "-mabi=v2", "-o", out / "start.o", start], "startup")
        objects.insert(0, out / "start.o")
        script = out / "simulation.ld"
        script.write_text("ENTRY(_start)\n"
                          "PROVIDE(rxfilter_original_count = 0xf036bcf8);\n"
                          "PROVIDE(rxfilter_original_multicast = 0xf032a1b4);\n"
                          "PROVIDE(rxfilter_original_reason = 0xf0317cec);\n"
                          "SECTIONS { . = 0xf0400000; "
                          ".text : { *(.text*) } .rodata : { *(.rodata*) } "
                          ".data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } "
                          ".fw 0xf02f0000 : { *(.fw) } .fwdata 0xf0386000 : { *(.fwdata) } }\n")
        link_args = [linker, "--no-relax", "-T", script]
        preliminary = out / "preliminary.elf"
        run([*link_args, "-o", preliminary, *objects], "preliminary-link")
        bound = symbols(preliminary)
        firmware = bytearray(target.read(0xF02F0000, 520464))
        stubs = []
        for address, name in stub_targets:
            assembly = "ret $lp\n" if name is None else f"li $r15,0x{bound[name]:x}\njr $r15\n"
            (out / "stub.s").write_text(".text\n" + assembly)
            run([assembler, "-EL", "-march=v2", "-mabi=v2", "-o", out / "stub.o", out / "stub.s"],
                f"stub-{address:x}-assemble")
            run([objcopy, "-O", "binary", "-j", ".text", out / "stub.o", out / "stub.bin"],
                f"stub-{address:x}-extract")
            replacement = (out / "stub.bin").read_bytes()
            offset = address - 0xF02F0000
            firmware[offset:offset + len(replacement)] = replacement
            stubs.append({"address": address, "symbol": name, "bytes": replacement.hex()})
        for address, size in bindings:
            offset = address - 0xF02F0000
            if firmware[offset:offset + size] != target.read(address, size):
                raise RuntimeError("Original function bytes changed")
        receipt["stubs"] = stubs
        image = out / "simulation-only-text.bin"
        image.write_bytes(firmware)
        data_image = out / "simulation-only-data.bin"
        data_image.write_bytes(target.read(0xF0386000, 163392))
        quoted = str(image).replace("\\", "\\\\").replace('"', '\\"')
        quoted_data = str(data_image).replace("\\", "\\\\").replace('"', '\\"')
        (out / "firmware.s").write_text('.section .fw,"ax"\n.incbin "' + quoted +
                                        '"\n.section .fwdata,"aw"\n.incbin "' + quoted_data + '"\n')
        run([assembler, "-EL", "-march=v2", "-mabi=v2", "-o", out / "firmware.o", out / "firmware.s"], "firmware-assemble")
        executable = out / "simulation.elf"
        run([*link_args, "-o", executable, *objects, out / "firmware.o"], "simulation-link")
        final_symbols = symbols(executable)
        if any(final_symbols.get(stub["symbol"]) != bound[stub["symbol"]]
               for stub in stubs if stub["symbol"]):
            raise RuntimeError("Stub destination changed during final link")
        command = [str(args.simulator.resolve()), "--memory-region=0,0x400000",
                   "--memory-region=0xf0200000,0x400000", str(executable)]
        result = subprocess.run(command, capture_output=True, timeout=args.timeout)
        (out / "simulation.stdout").write_bytes(result.stdout)
        (out / "simulation.stderr").write_bytes(result.stderr)
        commands.append({"argv": command, "exit_code": result.returncode})
        if len(result.stdout) != result_words * 4:
            raise RuntimeError("Simulator did not return the complete result record")
        values = struct.unpack("<" + str(result_words) + "I", result.stdout)
        receipt["result"] = dict(zip(["cases", "failures", "original", "replacement",
                                      "case_a", "case_b", "case_c", "case_d",
                                      "case_e", "case_f", "case_g", "case_h"], values))
        receipt["passed"] = result.returncode == 0 and values[0] >= minimum_cases and values[1] == 0
        if not receipt["passed"]:
            raise RuntimeError("Original and replacement execution differ")
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        receipt["passed"] = False
        receipt["error"] = str(error)
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({key: receipt[key] for key in ["passed", "result", "error"] if key in receipt}))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
