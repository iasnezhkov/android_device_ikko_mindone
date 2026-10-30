# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
import subprocess
import tempfile

bridge = Path(__file__).resolve().parents[1]
stub = Path(__file__).resolve().parent / 'stub'
with tempfile.TemporaryDirectory(prefix='mindone-lockout-') as temporary:
    root = Path(temporary)
    binary = root / 'check'
    subprocess.run(['clang++', '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-pthread', '-fsanitize=address,undefined', '-I', str(stub), '-I', str(bridge),
                    str(Path(__file__).with_suffix('.cpp')), '-o', str(binary)], check=True)
    data = root / 'data'
    data.mkdir()
    environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:abort_on_error=1',
                       UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    subprocess.run([str(binary), str(data)], check=True, env=environment, timeout=20)
