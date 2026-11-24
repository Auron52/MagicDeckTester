#!/bin/bash
# Heath re-measure (gen_manifest.py): ONE big process at a time under the box lock -- the base batch, then the fix batch
# (derived + fad pooled). Each batch pools every game of every deck. Markers: DONE_base, DONE_fix, DONE (all).
O=/workspaces/MagicDeckTester/build/scratch/heath/r8; S=/workspaces/MagicDeckTester/build/scratch/heath
N=$(nproc); LOCK=/workspaces/MagicDeckTester/build/scratch/claude_box.lock
[ -x $S/mtg.base2 ] && [ -x $S/mtg.fad ] || { echo "missing binaries" > $O/DONE; exit 1; }
mkdir -p $O/wins_base $O/wins_fix
echo "base waiting lock $(date -u)" >> $O/progress
cd /workspaces/MagicDeckTester/.claude/worktrees/heath-base2 && flock $LOCK env MTG_RSS_CAP_GB=40 $S/mtg.base2 --batch $O/manifest_base.json --threads $N --game-log-dir $O/wins_base > $O/base.out 2> $O/base.err
echo "base rc=$? $(date -u)" | tee -a $O/progress > $O/DONE_base
echo "fix waiting lock $(date -u)" >> $O/progress
cd /workspaces/MagicDeckTester/.claude/worktrees/heath-derived && flock $LOCK env MTG_RSS_CAP_GB=40 $S/mtg.fad --batch $O/manifest_fix.json --threads $N --game-log-dir $O/wins_fix > $O/fix.out 2> $O/fix.err
echo "fix rc=$? $(date -u)" | tee -a $O/progress > $O/DONE_fix
python3 $O/report.py $O > $O/report.txt 2>&1
echo "done $(date -u)" > $O/DONE
