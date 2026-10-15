#!/usr/bin/env bash
# Batch 2 on r2: stage-1 recovery of every slower game of ab_r2 (F1-F4 arm 'nodemote' and all-five arm 'on')
# + the unlimited-budget exactness cell (x3 = depth 3, budget 0), one pooled manifest.
#   x3 arms: off / nodemote / on on the cheap decks; off / on on hinata (a d3 b0 Hinata2 game costs >30 CPU-min).
set -u
export BP_DATA_ROOT=/workspaces/MagicDeckTester/build/scratch/bp_open/data
S=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9469f45bff622cfb/scripts/bp_open
B=/workspaces/MagicDeckTester/build/scratch/bp_open
OUT=$B/b2
T=$OUT/parts
mkdir -p "$T"
python3 "$S/make_recovery.py" build "$B/ab_r2" "$T/rec1_nodemote" --stage 1 --arm nodemote
python3 "$S/make_recovery.py" build "$B/ab_r2" "$T/rec1_on" --stage 1 --arm on
python3 "$S/make_ab.py" build --out "$T/x_cheap" --seed0 10400001 --decks auras,critter,dragonstorm,fluctuator,goblins --cells x3 --games x3=50 --arms off,nodemote,on --chunk 10
python3 "$S/make_ab.py" build --out "$T/x_mid" --seed0 10400001 --decks creature_giving,mirrorwing,pirates,th --cells x3 --games x3=20 --arms off,nodemote,on --chunk 5
python3 "$S/make_ab.py" build --out "$T/x_hin" --seed0 10400001 --decks hinata --cells x3 --games x3=4 --arms off,on --chunk 1
cp -f "$T/x_hin/manifest.json" "$OUT/manifest.json"
python3 "$S/merge_plain.py" "$OUT/manifest.json" "$T/rec1_nodemote/manifest.json" "$T/rec1_on/manifest.json" "$T/x_cheap/manifest.json" "$T/x_mid/manifest.json"
cp -f "$T/x_cheap/seeds.json" "$OUT/seeds.json"
python3 -c "import json; m=json.load(open('$OUT/manifest.json')); print(len(m['jobs']), 'jobs', sum(j['games'] for j in m['jobs']), 'games'); print([j['name'] for j in m['jobs'][:12]])"
