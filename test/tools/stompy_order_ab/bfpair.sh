#!/usr/bin/env bash
# Two battlefield permutations of the gi1526 T3 board (bforder.py output), logged, order OFF.
# usage: bfpair.sh <pA> <pB>   (e.g. p00 p01)
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
D=$SC/bforder
cd "$WT" || exit 1
for p in "$@"; do
  python3 - "$D/$p.json" "$D/$p.log.json" "$D/${p}_l.json" <<'EOF'
import json, sys
j = json.load(open(sys.argv[1])); j["log_out"] = sys.argv[2]
json.dump(j, open(sys.argv[3], "w"))
EOF
  MTG_STOMPY_ORDER=0 MTG_FD_TRACE=1 nice -n 10 "$SC/bin/mtg.new" --scenario "$D/${p}_l.json" > "$D/$p.out" 2> "$D/$p.err"
  echo "== $p $(grep '^scenario: win_turn' "$D/$p.out")"
  grep -E "^\[fd\] T3 LINE" "$D/$p.err" | head -1
  python3 - "$D/$p.log.json" <<'EOF'
import json, sys
j = json.load(open(sys.argv[1]))
for t in j["turns"]:
    if t["turn"] != 3:
        continue
    acts = [(a["type"], a.get("cardName"), a.get("manaPaid"), a.get("damage")) for a in t["actions"]]
    print("  ", t["phase"], acts)
    if t["phase"] in ("MAIN_1", "COMBAT"):
        print("     board:", [(b["cardName"], "T" if b.get("tapped") else "-") for b in t["boardAfter"]["battlefield"]])
EOF
done
