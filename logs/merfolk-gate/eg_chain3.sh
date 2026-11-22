#!/bin/bash
# ENUM-GAP chain 3 (ONE 2-slot lane job): build v4 (the per-selection land-growth refinement), the scaled-land unit tests.
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1651726d5b941608
E=$W/build/scratch/claude_merfolk_gate/eg
cd $W || exit 1
st() { echo "$1 $(date -u +%H:%M)" >> $E/chain3.status; }
sleep 200   # 9p clock lag after the edit
MTG_BUILD_JOBS=2 ./build.sh > $E/build5.log 2>&1; rc=$?; st "build rc=$rc"; [ $rc = 0 ] || { touch $E/CHAIN3.DONE; exit 1; }
cp build/Release/mtg $E/mtg.v4; cp build/Release/mtg-test $E/mtg-test.v4
$E/mtg-test.v4 -tc="*Dockhand before Seahunter*,*keeps Three Tree City*" > $E/ut4.out 2>&1; st "unit rc=$?"
touch $E/CHAIN3.DONE
