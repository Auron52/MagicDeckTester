#!/bin/bash
# ENUM-GAP chain 1 (ONE 4-slot lane job): build the tree, the ENUM-GAP unit test, then the oracle-on gate slice
# (Merfolk Turns gi 0-399 on the gate deck files) with three arms: off / a (bound + decider) / b (bound + Merfolk rule).
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1651726d5b941608
E=$W/build/scratch/claude_merfolk_gate/eg
T=$W/build/scratch/claude_merfolk_gate/gatetree
D=$W/build/scratch/claude_merfolk_gate/slice_eg
cd $W || exit 1
st() { echo "$1 $(date -u +%H:%M)" >> $E/chain1.status; }
MTG_BUILD_JOBS=4 ./build.sh > $E/build3.log 2>&1; rc=$?; st "build rc=$rc"; [ $rc = 0 ] || exit 1
cp build/Release/mtg $E/mtg.v2; cp build/Release/mtg-test $E/mtg-test.v2
$E/mtg-test.v2 -tc="*Dockhand before Seahunter*" > $E/ut2.out 2>&1; rc=$?; st "unit rc=$rc"; [ $rc = 0 ] || exit 1
mkdir -p $D/logs
python3 - "$D/manifest.json" <<'PY'
import json, sys
F = ("MTG_SCALED_GROWTH_BOUND", "MTG_SCALED_GROWER_DECIDER", "MTG_MERFOLK_GROWER_ORDER")
arms = {"off": (0, 0, 0), "a": (1, 1, 0), "b": (1, 0, 1)}
jobs = []
for off in range(0, 400, 25):
    for arm, v in arms.items():
        jobs.append({"name": f"{arm}__mf__g{off}", "deck": "decks/Merfolk Turns/Merfolk Turns.cod",
                     "profile": "decks/Merfolk Turns/Merfolk Turns.profile.json", "games": 25,
                     "seed": 30000000 + off, "game_index": off, "flags": {f: bool(x) for f, x in zip(F, v)}})
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=1)
PY
cd $T || exit 1
env MTG_MANA_ORACLE=2 $E/mtg.v2 --batch $D/manifest.json --threads 4 --game-log-dir $D/logs > $D/slice.out 2> $D/slice.err
st "slice rc=$?"
touch $E/CHAIN1.DONE
