#!/bin/bash
# Round 3: ONE heavy-lock hold, the pooled 3-arm A/B (18,000 jobs). Binary = the snapshot $S/mtg. Run from the
# worktree (cards.json and the providers come from the CWD). Markers: ab.QUEUED / ab.STARTED / ab.DONE (rc + time).
S=/workspaces/MagicDeckTester/build/scratch/elves-r3/ab
W=/workspaces/MagicDeckTester/.claude/worktrees/elves-r2
cd $W || exit 1
echo queued $(date -u +%FT%TZ) > $S/ab.QUEUED
flock /workspaces/MagicDeckTester/build/heavy.lock bash -c "
  echo started \$(date -u +%FT%TZ) > $S/ab.STARTED
  MTG_RSS_CAP_GB=20 $S/mtg --batch $S/ab.json > $S/ab.out 2> $S/ab.err
  echo rc=\$? \$(date -u +%FT%TZ) > $S/ab.DONE"
