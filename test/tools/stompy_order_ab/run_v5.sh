#!/usr/bin/env bash
# v5: recovery of every v4 slower game + the fix's movers at a larger sample, ONE pooled batch on the fix
# binary through the box lock. usage: run_v5.sh <tag>
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
T=$WT/test/tools/stompy_order_ab
D=$SC/ab_$1
python3 "$T/gen_v5.py" "$SC/ab_v4" "$D"
mkdir -p "$D/wins"
cd "$WT" || exit 1
date -u +"start %FT%TZ" > "$D/run.info"
MTG_BATCH_HEARTBEAT="$D/heartbeat.txt" MTG_DUMP_UNITS=1 bash /workspaces/MagicDeckTester/build/scratch/boxlock/boxrun.sh batch \
    "$SC/bin/mtg.fix" --batch "$D/manifest.json" --threads 20 --cards-json "$WT/src/cards/data/cards.json" \
    --game-log-dir "$D/wins" > "$D/batch.out" 2> "$D/batch.err"
date -u +"end %FT%TZ rc=$?" >> "$D/run.info"
cat "$D/run.info"
