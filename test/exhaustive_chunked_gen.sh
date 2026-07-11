#!/usr/bin/env bash
# ============================================================================================
# Resumable, poolable exhaustive keep/bottom profile generation (cancel + restart across nights).
#
#   bash test/exhaustive_chunked_gen.sh              # TH, target R=40 in 8 chunks of R=5
#   KM_DECK=decks/treasure_hunt.txt KM_TARGET_R=40 KM_CHUNK_R=5 bash test/exhaustive_chunked_gen.sh
#
# WHY chunked: a single R=40 run on a durdle-heavy deck (TH, burn) can exceed one overnight. This
# splits the target R into N UNIFORM chunks of R=KM_CHUNK_R, each a full pass over all hands on a
# DISTINCT seed. Pooling sums the raw sidecars element-wise (MTG_KEEP_MERGE), so effective R adds:
# after C completed chunks, effective R = C * KM_CHUNK_R. Reach KM_TARGET_R over as many sessions
# as you like.
#
# CANCEL/RESUME model:
#   * A chunk is "done" iff logs/<deck>_gen/chunk_s<seed>.raw.json exists (atomic rename on success).
#   * Ctrl-C / kill BETWEEN chunks loses nothing -- re-running skips completed chunks and continues.
#   * Ctrl-C MID-chunk loses only that one chunk's work (<= one chunk's wall time; that is why small
#     KM_CHUNK_R is safer). Within a chunk, MTG_KEEP_CHECKPOINT_SEC snapshots the partial sidecar so
#     a *crash* leaves a recoverable floor-state file, but this script re-runs an unfinished chunk
#     from scratch rather than auto-continuing it (continue-from-here is not yet supported upstream).
#
# NON-DESTRUCTIVE: writes ONLY under logs/<deck>_gen/. The committed decks/<deck>.keepmodel.exhaustive.*
# are NEVER touched. After each chunk it re-pools into logs/<deck>_gen/pooled.{profile,raw}.json (the
# staged candidate). Adopt manually later (gzip pooled.profile.json -> committed .gz, then rebaseline).
#
# FREEZE (Rule 0): generation is commit-bound. All chunks MUST be on ONE commit (the sidecar stamps it
# and the merge REJECTS mismatched commits). This script pins MTG_COMMIT to HEAD at first launch and
# refuses to add chunks if HEAD later moved. Only generate once cards/play are frozen.
# ============================================================================================
set -uo pipefail
cd "$(dirname "$0")/.."

DECK=${KM_DECK:-decks/treasure_hunt.txt}
CARDS=${KM_CARDS:-src/cards/data/cards.json}
TARGET_R=${KM_TARGET_R:-40}
CHUNK_R=${KM_CHUNK_R:-5}
SEED_BASE=${KM_SEED_BASE:-30000000}   # chunk i uses SEED_BASE + i (distinct -> disjoint streams)
MAXMULL=${KM_MAXMULL:-3}
CKPT_SEC=${KM_CKPT_SEC:-1800}         # in-chunk crash-safety snapshot cadence
BIN=./build/Release/mtg-analyze

# --- Pinned discovery params (MUST be identical across all chunks -> same bucket_fp; do not change
#     mid-target or the pool becomes unmergeable). ---
export MTG_EQUIV_PROBES=400 MTG_EQUIV_THRESHOLD=0.01 MTG_EQUIV_DEPTH=5 MTG_EQUIV_SEED=20260701

[ -x "$BIN" ] || { echo "ERROR: build Release first ($BIN missing)"; exit 1; }
[ -f "$DECK" ] || { echo "ERROR: deck not found: $DECK"; exit 1; }
[ -f "$CARDS" ] || { echo "ERROR: cards.json not found: $CARDS"; exit 1; }
(( TARGET_R % CHUNK_R == 0 )) || { echo "ERROR: KM_TARGET_R($TARGET_R) must be a multiple of KM_CHUNK_R($CHUNK_R)"; exit 1; }
NCHUNKS=$(( TARGET_R / CHUNK_R ))

STEM=$(basename "$DECK"); STEM=${STEM%.*}
OUT=logs/${STEM}_gen
mkdir -p "$OUT"
LOG=$OUT/chain.log
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
log(){ echo "[$(stamp)] $*" | tee -a "$LOG"; }

# --- Freeze commit: pin at first launch, enforce on resume. ---
HEAD=$(git rev-parse --short HEAD)
FREEZE_FILE=$OUT/FREEZE_COMMIT
if [ -f "$FREEZE_FILE" ]; then
  FROZEN=$(cat "$FREEZE_FILE")
  if [ "$FROZEN" != "$HEAD" ]; then
    echo "ERROR: HEAD ($HEAD) != frozen commit ($FROZEN) for this in-progress pool." >&2
    echo "       Chunks are commit-bound and cannot be pooled across a play-logic change." >&2
    echo "       Either 'git checkout $FROZEN' to continue this pool, or 'rm -rf $OUT' to restart at HEAD." >&2
    exit 2
  fi
else
  echo "$HEAD" > "$FREEZE_FILE"
fi
log "deck=$STEM target_R=$TARGET_R chunk_R=$CHUNK_R chunks=$NCHUNKS seed_base=$SEED_BASE commit=$HEAD"

# --- Pool whatever completed chunk sidecars exist into the staged candidate. ---
pool(){
  local inputs; inputs=$(ls "$OUT"/chunk_s*.raw.json 2>/dev/null | paste -sd, -)
  [ -z "$inputs" ] && { log "pool: no completed chunks yet"; return; }
  local ndone; ndone=$(ls "$OUT"/chunk_s*.raw.json 2>/dev/null | wc -l)
  MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS="$inputs" \
    MTG_MERGE_OUT_PROFILE="$OUT/pooled.profile.json" \
    MTG_MERGE_OUT_RAW="$OUT/pooled.raw.json" \
    "$BIN" "$DECK" --cards-json "$CARDS" --max-turns 8 >"$OUT/merge.log" 2>&1 \
    && log "pooled $ndone chunk(s) -> effective R=$(( ndone * CHUNK_R ))  ($OUT/pooled.profile.json)" \
    || log "POOL FAILED (see $OUT/merge.log)"
}

# --- Run the remaining chunks. ---
for (( i=0; i<NCHUNKS; i++ )); do
  seed=$(( SEED_BASE + i ))
  done_file=$OUT/chunk_s${seed}.raw.json
  if [ -f "$done_file" ]; then
    log "chunk $((i+1))/$NCHUNKS seed=$seed: already done -- skip"
    continue
  fi
  tmp=$OUT/chunk_s${seed}.raw.json.partial
  log "chunk $((i+1))/$NCHUNKS seed=$seed R=$CHUNK_R: START"
  cstart=$(date +%s)
  # UNIFORM chunk: floor==cap==CHUNK_R -> no adaptive refine, so every chunk contributes exactly
  # CHUNK_R rollouts/cell and the pool's effective R is exactly (#chunks)*CHUNK_R.
  if MTG_KEEP_EXHAUSTIVE=1 \
       MTG_KEEP_ROLLOUTS=$CHUNK_R MTG_KEEP_R_FLOOR=$CHUNK_R MTG_KEEP_MAXMULL=$MAXMULL \
       MTG_KEEP_CHECKPOINT_SEC=$CKPT_SEC MTG_COMMIT="$HEAD" \
       MTG_KEEP_OUT_RAW="$tmp" MTG_KEEP_OUT_PROFILE="$OUT/chunk_s${seed}.profile.json" \
       "$BIN" "$DECK" --cards-json "$CARDS" --max-turns 8 --seed "$seed" --threads 0 \
       > "$OUT/chunk_s${seed}.log" 2>&1
  then
    mv -f "$tmp" "$done_file"     # atomic "done" marker
    log "chunk $((i+1))/$NCHUNKS seed=$seed: DONE ($(( $(date +%s) - cstart ))s)"
    pool
  else
    log "chunk $((i+1))/$NCHUNKS seed=$seed: FAILED/INTERRUPTED (see $OUT/chunk_s${seed}.log); partial at $tmp"
    exit 1
  fi
done

log "ALL $NCHUNKS chunks done -> target R=$TARGET_R reached. Staged candidate: $OUT/pooled.profile.json"
log "Validate (KEEP + confounded BOTTOM A/B) then adopt: gzip -c $OUT/pooled.profile.json > decks/${STEM}.keepmodel.exhaustive.profile.json.gz"
