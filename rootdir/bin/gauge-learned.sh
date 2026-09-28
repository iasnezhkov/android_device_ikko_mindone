#!/vendor/bin/sh
D=/data/vendor/gauge
S=/sys/class/power_supply/battery
N="learned_aging_bp learned_cycles_x100"

save() {
  for n in $N; do
    v=$(cat "$S/$n" 2>/dev/null) || continue
    [ -n "$v" ] || continue
    echo "$v" > "$D/$n.new" && mv "$D/$n.new" "$D/$n"
  done
}

case "$1" in
  restore)
    for n in $N; do
      if [ -s "$D/$n" ]; then cat "$D/$n" > "$S/$n"; fi
    done
    ;;
  save)
    save
    ;;
  loop)
    while true; do
      sleep 3600
      save
    done
    ;;
  *)
    echo "usage: gauge-learned.sh restore|save|loop" >&2
    exit 2
    ;;
esac
