#!/usr/bin/env bash
# The StompySurprise order A/B: the base and the new binary, each ONE pooled batch, run CONCURRENTLY
# on the same cores (cores 0-5: a bounded footprint on the shared box), so there is no barrier
# between the arms. usage: run_ab.sh <tag>
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
TAG=$1
D=$SC/ab_$TAG
mkdir -p "$D/wins_base" "$D/wins_new"
python3 "$WT/test/tools/stompy_order_ab/gen_manifest.py" "$D" base
python3 "$WT/test/tools/stompy_order_ab/gen_manifest.py" "$D" new
cd "$WT" || exit 1
H=/workspaces/MagicDeckTester/build/scratch/HOLD
while [ -f $H ]; do sleep 30; done
date -u +"start %FT%TZ" > "$D/run.info"
export MTG_DUMP_WINS=1 MTG_DUMP_UNITS=1
taskset -c 0-5 "$SC/bin/mtg.base" --batch "$D/manifest.base.json" --threads 6 \
    --cards-json "$WT/src/cards/data/cards.json" --game-log-dir "$D/wins_base" \
    > "$D/batch.base.out" 2> "$D/batch.base.err" &
PB=$!
taskset -c 0-5 "$SC/bin/mtg.new" --batch "$D/manifest.new.json" --threads 6 \
    --cards-json "$WT/src/cards/data/cards.json" --game-log-dir "$D/wins_new" \
    > "$D/batch.new.out" 2> "$D/batch.new.err" &
PN=$!
wait $PB; RB=$?
wait $PN; RN=$?
date -u +"end %FT%TZ base=$RB new=$RN" >> "$D/run.info"
python3 "$WT/test/tools/stompy_order_ab/analyze.py" "$D/wins_base" "$D/wins_new" --slower-out "$D/slower.txt" > "$D/analysis.txt"
cat "$D/analysis.txt"
