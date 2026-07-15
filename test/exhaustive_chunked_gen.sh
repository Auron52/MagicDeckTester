#!/usr/bin/env bash
# ============================================================================================
# ADAPTIVE, resumable, poolable exhaustive keep/bottom profile generation (cancel + restart).
#
#   bash test/exhaustive_chunked_gen.sh            # TH: continue/build toward R=40 on LIVE cells
#   KM_DECK=... KM_TARGET_R=40 KM_ROUND_R=5 bash test/exhaustive_chunked_gen.sh
#
# WHAT MAKES IT ADAPTIVE (the point): a cell whose size-7 keep/mull decision is already CONFIDENT
# does not need more rollouts. Each round emits a PRUNE-SET of those confident cells; the next round
# CONSUMES it in skip mode (MTG_KEEP_CARRY_MODE=skip) so they get ZERO rollouts and the budget lands
# on the still-LIVE frontier (near-threshold size-7 + the bottoming sub-tables). As R climbs, more
# cells cross into "confident" and freeze, so each round is CHEAPER than the last. Frozen cells' counts
# come from the earlier rounds at merge (skip mode is exactly policy-preserving for same-commit pooling).
#
# Rounds add exactly KM_ROUND_R to every non-frozen cell (uniform on the live set), so effective R on
# the live frontier = BASE_R + rounds*KM_ROUND_R; a cell frozen after round k keeps the R it had then.
# (The bottoming sub-tables, sizes 4-6, are NOT pruned -- bottoming needs the full sub-table -- so they
# are re-sampled each round; that is the residual floor cost. The big, growing win is the size-7 skip.)
#
# BASE: pre-existing uniform chunk_s*.raw.json (e.g. from an earlier uniform run) count as banked base
# rollouts: BASE_R = (#chunk_s*) * KM_CHUNK_R. Continuation rounds write round_s*.raw.json and pool ON
# TOP -- nothing banked is wasted. A cold deck (no base) just starts at BASE_R=0; round 1 has an empty
# prune-set (full pass) and the frozen set grows from there.
#
# CANCEL/RESUME: a round is "done" iff its round_s<seed>.raw.json exists (atomic rename on success).
# Ctrl-C / kill BETWEEN rounds loses nothing; re-running skips finished rounds and continues. Mid-round
# loses only that round (MTG_KEEP_CHECKPOINT_SEC snapshots the partial for crash-recovery via merge).
#
# NON-DESTRUCTIVE: writes ONLY logs/<deck>_gen/. The committed profile is never touched. Adopt manually
# (validate, then gzip pooled.profile.json -> committed .gz). FREEZE (Rule 0): pooling is commit-bound;
# pins MTG_COMMIT to HEAD and refuses to add rounds if HEAD moved.
# ============================================================================================
set -uo pipefail
cd "$(dirname "$0")/.."

DECK=${KM_DECK:-decks/treasure_hunt/treasure_hunt.txt}
CARDS=${KM_CARDS:-src/cards/data/cards.json}
TARGET_R=${KM_TARGET_R:-40}      # target effective R on the live frontier
ROUND_R=${KM_ROUND_R:-5}         # R added to every non-frozen cell per round
CHUNK_R=${KM_CHUNK_R:-5}         # R of each pre-existing uniform base chunk (for BASE_R accounting)
MAXMULL=${KM_MAXMULL:-6}         # "all the way" — mulligan to the floor (was 3; mm6 is now the standard)
PRUNE_EPS=${KM_PRUNE_EPS:-0.005} # freeze gate: smaller = stricter (freeze fewer, safer)
CKPT_SEC=${KM_CKPT_SEC:-1800}
ROUND_SEED_BASE=${KM_ROUND_SEED_BASE:-30001000}   # distinct from the base chunks' seed space
BIN=./build/Release/mtg-analyze

# Pinned discovery params (identical across every chunk/round -> same bucket_fp; never change mid-target).
# Env-overridable with the SAME defaults as before (so other decks are unchanged); a deck that needs a
# different rollout regime (e.g. Hinata's d3/b10) sets MTG_EQUIV_DEPTH / MTG_EQUIV_BUDGET in the launch
# env -- pin them in a wrapper so RESUME reuses the identical values (else bucket_fp drifts, merge rejects).
export MTG_EQUIV_PROBES=${MTG_EQUIV_PROBES:-400} MTG_EQUIV_THRESHOLD=${MTG_EQUIV_THRESHOLD:-0.01} \
       MTG_EQUIV_DEPTH=${MTG_EQUIV_DEPTH:-5} MTG_EQUIV_SEED=${MTG_EQUIV_SEED:-20260701}
[ -n "${MTG_EQUIV_BUDGET:-}" ] && export MTG_EQUIV_BUDGET   # unset -> analyzer's own default (20); other decks unchanged

[ -x "$BIN" ] || { echo "ERROR: build Release first ($BIN missing)"; exit 1; }
[ -f "$DECK" ] || { echo "ERROR: deck not found: $DECK"; exit 1; }
(( (TARGET_R) % ROUND_R == 0 )) || { echo "ERROR: KM_TARGET_R must be a multiple of KM_ROUND_R"; exit 1; }

STEM=$(basename "$DECK"); STEM=${STEM%.*}
OUT=logs/${STEM}_gen
mkdir -p "$OUT"
LOG=$OUT/chain.log
stamp(){ date -u +%Y-%m-%dT%H:%M:%SZ; }
log(){ echo "[$(stamp)] $*" | tee -a "$LOG"; }

# Freeze commit: pin at first launch, enforce on resume.
HEAD=$(git rev-parse --short HEAD)
FREEZE_FILE=$OUT/FREEZE_COMMIT
if [ -f "$FREEZE_FILE" ]; then
  FROZEN=$(cat "$FREEZE_FILE")
  [ "$FROZEN" = "$HEAD" ] || { echo "ERROR: HEAD ($HEAD) != frozen commit ($FROZEN). 'git checkout $FROZEN' to continue this pool, or 'rm -rf $OUT' to restart at HEAD." >&2; exit 2; }
else
  echo "$HEAD" > "$FREEZE_FILE"
fi

# Pool every banked sidecar (uniform base chunk_s* + adaptive round_s*) into the staged candidate.
pool_all(){
  local inputs; inputs=$(ls "$OUT"/chunk_s*.raw.json "$OUT"/round_s*.raw.json 2>/dev/null | paste -sd, -)
  [ -z "$inputs" ] && return 1
  MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS="$inputs" \
    MTG_MERGE_OUT_PROFILE="$OUT/pooled.profile.json" MTG_MERGE_OUT_RAW="$OUT/pooled.raw.json" \
    "$BIN" "$DECK" --cards-json "$CARDS" --max-turns 8 >"$OUT/merge.log" 2>&1
}

# Emit the prune-set (confident size-7 cells) from the current pool. Zero rollouts.
emit_prune(){
  [ -f "$OUT/pooled.raw.json" ] || return 1
  MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS="$OUT/pooled.raw.json" \
    MTG_KEEP_PRUNE_EMIT="$OUT/prune.json" MTG_KEEP_PRUNE_EPS="$PRUNE_EPS" MTG_KEEP_NO_WRITE=1 \
    "$BIN" "$DECK" --cards-json "$CARDS" --max-turns 8 2>&1 | grep -i "PRUNE-EMIT" | tee -a "$LOG"
}

NBASE=$(ls "$OUT"/chunk_s*.raw.json 2>/dev/null | wc -l)
BASE_R=$(( NBASE * CHUNK_R ))
log "deck=$STEM target_R=$TARGET_R round_R=$ROUND_R base_chunks=$NBASE base_R=$BASE_R prune_eps=$PRUNE_EPS commit=$HEAD"

while :; do
  ROUNDS_DONE=$(ls "$OUT"/round_s*.raw.json 2>/dev/null | wc -l)
  LIVE_R=$(( BASE_R + ROUNDS_DONE * ROUND_R ))
  if [ "$LIVE_R" -ge "$TARGET_R" ]; then
    log "target R=$TARGET_R reached on live frontier (base_R=$BASE_R + $ROUNDS_DONE round(s) x $ROUND_R). Staged: $OUT/pooled.profile.json"
    break
  fi
  # Refresh the pool + prune-set from everything banked so this round skips the currently-confident cells.
  pool_all && emit_prune || log "note: no bank yet -> round runs with empty prune-set (full pass)"
  seed=$(( ROUND_SEED_BASE + ROUNDS_DONE ))
  tmp=$OUT/round_s${seed}.raw.json.partial
  next_R=$(( LIVE_R + ROUND_R ))
  log "round $((ROUNDS_DONE+1)) seed=$seed: live R ${LIVE_R}->${next_R} (skip-mode prune-set)  START"
  rstart=$(date +%s)
  if MTG_KEEP_EXHAUSTIVE=1 \
       MTG_KEEP_ROLLOUTS=$ROUND_R MTG_KEEP_R_FLOOR=$ROUND_R MTG_KEEP_MAXMULL=$MAXMULL \
       MTG_KEEP_PRUNE_SET="$OUT/prune.json" MTG_KEEP_CARRY_MODE=skip \
       MTG_KEEP_CHECKPOINT_SEC=$CKPT_SEC MTG_COMMIT="$HEAD" \
       MTG_KEEP_OUT_RAW="$tmp" MTG_KEEP_OUT_PROFILE="$OUT/round_s${seed}.profile.json" \
       "$BIN" "$DECK" --cards-json "$CARDS" --max-turns 8 --seed "$seed" --threads 0 \
       > "$OUT/round_s${seed}.log" 2>&1
  then
    mv -f "$tmp" "$OUT/round_s${seed}.raw.json"
    pool_all
    log "round $((ROUNDS_DONE+1)) seed=$seed: DONE ($(( $(date +%s) - rstart ))s) -> live R=$next_R  (pooled: $OUT/pooled.profile.json)"
  else
    log "round $((ROUNDS_DONE+1)) seed=$seed: FAILED/INTERRUPTED (see $OUT/round_s${seed}.log); partial at $tmp"
    exit 1
  fi
done

pool_all; emit_prune
log "Validate (KEEP + confounded BOTTOM A/B) then adopt: gzip -c $OUT/pooled.profile.json > decks/${STEM}.keepmodel.exhaustive.profile.json.gz"
