#!/usr/bin/env bash
# Cost probe for the unlimited-budget exactness cell (x3 = depth 3, budget 0): ONE game per deck, off arm,
# --threads 1, niced, outside the lock (RULES item 4), cheap decks first. Sizes the exactness batch.
set -u
export BP_DATA_ROOT=/workspaces/MagicDeckTester/build/scratch/bp_open/data
S=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9469f45bff622cfb/scripts/bp_open
OUT=/workspaces/MagicDeckTester/build/scratch/bp_open/probe_x3
mkdir -p "$OUT"
python3 "$S/make_ab.py" build --out "$OUT/raw" --seed0 10400001 \
  --decks fluctuator,mirrorwing,dragonstorm,goblins,auras,critter,pirates,th,creature_giving,hinata \
  --cells x3 --games x3=1 --arms off --chunk 1
python3 - "$OUT" <<'EOF'
import json, sys
out = sys.argv[1]
order = ["fluctuator", "mirrorwing", "dragonstorm", "goblins", "auras", "critter", "pirates", "th", "creature_giving", "hinata"]
m = json.load(open(out + "/raw/manifest.json"))
m["jobs"].sort(key=lambda j: order.index(j["name"].split("_x3_")[0]))
json.dump(m, open(out + "/manifest.json", "w"), indent=1)
print([j["name"] for j in m["jobs"]])
EOF
D=/workspaces/MagicDeckTester/build/scratch/bp_open/data
env -C "$D" MTG_DUMP_WINS=1 nice -n 10 /workspaces/MagicDeckTester/build/scratch/bp_open/bin/r2/mtg --batch "$OUT/manifest.json" --threads 1 --cards-json "$D/src/cards/data/cards.json" > "$OUT/batch.out" 2> "$OUT/batch.err"
echo "exit $?" >> "$OUT/batch.out"
