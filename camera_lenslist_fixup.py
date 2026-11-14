# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import hashlib
from pathlib import Path

STOCK_SHA256 = '0625c985fc838ce862f7422a2560bd9516870aa407e6b9b91f1f2633de0cf022'
FIXED_SHA256 = 'a05b9a43cb1234088642a81e1e1195bc33508a6fd381755abd5b004d41f5dcb7'

SUB_LENS_SENSOR_PATCH = (
    (0x2628, '670700000000000000980000445739383030574146', '660700000000000000980000445739383030574146'),
)


def name_imx766_in_sub_lens_list(file_path: str) -> None:
    path = Path(file_path)
    data = bytearray(path.read_bytes())
    digest = hashlib.sha256(data).hexdigest()
    if digest == FIXED_SHA256:
        return
    if digest != STOCK_SHA256:
        raise ValueError(f'{path.name}: unexpected sha256 {digest}, the lens list patch fits only {STOCK_SHA256}')
    for offset, old, new in SUB_LENS_SENSOR_PATCH:
        before, after = bytes.fromhex(old), bytes.fromhex(new)
        if data[offset:offset + len(before)] != before:
            raise ValueError(f'{path.name}: bytes at 0x{offset:x} differ from the stock image')
        data[offset:offset + len(after)] = after
    digest = hashlib.sha256(data).hexdigest()
    if digest != FIXED_SHA256:
        raise ValueError(f'{path.name}: patched sha256 {digest} != {FIXED_SHA256}')
    path.write_bytes(data)
