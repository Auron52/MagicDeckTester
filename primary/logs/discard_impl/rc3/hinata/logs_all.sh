#!/usr/bin/env bash
# Log both arms of every slower game of <deck> (default hinata) into <deck dir>/L/<cell>_<gi>_<arm>/
deck=${1:-hinata}
H=/workspaces/MagicDeckTester/logs/discard_impl/rc3/hinata
RC=/workspaces/MagicDeckTester/logs/discard_impl/rc3
OUT=$RC/$deck/L
mkdir -p "$OUT"
jobs=$OUT/jobs.txt
: > "$jobs"
while read -r d cell gi old new seed; do
  gi=${gi#gi=}; seed=${seed#seed=}
  for arm in old new; do
    echo "$d $cell $gi $seed $arm $OUT/${cell}_${gi}_${arm}" >> "$jobs"
  done
done < $RC/slower_$deck.txt
xargs -P 5 -L 1 bash -c 'bash '"$H"'/rg.sh "$0" "$1" "$2" "$3" "$4" "$5" ""' < "$jobs" > $OUT/run.out 2>&1
echo done
