from __future__ import annotations

import hashlib
from pathlib import Path

STOCK_SHA256 = '977636ed3639721287176a54976a754e5cb0f6d9b1a314e81e983f145c3ee401'
FIXED_SHA256 = '0374bb5edb16d9fb53e428e71605e04b5078ddce692473a732849b7b277e15c5'

INSTANCE_NAME_PATCH = (
    (0x20f4, 'e9058652', 'e9258752'),
)


def fix_provider_instance_name(file_path: str) -> None:
    path = Path(file_path)
    data = bytearray(path.read_bytes())
    digest = hashlib.sha256(data).hexdigest()
    if digest == FIXED_SHA256:
        return
    if digest != STOCK_SHA256:
        raise ValueError(f'{path.name}: unexpected sha256 {digest}, the instance rename fits only {STOCK_SHA256}')
    for offset, old, new in INSTANCE_NAME_PATCH:
        before, after = bytes.fromhex(old), bytes.fromhex(new)
        if data[offset:offset + len(before)] != before:
            raise ValueError(f'{path.name}: bytes at 0x{offset:x} differ from the stock image')
        data[offset:offset + len(after)] = after
    digest = hashlib.sha256(data).hexdigest()
    if digest != FIXED_SHA256:
        raise ValueError(f'{path.name}: patched sha256 {digest} != {FIXED_SHA256}')
    path.write_bytes(data)
