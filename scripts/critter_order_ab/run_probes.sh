#!/usr/bin/env bash
# Run every probe scenario on the base and new binaries (one scenario at a time; these are one-turn
# boards, seconds each).
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a239469ed898c5c26
S=/workspaces/MagicDeckTester/build/scratch/critter-order
python3 -I "$WT/scripts/critter_order_ab/probes.py" make
for arm in base new; do
  if [ "$arm" = base ]; then B="$S/bin/mtg.base"; else B="$S/bin/mtg.new"; fi
  while read -r f; do
    r="${f%.json}.result.txt"
    env -C "$WT" "$B" --scenario "$f" 2>&1 | grep -E "scenario:" > "$r"
  done < "$S/probes/$arm/list.txt"
done
python3 -I "$WT/scripts/critter_order_ab/probes.py" show
