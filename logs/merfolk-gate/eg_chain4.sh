#!/bin/bash
# ENUM-GAP chain 4 (ONE 2-slot lane job): build the ship tree (v5) and run the FULL unit suite.
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1651726d5b941608
E=$W/build/scratch/claude_merfolk_gate/eg
cd $W || exit 1
st() { echo "$1 $(date -u +%H:%M)" >> $E/chain4.status; }
sleep 200   # 9p clock lag after the edit
MTG_BUILD_JOBS=2 ./build.sh > $E/build6.log 2>&1; rc=$?; st "build rc=$rc"; [ $rc = 0 ] || { touch $E/CHAIN4.DONE; exit 1; }
cp build/Release/mtg $E/mtg.v5; cp build/Release/mtg-test $E/mtg-test.v5
$E/mtg-test.v5 > $E/ut5_full.out 2>&1; st "units rc=$?"
touch $E/CHAIN4.DONE
