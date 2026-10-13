#!/bin/bash
# gi1106 at the three cells under four lever combinations. Usage: g1106_all.sh <binary> <tag>
BIN=$1; TAG=$2
B=/workspaces/MagicDeckTester/build/scratch/dig_agent
S=/workspaces/MagicDeckTester/.claude/worktrees/agent-ac800b84e49fb7cf6/logs/dig_agent/g1106.sh
mkdir -p "$B/g1106"
bash "$S" "$BIN" > "$B/g1106/${TAG}_on.txt" 2>&1
bash "$S" "$BIN" MTG_DIG_DEFER_MAIN2=0 > "$B/g1106/${TAG}_on_defer0.txt" 2>&1
bash "$S" "$BIN" MTG_DIG_SEARCHED_CHOICE=0 > "$B/g1106/${TAG}_off.txt" 2>&1
bash "$S" "$BIN" MTG_DIG_SEARCHED_CHOICE=0 MTG_DIG_DEFER_MAIN2=0 > "$B/g1106/${TAG}_off_defer0.txt" 2>&1
for f in on on_defer0 off off_defer0; do
  echo "== $TAG $f"
  cut -c1-40 "$B/g1106/${TAG}_$f.txt"
done
