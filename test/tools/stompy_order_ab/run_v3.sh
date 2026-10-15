#!/usr/bin/env bash
# v3 resume of the StompySurprise order A/B under the box-wide batch lock (RULES.txt): ONE slot, both
# binaries inside it (v3_inner.sh), then the paired analysis merged with ab_v2's finished games.
# usage: run_v3.sh <tag>
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
T=$WT/test/tools/stompy_order_ab
D=$SC/ab_$1
python3 "$T/gen_manifest_v3.py" "$D"
bash /workspaces/MagicDeckTester/build/scratch/boxlock/boxrun.sh batch bash "$T/v3_inner.sh" "$D"
cat "$D/run.info"
python3 "$T/analyze2.py" "$SC/ab_v2/wins_base" "$SC/ab_v2/wins_new" "$D/wins_base" "$D/wins_new" \
    --slower-out "$D/slower.txt" > "$D/analysis.txt"
cat "$D/analysis.txt"
