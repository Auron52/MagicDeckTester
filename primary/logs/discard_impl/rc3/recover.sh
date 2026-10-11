#!/usr/bin/env bash
# Recovery check: each slower game at d3/b0 (d0 games) both arms; bruna ship gi198 at ship4x + shipb0.
K=/workspaces/MagicDeckTester/logs/discard_impl/rc3
OUT=$K/recover_mine.txt
: > $OUT
run_one() {
  deck=$1; cell=$2; gi=$3; seed=$4; arm=$5
  s=$(date +%s)
  t=$($K/rg.sh $deck $cell $gi $seed $arm - 2>&1 | awk '/avg \(turns\)/{print $4}')
  e=$(date +%s)
  echo "$deck $cell gi=$gi seed=$seed $arm=$t secs=$((e-s))"
}
export -f run_one; export K
{
for d in fivecolour stompy bruna giants; do
  awk '{g=$3; sub("gi=","",g); s=$6; sub("seed=","",s); print $1, $2, g, s}' $K/slower_$d.txt
done | while read deck cell gi seed; do
  if [ $cell = d0 ]; then
    echo "$deck d3b0 $gi $seed new"; echo "$deck d3b0 $gi $seed old"
  else
    echo "$deck ship4x $gi $seed new"; echo "$deck ship4x $gi $seed old"
    echo "$deck shipb0 $gi $seed new"; echo "$deck shipb0 $gi $seed old"
  fi
done
} | xargs -P 3 -L 1 bash -c 'run_one "$@"' _ >> $OUT
sort $OUT -o $OUT
echo done >> $OUT
