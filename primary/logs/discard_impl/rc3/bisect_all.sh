#!/usr/bin/env bash
# For every slower game: old arm + one lever ON, new arm + one lever OFF. Records turns.
R=/workspaces/MagicDeckTester
K=$R/logs/discard_impl/rc3
OUT=$K/bisect_mine.txt
: > $OUT
LEVERS="MTG_DRAGONSTORM_ORDER_OPAQUE MTG_PAYABLE_ORDER_RITUALS MTG_COLOR_EXACT_SCALED MTG_ATTACK_TAP_NEED_EXACT MTG_ATTACK_TAP_ABILITY_DISCOUNT MTG_ROLLOUT_LAND_ACT_COLOR MTG_SOLVE_VERIFIED_WIN MTG_EDF_GORGE_RED_SIZING MTG_SOLVE_PENDING_AURA_EXACT"
run_one() {
  deck=$1; cell=$2; gi=$3; seed=$4; mode=$5; lev=$6
  if [ $mode = on ]; then
    t=$(LEVER_ON=$lev $K/rg.sh $deck $cell $gi $seed old - 2>&1 | awk '/avg \(turns\)/{print $4}')
  else
    t=$(LEVER_OFF=$lev $K/rg.sh $deck $cell $gi $seed new - 2>&1 | awk '/avg \(turns\)/{print $4}')
  fi
  echo "$deck $cell gi=$gi $mode $lev $t"
}
export -f run_one
export K
for d in ${DECKS:-fivecolour stompy bruna giants}; do
  awk '{g=$3; sub("gi=","",g); s=$6; sub("seed=","",s); print $1, $2, g, s}' $K/slower_$d.txt
done | while read deck cell gi seed; do
  for l in $LEVERS; do
    echo "$deck $cell $gi $seed on $l"
    echo "$deck $cell $gi $seed off $l"
  done
done | xargs -P 3 -L 1 bash -c 'run_one "$@"' _ >> $OUT
sort $OUT -o $OUT
echo done >> $OUT
