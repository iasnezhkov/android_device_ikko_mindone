#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

import os
import sys

os.environ['MINDONE_DONOR_GROUPS'] = os.environ.get('MINDONE_DONOR_GROUPS') or 'graphics,radio,video,drm,connsys'

os.chdir(os.path.dirname(os.path.abspath(__file__)))
os.execv('./extract-files.py', ['./extract-files.py',
                              '--regenerate_makefiles', *sys.argv[1:]])
