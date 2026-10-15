#!/usr/bin/env bash
# The rebased (16dfb7e4 + ee8a2644) paired A/B: every arm on the SAME binary and the SAME seeds as the v5 A/B
# (seed0 10400001), so v5's off arm doubles as a check of what the rebase itself moved.
#   arms: off (all five =0), nodemote (F1-F4 = shipped default), on (all five), onnl (all five + NEW_LAST),
#         nl14 (F1-F4 + NEW_LAST); d0 cells off/on only.
set -u
export BP_DATA_ROOT=/workspaces/MagicDeckTester/build/scratch/bp_open/data   # stage_data.sh
S=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9469f45bff622cfb/scripts/bp_open
OUT=/workspaces/MagicDeckTester/build/scratch/bp_open/ab_r2
T=$OUT/parts
mkdir -p "$T"
ARMS=off,nodemote,on,onnl,nl14
python3 "$S/make_ab.py" build --out "$T/a" --seed0 10400001 --decks creature_giving,fluctuator,melira,mirrorwing --cells ship,d3 --games ship=200,d3=300 --arms $ARMS
python3 "$S/make_ab.py" build --out "$T/b" --seed0 10400001 --decks pd,pirates,th --cells ship,d3 --games ship=200,d3=100 --arms $ARMS
python3 "$S/make_ab.py" build --out "$T/c" --seed0 10400001 --decks hinata --cells ship,d3 --games ship=175,d3=250 --arms $ARMS
python3 "$S/make_ab.py" build --out "$T/d" --seed0 10400001 --decks bruna --cells ship,d3 --games ship=50,d3=100 --arms $ARMS
python3 "$S/make_ab.py" build --out "$T/e" --seed0 10400001 --decks hinata,creature_giving,fluctuator,melira,mirrorwing,pd,pirates,th,bruna --cells d0 --games d0=100 --arms off,on
cp -f "$T/a/manifest.json" "$OUT/manifest.json"
python3 "$S/merge_plain.py" "$OUT/manifest.json" "$T/b/manifest.json" "$T/c/manifest.json" "$T/d/manifest.json" "$T/e/manifest.json"
cp -f "$T/a/seeds.json" "$OUT/seeds.json"
python3 -c "import json; m=json.load(open('$OUT/manifest.json')); print(len(m['jobs']), 'jobs', sum(j['games'] for j in m['jobs']), 'games')"
