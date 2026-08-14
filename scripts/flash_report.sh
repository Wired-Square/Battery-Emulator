#!/bin/zsh
# Builds every environment and reports flash against the Phase 0 high-water mark.
# Exit 1 if any tight-three board regressed.
set -u
cd "$(dirname "$0")/.." || exit 1
PIO=~/.platformio/penv/bin/pio

# Phase 0 baseline (HEAD 836c8663) — the high-water mark. Do not raise these.
typeset -A HIGH_WATER=(
  lilygo_330             1917432
  esp32devkit_330        1916704
)
ENVS=(${(f)"$(sed -n 's/^\[env:\(.*\)\]$/\1/p' platformio.ini)"})
(( ${#ENVS} )) || { echo "no [env:] sections found in platformio.ini"; exit 1; }

fail=0
printf "%-24s %10s %10s %10s\n" ENV FLASH MARK DELTA
for e in $ENVS; do
  out=$($PIO run -e "$e" 2>&1) || { printf "%-24s BUILD FAILED\n" "$e"; echo "$out" | tail -20; fail=1; continue; }
  # Hybrid builds print the size table more than once; the last one is the image.
  used=$(echo "$out" | grep -oE 'Flash: \[[= ]*\] +[0-9.]+% \(used [0-9]+' | grep -oE '[0-9]+$' | tail -1)
  [[ -n "$used" ]] || { printf "%-24s NO FLASH FIGURE\n" "$e"; fail=1; continue; }
  mark=${HIGH_WATER[$e]:-}
  if [[ -n "$mark" ]]; then
    delta=$((used - mark))
    printf "%-24s %10s %10s %+10d\n" "$e" "$used" "$mark" "$delta"
    (( delta > 0 )) && { echo "  ^ REGRESSION: $e exceeds high-water mark by $delta bytes"; fail=1; }
  else
    printf "%-24s %10s %10s %10s\n" "$e" "$used" "-" "-"
  fi
done
exit $fail
