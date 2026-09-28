#!/vendor/bin/sh

MODDIR=/vendor/lib/modules
LOAD=$MODDIR/modules.load
SCHEDULE=/vendor/etc/mindone-insmod.schedule

legacy() {
  modprobe -a -d "$MODDIR" $(cat "$LOAD")
  setprop vendor.all.modules.ready 1
}

if [ $# -ne 1 ]; then
  echo "usage: mindone-fast-insmod.sh <cfg>" >&2
  exit 2
fi

case " $(cat /proc/cmdline) " in
  *" mindone_fast_insmod=1 "*) ;;
  *) legacy; exit 0 ;;
esac

if [ ! -f "$SCHEDULE" ] || [ ! -f "$LOAD" ]; then
  legacy
  exit 0
fi

sched_mods=$(cut -d' ' -f2- "$SCHEDULE" | tr ' ' '\n' | sort -u)
load_mods=$(sort -u "$LOAD")
if [ "$sched_mods" != "$load_mods" ]; then
  legacy
  exit 0
fi

ok=1
while [ "$ok" -eq 1 ] && read -r tag rest; do
  case "$tag" in
    L) printf '%s\n' $rest | xargs -r -P8 -n1 modprobe -d "$MODDIR" || ok=0 ;;
    T) modprobe -a -d "$MODDIR" $rest || ok=0 ;;
  esac
done < "$SCHEDULE"

if [ "$ok" -ne 1 ]; then
  legacy
  exit 0
fi

setprop vendor.all.modules.ready 1
