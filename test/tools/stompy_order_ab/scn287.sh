#!/usr/bin/env bash
# gi287 T3 board, Priest at each position, fix off/on (order off), plus the hold trace of the 6-mana line.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
OUT=$SC/scn287
python3 "$WT/test/tools/stompy_order_ab/scn287.py" "$OUT"
cd "$WT" || exit 1
for f in "$OUT"/g287_[0-9].json; do
  o=$(python3 -c "import json,sys; print(','.join(json.load(open(sys.argv[1]))['_order']))" "$f")
  a=$(MTG_HOLD_RELEASE_YIELD=0 nice -n 10 "$SC/bin/mtg.fix" --scenario "$f" 2>/dev/null | grep '^scenario: win_turn' | cut -d' ' -f2)
  b=$(MTG_HOLD_RELEASE_YIELD=1 nice -n 10 "$SC/bin/mtg.fix" --scenario "$f" 2>/dev/null | grep '^scenario: win_turn' | cut -d' ' -f2)
  echo "off:$a  fix:$b  $o"
done
