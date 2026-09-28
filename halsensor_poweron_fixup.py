from __future__ import annotations

import hashlib
from pathlib import Path

STOCK_SHA256 = 'b756d7d3fd0493cc3e8106a986b9b28a39ba9f3b3a762e39d904b24c3e90e6e3'
FIXED_SHA256 = '4fbb81581c5750145ddb99b92c02ac78d83e7cf7e224279a8841861731857754'

POWERON_SLEEP_PATCH = (
    (0x2cf04, '007c92528000a072', '007c92528000a072'),
    (0x2cf0c, '65030394', '1f2003d5'),
)


def drop_poweron_sleep(file_path: str) -> None:
    path = Path(file_path)
    data = bytearray(path.read_bytes())
    digest = hashlib.sha256(data).hexdigest()
    if digest == FIXED_SHA256:
        return
    if digest != STOCK_SHA256:
        raise ValueError(f'{path.name}: unexpected sha256 {digest}, the powerOn patch fits only {STOCK_SHA256}')
    for offset, old, new in POWERON_SLEEP_PATCH:
        before, after = bytes.fromhex(old), bytes.fromhex(new)
        if data[offset:offset + len(before)] != before:
            raise ValueError(f'{path.name}: bytes at 0x{offset:x} differ from the stock image')
        data[offset:offset + len(after)] = after
    digest = hashlib.sha256(data).hexdigest()
    if digest != FIXED_SHA256:
        raise ValueError(f'{path.name}: patched sha256 {digest} != {FIXED_SHA256}')
    path.write_bytes(data)
