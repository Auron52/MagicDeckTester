#!/usr/bin/env bash
# Reproduce both arms of every slower game for my decks, with JSON logs.
R=/workspaces/MagicDeckTester
K=$R/logs/discard_impl/rc3
OUT=$K/repro_mine.txt
: > $OUT
run_one() {
  deck=$1; cell=$2; gi=$3; seed=$4; arm=$5
  ld=$K/$deck/g${gi}_${cell}_$arm
  t=$($K/rg.sh $deck $cell $gi $seed $arm $ld 2>&1 | awk '/avg \(turns\)/{print $4}')
  echo "$deck $cell gi=$gi seed=$seed $arm=$t"
}
export -f run_one
export K
for d in fivecolour stompy bruna giants; do
  awk '{g=$3; sub("gi=","",g); s=$6; sub("seed=","",s); print $1, $2, g, s}' $K/slower_$d.txt
done | while read deck cell gi seed; do
  echo "$deck $cell $gi $seed new"
  echo "$deck $cell $gi $seed old"
done | xargs -P 3 -L 1 bash -c 'run_one "$@"' _ >> $OUT
sort $OUT -o $OUT
echo done >> $OUT
