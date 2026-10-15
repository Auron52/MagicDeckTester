#!/usr/bin/env bash
# Runs INSIDE one box-wide batch slot (boxrun.sh batch): the new binary's pooled batch (every
# StompySurprise arm + its identity jobs, 18 threads) and the base binary's small batch (identity + the
# old-arm prefix, 2 threads), concurrently -- ~20 threads in all. usage: v3_inner.sh <dir>
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
D=$1
cd "$WT" || exit 1
mkdir -p "$D/wins_base" "$D/wins_new"
date -u +"start %FT%TZ" > "$D/run.info"
export MTG_DUMP_WINS=1 MTG_DUMP_UNITS=1
"$SC/bin/mtg.base" --batch "$D/manifest.base.json" --threads 2 \
    --cards-json "$WT/src/cards/data/cards.json" --game-log-dir "$D/wins_base" \
    > "$D/batch.base.out" 2> "$D/batch.base.err" &
PB=$!
"$SC/bin/mtg.new" --batch "$D/manifest.new.json" --threads 18 \
    --cards-json "$WT/src/cards/data/cards.json" --game-log-dir "$D/wins_new" \
    > "$D/batch.new.out" 2> "$D/batch.new.err" &
PN=$!
wait $PB; RB=$?
wait $PN; RN=$?
date -u +"end %FT%TZ base=$RB new=$RN" >> "$D/run.info"
