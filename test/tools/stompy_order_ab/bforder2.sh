#!/usr/bin/env bash
# Every battlefield permutation of the gi1526 T3 board under the fix binary, MTG_HOLD_RELEASE_YIELD
# off and on (order off). Prints the win turns side by side.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
OUT=$SC/bforder
cd "$WT" || exit 1
for f in "$OUT"/p[0-9][0-9].json; do
  o=$(python3 -c "import json,sys; print(','.join(json.load(open(sys.argv[1]))['_order']))" "$f")
  a=$(MTG_HOLD_RELEASE_YIELD=0 nice -n 10 "$SC/bin/mtg.fix" --scenario "$f" 2>/dev/null | grep '^scenario: win_turn' | cut -d' ' -f2)
  b=$(MTG_HOLD_RELEASE_YIELD=1 nice -n 10 "$SC/bin/mtg.fix" --scenario "$f" 2>/dev/null | grep '^scenario: win_turn' | cut -d' ' -f2)
  echo "off:$a  fix:$b  $o"
done
