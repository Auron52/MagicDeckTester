#!/usr/bin/env bash
# Run every battlefield permutation of the gi1526 T3 board, order OFF (the base behaviour).
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
OUT=$SC/bforder
python3 "$WT/test/tools/stompy_order_ab/bforder.py" "$OUT"
cd "$WT" || exit 1
for f in "$OUT"/p*.json; do
  o=$(python3 -c "import json,sys; print(','.join(json.load(open(sys.argv[1]))['_order']))" "$f")
  r=$(MTG_STOMPY_ORDER=${1:-0} nice -n 10 "$SC/bin/mtg.new" --scenario "$f" 2>/dev/null | grep '^scenario: win_turn' | cut -d' ' -f2)
  echo "$r  $o"
done
