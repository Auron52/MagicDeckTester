#!/usr/bin/env bash
# Value-leaf regeneration queue driver -- Hinata2 then Anti-Lifegain.
# Runbook: docs/design/value-leaf-regeneration-queue.md
#
# WHAT THIS DOES, and what it deliberately does NOT do. It runs every GENERATION and MEASUREMENT
# stage unattended, and stops short of adoption: the retrained model and the regenerated table land
# in logs/eval/<deck>.value.STAGED.json, never in decks/<deck>/<deck>.value.json. The final stage
# MEASURES staged-vs-live so a human has the number the adoption decision needs. Installing is a
# separate, deliberate `cp` + smoke/regression + user approval.
#
# WHY HINATA IS REGENERATED (the reason that actually holds): decks/Hinata2/Hinata2.value.json is
# dated 2026-07-21, its exhaustive keep model 2026-07-26 -- so both the eval_model and the depth
# table describe a Hinata that mulliganed with DEFAULTS. That is a real invalidation. It is NOT the
# "measured profile-less" one in the queue doc's section 1(a): the engine auto-detects
# <deck>.profile.json from the deck path (src/main.cpp, since e71f51f), so the matrix loaded the
# profile all along -- see docs/design/value-leaf-ladder-truncation.md.
#
# WHY ANTI-LIFEGAIN NEEDS NO RETRAIN: its eval_model (07-21) POSTDATES its keep model (07-14), so the
# model itself is sound. Only its TABLE is bad, and specifically because it MIXES ENGINE STATES
# internally (H1-5/V1-5 clairvoyant, H6/V6-7 honest -- flagged by its own monotonicity_warnings).
# So: matrix only, re-anchored in one pass on one commit.
#
# THE FREEZE RULE. Every artifact here is an engine-state fingerprint, so the whole queue must run on
# ONE commit. This script records HEAD at start and re-checks it before each stage; if the source
# tree has moved it STOPS rather than silently mixing engine states into one table. Two machines work
# this branch -- announce the freeze or keep it quiet for the duration.
#
#   bash scripts/valueleaf_regen_queue.sh run       # start / resume the whole queue
#   bash scripts/valueleaf_regen_queue.sh status    # progress without touching anything
#
# Resumable: each stage writes a marker under logs/vlq/done/. Re-running skips finished stages. Safe
# to interrupt between stages; the two long stages (row dump, matrix) are independently resumable --
# the dump by (seed,turn) dedupe, the matrix by its <out>.cells.json.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh

VLQ=logs/vlq
DONE=$VLQ/done
mkdir -p "$DONE" logs/eval

ROW_TARGET=${ROW_TARGET:-11000}     # the knee, per the queue doc
CAL_GAMES=${CAL_GAMES:-48}          # calibration slice: sizes the main dump from MEASURED rows/game
CAL_BASE=${CAL_BASE:-30030}
MAIN_BASE=${MAIN_BASE:-40040}       # disjoint from the calibration range by construction
WORKERS=${WORKERS:-20}              # ~1 GB/process; 47 GB box
AB_GAMES=${AB_GAMES:-1000}          # per seed, per arm, for the staged-vs-live adoption A/B
AB_SEEDS=${AB_SEEDS:-"600000 601000 602000 603000 604000 605000 606000 607000"}

HIN_DECK=decks/Hinata2/Hinata2.cod
HIN_PROF=decks/Hinata2/Hinata2.profile.json
HIN_LIVE=decks/Hinata2/Hinata2.value.json
HIN_STAGED=logs/eval/Hinata2.value.STAGED.json
ALG_DECK=decks/Anti-Lifegain/Anti-Lifegain.cod
ALG_PROF=decks/Anti-Lifegain/Anti-Lifegain.profile.json
ALG_LIVE=decks/Anti-Lifegain/Anti-Lifegain.value.json
ALG_STAGED=logs/eval/Anti-Lifegain.value.STAGED.json
ROWS=logs/eval/hinata_value_pooled.rows

log() { echo "[$(date '+%m-%d %H:%M:%S')] $*" | tee -a "$VLQ/driver.log"; }
done_p() { [ -e "$DONE/$1" ]; }
mark()   { date '+%Y-%m-%dT%H:%M:%S' > "$DONE/$1"; }

# The freeze guard. Anything that changes PLAY invalidates every measurement taken before it, so a
# mid-queue source change is not "slight staleness" -- it silently produces a table whose rows
# disagree. Compare the SOURCE tree only (docs/logs churn is harmless and expected).
src_fingerprint() { git rev-parse HEAD:src 2>/dev/null; }
check_freeze() {
    local now frozen
    now=$(src_fingerprint); frozen=$(cat "$VLQ/freeze.src" 2>/dev/null || echo "")
    if [ -n "$frozen" ] && [ "$now" != "$frozen" ]; then
        log "FREEZE VIOLATION: src/ tree moved ($frozen -> $now)."
        log "  Everything measured so far describes a different engine. Restart the queue on one"
        log "  commit; do not mix. (rm -rf $DONE to restart, after deciding what to keep.)"
        return 1
    fi
    return 0
}

# A scratch deck folder whose ONLY difference from the real one is which value.json it carries.
# Everything else is symlinked, so the ~600 MB keep-model caches are not copied. The engine resolves
# every sibling model directory-relative off the deck/profile path, so this is a complete deck.
make_variant_deck() {   # $1 dest-dir  $2 src-deck-dir  $3 stem  $4 value.json to install
    local dest=$1 src=$2 stem=$3 val=$4 f b
    rm -rf "$dest"; mkdir -p "$dest"
    for f in "$src"/*; do
        b=$(basename "$f")
        [ "$b" = "$stem.value.json" ] && continue
        ln -sf "$(realpath "$f")" "$dest/$b"
    done
    cp "$val" "$dest/$stem.value.json"
}

# ---------------------------------------------------------------- stage 0: freeze + preflight
stage_freeze() {
    done_p 00_freeze && { log "skip 00_freeze"; return 0; }
    if ! git diff --quiet -- src/ || ! git diff --cached --quiet -- src/; then
        log "ABORT: uncommitted changes under src/ -- the freeze commit would not describe the binary."
        return 1
    fi
    src_fingerprint > "$VLQ/freeze.src"
    git rev-parse --short HEAD > "$VLQ/freeze.commit"
    log "FROZEN at commit $(cat "$VLQ/freeze.commit")  src-tree $(cut -c1-12 < "$VLQ/freeze.src")"
    log "rebuilding (build.sh -- never raw cmake)"
    bash build.sh >> "$VLQ/build.log" 2>&1 || { log "ABORT: build failed, see $VLQ/build.log"; return 1; }
    mark 00_freeze
}

# ------------------------------------------------- stage H1: calibration slice (sizes stage H2)
# Sized from MEASUREMENT, not extrapolation: a short pooled run over COMPLETE games gives an honest
# rows/game, which is what converts a row target into a game count. (The row RATE is a different
# quantity and is optimistic on any short run -- turn-1/2 positions are cheap and every dump starts
# fast, so judge duration by the sustained rate in stage H2, never by this slice.)
stage_cal() {
    done_p 10_cal && { log "skip 10_cal"; return 0; }
    log "H1 calibration: $CAL_GAMES games, base $CAL_BASE (measuring rows/game)"
    local t0=$SECONDS
    BASE=$CAL_BASE bash scripts/hinata_valueleaf_batch_dump.sh "$CAL_GAMES" >> "$VLQ/dump_cal.log" 2>&1
    local rows; rows=$(grep -vc '^#' "$ROWS" 2>/dev/null || echo 0)
    local secs=$((SECONDS - t0))
    [ "$rows" -gt 0 ] || { log "ABORT: calibration produced 0 rows -- see $VLQ/dump_cal.log"; return 1; }
    awk -v r="$rows" -v g="$CAL_GAMES" -v s="$secs" 'BEGIN{
        printf "rows_per_game=%.3f\nsec_per_game=%.2f\n", r/g, s/g }' > "$VLQ/calibration"
    log "H1 done: $rows rows / $CAL_GAMES games in ${secs}s -> $(tr '\n' ' ' < "$VLQ/calibration")"
    mark 10_cal
}

# ------------------------------------------------------------------ stage H2: the main row dump
# ONE pooled batch (the repo's batching rule): every game of every block goes into a single work
# queue, so the box is saturated to ONE load-imbalance tail. The predecessor -- a loop of per-chunk
# `mtg` invocations -- stranded ~20 of 24 cores for 3 h on two slow games and committed nothing.
stage_dump() {
    done_p 20_dump && { log "skip 20_dump"; return 0; }
    local have rpg need
    have=$(grep -vc '^#' "$ROWS" 2>/dev/null || echo 0)
    rpg=$(sed -n 's/^rows_per_game=//p' "$VLQ/calibration")
    need=$(awk -v t="$ROW_TARGET" -v h="$have" -v r="$rpg" 'BEGIN{n=(t-h)/r; print (n<0?0:int(n+0.999))}')
    if [ "$need" -le 0 ]; then log "H2: already at $have rows >= $ROW_TARGET"; mark 20_dump; return 0; fi
    log "H2 dump: $have/$ROW_TARGET rows; queueing $need games at base $MAIN_BASE (rows/game $rpg)"
    log "H2: this is the long pole -- expect many hours. Judge it by its SUSTAINED rate, not its first minute."
    BASE=$MAIN_BASE bash scripts/hinata_valueleaf_batch_dump.sh "$need" >> "$VLQ/dump_main.log" 2>&1
    have=$(grep -vc '^#' "$ROWS" 2>/dev/null || echo 0)
    log "H2 done: $have rows"
    mark 20_dump
}

# ------------------------------------------------------- stage H3/H4: collect + train (STAGED)
stage_train() {
    done_p 30_train && { log "skip 30_train"; return 0; }
    log "H3 collect (dedupe on seed,turn)"
    bash scripts/hinata_valueleaf_batch_dump.sh collect 2>&1 | tee -a "$VLQ/driver.log"
    local src=logs/eval/hinata_rows_B.all.rows
    local n; n=$(grep -vc '^#' "$src" 2>/dev/null || echo 0)
    [ "$n" -ge 1000 ] || { log "ABORT: only $n unique rows collected"; return 1; }
    log "H4 train on $n rows -> $HIN_STAGED (eval_model merged into a COPY; live sidecar untouched)"
    bash scripts/hinata_valueleaf_pipeline.sh train "$src" >> "$VLQ/train.log" 2>&1
    [ -s "$HIN_STAGED" ] || { log "ABORT: no staged model at $HIN_STAGED -- see $VLQ/train.log"; return 1; }
    python3 - "$HIN_STAGED" "$(cat "$VLQ/freeze.commit")" "$n" <<'PY'
import json, sys
p, commit, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
d = json.load(open(p))
d.setdefault("provenance", {}).update({
    "regenerated_commit": commit,
    "rows": n,
    "reason": "eval_model + table predated the exhaustive keep model (value.json 07-21 vs keepmodel 07-26)",
})
json.dump(d, open(p, "w"), indent=1)
PY
    mark 30_train
}

# ------------------------------------------------------------------------- stage H5: the matrix
# The PROPER generator: incremental + batched + tractability-aware. Every cell advances in 25-game
# batches round-robin, so the WHOLE table exists at 25 games, then 50, ...; each batch is written the
# instant it lands (<out>.cells.json = resume). A cell measured slower than --intractable-sec-per-game
# is capped at a small reference sample instead of burning the box on a cell no production run could
# use. Run with the box to ITSELF: the cutoff is wall-clock based, so a loaded box misclassifies slow
# cells as intractable and silently truncates the table.
run_matrix() {   # $1 deck-key  $2 out  $3 hdepths  $4 vdepths  $5 target  $6 ref-target
    python3 scripts/attic/valueleaf_depth_matrix.py --incremental --decks "$1" \
        --hdepths $3 --vdepths $4 --seeds 8008 9009 10010 11011 \
        --target "$5" --reference-target "$6" --batch 25 --workers "$WORKERS" \
        --value-min-depth 0 --intractable-sec-per-game 3.0 --out "$2"
}
stage_hin_matrix() {
    done_p 40_hin_matrix && { log "skip 40_hin_matrix"; return 0; }
    log "H5 matrix: hinata_staged H1-5 x V1-5, target 400/cell, workers $WORKERS"
    run_matrix hinata_staged logs/eval/valueleaf_depth_hinata_regen.txt "1 2 3 4 5" "1 2 3 4 5" 400 50 \
        >> "$VLQ/matrix_hinata.log" 2>&1
    [ -s logs/eval/valueleaf_depth_hinata_regen.txt ] || { log "ABORT: no hinata matrix output"; return 1; }
    mark 40_hin_matrix
}
stage_hin_meta() {
    done_p 50_hin_meta && { log "skip 50_hin_meta"; return 0; }
    log "H6 table -> metadata (crossover + trust depth) into $HIN_STAGED"
    python3 scripts/attic/valueleaf_table_to_metadata.py logs/eval/valueleaf_depth_hinata_regen.txt \
        --decks hinata_staged --average-seeds 2>&1 | tee -a "$VLQ/driver.log"
    mark 50_hin_meta
}

# --------------------------------------------------------- stage A: Anti-Lifegain (matrix only)
stage_alg_matrix() {
    done_p 60_alg_matrix && { log "skip 60_alg_matrix"; return 0; }
    # No retrain: the model postdates the keep model. Stage a copy so the table lands off the live file.
    [ -s "$ALG_STAGED" ] || cp "$ALG_LIVE" "$ALG_STAGED"
    log "A1 matrix: antilife_staged H1-5 x V1-5, target 400/cell (deep cells are the known cost risk)"
    run_matrix antilife_staged logs/eval/valueleaf_depth_antilife_regen.txt "1 2 3 4 5" "1 2 3 4 5" 400 50 \
        >> "$VLQ/matrix_antilife.log" 2>&1
    [ -s logs/eval/valueleaf_depth_antilife_regen.txt ] || { log "ABORT: no antilife matrix output"; return 1; }
    mark 60_alg_matrix
}
stage_alg_meta() {
    done_p 70_alg_meta && { log "skip 70_alg_meta"; return 0; }
    log "A2 table -> metadata into $ALG_STAGED"
    python3 scripts/attic/valueleaf_table_to_metadata.py logs/eval/valueleaf_depth_antilife_regen.txt \
        --decks antilife_staged --average-seeds 2>&1 | tee -a "$VLQ/driver.log"
    mark 70_alg_meta
}

# ------------------------------------------- stage AB: staged vs live, MEASURED, not adopted
# Both arms in ONE pooled batch. Bases are spaced by exactly AB_GAMES so the arms tile the seed space
# once each: per-game identity is base+game_index, so bases spaced closer than games-per-job make jobs
# REPLAY games -- which once turned a 1.3-sigma result into a fake -14.4 sigma. See rule 7 of the
# regression-testing skill.
stage_ab() {   # $1 tag  $2 deck-dir  $3 stem  $4 live-value  $5 staged-value
    done_p "80_ab_$1" && { log "skip 80_ab_$1"; return 0; }
    local vdir=$VLQ/ab_$1; rm -rf "$vdir"; mkdir -p "$vdir"
    make_variant_deck "$vdir/live"   "$2" "$3" "$4"
    make_variant_deck "$vdir/staged" "$2" "$3" "$5"
    local out=$VLQ/ab_$1/manifest.json s
    { for s in $AB_SEEDS; do
        h_job "live_s$s"   "$vdir/live/$3.cod"   "$vdir/live/$3.profile.json"   "$AB_GAMES" "$s"
        h_job "staged_s$s" "$vdir/staged/$3.cod" "$vdir/staged/$3.profile.json" "$AB_GAMES" "$s"
      done; } | h_manifest "$out" >/dev/null
    log "AB $1: $(grep -c '"name"' "$out") jobs, ${AB_GAMES}g x $(echo $AB_SEEDS | wc -w) seeds per arm"
    ./build/Release/mtg --batch "$out" > "$VLQ/ab_$1.log" 2> "$VLQ/ab_$1.err"
    python3 scripts/vlq_ab_report.py "$VLQ/ab_$1.log" 2>&1 | tee -a "$VLQ/driver.log"
    mark "80_ab_$1"
}

case "${1:-status}" in
run)
    log "=== value-leaf regeneration queue: START ==="
    for st in stage_freeze stage_cal stage_dump stage_train stage_hin_matrix stage_hin_meta; do
        check_freeze || exit 1
        $st || { log "STOPPED at $st"; exit 1; }
    done
    check_freeze && stage_ab hinata decks/Hinata2 Hinata2 "$HIN_LIVE" "$HIN_STAGED"
    for st in stage_alg_matrix stage_alg_meta; do
        check_freeze || exit 1
        $st || { log "STOPPED at $st"; exit 1; }
    done
    check_freeze && stage_ab antilife decks/Anti-Lifegain Anti-Lifegain "$ALG_LIVE" "$ALG_STAGED"
    log "=== QUEUE COMPLETE -- nothing adopted; review the A/B reports above ==="
    ;;
status)
    echo "frozen at : $(cat "$VLQ/freeze.commit" 2>/dev/null || echo '(not started)')"
    echo "src now   : $(src_fingerprint | cut -c1-12)   frozen: $( [ -e "$VLQ/freeze.src" ] && cut -c1-12 "$VLQ/freeze.src" || echo - )"
    echo "rows      : $(grep -vc '^#' "$ROWS" 2>/dev/null || echo 0) / $ROW_TARGET"
    echo "stages    : $(ls "$DONE" 2>/dev/null | tr '\n' ' ')"
    echo "mtg procs : $(pgrep -c -x mtg 2>/dev/null || echo 0)"
    tail -5 "$VLQ/driver.log" 2>/dev/null
    ;;
*) echo "usage: $0 {run|status}"; exit 2 ;;
esac
