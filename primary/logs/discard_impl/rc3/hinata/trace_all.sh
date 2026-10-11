#!/usr/bin/env bash
# Trace (head binary, MTG_USER_ORDER_TRACE=2 + MTG_PAYABLE_ORDER_STATS) both arms of every slower game of <deck>.
deck=${1:-hinata}
H=/workspaces/MagicDeckTester/logs/discard_impl/rc3/hinata
RC=/workspaces/MagicDeckTester/logs/discard_impl/rc3
OUT=$RC/$deck/T
mkdir -p "$OUT"
jobs=$OUT/jobs.txt
: > "$jobs"
while read -r d cell gi old new seed; do
  gi=${gi#gi=}; seed=${seed#seed=}
  for arm in old new; do
    echo "$d $cell $gi $seed $arm $OUT/${cell}_${gi}_${arm}.txt" >> "$jobs"
  done
done < $RC/slower_$deck.txt
export BIN=$RC/mtg_head_ccacda4e
xargs -P 5 -L 1 bash -c 'r=$(bash '"$H"'/trace.sh "$0" "$1" "$2" "$3" "$4" "$5" MTG_USER_ORDER_TRACE=2 MTG_PAYABLE_ORDER_STATS=1 MTG_EXEC_TAP_TRACE=1); echo "$0 $1 $2 $4 $r"' < "$jobs" > $OUT/run.out 2>&1
sort $OUT/run.out -o $OUT/run.out
echo done
