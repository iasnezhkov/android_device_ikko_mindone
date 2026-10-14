import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import zlib


ROOT = Path(__file__).resolve().parents[2]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools/wifi" / (name + ".py"))
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


def compose(firmware, proof_directory, evidence_path):
    target_module = module("rxfilter-target")
    builder = module("rxfilter-build")
    target = target_module.TargetImage(firmware)
    proof_directory = Path(proof_directory)
    manifest_data = (proof_directory / "manifest.json").read_bytes()
    manifest = json.loads(manifest_data)
    proof_path = proof_directory / "proof.elf"
    proof_digest = sha(proof_path.read_bytes())
    evidence_data = Path(evidence_path).read_bytes()
    evidence = json.loads(evidence_data)
    require(manifest.get("schema") == 1 and manifest.get("passed") is True and
            manifest.get("proof_only") is True, "A passing placement proof is required")
    require(manifest.get("policy_split") == "none", "Generated policy transformations are not deployable")
    require(manifest.get("proof_elf_sha256") == proof_digest, "Proof ELF hash mismatch")
    require(manifest.get("firmware_sha256") == sha(firmware), "Proof firmware binding mismatch")
    require(evidence.get("passed") is True and evidence.get("proof_elf_sha256") == proof_digest and
            evidence.get("firmware_sha256") == sha(firmware), "Passing image composition evidence is required")
    require(evidence.get("placement_manifest_sha256") == sha(manifest_data),
            "Composition evidence references a different placement manifest")
    for key, name in (("fixture_sha256", "wifi-rxfilter-image-check.c"),
                      ("runner_sha256", "wifi-rxfilter-image-check.py")):
        require(evidence.get(key) == sha((ROOT / "tools/rigs" / name).read_bytes()),
                "Composition test source differs: " + name)
    logic = manifest.get("build_logic_sha256", {})
    require(set(logic) == {"tools/wifi/rxfilter-build.py", "tools/wifi/rxfilter-target.py",
                           "tools/rigs/wifi-rxfilter-sim-check.py"}, "Build logic source set differs")
    for name, digest in logic.items():
        require(sha((ROOT / name).read_bytes()) == digest, "Build logic differs: " + name)
    sources = manifest.get("sources", {})
    expected_sources = {"tools/wifi/rxfilter-" + unit + "." + extension
                        for unit in builder.UNITS for extension in ("c", "h")}
    require(set(sources) == expected_sources, "Placement source set mismatch")
    for name, digest in sources.items():
        require(sha((ROOT / name).read_bytes()) == digest, "Source differs from proof: " + name)
    attempts = manifest.get("attempts", [])
    require(len(attempts) == 1 and attempts[0].get("fits") is True and
            attempts[0].get("update_noinline") is False and
            attempts[0].get("generated_policy_sha256") == sources["tools/wifi/rxfilter-policy.c"] and
            attempts[0].get("generated_firmware_sha256") == sources["tools/wifi/rxfilter-firmware.c"],
            "Proof was built from transformed source")
    require(manifest.get("aliases") == builder.ALIASES, "Original helper bindings differ")
    windows = manifest.get("windows", [])
    require(len(windows) == len(builder.WINDOWS), "Replacement window count differs")
    for window, (base, length, entry) in zip(windows, builder.WINDOWS):
        require((window.get("address"), window.get("size"), window.get("entry")) == (base, length, entry),
                "Replacement window geometry differs")
        require(window.get("original_sha256") == sha(target.read(base, length)),
                "Original window bytes differ")
    elf = builder.Elf(proof_path, 2)
    require(not elf.undefined() and not any(s["kind"] in (4, 9) and s["size"] for s in elf.sections),
            "Proof contains unresolved imports or relocations")
    sections = sorted(elf.allocated(), key=lambda section: section["address"])
    require(sections, "Proof has no allocated code")
    symbols = {symbol["name"]: symbol["value"] for symbol in elf.symbols if symbol["section"]}
    require(all(symbols.get(name) == address for name, address in builder.ALIASES.items()),
            "Linked original helper bindings differ")
    anchors = manifest.get("in_place_entries", [])
    require(len(set(anchors)) == len(anchors) and all(type(i) is int and 0 <= i < len(windows)
                                                    for i in anchors), "Invalid original entry set")
    output = bytearray(firmware)
    patches = []
    previous_end = 0
    for section in sections:
        address, length = section["address"], section["size"]
        require(section["kind"] == 1 and section["flags"] in (2, 6), "Unexpected allocated section type")
        require(address >= previous_end, "Allocated sections overlap")
        require(sum(base <= address and address + length <= base + size
                    for base, size, _ in builder.WINDOWS) == 1, "Section escapes reviewed windows")
        replacement = elf.read(section["offset"], length)
        original = target.read(address, length)
        mappings = [offset + address - base for base, size, feature, offset in target.regions
                    if feature == 0x80 and base <= address and address + length <= base + size]
        require(len(mappings) == 1, "Section has no unique plaintext mapping")
        offset = mappings[0]
        output[offset:offset + length] = replacement
        patches.append(dict(address=address, offset=offset, size=length,
                            original_sha256=sha(original), replacement_sha256=sha(replacement)))
        previous_end = address + length
    for index, (base, _, entry) in enumerate(builder.WINDOWS):
        require(symbols.get("rxfilter_entry_" + str(index)) == base, "Original entry symbol moved")
        destination = symbols.get(entry)
        require(type(destination) is int and windows[index].get("replacement_entry") == destination,
                "Replacement entry binding differs")
        require(any(s["flags"] == 6 and s["address"] <= destination < s["address"] + s["size"]
                    for s in sections), "Entry destination is not executable")
        if index in anchors:
            require(destination == base, "In-place entry moved")
        else:
            entry_sections = [s for s in sections if s["address"] == base and s["size"] == 4]
            require(len(entry_sections) == 1, "Relocated entry lacks a four-byte veneer")
            instruction = int.from_bytes(elf.read(entry_sections[0]["offset"], 4), "big")
            require(instruction & 0xFF000000 == 0x48000000, "Invalid entry jump opcode")
            displacement = instruction & 0xFFFFFF
            if displacement & 0x800000:
                displacement -= 0x1000000
            require((base + displacement * 2) & 0xFFFFFFFF == destination, "Entry jump target differs")
    struct.pack_into("<I", output, len(output) - 4, zlib.crc32(output[:-4]))
    allowed = bytearray(len(output))
    for patch in patches:
        allowed[patch["offset"]:patch["offset"] + patch["size"]] = b"\x01" * patch["size"]
    allowed[-4:] = b"\x01" * 4
    require(all(before == after or permitted for before, after, permitted in zip(firmware, output, allowed)),
            "Unrelated firmware bytes changed")
    receipt = dict(schema=1, input_sha256=sha(firmware), output_sha256=sha(output), size=len(output),
                   proof_elf_sha256=proof_digest, placement_manifest_sha256=sha(manifest_data),
                   composition_receipt_sha256=sha(evidence_data), sources=sources, patches=patches,
                   crc32=zlib.crc32(output[:-4]), hardware_validated=False,
                   limits=["Exact bound firmware only", "Instruction simulation does not establish hardware or energy behavior"])
    return bytes(output), receipt


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--proof", required=True, type=Path)
    parser.add_argument("--composition-receipt", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        require(not args.output.exists(), "Output path already exists")
        receipt_path = args.output.with_suffix(args.output.suffix + ".json")
        require(not receipt_path.exists(), "Output receipt already exists")
        output, receipt = compose(args.firmware.read_bytes(), args.proof, args.composition_receipt)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("xb") as stream:
            stream.write(output)
        with receipt_path.open("x") as stream:
            json.dump(receipt, stream, indent=2)
            stream.write("\n")
        readback = args.output.read_bytes()
        require(readback == output and sha(readback) == receipt["output_sha256"],
                "Firmware artifact readback mismatch")
        require(struct.unpack_from("<I", readback, len(readback) - 4)[0] == zlib.crc32(readback[:-4]),
                "Firmware artifact readback CRC mismatch")
        require(json.loads(receipt_path.read_text()) == receipt, "Artifact receipt readback mismatch")
        print(json.dumps({"output": str(args.output), "sha256": receipt["output_sha256"], "size": len(output)}))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
