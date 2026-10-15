#!/usr/bin/env bash
# Inside ONE box-lock slot: (1) the recovery batch for every v6 slower game (one pooled mtg --batch, if its
# manifest exists), then (2) the reference bench twice on this worktree's build/Release/mtg -- A =
# defaults, B = the levers recommended for adoption. usage: refs_rec_inner.sh <out_dir>
# (Moved over refs_inner.sh, by an atomic mv, while that slot was still queued.)
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
OUT=$1
REC=$SC/rec_v6
mkdir -p "$OUT/A" "$OUT/B"
cd "$WT" || exit 1
if [ -s "$REC/manifest.json" ]; then
  mkdir -p "$REC/wins"
  date -u +"REC start %FT%TZ" >> "$OUT/run.info"
  MTG_BATCH_HEARTBEAT="$REC/heartbeat.txt" MTG_DUMP_UNITS=1 "$SC/bin/mtg.final" --batch "$REC/manifest.json" \
      --threads 20 --cards-json "$WT/src/cards/data/cards.json" --game-log-dir "$REC/wins" \
      > "$REC/batch.out" 2> "$REC/batch.err"
  date -u +"REC end %FT%TZ rc=$?" >> "$OUT/run.info"
fi
export MTG_BATCH_HEARTBEAT="$OUT/heartbeat.txt"
date -u +"A start %FT%TZ" >> "$OUT/run.info"
python3 scripts/ref_bench.py --threads 20 --log-root "$OUT/A" --json "$OUT/A.json" > "$OUT/A.txt" 2> "$OUT/A.err"
date -u +"A end %FT%TZ rc=$?" >> "$OUT/run.info"
date -u +"B start %FT%TZ" >> "$OUT/run.info"
MTG_STOMPY_ORDER=1 MTG_HOLD_RELEASE_YIELD=1 MTG_SAC_VICTIM_SAME_NAME=1 MTG_EXEC_PUT_DEFERRED=1 MTG_HOLD_PRODUCER_FIRST=1 \
    python3 scripts/ref_bench.py --threads 20 --log-root "$OUT/B" --json "$OUT/B.json" > "$OUT/B.txt" 2> "$OUT/B.err"
date -u +"B end %FT%TZ rc=$?" >> "$OUT/run.info"
