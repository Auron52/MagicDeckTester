#!/usr/bin/env bash
# Inside ONE box-lock slot: the reference bench (scripts/ref_bench.py, every reference deck, the shipped
# play policy) twice on this worktree's build/Release/mtg -- A = defaults, B = the levers recommended for
# adoption. usage: refs_inner.sh <out_dir>
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
OUT=$1
mkdir -p "$OUT/A" "$OUT/B"
cd "$WT" || exit 1
export MTG_BATCH_HEARTBEAT="$OUT/heartbeat.txt"
date -u +"A start %FT%TZ" >> "$OUT/run.info"
python3 scripts/ref_bench.py --threads 20 --log-root "$OUT/A" --json "$OUT/A.json" > "$OUT/A.txt" 2> "$OUT/A.err"
date -u +"A end %FT%TZ rc=$?" >> "$OUT/run.info"
date -u +"B start %FT%TZ" >> "$OUT/run.info"
MTG_STOMPY_ORDER=1 MTG_HOLD_RELEASE_YIELD=1 MTG_SAC_VICTIM_SAME_NAME=1 MTG_EXEC_PUT_DEFERRED=1 MTG_HOLD_PRODUCER_FIRST=1 \
    python3 scripts/ref_bench.py --threads 20 --log-root "$OUT/B" --json "$OUT/B.json" > "$OUT/B.txt" 2> "$OUT/B.err"
date -u +"B end %FT%TZ rc=$?" >> "$OUT/run.info"
