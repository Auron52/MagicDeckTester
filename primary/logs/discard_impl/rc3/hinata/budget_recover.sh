#!/usr/bin/env bash
# Recovery check for the searched slower games: both arms at 1x/4x/16x budget.
H=/workspaces/MagicDeckTester/logs/discard_impl/rc3/hinata
OUT=$H/budget.out
jobs=$H/budget_jobs.txt
: > "$jobs"
# deck cell gi seed base_budget
for spec in "hinata d3 315 1325316 10" "hinata ship 42 1320043 20" "hinata ship 58 1320059 20" "hinata ship 191 1320192 20"; do
  set -- $spec
  for m in 4 16; do
    b=$(( $5 * m ))
    for arm in new old; do echo "$1 $2 $3 $4 $arm - $b" >> "$jobs"; done
  done
done
xargs -P 4 -L 1 bash -c 'bash '"$H"'/rg.sh "$0" "$1" "$2" "$3" "$4" "$5" "$6"' < "$jobs" > "$OUT" 2>&1
sort "$OUT" -o "$OUT"
echo done
