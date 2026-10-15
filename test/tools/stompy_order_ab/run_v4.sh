#!/usr/bin/env bash
# v4 A/B under the box-wide batch lock: one slot, both binaries inside it, then the analysis.
# usage: run_v4.sh <tag> [screen games per cell]
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
T=$WT/test/tools/stompy_order_ab
D=$SC/ab_$1
python3 "$T/gen_manifest_v4.py" "$D" --screen "${2:-150}"
bash /workspaces/MagicDeckTester/build/scratch/boxlock/boxrun.sh batch bash "$T/v4_inner.sh" "$D"
cat "$D/run.info"
python3 "$T/analyze_v4.py" "$D" --slower-out "$D/slower" > "$D/analysis.txt"
cat "$D/analysis.txt"
