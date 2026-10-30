# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
import argparse
import ast
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zlib


DEVICE = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("rxfilter_firmware", DEVICE / "rxfilter_firmware.py")
firmware = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(firmware)
DONOR_ROOT = None


class InstallChecks(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="rxfilter-install-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.destination = self.root / "firmware.bin"
        self.donor = (DONOR_ROOT / "vendor/firmware/WIFI_RAM_CODE_soc2_2_1d_1.bin").read_bytes()
        self.destination.write_bytes(self.donor)

    def test_donor_unchanged(self):
        self.destination.chmod(0o640)
        before = self.destination.stat()
        with patch.object(firmware.os, "replace", side_effect=AssertionError("unexpected replacement")):
            for _ in range(2):
                self.assertFalse(firmware.install_firmware(self.destination))
        self.assertEqual(self.destination.read_bytes(), self.donor)
        self.assertEqual(self.destination.stat().st_mtime_ns, before.st_mtime_ns)
        self.assertEqual(stat.S_IMODE(self.destination.stat().st_mode), 0o640)
        self.assertEqual(firmware.firmware_support(self.donor), "RXFILTER firmware support absent")
        self.assertEqual(firmware.digest(self.donor), firmware.DONOR_SHA256)
        self.assertEqual(len(self.donor), firmware.DONOR_SIZE)

    def test_donor_cli(self):
        result = subprocess.run([sys.executable, str(DEVICE / "rxfilter_firmware.py"),
                                 "--destination", str(self.destination)],
                                check=True, text=True, capture_output=True)
        self.assertEqual(json.loads(result.stdout), dict(sha256=firmware.DONOR_SHA256,
                         changed=False, verified=True, rxfilter_support="RXFILTER firmware support absent"))
        self.assertEqual(self.destination.read_bytes(), self.donor)

    def test_unknown_inputs(self):
        for data in (b"unknown", self.donor[:-1], self.donor + b"\0",
                     bytes([self.donor[0] ^ 1]) + self.donor[1:]):
            with self.subTest(size=len(data)):
                self.destination.write_bytes(data)
                with self.assertRaises(ValueError):
                    firmware.install_firmware(self.destination)
                self.assertEqual(self.destination.read_bytes(), data)

    def test_donor_size_check(self):
        with patch.dict(firmware.KNOWN_INPUTS, {firmware.DONOR_SHA256:
                        dict(size=firmware.DONOR_SIZE - 1, output_sha256=firmware.DONOR_SHA256)}):
            with self.assertRaises(ValueError):
                firmware.install_firmware(self.destination)

    def test_donor_rejects_symlink(self):
        link = self.root / "link.bin"
        link.symlink_to(self.destination)
        with self.assertRaises(ValueError):
            firmware.install_firmware(link)
        self.assertEqual(self.destination.read_bytes(), self.donor)

    def test_donor_keeps_patch_checks(self):
        description = json.loads(firmware.PATCH.read_text())
        for key, value in (("input_sha256", "0" * 64), ("output_sha256", "0" * 64),
                           ("size", 1), ("crc32_rule", "wrong"), ("sources", {})):
            with self.subTest(key=key):
                candidate = self.root / "patch.json"
                candidate.write_text(json.dumps(dict(description, **{key: value})))
                with self.assertRaises(ValueError):
                    firmware.install_firmware(self.destination, candidate)
                self.assertEqual(self.destination.read_bytes(), self.donor)

    def test_donor_rejects_stale_sources(self):
        for name in firmware.SOURCE_FILES:
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(DEVICE / name, target)
        source = self.root / "tools/wifi/rxfilter-policy.c"
        source.write_bytes(source.read_bytes() + b"\n")
        with self.assertRaisesRegex(ValueError, "stale"):
            firmware.install_firmware(self.destination, source_root=self.root)

    def test_complete_cohorts_and_retirements(self):
        entries = (DEVICE / "proprietary-files-donor-a075f-connsys.txt").read_text().splitlines()
        entries = [line for line in entries if line and not line.startswith("#")]
        self.assertEqual(len(entries), 8)
        names = set()
        for line in entries:
            name, pin = line.split("|")
            names.add(name)
            self.assertEqual(hashlib.sha1((DONOR_ROOT / name).read_bytes()).hexdigest(), pin)
        retirements = (DEVICE / "proprietary-files-donor-a075f-connsys.retire.txt").read_text().splitlines()
        self.assertEqual(names, {line for line in retirements if line and not line.startswith("#")})
        stock = set((DEVICE / "proprietary-files.txt").read_text().splitlines())
        self.assertTrue(names <= stock)
        for name in ("WMT_SOC.cfg", "BT_FW.cfg"):
            self.assertIn("vendor/firmware/" + name, stock)
            self.assertNotIn("vendor/firmware/" + name, names)

    def test_extraction_and_staging(self):
        tree = ast.parse((DEVICE / "extract-files.py").read_text())
        callback = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                        and node.name == "fixup_rxfilter_firmware")
        namespace = dict(os=os, Path=Path, install_firmware=firmware.install_firmware,
                         firmware_support=firmware.firmware_support)
        exec(compile(ast.Module(body=[callback], type_ignores=[]), "callback", "exec"), namespace)
        module = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == "MindoneModule")
        stage = next(node for node in module.body if isinstance(node, ast.FunctionDef) and node.name == "stage_donor")
        namespace.update(tempfile=tempfile, shutil=shutil, sha1=hashlib.sha1,
                         donor_roots={"a075f": DONOR_ROOT},
                         verify_donor=lambda root, file: root / file.dst,
                         FileArgs=SimpleNamespace(FIX_SONAME="FIX_SONAME"), DONOR_FIXUPS={})
        exec(compile(ast.Module(body=[stage], type_ignores=[]), "stage", "exec"), namespace)
        file = SimpleNamespace(dst="vendor/firmware/WIFI_RAM_CODE_soc2_2_1d_1.bin",
                               hash=hashlib.sha1(self.donor).hexdigest(), fixup_hash=None, args={})
        output = io.StringIO()
        with patch.dict(os.environ, MINDONE_PERSONAL_BUILD="true"), contextlib.redirect_stdout(output):
            namespace["stage_donor"](None, "a075f", file, self.destination)
        self.assertEqual(self.destination.read_bytes(), self.donor)
        self.assertEqual(output.getvalue().strip(), "RXFILTER firmware support absent")

    def test_stock_patch_fixture(self):
        baseline = bytes(range(64))
        output = bytearray(baseline)
        output[4:6] = b"xy"
        crc = zlib.crc32(output[:-4])
        struct.pack_into("<I", output, len(output) - 4, crc)
        output = bytes(output)
        source_hashes = {name: firmware.digest((DEVICE / name).read_bytes()) for name in firmware.SOURCE_FILES}
        description = dict(schema=1, input_sha256=firmware.digest(baseline),
                           output_sha256=firmware.digest(output), size=len(baseline), crc32=crc,
                           crc32_rule=firmware.CRC32_RULE, sources=source_hashes,
                           patches=[dict(offset=4, size=2, original_sha256=firmware.digest(baseline[4:6]),
                                         replacement_hex=b"xy".hex(), replacement_sha256=firmware.digest(b"xy"))])
        candidate = self.root / "fixture.json"
        candidate.write_text(json.dumps(description))
        with patch.multiple(firmware, BASELINE_SHA256=firmware.digest(baseline),
                            FIRMWARE_SHA256=firmware.digest(output), FIRMWARE_SIZE=len(baseline),
                            KNOWN_INPUTS={firmware.digest(baseline):
                                          dict(size=len(baseline), output_sha256=firmware.digest(output))}):
            self.destination.write_bytes(baseline)
            self.destination.chmod(0o640)
            self.assertTrue(firmware.install_firmware(self.destination, candidate))
            self.assertEqual(self.destination.read_bytes(), output)
            self.assertEqual(stat.S_IMODE(self.destination.stat().st_mode), 0o640)
            stamp = self.destination.stat().st_mtime_ns
            self.assertFalse(firmware.install_firmware(self.destination, candidate))
            self.assertEqual(self.destination.stat().st_mtime_ns, stamp)
            self.assertEqual(firmware.firmware_support(output), "RXFILTER firmware support present")
            for field, value in (("original_sha256", "0" * 64), ("replacement_sha256", "0" * 64),
                                 ("offset", len(baseline)), ("size", 0)):
                with self.subTest(field=field):
                    region = dict(description["patches"][0], **{field: value})
                    candidate.write_text(json.dumps(dict(description, patches=[region])))
                    self.destination.write_bytes(baseline)
                    with self.assertRaises(ValueError):
                        firmware.install_firmware(self.destination, candidate)
                    self.assertEqual(self.destination.read_bytes(), baseline)
            candidate.write_text(json.dumps(description))
            with patch.object(firmware.os, "replace", side_effect=OSError("rename failed")):
                with self.assertRaises(OSError):
                    firmware.install_firmware(self.destination, candidate)
            self.assertEqual(self.destination.read_bytes(), baseline)
            self.assertFalse(list(self.root.glob(".rxfilter-*")))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--donor-root", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    DONOR_ROOT = args.donor_root
    unittest.main(argv=[sys.argv[0], *remaining])
