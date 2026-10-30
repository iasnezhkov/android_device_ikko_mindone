# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
import subprocess
import tempfile

bridge = Path(__file__).resolve().parents[1]
stub = Path(__file__).resolve().parent / 'stub'
repository = bridge.parents[4]
rig = repository / 'tools/rigs/bridge-fingerprint-reset-check.py'
with tempfile.TemporaryDirectory(prefix='mindone-reset-contract-') as temporary:
    root = Path(temporary)
    fixture = rig.with_suffix('.cpp').read_text()
    begin = fixture.index('struct Callback {')
    end = fixture.index('class SessionBridge {', begin)
    fixture = fixture[:begin] + '''#include "LockoutTracker.h"
using Callback = mindone::LockoutTracker::Callback;
''' + fixture[end:]
    fixture = fixture.replace('    std::atomic<bool> dead_{false};', '''    std::shared_ptr<mindone::LockoutTracker> lockout_ =
        std::make_shared<mindone::LockoutTracker>(0, std::getenv("MINDONE_FP_TEST_DATA"));
    std::atomic<bool> dead_{false};''')
    fixture = fixture.replace('test(100000, lo, true, true);',
                              'test(100000, lo, true, false); test(100000, 100000, true, true);')
    runner = root / rig.name
    runner.write_text(rig.read_text().replace("'-std=c++20',", "'-std=c++20', '-pthread', '-I', " +
                      repr(str(stub)) + ", '-I', " + repr(str(bridge)) + ','))
    runner.with_suffix('.cpp').write_text(fixture)
    data = root / 'data'
    data.mkdir()
    environment = dict(os.environ, MINDONE_FP_TEST_DATA=str(data))
    result = subprocess.run(['python3', str(runner), '--source', str(bridge / 'SessionBridge.cpp'),
                             '--fault-controls', '--output', str(root / 'results')], env=environment, timeout=90)
    if result.returncode:
        for log in (root / 'results').glob('*.log'):
            print(log.name, log.read_text())
        result.check_returncode()
