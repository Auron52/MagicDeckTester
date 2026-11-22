#!/bin/bash
# ENUM-GAP chain 2 (ONE 2-slot lane job): build the entrant-credit fix (v3), the three scaled-land unit tests, the two
# APPLY-DROP repros (v3 arm a; v2 arm a with the exec tap trace, for the record), then the oracle gate slice arms a / b
# on v3 (off is byte-identical by construction: the credit is read only when MTG_SCALED_GROWTH_BOUND arms it; v2's off
# arm stands).
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a1651726d5b941608
E=$W/build/scratch/claude_merfolk_gate/eg
T=$W/build/scratch/claude_merfolk_gate/gatetree
D=$W/build/scratch/claude_merfolk_gate/slice_eg3
R=$E/rep2
cd $W || exit 1
st() { echo "$1 $(date -u +%H:%M)" >> $E/chain2.status; }
MTG_BUILD_JOBS=2 ./build.sh > $E/build4.log 2>&1; rc=$?; st "build rc=$rc"; [ $rc = 0 ] || { touch $E/CHAIN2.DONE; exit 1; }
cp build/Release/mtg $E/mtg.v3; cp build/Release/mtg-test $E/mtg-test.v3
$E/mtg-test.v3 -tc="*Dockhand before Seahunter*,*keeps Three Tree City*" > $E/ut3.out 2>&1; st "unit rc=$?"
mkdir -p $R
cd $T || exit 1
run() { gi=$(( $1 - 30000000 )); env MTG_MANA_ORACLE=2 MTG_MANA_ORACLE_TURN=5 MTG_EXEC_TAP_TRACE=1 MTG_SCALED_GROWTH_BOUND=1 \
  MTG_SCALED_GROWER_DECIDER=1 MTG_MERFOLK_GROWER_ORDER=0 $E/mtg.$2 "decks/Merfolk Turns/Merfolk Turns.cod" \
  --profile "decks/Merfolk Turns/Merfolk Turns.profile.json" --games 1 --seed $1 --game-index $gi \
  --log-dir $R/log_$1_$2 > $R/$1_$2.out 2> $R/$1_$2.err; }
run 30000034 v3 & run 30000075 v3 & wait
run 30000034 v2 & run 30000075 v2 & wait
st "repro done"
mkdir -p $D/logs
python3 - "$D/manifest.json" <<'PY'
import json, sys
F = ("MTG_SCALED_GROWTH_BOUND", "MTG_SCALED_GROWER_DECIDER", "MTG_MERFOLK_GROWER_ORDER")
arms = {"a": (1, 1, 0), "b": (1, 0, 1)}
jobs = []
for off in range(0, 400, 25):
    for arm, v in arms.items():
        jobs.append({"name": f"{arm}__mf__g{off}", "deck": "decks/Merfolk Turns/Merfolk Turns.cod",
                     "profile": "decks/Merfolk Turns/Merfolk Turns.profile.json", "games": 25,
                     "seed": 30000000 + off, "game_index": off, "flags": {f: bool(x) for f, x in zip(F, v)}})
json.dump({"jobs": jobs}, open(sys.argv[1], "w"), indent=1)
PY
env MTG_MANA_ORACLE=2 $E/mtg.v3 --batch $D/manifest.json --threads 2 --game-log-dir $D/logs > $D/slice.out 2> $D/slice.err
st "slice rc=$?"
touch $E/CHAIN2.DONE
