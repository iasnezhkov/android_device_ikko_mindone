import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
TEXT_BASE = 0xF02F0000
TEXT_SIZE = 520464


def digest(data):
    return hashlib.sha256(data).hexdigest()


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def validate(target, manifest, elf, builder):
    if manifest.get("schema") != 1 or manifest.get("passed") is not True or manifest.get("proof_only") is not True:
        raise ValueError("A successful strict placement proof is required")
    if manifest.get("deployment_image_created") is not False:
        raise ValueError("Expected a proof-only artifact")
    if manifest.get("firmware_sha256") != digest(target.data):
        raise ValueError("Firmware binding mismatch")
    if manifest.get("proof_elf_sha256") != digest(elf.data):
        raise ValueError("Proof ELF binding mismatch")
    if manifest.get("aliases") != builder.ALIASES:
        raise ValueError("Original helper bindings changed")
    if elf.undefined() or any(s["kind"] in (4, 9) and s["size"] for s in elf.sections):
        raise ValueError("Proof ELF contains unresolved symbols or relocations")
    windows = manifest.get("windows", [])
    if len(windows) != len(builder.WINDOWS):
        raise ValueError("Expected all nine proven windows")
    symbols = {s["name"]: s["value"] for s in elf.symbols if s["name"] and s["section"]}
    sections = elf.allocated()
    for index, (address, size, entry) in enumerate(builder.WINDOWS):
        item = windows[index]
        if (item.get("address"), item.get("size"), item.get("entry")) != (address, size, entry):
            raise ValueError("Proven window identity changed")
        if item.get("original_sha256") != digest(target.read(address, size)):
            raise ValueError("Original window bytes mismatch")
        destination = symbols.get(entry)
        if item.get("replacement_entry") != destination or symbols.get("rxfilter_entry_" + str(index)) != address:
            raise ValueError("Entry symbol binding mismatch")
        containing = [s for s in sections if s["address"] <= address and address + 4 <= s["address"] + s["size"]]
        if len(containing) != 1:
            raise ValueError("Entry is not covered by exactly one allocated section")
        if destination != address:
            section = containing[0]
            word = int.from_bytes(elf.read(section["offset"] + address - section["address"], 4), "big")
            delta = word & 0xFFFFFF
            if delta & 0x800000:
                delta -= 0x1000000
            if word & 0xFF000000 != 0x48000000 or (address + delta * 2) & 0xFFFFFFFF != destination:
                raise ValueError("Entry veneer does not reach the replacement")
    intervals = []
    for section in sections:
        address, size = section["address"], section["size"]
        if section["kind"] != 1 or section["flags"] & ~6 or not section["name"].startswith(".rxfilter."):
            raise ValueError("Unexpected allocated proof section")
        if sum(base <= address and address + size <= base + length for base, length, _ in builder.WINDOWS) != 1:
            raise ValueError("Section escapes proven windows")
        if address % section["alignment"]:
            raise ValueError("Unaligned proof section")
        if any(address < end and start < address + size for start, end in intervals):
            raise ValueError("Overlapping proof sections")
        intervals.append((address, address + size))
    for name, address in builder.ALIASES.items():
        if symbols.get(name) != address:
            raise ValueError("Linked helper alias mismatch")
    expected_sources = {"tools/wifi/rxfilter-" + unit + "." + extension
                        for unit in builder.UNITS for extension in ("c", "h")}
    if set(manifest.get("sources", {})) != expected_sources:
        raise ValueError("Source binding set differs")
    for relative, sha in manifest["sources"].items():
        path = ROOT / relative
        if path.resolve().parent != (ROOT / "tools/wifi").resolve() or digest(path.read_bytes()) != sha:
            raise ValueError("Canonical source binding mismatch: " + relative)
    return symbols


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--proof", required=True, type=Path)
    parser.add_argument("--gcc", required=True, type=Path)
    parser.add_argument("--binutils", required=True, type=Path)
    parser.add_argument("--simulator", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 600:
        parser.error("timeout must be between 1 and 600 seconds")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    fixture = ROOT / "tools/rigs/wifi-rxfilter-image-check.c"
    runner = Path(__file__).resolve()
    builder_path = ROOT / "tools/wifi/rxfilter-build.py"
    target_path = ROOT / "tools/wifi/rxfilter-target.py"
    receipt = dict(suite="linked-firmware-composition", passed=False, deployment_image_created=False,
                   fixture_sha256=digest(fixture.read_bytes()), runner_sha256=digest(runner.read_bytes()),
                   builder_sha256=digest(builder_path.read_bytes()), target_guard_sha256=digest(target_path.read_bytes()),
                   limits=["NDS32 instruction simulation, not Wi-Fi hardware or driver validation",
                           "Only fixture is compiled; all nine replacement entrypoints execute the intact strict placement ELF over original firmware text",
                           "Allocator, hardware reconfigure, diagnostics and hardware/event callbacks are explicit stubs outside replacement windows",
                           "Mapped full descriptor backing remains the original legacy classifier precondition; advertised packet lengths test the new bounded parser",
                           "Hardware GP value, startup mapping and numerical alias relationships are unmeasured; two synthetic GP mappings test preservation of the original relative callback contract"],
                   hardware_gp_known=False, gp_runs=[],
                   commands=[], negative_controls=[])

    def run(argv, label):
        command = [str(value) for value in argv]
        result = subprocess.run(command, capture_output=True, timeout=args.timeout)
        (out / (label + ".stdout")).write_bytes(result.stdout)
        (out / (label + ".stderr")).write_bytes(result.stderr)
        receipt["commands"].append(dict(argv=command, exit_code=result.returncode))
        if result.returncode:
            raise ValueError(label + " failed: " + result.stderr.decode(errors="replace")[:2000])
        return result.stdout

    try:
        builder = module("rxfilter_builder", builder_path)
        target_module = module("rxfilter_target", target_path)
        firmware_data = args.firmware.read_bytes()
        target = target_module.TargetImage(firmware_data)
        proof = args.proof.resolve()
        manifest_path, elf_path = proof / "manifest.json", proof / "proof.elf"
        manifest_bytes = manifest_path.read_bytes()
        manifest = json.loads(manifest_bytes)
        elf = builder.Elf(elf_path, 2)
        bound = validate(target, manifest, elf, builder)
        receipt.update(firmware_sha256=digest(firmware_data), proof_elf_sha256=digest(elf.data),
                       proof_manifest_sha256=digest(manifest_bytes), placement_manifest_sha256=digest(manifest_bytes), proof_manifest_path=str(manifest_path),
                       proof_elf_path=str(elf_path), sources=manifest["sources"], windows=manifest["windows"])
        for name in ("passed", "firmware_sha256", "proof_elf_sha256", "windows", "aliases"):
            invalid = copy.deepcopy(manifest)
            invalid[name] = False if name == "passed" else ([] if name == "windows" else {})
            try:
                validate(target, invalid, elf, builder)
            except ValueError:
                receipt["negative_controls"].append(dict(name="guard-" + name, rejected=True))
            else:
                raise ValueError("Input negative control was accepted: " + name)
        compiler = args.gcc.resolve()
        binary = args.binutils.resolve()
        assembler, linker = binary / "gas/as-new", binary / "ld/ld-new"
        objcopy, nm = binary / "binutils/objcopy", binary / "binutils/nm-new"
        receipt["tools"] = {name: dict(path=str(path), sha256=digest(path.read_bytes())) for name, path in
                            (("gcc", compiler), ("assembler", assembler), ("linker", linker),
                             ("objcopy", objcopy), ("simulator", args.simulator.resolve()))}
        run([compiler, "-B" + str(compiler.parent) + "/", *builder.FLAGS, "-mcmodel=large",
             "-c", fixture, "-o", out / "fixture.o"], "fixture-compile")
        fixture_disassembly = run([binary / "binutils/objdump", "-dr", out / "fixture.o"], "fixture-disassembly").decode()
        if ".gp\t" in fixture_disassembly or "R_NDS32_SDA" in fixture_disassembly:
            raise ValueError("Fixture globals unexpectedly depend on GP")
        (out / "start.s").write_text(".text\n.global _start\n_start:\n li $sp,0x200000\n la $gp,_SDA_BASE_\n bal main\n syscall 1\n")
        run([assembler, "-EL", "-march=v2", "-mabi=v2", "-mreduced-regs", "-o", out / "start.o", out / "start.s"], "startup")
        script = out / "simulation.ld"
        def link_gp(value):
            script.write_text(f"ENTRY(_start)\n_SDA_BASE_ = 0x{value:x};\nSECTIONS {{ . = 0xf0400000; "
                              ".text : { *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } "
                              ".bss : { *(.bss*) *(COMMON) } .fw 0xf02f0000 : { KEEP(*(.fw)) } "
                              ".fwdata 0xf0386000 : { KEEP(*(.fwdata)) } }\n")
        link_gp(0xF0408000)
        roots = ["sim_allocate", "sim_configure", "sim_assert_log"]
        link = [linker, "--no-relax", "--gc-sections", "-T", script,
                *[item for name in roots for item in ("-u", name)], out / "start.o", out / "fixture.o"]
        run([*link, "-o", out / "preliminary.elf"], "preliminary-link")

        def symbols(path):
            output = run([nm, path], path.stem + "-symbols").decode()
            return {parts[2]: int(parts[0], 16) for line in output.splitlines()
                    if len(parts := line.split()) == 3}

        fixture_symbols = symbols(out / "preliminary.elf")
        original = target.read(TEXT_BASE, TEXT_SIZE)
        text = bytearray(original)
        for section in elf.allocated():
            offset = section["address"] - TEXT_BASE
            text[offset:offset + section["size"]] = elf.read(section["offset"], section["size"])
        stub_targets = [(0xF030548C, "sim_allocate"), (0xF032AC94, "sim_configure"),
                        (0xF0318ED8, "sim_assert_log"), (0xF0319120, None),
                        (0xF0318D74, None), (0xF0319060, None)]
        stubs = []

        def assemble(source, label):
            (out / (label + ".s")).write_text(".text\n" + source)
            run([assembler, "-EL", "-march=v2", "-mabi=v2", "-mreduced-regs", "-o", out / (label + ".o"), out / (label + ".s")], label + "-assemble")
            run([objcopy, "-O", "binary", "-j", ".text", out / (label + ".o"), out / (label + ".bin")], label + "-extract")
            return (out / (label + ".bin")).read_bytes()

        for address, name in stub_targets:
            replacement = assemble("ret $lp\n" if name is None else f"li $r15,0x{fixture_symbols[name]:x}\njr $r15\n", f"stub-{address:x}")
            if any(address < base + size and base < address + len(replacement) for base, size, _ in builder.WINDOWS):
                raise ValueError("Stub overlaps a replacement window")
            text[address - TEXT_BASE:address - TEXT_BASE + len(replacement)] = replacement
            stubs.append(dict(address=address, symbol=name, bytes=replacement.hex()))
        receipt["stubs"] = stubs
        for section in elf.allocated():
            offset = section["address"] - TEXT_BASE
            if text[offset:offset + section["size"]] != elf.read(section["offset"], section["size"]):
                raise ValueError("Placed replacement bytes changed during composition")
        data_path = out / "simulation-only-data.bin"
        data_path.write_bytes(target.read(0xF0386000, 163392))

        def simulate(image, label, gp=0xF0408000):
            link_gp(gp)
            image_path = out / (label + "-simulation-only-text.bin")
            image_path.write_bytes(image)
            def quoted(path):
                return str(path).replace("\\", "\\\\").replace('"', '\\"')
            source = out / (label + "-firmware.s")
            source.write_text('.section .fw,"ax"\n.incbin "' + quoted(image_path) +
                              '"\n.section .fwdata,"aw"\n.incbin "' + quoted(data_path) + '"\n')
            obj, executable = out / (label + "-firmware.o"), out / (label + ".elf")
            run([assembler, "-EL", "-march=v2", "-mabi=v2", "-mreduced-regs", "-o", obj, source], label + "-assemble")
            run([*link, obj, "-o", executable], label + "-link")
            linked = symbols(executable)
            if any(linked.get(name) != fixture_symbols[name] for name in roots):
                raise ValueError("Fixture callback address changed at final link")
            argv = [str(args.simulator.resolve()), "--memory-region=0,0x400000", "--memory-region=0xf0200000,0x400000", str(executable)]
            execution = subprocess.run(argv, capture_output=True, timeout=args.timeout)
            (out / (label + ".stdout")).write_bytes(execution.stdout)
            (out / (label + ".stderr")).write_bytes(execution.stderr)
            receipt["commands"].append(dict(argv=argv, exit_code=execution.returncode))
            if len(execution.stdout) != 32:
                raise ValueError(label + " did not produce a complete fixture result")
            values = struct.unpack("<8I", execution.stdout)
            result = dict(zip(("checks", "failures", "actual", "expected", "line", "entry_gp", "case_b", "case_c"), values))
            if result["entry_gp"] != gp:
                raise ValueError("Fixture did not execute with the requested GP")
            result["exit_code"] = execution.returncode
            return result

        for gp in (0xF0398000, 0xF0408000):
            outcome = simulate(text, f"composition-{gp:x}", gp)
            receipt["gp_runs"].append(outcome)
            if outcome["exit_code"] or outcome["failures"] or outcome["checks"] < 10000:
                raise ValueError("Composed firmware did not pass all integration checks")
        receipt["result"] = receipt["gp_runs"][-1]
        allow = assemble("movi $r0,0\nret $lp\n", "mutant-return-allow")
        for name, address, replacement in (("family-filter-disabled", bound["rxfilter_packet"], allow),
                                            ("table-reset-disabled", 0xF032AA64, allow),
                                            ("legacy-reply-disabled", 0xF031C1C0, allow)):
            mutant = bytearray(text)
            offset = address - TEXT_BASE
            mutant[offset:offset + len(replacement)] = replacement
            outcome = simulate(mutant, name)
            if outcome["failures"] == 0 or outcome["exit_code"] == 0:
                raise ValueError("Semantic negative control was accepted: " + name)
            receipt["negative_controls"].append(dict(name=name, rejected=True, result=outcome))
        validate(target, json.loads(manifest_path.read_bytes()), builder.Elf(elf_path, 2), builder)
        if (digest(manifest_path.read_bytes()) != receipt["proof_manifest_sha256"] or
                digest(elf_path.read_bytes()) != receipt["proof_elf_sha256"] or
                digest(fixture.read_bytes()) != receipt["fixture_sha256"] or
                digest(runner.read_bytes()) != receipt["runner_sha256"] or
                digest(builder_path.read_bytes()) != receipt["builder_sha256"] or
                digest(target_path.read_bytes()) != receipt["target_guard_sha256"]):
            raise ValueError("Inputs changed during composition verification")
        receipt["passed"] = True
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        receipt["error"] = str(error)
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({key: receipt[key] for key in ("passed", "result", "error") if key in receipt}))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
