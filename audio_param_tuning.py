#!/usr/bin/env python3
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Speech tuning applied on top of the device's own extracted audio_param blobs.
#
# The stock Speech_AudioParam.xml is ~600 KB of MediaTek speech tuning tables and
# belongs to MediaTek, so it is not carried in this repository -- it is extracted
# from your own device like every other proprietary file. What is ours is three
# values in it, and they live here rather than in a copy of the vendor file.
#
# What is changed and why:
#
#   1. Speech_AudioParam.xml, the first "speech_mode_para" of ParamUnit 0 and 12
#      (the handset paths, media and voice). Fields 32..42 and 44..47 carry the
#      uplink DRC block, which the stock tables leave at zero -- the compressor
#      is present in the firmware but switched off. With it off, the handset
#      uplink clips on loud talkers well before the far end hears a normal level.
#      Turning it on with a moderate curve costs nothing and removes the clipping.
#
#   2. SpeechVol_AudioParam.xml, "ul_gain" 23 -> 17. The stock uplink gain drives
#      the same path into limiting on its own; the DRC above only shapes what
#      survives that. Backing the gain off six steps is what actually stops it,
#      and the DRC then handles the peaks that remain.
#
# Only our values appear below. The fields we do not touch are left exactly as
# the device's own file has them, so this stays correct across firmware
# revisions instead of pinning a whole table to one of them.

from __future__ import annotations

import re
import sys
from pathlib import Path

# ParamUnit param_id -> {field index: our value}, applied to the FIRST
# speech_mode_para of that unit. Index 43 is deliberately absent: it is zero in
# the stock tables and stays zero.
SPEECH_MODE_PARA_TUNING: dict[str, dict[int, str]] = {
    "0": {
        32: "0x95",
        33: "0x5a", 34: "0x5a", 35: "0x5a", 36: "0x5a", 37: "0x5a",
        38: "0xf", 39: "0xf", 40: "0xf", 41: "0xf", 42: "0xf",
        44: "0x28", 45: "0x50", 46: "0x78", 47: "0xa0",
    },
    "12": {
        32: "0x95",
        33: "0x5a", 34: "0x5a", 35: "0x5a", 36: "0x5a", 37: "0x5a",
    },
}

SPEECH_MODE_PARA_FIELDS = 64

# ParamUnit param_id -> our uplink gain. Only the handset path is touched; the
# other units in this file are other routes and keep the device's own values.
UL_GAIN_TUNING: dict[str, str] = {"0": "17"}

_UNIT_RE = re.compile(r'<ParamUnit\s+param_id="(\d+)"')
_VALUE_RE = re.compile(r'(value=")([^"]*)(")')


def tune_speech_audio_param(path: str | Path) -> int:
    """Enable the uplink DRC block on the handset paths. Returns units changed."""
    path = Path(path)
    lines = path.read_text(encoding="utf-8").split("\n")

    unit: str | None = None
    seen_in_unit = 0
    changed = 0

    for i, line in enumerate(lines):
        unit_match = _UNIT_RE.search(line)
        if unit_match:
            unit = unit_match.group(1)
            seen_in_unit = 0
            continue

        if "speech_mode_para" not in line:
            continue

        seen_in_unit += 1
        tuning = SPEECH_MODE_PARA_TUNING.get(unit or "")
        if tuning is None or seen_in_unit != 1:
            continue

        value_match = _VALUE_RE.search(line)
        if not value_match:
            raise ValueError(f"{path}: speech_mode_para of unit {unit} has no value")

        fields = [f.strip() for f in value_match.group(2).split(",")]
        if len(fields) != SPEECH_MODE_PARA_FIELDS:
            # A firmware revision that reshaped this table is not one these
            # field indices describe. Refuse rather than write into the wrong slot.
            raise ValueError(
                f"{path}: speech_mode_para of unit {unit} has {len(fields)} fields, "
                f"expected {SPEECH_MODE_PARA_FIELDS} -- refusing to patch"
            )

        for index, value in tuning.items():
            fields[index] = value

        lines[i] = line[: value_match.start(2)] + ",".join(fields) + line[value_match.end(2) :]
        changed += 1

    if changed != len(SPEECH_MODE_PARA_TUNING):
        raise ValueError(
            f"{path}: patched {changed} units, expected {len(SPEECH_MODE_PARA_TUNING)}"
        )

    path.write_text("\n".join(lines), encoding="utf-8")
    return changed


def tune_speech_vol_audio_param(path: str | Path) -> int:
    """Back the handset uplink gain off so the DRC above shapes rather than rescues."""
    path = Path(path)
    lines = path.read_text(encoding="utf-8").split("\n")

    unit: str | None = None
    changed = 0

    for i, line in enumerate(lines):
        unit_match = _UNIT_RE.search(line)
        if unit_match:
            unit = unit_match.group(1)
            continue

        if "ul_gain" not in line:
            continue

        value = UL_GAIN_TUNING.get(unit or "")
        if value is None:
            continue

        value_match = _VALUE_RE.search(line)
        if not value_match:
            raise ValueError(f"{path}: ul_gain of unit {unit} has no value")

        lines[i] = line[: value_match.start(2)] + value + line[value_match.end(2) :]
        changed += 1

    if changed != len(UL_GAIN_TUNING):
        raise ValueError(
            f"{path}: set {changed} ul_gain entries, expected {len(UL_GAIN_TUNING)}"
        )

    path.write_text("\n".join(lines), encoding="utf-8")
    return changed


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(
            "usage: audio_param_tuning.py <Speech_AudioParam.xml> <SpeechVol_AudioParam.xml>",
            file=sys.stderr,
        )
        return 2
    units = tune_speech_audio_param(argv[1])
    gains = tune_speech_vol_audio_param(argv[2])
    print(f"speech_mode_para units tuned: {units}; ul_gain entries set: {gains}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
