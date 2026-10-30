# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
import argparse
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from rxfilter_firmware import BASELINE_SHA256, CRC32_RULE, FIRMWARE_SIZE, digest, verify_data, verified_patch


def make_patch(stock, modified, receipt, source_root):
    if len(stock) != FIRMWARE_SIZE or digest(stock) != BASELINE_SHA256:
        raise ValueError("RXFILTER stock firmware SHA256 differs")
    if len(modified) != len(stock):
        raise ValueError("RXFILTER modified firmware size differs")
    regions = receipt.get("patches")
    if not isinstance(regions, list) or not regions:
        raise ValueError("RXFILTER receipt has no patch regions")
    previous_end = 0
    patches = []
    for region in regions:
        offset, size = region["offset"], region["size"]
        if (type(offset) is not int or type(size) is not int or size <= 0 or
                offset < previous_end or offset + size > len(stock) - 4):
            raise ValueError("RXFILTER receipt regions overlap or escape the image")
        if stock[previous_end:offset] != modified[previous_end:offset]:
            raise ValueError("RXFILTER bytes outside listed regions differ")
        replacement = modified[offset:offset + size]
        if digest(stock[offset:offset + size]) != region["original_sha256"]:
            raise ValueError("RXFILTER original region differs")
        if digest(replacement) != region["replacement_sha256"]:
            raise ValueError("RXFILTER replacement region differs")
        patches.append(dict(offset=offset, size=size, original_sha256=region["original_sha256"],
                            replacement_sha256=region["replacement_sha256"],
                            replacement_hex=replacement.hex()))
        previous_end = offset + size
    if stock[previous_end:-4] != modified[previous_end:-4]:
        raise ValueError("RXFILTER bytes outside listed regions differ")
    verify_data(modified, receipt, source_root)
    return dict(schema=1, input_sha256=receipt["input_sha256"],
                output_sha256=receipt["output_sha256"], size=len(stock),
                crc32=receipt["crc32"], crc32_rule=CRC32_RULE,
                sources=receipt["sources"], patches=patches)


def main():
    parser = argparse.ArgumentParser(description="Publish only reviewed RXFILTER replacement regions from an exact stock/modified image pair; refuse changes outside receipt regions except the CRC32 trailer")
    parser.add_argument("--stock", required=True, type=Path)
    parser.add_argument("--modified", required=True, type=Path)
    parser.add_argument("--receipt", type=Path, help="Defaults to the modified image path plus .json")
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        receipt = args.receipt or args.modified.with_suffix(args.modified.suffix + ".json")
        description = make_patch(args.stock.read_bytes(), args.modified.read_bytes(),
                                 json.loads(receipt.read_bytes()), args.source_root)
        encoded = (json.dumps(description, indent=2) + "\n").encode()
        with args.output.open("xb") as stream:
            stream.write(encoded)
        try:
            verified_patch(args.output, args.source_root)
        except (OSError, ValueError, TypeError, AttributeError, KeyError):
            args.output.unlink()
            raise
        print(json.dumps(dict(output=str(args.output), regions=len(description["patches"]),
                              replacement_bytes=sum(region["size"] for region in description["patches"]),
                              sha256=description["output_sha256"])))
        return 0
    except (OSError, ValueError, TypeError, AttributeError, KeyError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
