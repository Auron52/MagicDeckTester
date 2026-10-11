#!/usr/bin/env bash
# For every slower hinata + selesnya game: old arm + ONE lever on, and new arm with ONE lever off.
H=/workspaces/MagicDeckTester/logs/discard_impl/rc3/hinata
RC=/workspaces/MagicDeckTester/logs/discard_impl/rc3
LEVERS="MTG_DRAGONSTORM_ORDER_OPAQUE MTG_PAYABLE_ORDER_RITUALS MTG_COLOR_EXACT_SCALED MTG_ATTACK_TAP_NEED_EXACT MTG_ATTACK_TAP_ABILITY_DISCOUNT MTG_ROLLOUT_LAND_ACT_COLOR MTG_SOLVE_VERIFIED_WIN MTG_EDF_GORGE_RED_SIZING MTG_SOLVE_PENDING_AURA_EXACT"
jobs=$H/bisect_jobs.txt
: > "$jobs"
for f in $RC/slower_hinata.txt $RC/slower_selesnya.txt; do
  while read -r deck cell gi old new seed; do
    gi=${gi#gi=}; seed=${seed#seed=}
    for L in $LEVERS; do
      echo "$deck $cell $gi $seed old $L=1" >> "$jobs"
      echo "$deck $cell $gi $seed new $L=0" >> "$jobs"
    done
  done < "$f"
done
xargs -P 5 -L 1 bash -c 'bash '"$H"'/rg.sh "$0" "$1" "$2" "$3" "$4" - "" "$5"' < "$jobs" > $H/bisect_all.out 2>&1
sort $H/bisect_all.out -o $H/bisect_all.out
echo done
