# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
import re
import subprocess
import tempfile


def extract(source, signature):
    if source.count(signature) != 1:
        raise ValueError(f"Expected one definition of {signature}")
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


def main():
    tests_dir = Path(__file__).resolve().parent
    bridge_dir = tests_dir.parent
    device_root = bridge_dir.parents[1]
    audio_hf_dir = device_root / "audio-hf"
    stub_dir = tests_dir / "stub"

    audio_hw = (audio_hf_dir / "audio_hw.c").read_text(encoding="utf-8")
    template = (tests_dir / "sco_backend.c").read_text(encoding="utf-8")
    out_enum = re.search(r"enum output_device \{.*?\n\};", audio_hw, re.S).group()
    in_enum = re.search(r"enum input_device \{.*?\n\};", audio_hw, re.S).group()
    set_sco_fn = extract(audio_hw, "int mindone_audio_set_sco(")
    backend_c = (
        template
        .replace("PRODUCTION_ENUMS", out_enum + "\n\n" + in_enum)
        .replace("PRODUCTION_SET_SCO", set_sco_fn)
    )

    cc = os.environ.get("CC", "clang")
    cxx = os.environ.get("CXX", "clang++")
    with tempfile.TemporaryDirectory(prefix="mindone-audio-sco-") as temporary:
        root = Path(temporary)
        backend_src = root / "sco_backend.c"
        backend_obj = root / "sco_backend.o"
        binary = root / "sco_check"
        backend_src.write_text(backend_c, encoding="utf-8")

        subprocess.run([
            cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(stub_dir), "-I", str(audio_hf_dir),
            "-c", str(backend_src), "-o", str(backend_obj),
        ], check=True, timeout=60)

        subprocess.run([
            cxx, "-std=c++20", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-Wthread-safety", "-pthread", "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I", str(stub_dir), "-I", str(audio_hf_dir), "-I", str(bridge_dir),
            str(bridge_dir / "Backend.cpp"),
            str(bridge_dir / "BluetoothMindone.cpp"),
            str(bridge_dir / "TelephonyMindone.cpp"),
            str(bridge_dir / "ModuleMindone.cpp"),
            str(tests_dir / "sco_check.cpp"),
            str(backend_obj),
            "-o", str(binary),
        ], check=True, timeout=60)

        env = dict(
            os.environ,
            ASAN_OPTIONS="detect_leaks=0:abort_on_error=1",
            UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
        )
        subprocess.run([str(binary)], check=True, env=env, timeout=20)


if __name__ == "__main__":
    main()
