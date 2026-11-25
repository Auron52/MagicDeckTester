#!/bin/bash
O=/workspaces/MagicDeckTester/build/scratch/heath-morph/t3; S=/workspaces/MagicDeckTester/build/scratch/heath-morph; LOCK=/workspaces/MagicDeckTester/build/scratch/claude_box.lock; W=/workspaces/MagicDeckTester/.claude/worktrees
for arm in base new; do
  if [ $arm = base ]; then B=$S/mtg.origin; WT=$W/land-heath-base; else B=$S/mtg.new; WT=$W/land-heath; fi
  for mode in cp nc; do
    if [ $mode = cp ]; then M=m_all.json; E=""; else M=m_play.json; E="MTG_NC_SEARCH=1"; fi
    mkdir -p $O/$arm.$mode.tr
    (cd $WT && flock $LOCK env MTG_RSS_CAP_GB=40 MTG_PGO=0 $E $B --batch $O/$M --threads $(nproc) --game-trace-dir $O/$arm.$mode.tr > $O/$arm.$mode.out 2> $O/$arm.$mode.err)
    echo "$arm $mode rc=$? $(date -u)" >> $O/progress
  done
done
echo done > $O/DONE
