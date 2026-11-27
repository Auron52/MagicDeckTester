#!/bin/bash
# ONE heavy-lock hold: the pooled 5-arm batch with the already-built binary snapshot.
S=/workspaces/MagicDeckTester/build/scratch/elves-scal2/ab
W=/workspaces/MagicDeckTester/.claude/worktrees/elves-scal2
cd $W
flock /workspaces/MagicDeckTester/build/heavy.lock bash -c "
  echo started \$(date -u +%FT%TZ) > $S/ab.STARTED
  MTG_RSS_CAP_GB=20 $S/mtg --batch $S/ab.json > $S/ab.out 2> $S/ab.err
  echo rc=\$? > $S/ab.DONE"
