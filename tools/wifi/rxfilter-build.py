import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
WINDOWS = (
    (0xF032A1B4, 484, "rxfilter_legacy_mcast"),
    (0xF032A398, 228, "rxfilter_firmware_rx"),
    (0xF036B81C, 1244, "rxfilter_legacy_bitmap"),
    (0xF032B614, 448, "rxfilter_firmware_command"),
    (0xF031C1C0, 152, "rxfilter_firmware_legacy_reply"),
    (0xF032AA64, 132, "rxfilter_legacy_table_reset"),
    (0xF032B564, 176, "rxfilter_legacy_table_seek"),
    (0xF036B6E0, 116, "rxfilter_legacy_set_config"),
    (0xF036B754, 84, "rxfilter_legacy_init_config"),
)
ALIASES = {
    "rxfilter_original_allocate": 0xF030548C,
    "rxfilter_original_assert_log": 0xF0318ED8,
    "rxfilter_original_count": 0xF036BCF8,
    "rxfilter_original_multicast": 0xF032A1B4,
    "rxfilter_original_reason": 0xF0317CEC,
    "rxfilter_original_reconfigure": 0xF032AC94,
    "rxfilter_original_reset": 0xF032AA64,
    "rxfilter_original_seek": 0xF032B564,
}
UNITS = ("policy", "query", "firmware", "legacy", "mcast", "table", "config")
FLAGS = [
    "-std=c11", "-Os", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
    "-fno-pic", "-fno-common", "-fno-jump-tables", "-fno-asynchronous-unwind-tables",
    "-fno-unwind-tables", "-Wall", "-Wextra", "-Werror", "-mlittle-endian",
    "-march=v2", "-mabi=2", "-mreduced-regs", "-mno-fp-as-gp", "-mno-relax", "-mno-relax-hint",
    "-mno-v3push", "-mno-cmov", "-mno-hw-abs", "-mno-ext-perf", "-mno-ext-perf2",
    "-mno-ext-string", "-mno-ext-dsp", "-mno-unaligned-access", "-fno-tree-forwprop",
    "-ffunction-sections", "-fdata-sections",
]
LEGACY_FLAGS = ["-fno-guess-branch-probability", "-fno-tree-dominator-opts", "-fno-if-conversion"]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def align(value, alignment):
    if alignment < 1 or alignment & (alignment - 1):
        raise ValueError("Invalid section alignment")
    return (value + alignment - 1) & -alignment


class Elf:
    def __init__(self, path, expected_type):
        self.data = Path(path).read_bytes()
        if len(self.data) < 52:
            raise ValueError("Truncated ELF header")
        fields = struct.unpack_from("<16sHHIIIIIHHHHHH", self.data)
        ident, kind, machine, version, _, _, table, flags, size, _, _, stride, count, names = fields
        if ident[:7] != b"\x7fELF\x01\x01\x01" or kind != expected_type or machine != 167 or version != 1:
            raise ValueError("Expected little-endian ELF32 NDS32 artifact")
        if flags != 0x20010042 or size != 52 or stride != 40 or not count or names >= count:
            raise ValueError("Unexpected NDS32 v2 ABI or section header geometry")
        raw = [struct.unpack("<IIIIIIIIII", self.read(table + i * stride, stride)) for i in range(count)]
        strings = self.read(raw[names][4], raw[names][5])
        self.sections = []
        self.symbols = []
        for index, section in enumerate(raw):
            name, kind, flags, address, offset, length, link, info, alignment, entry_size = section
            item = dict(index=index, name=self.string(strings, name), kind=kind, flags=flags,
                        address=address, offset=offset, size=length, link=link, info=info,
                        alignment=max(alignment, 1), entry_size=entry_size)
            if kind != 8:
                self.read(offset, length)
            self.sections.append(item)
        for section in self.sections:
            if section["kind"] != 2:
                continue
            if section["entry_size"] != 16 or section["size"] % 16 or section["link"] >= count:
                raise ValueError("Invalid ELF symbol table")
            names_section = self.sections[section["link"]]
            names_data = self.read(names_section["offset"], names_section["size"])
            for offset in range(section["offset"], section["offset"] + section["size"], 16):
                name, value, size, info, other, index = struct.unpack("<IIIBBH", self.read(offset, 16))
                self.symbols.append(dict(name=self.string(names_data, name), value=value,
                                         size=size, info=info, other=other, section=index))

    def read(self, offset, length):
        if offset < 0 or length < 0 or offset > len(self.data) or length > len(self.data) - offset:
            raise ValueError("ELF range outside file")
        return self.data[offset:offset + length]

    @staticmethod
    def string(data, offset):
        if offset >= len(data):
            raise ValueError("ELF string offset outside table")
        end = data.find(b"\0", offset)
        if end < 0:
            raise ValueError("Unterminated ELF string")
        return data[offset:end].decode("ascii")

    def allocated(self):
        return [s for s in self.sections if s["flags"] & 2 and s["size"]]

    def undefined(self):
        return sorted({s["name"] for s in self.symbols if s["name"] and s["section"] == 0})


def pack(sections, anchors=(), limit=2000000):
    anchored_names = {".text." + WINDOWS[i][2] for i in anchors}
    ordered = sorted((s for s in sections if s["name"] not in anchored_names),
                     key=lambda s: (-s["size"], -s["alignment"], s["name"]))
    ends = [base + 4 for base, _, _ in WINDOWS]
    capacities = [base + size for base, size, _ in WINDOWS]
    failed = set()
    placements = []
    nodes = 0
    for index in anchors:
        base, _, entry = WINDOWS[index]
        section = next(s for s in sections if s["name"] == ".text." + entry)
        if align(base, section["alignment"]) != base:
            return None, 0
        ends[index] = base + section["size"]
        placements.append(dict(section=section["name"], address=base, size=section["size"],
                               alignment=section["alignment"], window=index))

    def visit(index):
        nonlocal nodes
        nodes += 1
        if nodes > limit:
            raise ValueError("Placement search budget exhausted")
        if index == len(ordered):
            return True
        key = (index, tuple(ends))
        if key in failed:
            return False
        if sum(s["size"] for s in ordered[index:]) > sum(cap - end for cap, end in zip(capacities, ends)):
            return False
        section = ordered[index]
        candidates = []
        for window, end in enumerate(ends):
            start = align(end, section["alignment"])
            remaining = capacities[window] - start - section["size"]
            if remaining >= 0:
                candidates.append((remaining, window, start))
        seen = set()
        for _, window, start in sorted(candidates):
            shape = (capacities[window] - start, start % max(section["alignment"], 4))
            if shape in seen:
                continue
            seen.add(shape)
            previous = ends[window]
            ends[window] = start + section["size"]
            placements.append(dict(section=section["name"], address=start,
                                   size=section["size"], alignment=section["alignment"], window=window))
            if visit(index + 1):
                return True
            placements.pop()
            ends[window] = previous
        failed.add(key)
        return False

    return (placements if visit(0) else None), nodes


def choose_placement(sections):
    sizes = {s["name"]: s["size"] for s in sections}
    eligible = [i for i, (_, size, entry) in enumerate(WINDOWS)
                if sizes.get(".text." + entry, size + 1) <= size]
    choices = sorted(range(1 << len(eligible)), key=lambda mask: (-mask.bit_count(), mask))
    nodes = 0
    for mask in choices:
        anchors = [index for bit, index in enumerate(eligible) if mask & (1 << bit)]
        placement, count = pack(sections, anchors, max(1, 2000000 - nodes))
        nodes += count
        if placement is not None:
            return placement, anchors, nodes
        if nodes >= 2000000:
            raise ValueError("Placement search budget exhausted")
    return None, [], nodes


def check_gp(disassembly):
    count = 0
    for line in disassembly.splitlines():
        match = re.match(r"\s*([0-9a-f]+):\s+(?:(?:[0-9a-f]{2})\s+)+(\S+)\s*(.*)", line)
        if not match:
            continue
        count += 1
        opcode, full_operands = match.group(2), match.group(3)
        operands = full_operands.split("!")[0]
        allowed = set(range(11)) | {15, 28, 29, 30, 31}
        registers = {int(reg) for reg in re.findall(r"\$r(\d+)\b", operands)}
        expanded = full_operands
        for alias, number in (("fp", 28), ("gp", 29), ("lp", 30), ("sp", 31)):
            expanded = expanded.replace("$" + alias, "$r" + str(number))
        for first, last in re.findall(r"\$r(\d+)\s*~\s*\$r(\d+)", expanded):
            registers.update(range(int(first), int(last) + 1))
        if opcode.startswith(("lmw", "smw")):
            bounds = re.match(r"\$r(\d+),\s*\[[^]]+\],\s*\$r(\d+)", expanded)
            if not bounds:
                raise ValueError("Undecoded multiple-register transfer")
            first, last = map(int, bounds.groups())
            if first > last:
                raise ValueError("Reversed multiple-register transfer")
            registers.update(range(first, last + 1))
        if registers - allowed:
            raise ValueError("Instruction uses registers outside reduced-register hardware: " + line.strip())
        if opcode in (".word", ".short", ".byte") or "unknown" in opcode + operands:
            raise ValueError("Undecoded instruction in executable section")
        if opcode.startswith("pop"):
            raise ValueError("Implicit register restore is outside the selected ISA contract")
        if ".gp" in opcode:
            raise ValueError("Implicit GP addressing is outside the selected context contract")
        if opcode.startswith("lmw"):
            expanded = full_operands.replace("$sp", "$r31").replace("$lp", "$r30").replace("$fp", "$r28")
            for first, last in re.findall(r"\$r(\d+)\s*~\s*\$r(\d+)", expanded):
                if int(first) <= 29 <= int(last):
                    raise ValueError("Generated instruction restores a range containing GP")
            if re.search(r"\$(?:gp|r29)\b", full_operands):
                raise ValueError("Generated instruction restores GP")
        if not re.search(r"\$(?:gp|r29)\b", operands):
            continue
        if opcode != "mov55" or not re.fullmatch(r"\$r(?:[0-9]|1[0-9]|2[0-8]),\s*\$(?:gp|r29)\s*", operands):
            raise ValueError("GP use is not an explicit preserved-context read: " + line.strip())
    if not count:
        raise ValueError("No decoded executable instructions")
    return count


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--gcc", required=True, type=Path)
    parser.add_argument("--binutils", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--policy-split", choices=("none", "ipv4", "ipv6", "both", "auto"), default="none")
    parser.add_argument("--split-update", action="store_true")
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error("output directory must be absent or empty")
    out.mkdir(parents=True, exist_ok=True)
    manifest = dict(schema=1, passed=False, proof_only=True, deployment_image_created=False,
                    commands=[], attempts=[], sources={}, tools={}, aliases=ALIASES)
    try:
        manifest["build_logic_sha256"] = {
            str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in (
                Path(__file__).resolve(), ROOT / "tools/wifi/rxfilter-target.py",
                ROOT / "tools/rigs/wifi-rxfilter-sim-check.py")
        }
        spec = importlib.util.spec_from_file_location("rxfilter_target", ROOT / "tools/wifi/rxfilter-target.py")
        target_module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(target_module)
        target = target_module.TargetImage(args.firmware.read_bytes())
        manifest["firmware_sha256"] = digest(target.data)
        manifest["windows"] = [dict(address=base, size=size, entry=entry,
                                    original_sha256=digest(target.read(base, size))) for base, size, entry in WINDOWS]
        for name, address in ALIASES.items():
            target.read(address, 4)
            for base, length, _ in WINDOWS:
                if base < address < base + length:
                    raise ValueError("Original helper points into reclaimed interior: " + name)
        gcc = args.gcc.resolve()
        binary = args.binutils.resolve()
        tools = dict(gcc=gcc, assembler=binary / "gas/as-new", linker=binary / "ld/ld-new",
                     objdump=binary / "binutils/objdump")
        for name, path in tools.items():
            manifest["tools"][name] = dict(path=str(path), sha256=digest(path.read_bytes()))

        def run(argv, label):
            command = [str(value) for value in argv]
            result = subprocess.run(command, capture_output=True, timeout=60)
            (out / (label + ".stdout")).write_bytes(result.stdout)
            (out / (label + ".stderr")).write_bytes(result.stderr)
            manifest["commands"].append(dict(argv=command, returncode=result.returncode))
            if result.returncode:
                raise ValueError(label + " failed: " + result.stderr.decode(errors="replace")[:2000])
            return result.stdout.decode()

        snapshot = out / "source"
        snapshot.mkdir()
        for unit in UNITS:
            for extension in ("c", "h"):
                name = "rxfilter-" + unit + "." + extension
                source = ROOT / "tools/wifi" / name
                data = source.read_bytes()
                manifest["sources"][str(source.relative_to(ROOT))] = digest(data)
                (snapshot / name).write_bytes(data)
        policy = (snapshot / "rxfilter-policy.c").read_text()
        firmware = (snapshot / "rxfilter-firmware.c").read_text()
        if args.split_update:
            old = "static void update(void) {"
            if firmware.count(old) != 1:
                raise ValueError("Firmware update split source anchor changed")
            firmware = firmware.replace(old, "static __attribute__((noinline)) void update(void) {", 1)
        variants = ("none", "ipv4", "ipv6", "both") if args.policy_split == "auto" else (args.policy_split,)
        selected = None
        for variant in variants:
            stage = out / variant
            stage.mkdir()
            policy_source = policy
            for family in ("ipv4", "ipv6"):
                if variant not in (family, "both"):
                    continue
                old = "static enum rxfilter_verdict " + family + "_packet("
                if policy_source.count(old) != 1:
                    raise ValueError("Policy split source anchor changed")
                policy_source = policy_source.replace(old, "static __attribute__((noinline)) enum rxfilter_verdict " + family + "_packet(", 1)
            (stage / "rxfilter-policy.c").write_text(policy_source)
            (stage / "rxfilter-firmware.c").write_text(firmware)
            objects = []
            for unit in UNITS:
                source = (stage if unit in ("policy", "firmware") else snapshot) / ("rxfilter-" + unit + ".c")
                obj = stage / (unit + ".o")
                options = LEGACY_FLAGS if unit == "legacy" else []
                run([gcc, "-B" + str(gcc.parent) + "/", *FLAGS, *options, "-I", snapshot,
                     "-c", source, "-o", obj], variant + "-" + unit)
                Elf(obj, 1)
                objects.append(obj)
            retained = stage / "retained.o"
            roots = [argument for _, _, entry in WINDOWS for argument in ("-u", entry)]
            run([tools["linker"], "-r", "--gc-sections", "--no-relax", *roots, *objects,
                 "-o", retained], variant + "-gc")
            elf = Elf(retained, 1)
            unknown = set(elf.undefined()) - set(ALIASES)
            if unknown:
                raise ValueError("Unexpected runtime helpers: " + ", ".join(sorted(unknown)))
            sections = elf.allocated()
            for section in sections:
                if section["kind"] != 1 or section["flags"] & ~6 or not re.fullmatch(r"\.(?:text|rodata)\.[A-Za-z0-9_.]+", section["name"]):
                    raise ValueError("Unexpected allocated section: " + section["name"])
            for _, _, entry in WINDOWS:
                symbols = [s for s in elf.symbols if s["name"] == entry]
                if len(symbols) != 1 or symbols[0]["value"] != 0 or symbols[0]["section"] >= len(elf.sections):
                    raise ValueError("Unexpected original entry symbol geometry: " + entry)
                if elf.sections[symbols[0]["section"]]["name"] != ".text." + entry:
                    raise ValueError("Original entry lacks a dedicated function section: " + entry)
            placement, anchors, nodes = choose_placement(sections)
            attempt = dict(policy_split=variant, generated_policy_sha256=digest(policy_source.encode()),
                           update_noinline=args.split_update, generated_firmware_sha256=digest(firmware.encode()),
                           retained_bytes=sum(s["size"] for s in sections), sections=sections,
                           search_nodes=nodes, fits=placement is not None)
            manifest["attempts"].append(attempt)
            if placement is not None:
                selected = (variant, retained, placement, anchors)
                break
        if selected is None:
            raise ValueError("Reachable sections cannot fit within the nine reviewed windows")
        variant, retained, placement, anchors = selected
        manifest["policy_split"] = variant
        manifest["placements"] = placement
        manifest["in_place_entries"] = anchors
        assembly = []
        for index, (_, _, entry) in enumerate(WINDOWS):
            if index in anchors:
                continue
            assembly += [f'.section .entry{index},"ax"', f".global rxfilter_entry_{index}",
                         f"rxfilter_entry_{index}:", "j " + entry]
        veneer_source = out / "entries.s"
        veneer_source.write_text("\n".join(assembly) + "\n")
        veneer_object = out / "entries.o"
        run([tools["assembler"], "-EL", "-march=v2", "-mabi=v2", "-mreduced-regs", "-o", veneer_object,
             veneer_source], "entries")
        veneer_elf = Elf(veneer_object, 1)
        if len(veneer_elf.allocated()) != len(WINDOWS) - len(anchors) or any(s["size"] != 4 for s in veneer_elf.allocated()):
            raise ValueError("Expected four-byte veneers for relocated entries")
        script = ["OUTPUT_FORMAT(elf32-nds32le)", "OUTPUT_ARCH(nds32)", "ENTRY(rxfilter_entry_1)"]
        script += [f"{name} = 0x{address:x};" for name, address in ALIASES.items()]
        script += ["SECTIONS {"]
        expected = {}
        for index, (base, size, _) in enumerate(WINDOWS):
            output_name = ".rxfilter.entry" + str(index)
            if index in anchors:
                script += [f"rxfilter_entry_{index} = 0x{base:x};"]
            else:
                script += [f"{output_name} 0x{base:x} : {{ *entries.o(.entry{index}) }}",
                           f'ASSERT(SIZEOF({output_name}) == 4, "entry size")']
                expected[output_name] = (base, 4, 6, 1)
            chunks = sorted((p for p in placement if p["window"] == index), key=lambda p: p["address"])
            for ordinal, part in enumerate(chunks):
                name = f".rxfilter.chunk{index}_{ordinal}"
                script += [f'{name} 0x{part["address"]:x} : {{ *retained.o({part["section"]}) }}',
                           f'ASSERT(SIZEOF({name}) == {part["size"]}, "section size")',
                           f'ASSERT(ADDR({name}) >= 0x{base + (0 if index in anchors else 4):x} && ADDR({name}) + SIZEOF({name}) <= 0x{base + size:x}, "window bounds")']
                original = next(s for s in manifest["attempts"][-1]["sections"] if s["name"] == part["section"])
                expected[name] = (part["address"], part["size"], original["flags"], original["kind"])
            if index not in anchors:
                script += [f'ASSERT(ADDR({output_name}) + SIZEOF({output_name}) <= 0x{base + size:x}, "entry bounds")']
        script += ["/DISCARD/ : { *(.comment) *(.note.GNU-stack) }", "}"]
        linker_script = out / "placement.ld"
        linker_script.write_text("\n".join(script) + "\n")
        final = out / "proof.elf"
        run([tools["linker"], "--no-relax", "--orphan-handling=place", "-T", linker_script,
             "-Map", out / "proof.map", veneer_object, retained, "-o", final], "final-link")
        elf = Elf(final, 2)
        if elf.undefined():
            raise ValueError("Undefined symbols remain in proof ELF")
        if any(s["kind"] in (4, 9) and s["size"] for s in elf.sections):
            raise ValueError("Relocations remain in proof ELF")
        actual = {s["name"]: (s["address"], s["size"], s["flags"], s["kind"]) for s in elf.allocated()}
        if actual != expected or len(elf.allocated()) != len(expected):
            raise ValueError("Unexpected, missing or unplaced allocated output section")
        ranges = sorted((s["address"], s["address"] + s["size"]) for s in elf.allocated())
        if any(start < previous[1] for previous, (start, _) in zip(ranges, ranges[1:])):
            raise ValueError("Allocated output sections overlap")
        symbols = {s["name"]: s["value"] for s in elf.symbols if s["name"] and s["section"]}
        for index, (base, _, entry) in enumerate(WINDOWS):
            if index in anchors:
                if symbols[entry] != base or symbols["rxfilter_entry_" + str(index)] != base:
                    raise ValueError("In-place entry symbol moved")
                manifest["windows"][index]["replacement_entry"] = base
                continue
            section = next(s for s in elf.sections if s["name"] == ".rxfilter.entry" + str(index))
            instruction = int.from_bytes(elf.read(section["offset"], 4), "big")
            if instruction & 0xFF000000 != 0x48000000:
                raise ValueError("Entry is not an NDS32 direct jump")
            displacement = instruction & 0xFFFFFF
            if displacement & 0x800000:
                displacement -= 0x1000000
            destination = (base + displacement * 2) & 0xFFFFFFFF
            if destination != symbols[entry] or symbols["rxfilter_entry_" + str(index)] != base:
                raise ValueError("Entry veneer target mismatch")
            manifest["windows"][index]["replacement_entry"] = destination
        disassembly = run([tools["objdump"], "-d", final], "proof-disassembly")
        manifest["decoded_instructions"] = check_gp(disassembly)
        for relative, sha in manifest["sources"].items():
            if digest((ROOT / relative).read_bytes()) != sha:
                raise ValueError("Canonical source changed during proof build: " + relative)
        manifest["proof_elf_sha256"] = digest(final.read_bytes())
        manifest["passed"] = True
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        manifest["error"] = str(error)
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({key: manifest[key] for key in ("passed", "proof_only", "error") if key in manifest}))
    return 0 if manifest["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
