# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import tempfile
import zlib


BASELINE_SHA256 = "453912e5f79ba313986a8a6047a22f4d8033300f6db728f281a7f1c2ee92c37b"
FIRMWARE_SHA256 = "bfb11a2b31fdd22aadb603b186e9ae0ebc98a476a7f7adf0f4a572d520abbfec"
FIRMWARE_SIZE = 698052
DONOR_SHA256 = "b01ef53c5baeb56f5c60a910bb074a604b8cb10a16c0f4660b015b813e08e953"
DONOR_SIZE = 722508
KNOWN_INPUTS = {
    BASELINE_SHA256: {"size": FIRMWARE_SIZE, "output_sha256": FIRMWARE_SHA256},
    DONOR_SHA256: {"size": DONOR_SIZE, "output_sha256": DONOR_SHA256},
}
CRC32_RULE = "uint32_le(zlib.crc32(image[:-4])) at image[-4:]"
PATCH = Path(__file__).resolve().parent / "tools/wifi/rxfilter-patch.json"
SOURCE_FILES = {f"tools/wifi/rxfilter-{unit}.{extension}"
                for unit in ("policy", "query", "firmware", "legacy", "mcast", "table", "config")
                for extension in ("c", "h")}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def firmware_support(data):
    identity = digest(data)
    if identity == FIRMWARE_SHA256 and len(data) == FIRMWARE_SIZE:
        return "RXFILTER firmware support present"
    known = KNOWN_INPUTS.get(identity)
    if known is None or len(data) != known["size"]:
        raise ValueError("RXFILTER unknown firmware identity or size")
    return "RXFILTER firmware support absent"


def source_directory(source_root=None):
    return Path(source_root or os.environ.get("MINDONE_RXFILTER_SOURCE_ROOT") or
                Path(__file__).resolve().parent).resolve()


def verify_data(data, receipt, root):
    if len(data) != FIRMWARE_SIZE or digest(data) != FIRMWARE_SHA256:
        raise ValueError("RXFILTER firmware differs from the reviewed artifact")
    if (receipt.get("schema") != 1 or receipt.get("input_sha256") != BASELINE_SHA256 or
            receipt.get("output_sha256") != FIRMWARE_SHA256 or receipt.get("size") != len(data)):
        raise ValueError("RXFILTER firmware receipt identity differs")
    crc = zlib.crc32(data[:-4])
    if struct.unpack_from("<I", data, len(data) - 4)[0] != crc or receipt.get("crc32") != crc:
        raise ValueError("RXFILTER firmware CRC differs")
    sources = receipt.get("sources")
    if not isinstance(sources, dict) or set(sources) != SOURCE_FILES:
        raise ValueError("RXFILTER firmware source binding set differs")
    for name, expected in sources.items():
        if digest((root / name).read_bytes()) != expected:
            raise ValueError("RXFILTER firmware is stale for source: " + name)
    return data


def verified_patch(patch=None, source_root=None):
    description = json.loads(Path(patch or PATCH).read_bytes())
    if (description.get("schema") != 1 or
            description.get("input_sha256") != BASELINE_SHA256 or
            description.get("output_sha256") != FIRMWARE_SHA256 or
            description.get("size") != FIRMWARE_SIZE or
            description.get("crc32_rule") != CRC32_RULE or
            type(description.get("crc32")) is not int or
            not 0 <= description["crc32"] <= 0xffffffff):
        raise ValueError("RXFILTER patch identity or CRC32 rule differs")
    sources = description.get("sources")
    if not isinstance(sources, dict) or set(sources) != SOURCE_FILES:
        raise ValueError("RXFILTER firmware source binding set differs")
    root = source_directory(source_root)
    for name, expected in sources.items():
        if digest((root / name).read_bytes()) != expected:
            raise ValueError("RXFILTER firmware is stale for source: " + name)
    regions = description.get("patches")
    if not isinstance(regions, list) or not regions:
        raise ValueError("RXFILTER patch regions are missing")
    previous_end = 0
    for region in regions:
        offset, size = region["offset"], region["size"]
        if (type(offset) is not int or type(size) is not int or size <= 0 or
                offset < previous_end or offset + size > FIRMWARE_SIZE - 4):
            raise ValueError("RXFILTER patch regions overlap or escape the image")
        original = region["original_sha256"]
        if (not isinstance(original, str) or len(original) != 64 or
                any(character not in "0123456789abcdef" for character in original)):
            raise ValueError("RXFILTER original region digest is invalid")
        replacement = bytes.fromhex(region["replacement_hex"])
        if (len(replacement) != size or len(region["replacement_hex"]) != size * 2 or
                digest(replacement) != region["replacement_sha256"]):
            raise ValueError("RXFILTER replacement region differs")
        previous_end = offset + size
    return description


def apply_patch(data, description):
    if len(data) != description["size"] or digest(data) != description["input_sha256"]:
        raise ValueError("RXFILTER stock firmware SHA256 differs")
    output = bytearray(data)
    for region in description["patches"]:
        offset, size = region["offset"], region["size"]
        if digest(data[offset:offset + size]) != region["original_sha256"]:
            raise ValueError("RXFILTER original region differs")
        output[offset:offset + size] = bytes.fromhex(region["replacement_hex"])
    struct.pack_into("<I", output, len(output) - 4, zlib.crc32(output[:-4]))
    if digest(output) != description["output_sha256"]:
        raise ValueError("RXFILTER patched firmware SHA256 differs")
    return bytes(output)


def install_firmware(file_path, patch=None, source_root=None):
    description = verified_patch(patch, source_root)
    destination = Path(file_path)
    if destination.is_symlink() or not destination.is_file():
        raise ValueError("Expected an existing regular vendor firmware file")
    before = destination.read_bytes()
    if digest(before) == FIRMWARE_SHA256:
        verify_data(before, description, source_directory(source_root))
        return False
    identity = digest(before)
    known = KNOWN_INPUTS.get(identity)
    if known is None or len(before) != known["size"]:
        raise ValueError("RXFILTER unknown firmware identity or size")
    if known["output_sha256"] == identity:
        return False
    data = apply_patch(before, description)
    verify_data(data, description, source_directory(source_root))
    mode = stat.S_IMODE(destination.stat().st_mode)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, prefix=".rxfilter-", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
            os.fchmod(stream.fileno(), mode)
        if temporary.read_bytes() != data or destination.read_bytes() != before:
            raise ValueError("Vendor firmware changed during replacement")
        os.replace(temporary, destination)
        temporary = None
        if destination.read_bytes() != data:
            raise ValueError("RXFILTER firmware readback failed")
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return True


def main():
    parser = argparse.ArgumentParser(description="Verify the public RXFILTER patch and source digests, or apply it atomically to known extracted firmware; MINDONE_RXFILTER_SOURCE_ROOT configures standalone source verification")
    parser.add_argument("--patch", type=Path, help="Public patch description; defaults to tools/wifi/rxfilter-patch.json next to this module")
    parser.add_argument("--source-root", type=Path)
    operation = parser.add_mutually_exclusive_group(required=True)
    operation.add_argument("--verify-only", action="store_true")
    operation.add_argument("--destination", type=Path)
    args = parser.parse_args()
    try:
        changed = False
        if args.verify_only:
            verified_patch(args.patch, args.source_root)
        else:
            changed = install_firmware(args.destination, args.patch, args.source_root)
        result = dict(sha256=FIRMWARE_SHA256, changed=changed, verified=True)
        if not args.verify_only:
            installed = args.destination.read_bytes()
            result.update(sha256=digest(installed), rxfilter_support=firmware_support(installed))
        print(json.dumps(result))
        return 0
    except (OSError, ValueError, TypeError, AttributeError, KeyError) as error:
        print("RXFILTER firmware required: " + str(error))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
