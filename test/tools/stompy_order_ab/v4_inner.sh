#!/usr/bin/env bash
# Runs INSIDE one box-wide batch slot: the new binary's pooled batch (every arm, 16 threads) and the
# base binary's identity batch (4 threads), concurrently -- 20 threads in all. usage: v4_inner.sh <dir>
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
D=$1
cd "$WT" || exit 1
mkdir -p "$D/wins_base" "$D/wins_new"
date -u +"start %FT%TZ" > "$D/run.info"
export MTG_DUMP_UNITS=1
"$SC/bin/mtg.base" --batch "$D/manifest.base.json" --threads 4 \
    --cards-json "$WT/src/cards/data/cards.json" --game-log-dir "$D/wins_base" \
    > "$D/batch.base.out" 2> "$D/batch.base.err" &
PB=$!
"$SC/bin/mtg.fix" --batch "$D/manifest.new.json" --threads 16 \
    --cards-json "$WT/src/cards/data/cards.json" --game-log-dir "$D/wins_new" \
    > "$D/batch.new.out" 2> "$D/batch.new.err" &
PN=$!
wait $PB; RB=$?
wait $PN; RN=$?
date -u +"end %FT%TZ base=$RB new=$RN" >> "$D/run.info"
