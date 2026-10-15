#!/usr/bin/env bash
# A scenario at several depths (budget 0) under the new binary, order ON vs OFF (and env extras).
# usage: scn_depths.sh <scenario.json> <out_dir> [ENV=VAL ...]
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
F=$1; OUT=$2; shift 2
mkdir -p "$OUT"
cd "$WT" || exit 1
for d in 2 3 4 5; do
  python3 - "$F" "$OUT/s_d$d.json" "$d" <<'EOF'
import json, sys
j = json.load(open(sys.argv[1])); j["depth"] = int(sys.argv[3]); j["budget_ms"] = 0; j.pop("expect_win_turn", None)
json.dump(j, open(sys.argv[2], "w"))
EOF
  for O in 1 0; do
    r=$(env "$@" MTG_STOMPY_ORDER=$O nice -n 10 "$SC/bin/mtg.new" --scenario "$OUT/s_d$d.json" 2>/dev/null | grep '^scenario: win_turn')
    echo "depth=$d order=$O $r"
  done
done
