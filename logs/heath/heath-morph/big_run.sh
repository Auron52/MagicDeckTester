#!/bin/bash
O=/workspaces/MagicDeckTester/build/scratch/heath-morph/big; S=/workspaces/MagicDeckTester/build/scratch/heath-morph; LOCK=/workspaces/MagicDeckTester/build/scratch/claude_box.lock
W=/workspaces/MagicDeckTester/.claude/worktrees
mkdir -p $O/wins_base $O/wins_new
echo "base wait $(date -u)" >> $O/progress
cd $W/land-heath-base && flock $LOCK env MTG_RSS_CAP_GB=40 MTG_PGO=0 $S/mtg.origin --batch $O/m_base.json --threads $(nproc) --game-log-dir $O/wins_base > $O/base.out 2> $O/base.err
echo "base rc=$? $(date -u)" >> $O/progress
cd $W/land-heath && flock $LOCK env MTG_RSS_CAP_GB=40 MTG_PGO=0 $S/mtg.new --batch $O/m_new.json --threads $(nproc) --game-log-dir $O/wins_new > $O/new.out 2> $O/new.err
echo "new rc=$? $(date -u)" >> $O/progress
echo "done $(date -u)" > $O/DONE
