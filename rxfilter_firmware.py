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
PREDECESSOR_SHA256 = "9f43876db01a72142bb221d18587a2c5f8204902006279bb786be8d1493a8f7d"
FIRMWARE_SHA256 = "bfb11a2b31fdd22aadb603b186e9ae0ebc98a476a7f7adf0f4a572d520abbfec"
FIRMWARE_SIZE = 698052
ARTIFACT = "firmware/candidates/rxfilter-1410-C37/WIFI_RAM_CODE_soc2_2_1d_1.bin"
SOURCE_FILES = {f"tools/wifi/rxfilter-{unit}.{extension}"
                for unit in ("policy", "query", "firmware", "legacy", "mcast", "table", "config")
                for extension in ("c", "h")}


def digest(data):
    return hashlib.sha256(data).hexdigest()


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


def verified_firmware(artifact=None, source_root=None):
    root = source_directory(source_root)
    path = Path(artifact or os.environ.get("MINDONE_RXFILTER_ARTIFACT") or root / ARTIFACT)
    return verify_data(path.read_bytes(),
                       json.loads(path.with_suffix(path.suffix + ".json").read_bytes()), root)


def install_firmware(file_path, artifact=None, source_root=None):
    destination = Path(file_path)
    if destination.is_symlink() or not destination.is_file():
        raise ValueError("Expected an existing regular vendor firmware file")
    before = destination.read_bytes()
    if (artifact is None and not os.environ.get("MINDONE_RXFILTER_ARTIFACT") and
            digest(before) == FIRMWARE_SHA256):
        receipt = Path(__file__).resolve().parent / "tools/wifi/rxfilter-release.json"
        verify_data(before, json.loads(receipt.read_bytes()), source_directory(source_root))
        return False
    data = verified_firmware(artifact, source_root)
    if digest(before) not in (BASELINE_SHA256, PREDECESSOR_SHA256, FIRMWARE_SHA256):
        raise ValueError("Refusing to replace an unrecognized vendor firmware")
    if before == data:
        return False
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
    parser = argparse.ArgumentParser(description="Verify the reviewed RXFILTER artifact and install it into extracted vendor firmware; MINDONE_RXFILTER_ARTIFACT and MINDONE_RXFILTER_SOURCE_ROOT configure standalone extraction")
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--source-root", type=Path)
    operation = parser.add_mutually_exclusive_group(required=True)
    operation.add_argument("--verify-only", action="store_true")
    operation.add_argument("--destination", type=Path)
    args = parser.parse_args()
    try:
        changed = False
        if args.verify_only:
            verified_firmware(args.artifact, args.source_root)
        else:
            changed = install_firmware(args.destination, args.artifact, args.source_root)
        print(json.dumps(dict(sha256=FIRMWARE_SHA256, changed=changed, verified=True)))
        return 0
    except (OSError, ValueError, TypeError, AttributeError) as error:
        print("RXFILTER firmware required: " + str(error))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
