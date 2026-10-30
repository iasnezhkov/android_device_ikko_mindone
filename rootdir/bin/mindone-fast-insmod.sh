#!/vendor/bin/sh
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

exec > /dev/kmsg 2>&1
MODDIR=/vendor/lib/modules
LOAD=$MODDIR/modules.load
SYSTEM_MODDIR=/system/lib/modules
SYSTEM_LOAD=$SYSTEM_MODDIR/modules.load

if [ $# -ne 1 ]; then
  echo "usage: mindone-fast-insmod.sh <cfg>" >&2
  exit 2
fi

status=ok
if [ -s "$SYSTEM_LOAD" ]; then
  modprobe -a -d "$SYSTEM_MODDIR" $(cat "$SYSTEM_LOAD")
  rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "mindone-fast-insmod: modprobe -a of $SYSTEM_LOAD failed with status $rc" > /dev/kmsg
    status="failed:system:$rc"
  fi
else
  echo "mindone-fast-insmod: $SYSTEM_LOAD is missing or empty" > /dev/kmsg
  status="failed:system:missing"
fi
modprobe -a -d "$MODDIR" $(cat "$LOAD")
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "mindone-fast-insmod: modprobe -a of $LOAD failed with status $rc" > /dev/kmsg
  status="failed:$rc"
fi
setprop vendor.mindone.modules.status "$status"
setprop vendor.all.modules.ready 1
