#!/usr/bin/env bash
# The shipped search on every CritterLifegain reference game's opening hand: new order vs the =0 hatch
# (the hatch is byte-identical to the base binary -- the A/B's hatch check).
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a239469ed898c5c26
S=/workspaces/MagicDeckTester/build/scratch/critter-order
mkdir -p "$S/refbench"
env -C "$WT" python3 scripts/ref_bench.py --deck critterlifegain critterlifegain_v1_thune4_basilica --threads 2 > "$S/refbench/new.txt" 2>&1
echo "new exit $?" >> "$S/refbench/new.txt"
env -C "$WT" MTG_CRITTER_ORDER=0 python3 scripts/ref_bench.py --deck critterlifegain critterlifegain_v1_thune4_basilica --threads 2 > "$S/refbench/hatch.txt" 2>&1
echo "hatch exit $?" >> "$S/refbench/hatch.txt"
tail -40 "$S/refbench/new.txt"
echo ======
tail -40 "$S/refbench/hatch.txt"
