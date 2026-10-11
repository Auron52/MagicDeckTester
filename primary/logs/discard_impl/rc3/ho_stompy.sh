#!/usr/bin/env bash
K=/workspaces/MagicDeckTester/logs/discard_impl/rc3
for gi in 416 1118 2036 3068 3504 3746 4612 4847 5172 6068 6828 7960 8172 8401 8514 8979 9287 2199; do
  s=$((9300001+gi))
  for a in old new; do $K/rg.sh stompy d0 $gi $s $a $K/stompy/h${gi}_d0_$a > /dev/null 2>&1; done
done
