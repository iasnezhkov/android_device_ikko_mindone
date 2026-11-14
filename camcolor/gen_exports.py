#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
import re
import struct
import sys
from pathlib import Path

USAGE = '''usage: gen_exports.py <stock libcameracustom.camera.isp.so> [--check exports.def]
Reads the dynamic symbol table of the stock library and prints exports.def:
CAMCOLOR_WRAPPED for createInstance (slot 0, implemented in shim.cpp),
CAMCOLOR_FORWARD for every other defined function (trampoline in forward.S),
CAMCOLOR_DATA for defined data objects, which the shim does not re-export.
With --check, compares the result with the given file and exits 1 on a difference.'''

WRAPPED = '_ZN11NSIspTuning15IspTuningCustom14createInstanceEjji'
LINKER_DEFINED = {'__bss_start', '_edata', '_end'}
STT_NOTYPE, STT_OBJECT, STT_FUNC = 0, 1, 2
STB_GLOBAL, STB_WEAK = 1, 2
SHT_DYNSYM = 11
SHN_UNDEF = 0
STV_DEFAULT = 0


def dynamic_symbols(data):
    if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
        raise SystemExit('not a little-endian ELF64 file')
    shoff, = struct.unpack_from('<Q', data, 0x28)
    shentsize, shnum = struct.unpack_from('<HH', data, 0x3a)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shentsize) for i in range(shnum)]
    for sh_name, sh_type, _, _, offset, size, link, _, _, entsize in sections:
        if sh_type != SHT_DYNSYM:
            continue
        strtab = sections[link]
        str_offset = strtab[4]
        for i in range(1, size // entsize):
            name_off, info, other, shndx, _, sym_size = struct.unpack_from('<IBBHQQ', data, offset + i * entsize)
            end = data.index(b'\0', str_offset + name_off)
            name = data[str_offset + name_off:end].decode()
            yield name, info & 0xf, info >> 4, other & 3, shndx, sym_size
        return
    raise SystemExit('no .dynsym section')


def build(path):
    functions = []
    data_objects = []
    for name, kind, bind, visibility, shndx, _ in dynamic_symbols(Path(path).read_bytes()):
        if shndx == SHN_UNDEF or bind not in (STB_GLOBAL, STB_WEAK) or visibility != STV_DEFAULT:
            continue
        if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', name):
            raise SystemExit(f'unsupported symbol name: {name}')
        if kind == STT_FUNC:
            functions.append((name, 'weak' if bind == STB_WEAK else 'globl'))
        elif kind == STT_OBJECT:
            data_objects.append(name)
        elif not (kind == STT_NOTYPE and name in LINKER_DEFINED):
            raise SystemExit(f'unsupported symbol type {kind}: {name}')
    names = [name for name, _ in functions]
    if WRAPPED not in names:
        raise SystemExit(f'{WRAPPED} is not exported')
    if len(set(names)) != len(names):
        raise SystemExit('duplicate exported function')
    lines = [f'CAMCOLOR_WRAPPED(0, {WRAPPED})']
    index = 1
    for name, bind in sorted(functions):
        if name == WRAPPED:
            continue
        lines.append(f'CAMCOLOR_FORWARD({index}, {name}, {bind})')
        index += 1
    lines.extend(f'CAMCOLOR_DATA({name})' for name in sorted(data_objects))
    return '\n'.join(lines) + '\n'


def main(argv):
    if len(argv) == 2 and argv[1] not in ('-h', '--help'):
        sys.stdout.write(build(argv[1]))
        return 0
    if len(argv) == 4 and argv[2] == '--check':
        if Path(argv[3]).read_text() != build(argv[1]):
            sys.stderr.write(f'{argv[3]} does not match the exports of {argv[1]}\n')
            return 1
        return 0
    sys.stderr.write(USAGE + '\n')
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
