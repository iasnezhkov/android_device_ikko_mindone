#!/vendor/bin/sh
D=/data/vendor/gauge
S=/sys/class/power_supply/battery
R=/dev/gauge-learned
N="learned_aging_bp learned_cycles_x100"

valid() {
  case "$2" in ''|*[!0-9]*) return 1 ;; esac
  case "$2" in 0|[1-9]*) ;; *) return 1 ;; esac
  [ "${#2}" -le 10 ] && [ "$2" -le 2147483647 ] || return 1
  case "$1" in
    learned_aging_bp) [ "$2" -ge 7000 ] && [ "$2" -le 10000 ] ;;
    learned_cycles_x100) return 0 ;;
    *) return 1 ;;
  esac
}

read_pair() {
  aging=$(cat "$S/learned_aging_bp" 2>/dev/null) &&
    cycles=$(cat "$S/learned_cycles_x100" 2>/dev/null) &&
    valid learned_aging_bp "$aging" && valid learned_cycles_x100 "$cycles"
}

size_matches() {
  bytes=$(wc -c < "$1") || return 1
  [ "$bytes" -eq "$2" ] 2>/dev/null
}

restore() (
  mkdir "$R/lock" 2>/dev/null || return 1
  trap 'rm -f "$R/ready.new.$$"; rmdir "$R/lock" || exit 1' EXIT
  trap 'exit 1' HUP INT TERM
  rm -f "$R/ready" || return 1
  read_pair || return 0
  saved_aging=$aging
  saved_cycles=$cycles
  restore_aging=0
  restore_cycles=0
  if [ -e "$D/learned_state" ] || [ -L "$D/learned_state" ]; then
    [ -f "$D/learned_state" ] && [ ! -L "$D/learned_state" ] || return 1
    {
      IFS= read -r state || return 1
      extra_line=
      if IFS= read -r extra_line || [ -n "$extra_line" ]; then return 1; fi
    } < "$D/learned_state"
    IFS=' ' read -r saved_aging saved_cycles extra <<EOF
$state
EOF
    [ "$state" = "$saved_aging $saved_cycles" ] && [ -z "$extra" ] || return 1
    valid learned_aging_bp "$saved_aging" && valid learned_cycles_x100 "$saved_cycles" || return 1
    size_matches "$D/learned_state" "$((${#saved_aging} + ${#saved_cycles} + 2))" || return 1
    restore_aging=1
    restore_cycles=1
  else
    for n in $N; do
      if [ -e "$D/$n" ] || [ -L "$D/$n" ]; then
        [ -f "$D/$n" ] && [ ! -L "$D/$n" ] || return 1
        v=$(cat "$D/$n") && valid "$n" "$v" || return 1
        size_matches "$D/$n" "$((${#v} + 1))" || return 1
        case "$n" in
          learned_aging_bp) saved_aging=$v; restore_aging=1 ;;
          learned_cycles_x100) saved_cycles=$v; restore_cycles=1 ;;
        esac
      fi
    done
  fi
  for n in $N; do
    case "$n" in
      learned_aging_bp) v=$saved_aging; restore_field=$restore_aging ;;
      learned_cycles_x100) v=$saved_cycles; restore_field=$restore_cycles ;;
    esac
    if [ "$restore_field" = 1 ]; then
      printf '%s\n' "$v" > "$S/$n" || return 1
      actual=$(cat "$S/$n") && valid "$n" "$actual" || return 1
      case "$n" in
        learned_aging_bp) [ "$actual" = "$v" ] || return 1 ;;
        learned_cycles_x100)
          [ "$actual" -le "$v" ] && [ "$actual" -ge "$((v > 0 ? v - 1 : 0))" ] || return 1
          ;;
      esac
    fi
  done
  read_pair || return 1
  printf '%s\n' ready > "$R/ready.new.$$" && mv "$R/ready.new.$$" "$R/ready"
)

save() (
  mkdir "$R/lock" 2>/dev/null || return 1
  trap 'rm -f "$D/learned_state.new.$$"; rmdir "$R/lock" || exit 1' EXIT
  trap 'exit 1' HUP INT TERM
  [ "$(cat "$R/ready" 2>/dev/null)" = ready ] || return 0
  read_pair || return 1
  printf '%s %s\n' "$aging" "$cycles" > "$D/learned_state.new.$$" &&
    mv "$D/learned_state.new.$$" "$D/learned_state"
)

case "$1" in
  restore)
    restore
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
