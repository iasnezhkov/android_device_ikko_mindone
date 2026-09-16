#!/vendor/bin/sh
# Collect boot evidence into /metadata/mindone (survives reboot), then reboot if boot never completed.
# 05.09 (F3758): runs from the VENDOR shell — system tools are NOT on PATH ("logcat: not found"), and
# `reboot` never fired; use absolute /system/bin paths, bounded by timeout, and reboot through init
# (setprop sys.powerctl) BEFORE any dumpsys that can hang when the service is missing.
#
# 🔴 08.09 (F3925) - three fixes, each backed by a measurement, not a guess:
#
# 1. SET ROTATION. Evidence used to be written to fixed names, and the next boot would overwrite it.
#    On 08.09 the monitor honestly brought the device back from a black screen on _b to _a, but only
#    ONE file (reboot-240.txt) survived from the failed boot - everything else was overwritten by _a
#    coming back within the same four minutes. That is exactly why logs "got lost" all day. Now the
#    set is written to $D/cur, and on startup the previous one moves to $D/prev: after a self-recovery
#    the evidence of the failure sits intact in prev.
#
# 2. WAIT LIMIT FROM CMDLINE (androidboot.mindone.bootwatch.limit=<sec>). The default of 600s stays
#    for everyday builds - the first boot after a wipe with dexopt is legitimately longer (F3796), and
#    a short limit would mistake it for broken. But in an experiment on _b, ten minutes to recovery is
#    a cost per iteration we cannot afford: the experiment starts at 150 and the device comes back in 2.5 minutes.
#
# 3. A HEALTHY BOOT NO LONGER WRITES 7.5 MB. Previously the full set (logcat of all buffers, dumpsys,
#    dmesg) was written to /metadata on EVERY boot, including successful ones - and the partition is small.
#    Now, with sys.boot_completed=1, only ok.txt is written: the same data can be pulled from the live
#    device over adb, so there is no point filling the partition for it.

D=/metadata/mindone
T=/system/bin/timeout

rm -rf $D/prev 2>/dev/null
[ -d $D/cur ] && mv $D/cur $D/prev
mkdir -p $D/cur
D=$D/cur

# Wait limit. Read via THREE paths, from most reliable to most convenient, because on this
# device it is not proven that init turns androidboot.* from cmdline into a property: all
# androidboot keys live in /proc/bootconfig, while /proc/cmdline (1245 bytes) has zero of them (F3926).
# So the first attempt is a direct read of cmdline - it does not depend on init's behavior at all.
# 🔴 08.09 (F3952): FIRST THING, save the PREVIOUS boot's log from pstore.
# Why: ramoops is wired up as a CONSOLE (`printk: console [ramoops-1] enabled`), so every
# new boot overwrites the buffer - by the time anyone gets to it, the evidence is already gone.
# This is exactly how the causes of three failed 6.12 boots on 08.09 were lost: the device went
# into fastboot, and after returning to a working system the buffer had already been overwritten.
# The copy is made BEFORE everything else and goes to /metadata, which survives both a /data
# rollback and a reboot.
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

# Seconds since the monitor started. Time is taken from the kernel CLOCK (/proc/uptime), not from an
# iteration count: counting iterations already produced a false conclusion once, when a loop step
# turned out longer than expected.
elapsed() { set -- $(cat /proc/uptime); echo ${1%.*}; }
START=$(elapsed)
BOOTED=

# Wait until the $1-second mark. Returns 0 if the mark was reached (boot did not complete),
# 1 if either boot completed or the mark is past the limit.
waitto() {
    [ "$1" -gt "$LIMIT" ] && return 1
    while :; do
        if [ "$(getprop sys.boot_completed)" = "1" ]; then BOOTED=1; return 1; fi
        [ $(( $(elapsed) - START )) -ge "$1" ] && return 0
        sleep 5
    done
}

# Three snapshots, so an early stall can be told apart from a late one.
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
# 1) ask init nicely; 2) if init is stuck inside a builtin command (e.g. mount_all
# --late waiting on vold), it will never read sys.powerctl - fall back to the KERNEL: magic sysrq 'b'
# = emergency_restart, userspace is not involved (CONFIG_MAGIC_SYSRQ=y; init.rc
# sets /proc/sys/kernel/sysrq to 0, we turn it back on).
setprop sys.powerctl reboot
sleep 15
sync
echo 1 > /proc/sys/kernel/sysrq
echo b > /proc/sysrq-trigger
sleep 30
/system/bin/reboot 2>/dev/null
exit 0
