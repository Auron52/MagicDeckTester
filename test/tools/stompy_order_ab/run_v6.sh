#!/usr/bin/env bash
# v6 A/B: ONE pooled batch through the box-wide FIFO lock (--threads 20), then the analysis.
# usage: run_v6.sh <tag> <binary>
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
T=$WT/test/tools/stompy_order_ab
D=$SC/ab_$1
BIN=$2
python3 "$T/gen_v6.py" "$D"
cd "$WT" || exit 1
date -u +"start %FT%TZ bin=$BIN" > "$D/run.info"
MTG_BATCH_HEARTBEAT="$D/heartbeat.txt" MTG_DUMP_UNITS=1 bash /workspaces/MagicDeckTester/build/scratch/boxlock/boxrun.sh batch \
    "$BIN" --batch "$D/manifest.json" --threads 20 --cards-json "$WT/src/cards/data/cards.json" \
    --game-log-dir "$D/wins" > "$D/batch.out" 2> "$D/batch.err"
rc=$?
date -u +"end %FT%TZ rc=$rc" >> "$D/run.info"
cat "$D/run.info"
python3 "$T/analyze_v6.py" "$D" "$SC/ab_v4" --slower-out "$D/slower" > "$D/analysis.txt"
cat "$D/analysis.txt"
