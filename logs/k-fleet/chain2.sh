#!/bin/bash
# Follow-up chain under ONE hold of the box lock: ab2 (Maze/Gods/Mill extra seeds) -> d8 b0 replays of every slower game
# (ab + ab2) -> MTG_NC_SEARCH=1 play-setting replays of the ones that do not recover -> verdicts.
set -u
S=/workspaces/MagicDeckTester/build/scratch/k-fleet
K=/workspaces/MagicDeckTester/.claude/worktrees/k-fleet
rm -f $S/CHAIN2.DONE
flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock bash -c '
  S='$S'; K='$K'
  wt() { if [ $1 = O ]; then echo $K; else echo /workspaces/MagicDeckTester/.claude/worktrees/beam-staged2; fi; }
  run() { # run <bin O|S> <manifest> <windir> <log> [env...]
    b=$1; m=$2; w=$3; l=$4; shift 4
    [ -f $m ] && [ $(python3 -c "import json;print(len(json.load(open(\"$m\"))[\"jobs\"]))") -gt 0 ] || return 0
    rm -rf $w; mkdir -p $w
    (cd $(wt $b) && env MTG_PGO=0 MTG_RSS_CAP_GB=40 "$@" $S/ab/mtg.$b --batch $m --threads $(nproc) --game-log-dir $w > $l.out 2> $l.err)
    echo "$l rc=$?" >> $S/chain2.rc
  }
  run O $S/ab2/ab_O.json $S/ab2/wins_O $S/ab2/batch_O
  python3 $K/logs/k-fleet/summarize.py $S/ab2 > $S/ab2/summary.md
  for d in ab ab2; do
    python3 $K/logs/k-fleet/slower.py mk $S/$d
    for b in O S; do run $b $S/$d/sl/d8_$b.json $S/$d/sl/w_d8_$b $S/$d/sl/d8_$b; done
    python3 $K/logs/k-fleet/slower.py mknc $S/$d
    for b in O S; do run $b $S/$d/sl/nc_$b.json $S/$d/sl/w_nc_$b $S/$d/sl/nc_$b MTG_NC_SEARCH=1; done
    python3 $K/logs/k-fleet/slower.py verdict $S/$d > $S/$d/sl/verdicts.txt
  done'
echo done > $S/CHAIN2.DONE
