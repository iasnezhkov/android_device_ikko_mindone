#!/vendor/bin/sh
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

D=/metadata/mindone
T=/system/bin/timeout

rm -rf $D/prev 2>/dev/null
[ -d $D/cur ] && mv $D/cur $D/prev
mkdir -p $D/cur
D=$D/cur

if [ -d /sys/fs/pstore ] && [ -n "$(ls /sys/fs/pstore 2>/dev/null)" ]; then
    mkdir -p $D/pstore
    for f in /sys/fs/pstore/*; do
        [ -f "$f" ] && cat "$f" > "$D/pstore/$(basename "$f")" 2>/dev/null
    done
    sync
fi

LIMIT=$(cat /proc/cmdline 2>/dev/null | tr ' ' '\n' | grep '^androidboot\.mindone\.bootwatch\.limit=' | cut -d= -f2)
[ -z "$LIMIT" ] && LIMIT=$(getprop ro.boot.mindone.bootwatch.limit)
[ -z "$LIMIT" ] && LIMIT=$(getprop ro.vendor.mindone.bootwatch.limit)
[ -z "$LIMIT" ] && LIMIT=600

snap() {
    S=$1
    { echo "== uptime =="; cat /proc/uptime; echo "== cmdline =="; cat /proc/cmdline; echo "== boot_completed=$(getprop sys.boot_completed) bootanim=$(getprop init.svc.bootanim) sf=$(getprop init.svc.surfaceflinger) zygote=$(getprop init.svc.zygote) adbd=$(getprop init.svc.adbd) usb.config=$(getprop sys.usb.config) usb.state=$(getprop sys.usb.state) usb.controller=$(getprop sys.usb.controller) =="
      echo "== udc =="; ls /sys/class/udc 2>&1; cat /sys/class/udc/*/state 2>&1; cat /config/usb_gadget/g1/UDC 2>&1
      echo "== dev =="; ls -la /dev/dri /dev/mali0 /dev/ion /dev/dma_heap /dev/tee* /dev/tkcore* /dev/trusty* 2>&1
      echo "== mapper =="; ls -la /dev/block/mapper 2>&1; echo "== mounts =="; cat /proc/mounts
      echo "== ps =="; ps -A 2>&1; echo "== init stack =="; cat /proc/1/stack 2>&1; cat /proc/1/wchan 2>&1; echo
      echo "== vold stack =="; for p in $(pidof vold); do cat /proc/$p/stack 2>&1; done; echo "== services =="; $T 15 /system/bin/service list 2>&1
    } > $D/state-$S.txt 2>&1
    getprop > $D/getprop-$S.txt 2>&1
}

elapsed() { set -- $(cat /proc/uptime); echo ${1%.*}; }
START=$(elapsed)
BOOTED=

waitto() {
    [ "$1" -gt "$LIMIT" ] && return 1
    while :; do
        if [ "$(getprop sys.boot_completed)" = "1" ]; then BOOTED=1; return 1; fi
        [ $(( $(elapsed) - START )) -ge "$1" ] && return 0
        sleep 5
    done
}

waitto 30  && snap 030
waitto 90  && snap 090
waitto 240 && snap 240
[ -z "$BOOTED" ] && waitto "$LIMIT" && snap fin

if [ "$(getprop sys.boot_completed)" = "1" ]; then
    { echo "boot OK"; echo "uptime=$(cat /proc/uptime)"; echo "slot=$(getprop ro.boot.slot_suffix)"; } > $D/ok.txt 2>&1
    sync
    exit 0
fi

$T 20 /system/bin/logcat -d -b all > $D/logcat.txt 2>&1
$T 20 /system/bin/logcat -d -b events > $D/logcat-events.txt 2>&1
dmesg > $D/dmesg.txt 2>&1
grep -ai 'avc:' $D/dmesg.txt > $D/avc.txt 2>&1
$T 20 /system/bin/dumpsys SurfaceFlinger --skip-color > $D/sf.txt 2>&1
cat /data/vendor/t6/tkcore.log > $D/tkcore.txt 2>&1
sync
echo "boot not completed after ${LIMIT}s on slot $(getprop ro.boot.slot_suffix), rebooting for evidence" > $D/reboot.txt
sync
sleep 2
setprop sys.powerctl reboot
sleep 15
sync
echo 1 > /proc/sys/kernel/sysrq
echo b > /proc/sysrq-trigger
sleep 30
/system/bin/reboot 2>/dev/null
exit 0
