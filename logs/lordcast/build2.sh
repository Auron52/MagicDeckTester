#!/bin/bash
# build2.sh: the mtg target first (so it can be used while the rest compiles), then the full tree (unit tests).
# Writes MTG.DONE after the first stage. Run under box_lane.
cd /workspaces/MagicDeckTester/.claude/worktrees/elves-lordcast || exit 1
S=/workspaces/MagicDeckTester/build/scratch/elves-lordcast
rm -f $S/MTG.DONE
./build.sh release -- --target mtg || exit $?
touch $S/MTG.DONE
./build.sh release
