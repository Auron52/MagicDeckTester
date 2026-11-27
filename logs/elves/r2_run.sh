#!/bin/bash
# ONE heavy-lock hold (queued behind the Elves mulligan generation): the pooled 6-arm A/B, then three short
# census batches (diagnostic only). Binary = the snapshot $S/mtg built from wip/elves-r2. Run from the worktree
# (cards.json and the providers come from the CWD).
S=/workspaces/MagicDeckTester/build/scratch/elves-r2/ab
W=/workspaces/MagicDeckTester/.claude/worktrees/elves-r2
cd $W || exit 1
echo queued $(date -u +%FT%TZ) > $S/ab.QUEUED
flock /workspaces/MagicDeckTester/build/heavy.lock bash -c "
  echo started \$(date -u +%FT%TZ) > $S/ab.STARTED
  MTG_RSS_CAP_GB=20 $S/mtg --batch $S/ab.json > $S/ab.out 2> $S/ab.err
  echo rc=\$? \$(date -u +%FT%TZ) > $S/ab.DONE
  MTG_RSS_CAP_GB=20 MTG_ELVES_CHOSEN_CENSUS=1 MTG_ELVES_SCALING_CENSUS=1 $S/mtg --batch $S/cen_std.json > $S/cen_std.out 2> $S/cen_std.err
  MTG_RSS_CAP_GB=20 MTG_ELVES_CHOSEN_CENSUS=1 MTG_ELVES_SCALING_CENSUS=1 $S/mtg --batch $S/cen_2hg.json > $S/cen_2hg.out 2> $S/cen_2hg.err
  MTG_RSS_CAP_GB=20 MTG_ELVES_CHOSEN_CENSUS=1 $S/mtg --batch $S/cen_tap.json > $S/cen_tap.out 2> $S/cen_tap.err
  echo rc=\$? \$(date -u +%FT%TZ) > $S/census.DONE"
