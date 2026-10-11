#!/usr/bin/env bash
# Reproduce both arms of every slower hinata + selesnya game (no logs). Output: repro_all.out
H=/workspaces/MagicDeckTester/logs/discard_impl/rc3/hinata
RC=/workspaces/MagicDeckTester/logs/discard_impl/rc3
jobs=$H/repro_jobs.txt
: > "$jobs"
for f in $RC/slower_hinata.txt $RC/slower_selesnya.txt; do
  while read -r deck cell gi old new seed; do
    gi=${gi#gi=}; seed=${seed#seed=}
    for arm in old new; do
      echo "$deck $cell $gi $seed $arm" >> "$jobs"
    done
  done < "$f"
done
xargs -P 4 -L 1 bash -c 'bash '"$H"'/rg.sh "$0" "$1" "$2" "$3" "$4" - ""' < "$jobs" > $H/repro_all.out 2>&1
sort $H/repro_all.out -o $H/repro_all.out
echo done
