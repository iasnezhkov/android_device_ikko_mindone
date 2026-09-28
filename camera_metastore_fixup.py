#!/usr/bin/env python3
from __future__ import annotations

import hashlib
from pathlib import Path

STOCK_SHA256 = 'e2a88f370a0bb96f43dfcd38d1d2d97528dc65381e70e10ed34b5e1a3d6d5b50'
FIXED_SHA256 = '35a3a7771c4bf0ca7658f859fd469970e99fe5630972f085982a29a289244cae'

IMX766_STREAM_TABLE = (
    (0x2bd71, '08', '00'),
    (0x2bd85, '86', '80'),
    (0x2bda8, '5b1588', '1b3a9c'),
    (0x2bdac, '3b7f', '7b44'),
    (0x2bde8, 'f2240294', '1f2003d5'),
    (0x2bdf8, 'ee240294', '1f2003d5'),
    (0x2be08, 'ea240294', '1f2003d5'),
    (0x2be18, 'e6240294', '1f2003d5'),
    (0x2be28, 'e2240294', '1f2003d5'),
    (0x2be38, 'de240294', '1f2003d5'),
    (0x2bf18, 'a6240294', '1f2003d5'),
    (0x2bf28, 'a2240294', '1f2003d5'),
    (0x2bf3c, '9d240294', '1f2003d5'),
    (0x2bf4c, '99240294', '1f2003d5'),
    (0x2bf5c, '95240294', '1f2003d5'),
    (0x2bf6c, '91240294', '1f2003d5'),
    (0x2bff0, '8824', '0820'),
    (0x2bff8, '9a24', '1a20'),
    (0x2c044, '5b240294', '1f2003d5'),
    (0x2c054, '57240294', '1f2003d5'),
    (0x2c05c, '9a24', '1a20'),
    (0x2c068, '52240294', '1f2003d5'),
    (0x2c078, '4e240294', '1f2003d5'),
    (0x2c088, '4a240294', '1f2003d5'),
    (0x2c098, '46240294', '1f2003d5'),
    (0x2c16c, '11240294', '1f2003d5'),
    (0x2c17c, '0d240294', '1f2003d5'),
    (0x2c18c, '09240294', '1f2003d5'),
    (0x2c19c, '05240294', '1f2003d5'),
    (0x2c1ac, '01240294', '1f2003d5'),
    (0x2c1bc, 'fd230294', '1f2003d5'),
    (0x2c28c, 'c9230294', '1f2003d5'),
    (0x2c29c, 'c5230294', '1f2003d5'),
    (0x2c2ac, 'c1230294', '1f2003d5'),
    (0x2c2bc, 'bd230294', '1f2003d5'),
    (0x2c2cc, 'b9230294', '1f2003d5'),
    (0x2c2dc, 'b5230294', '1f2003d5'),
    (0x2c3b0, '80230294', '1f2003d5'),
    (0x2c3c0, '7c230294', '1f2003d5'),
    (0x2c3d0, '78230294', '1f2003d5'),
    (0x2c3e0, '74230294', '1f2003d5'),
    (0x2c3f0, '70230294', '1f2003d5'),
    (0x2c400, '6c230294', '1f2003d5'),
    (0x2c4d4, '37230294', '1f2003d5'),
    (0x2c4e4, '33230294', '1f2003d5'),
    (0x2c4f4, '2f230294', '1f2003d5'),
    (0x2c504, '2b230294', '1f2003d5'),
    (0x2c514, '27230294', '1f2003d5'),
    (0x2c524, '23230294', '1f2003d5'),
    (0x2c599, 'd1', 'b8'),
    (0x2c5ad, '95', '80'),
    (0x2c5fc, 'ed220294', '1f2003d5'),
    (0x2c60c, 'e9220294', '1f2003d5'),
    (0x2c61c, 'e5220294', '1f2003d5'),
    (0x2c62c, 'e1220294', '1f2003d5'),
    (0x2c63c, 'dd220294', '1f2003d5'),
    (0x2c64c, 'd9220294', '1f2003d5'),
    (0x2c728, 'a2220294', '1f2003d5'),
    (0x2c738, '9e220294', '1f2003d5'),
    (0x2c74c, '99220294', '1f2003d5'),
    (0x2c75c, '95220294', '1f2003d5'),
    (0x2c76c, '91220294', '1f2003d5'),
    (0x2c77c, '8d220294', '1f2003d5'),
    (0x2c858, '56220294', '1f2003d5'),
    (0x2c868, '52220294', '1f2003d5'),
    (0x2c878, '4e220294', '1f2003d5'),
    (0x2c888, '4a220294', '1f2003d5'),
    (0x2c898, '46220294', '1f2003d5'),
    (0x2c8a8, '42220294', '1f2003d5'),
    (0x2c97c, '0d220294', '1f2003d5'),
    (0x2c98c, '09220294', '1f2003d5'),
    (0x2c99c, '05220294', '1f2003d5'),
    (0x2c9ac, '01220294', '1f2003d5'),
    (0x2c9bc, 'fd210294', '1f2003d5'),
    (0x2c9cc, 'f9210294', '1f2003d5'),
    (0x2caa4, 'c3210294', '1f2003d5'),
    (0x2cab4, 'bf210294', '1f2003d5'),
    (0x2cac4, 'bb210294', '1f2003d5'),
    (0x2cad4, 'b7210294', '1f2003d5'),
    (0x2cae4, 'b3210294', '1f2003d5'),
    (0x2caf4, 'af210294', '1f2003d5'),
)


def fix_imx766_stream_table(file_path: str) -> None:
    path = Path(file_path)
    data = bytearray(path.read_bytes())
    digest = hashlib.sha256(data).hexdigest()
    if digest == FIXED_SHA256:
        return
    if digest != STOCK_SHA256:
        raise ValueError(f'{path.name}: unexpected sha256 {digest}, the IMX766 table patch fits only {STOCK_SHA256}')
    for offset, old, new in IMX766_STREAM_TABLE:
        before, after = bytes.fromhex(old), bytes.fromhex(new)
        if data[offset:offset + len(before)] != before:
            raise ValueError(f'{path.name}: bytes at 0x{offset:x} differ from the stock image')
        data[offset:offset + len(after)] = after
    digest = hashlib.sha256(data).hexdigest()
    if digest != FIXED_SHA256:
        raise ValueError(f'{path.name}: patched sha256 {digest} != {FIXED_SHA256}')
    path.write_bytes(data)
