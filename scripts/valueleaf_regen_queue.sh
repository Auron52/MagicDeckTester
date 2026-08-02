#!/usr/bin/env bash
# Value-leaf + play-profile regeneration queue, all suite decks except Goblins.
# Runbook this implements: docs/design/value-leaf-regeneration-queue.md
#
# PER DECK, IN THIS ORDER -- the model comes FIRST, because the matrix MEASURES the model. A table
# built against a stale eval_model describes a model we are not going to ship, so regenerating the
# table without regenerating the model first gets the dependency backwards:
#     rows -> train (STAGED) -> matrix -> table metadata -> A/B vs live -> play-profile sweep
#
# NOTHING IS ADOPTED. Every artifact lands in logs/eval/<stem>.value.STAGED.json; the live sidecar
# decks/<deck>/<stem>.value.json is never written. The A/B and sweep stages produce the numbers an
# adoption decision needs; installing is a separate deliberate `cp` + smoke/regression + approval.
#
# DECK ORDER IS SLOWEST-FIRST, so that stopping the queue part-way leaves the cheap decks undone --
# they can be finished any time, whereas the slow ones need a long uninterrupted window. Cost basis
# (measured, single-thread, from test/regression_cases.sh + the queue doc): hinata d5 ~1.25 s/game and
# a ~24 h row dump; antilife deep H cells ~5.8 h for ONE seed; dragonstorm/slivers mid; auras cheapest
# in the suite at d5 ~0.0086 s/game. Goblins is deliberately absent: it is regenerating after its
# mulligan profile lands, so measuring it now would be thrown away.
#
# THE FREEZE RULE. Every artifact is an engine-state fingerprint, so the whole queue must run on ONE
# commit -- a play change midway silently produces a table whose own rows disagree. HEAD:src is
# recorded at start and re-checked before every stage; if it moves the queue STOPS rather than mixing
# engine states. Two machines work this branch: announce the freeze or keep it quiet for the duration.
#
#   bash scripts/valueleaf_regen_queue.sh run      # start / resume
#   bash scripts/valueleaf_regen_queue.sh status   # progress, touches nothing
#
# Resumable at stage granularity (markers in logs/vlq/done/). The two long stages resume internally
# too: the dump by (seed,turn) dedupe, the matrix by its <out>.cells.json.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh

VLQ=${VLQ:-logs/vlq}
DONE=$VLQ/done
# Row files live in a directory THIS QUEUE OWNS, not logs/eval/. Two reasons, the second discovered
# the hard way: (1) logs/eval holds other agents' row dumps, and (2) THIS FILESYSTEM IS
# CASE-INSENSITIVE -- logs/eval/Auras_value.rows resolves to the existing logs/eval/auras_value.rows
# (11k rows dumped 2026-07-24 on a pre-merge engine), so a per-deck dump keyed on the deck STEM would
# have silently appended to it and trained on a MIX OF ENGINE STATES. That is the exact defect
# (antilife's internally-mixed table) this whole queue exists to fix, so it cannot be reintroduced by
# a filename collision. Deck stems differing only in case would collide here too; none currently do.
ROWDIR=$VLQ/rows
mkdir -p "$DONE" "$ROWDIR" logs/eval

ROW_TARGET=${ROW_TARGET:-11000}   # the knee, per the queue doc
CAL_GAMES=${CAL_GAMES:-48}        # calibration slice: sizes the real dump from MEASURED rows/game
WORKERS=${WORKERS:-20}            # ~1 GB/process on a 47 GB box
MATRIX_TARGET=${MATRIX_TARGET:-400}
MATRIX_REF=${MATRIX_REF:-50}      # cap for cells ruled intractable
AB_GAMES=${AB_GAMES:-1000}
AB_SEEDS=${AB_SEEDS:-"600000 601000 602000 603000 604000 605000 606000 607000"}
PLAY_GAMES=${PLAY_GAMES:-500}
PLAY_SEEDS=${PLAY_SEEDS:-"610000 611000 612000 613000"}
# ONLY=<key>[ <key>...] restricts the queue to a subset -- for resuming one deck, or for smoke-testing
# the whole per-deck chain cheaply before committing the box to the real multi-day run.
ONLY=${ONLY:-}

# key | deck-dir | stem | matrix-key | K | rollout-labels
# ROLLOUT=1 uses cheap non-clairvoyant d0 labels instead of the clairvoyant searched label. It is the
# right choice where the heuristic is already good (it converges cheap), which is the regime
# learned-d0-policy.md says favours imitating the baseline over distilling an optimum the baseline
# cannot reach -- and it is ~10x cheaper. Dragonstorm's shipped model was built that way deliberately
# (docs/design/dragonstorm-d5-default-and-value-leaf.md); the rest keep searched labels at K=3.
DECK_TABLE=(
  "hinata|decks/Hinata2|Hinata2|hinata|3|0"
  "antilife|decks/Anti-Lifegain|Anti-Lifegain|antilife|3|0"
  "dragonstorm|decks/Dragonstorm|Dragonstorm|dragonstorm|8|1"
  "slivers|decks/slivers_vial|slivers_vial|slivers|3|0"
  "th|decks/treasure_hunt|treasure_hunt|TH|3|0"
  "knights|decks/Knights|Knights|knights|3|0"
  "burn|decks/burn|burn|burn|3|0"
  "auras|decks/Auras|Auras|auras|3|0"
)

log() { echo "[$(date '+%m-%d %H:%M:%S')] $*" | tee -a "$VLQ/driver.log"; }
done_p() { [ -e "$DONE/$1" ]; }
mark()   { date '+%Y-%m-%dT%H:%M:%S' > "$DONE/$1"; }

src_fingerprint() { git rev-parse HEAD:src 2>/dev/null; }
check_freeze() {
    local now frozen
    now=$(src_fingerprint); frozen=$(cat "$VLQ/freeze.src" 2>/dev/null || echo "")
    if [ -n "$frozen" ] && [ "$now" != "$frozen" ]; then
        log "FREEZE VIOLATION: src/ moved ($frozen -> $now). Everything measured so far describes a"
        log "  different engine. Decide what to keep, then restart the queue on one commit."
        return 1
    fi
    return 0
}

# A scratch deck folder differing from the real one ONLY in which value.json it carries: every other
# sibling is symlinked, so the ~600 MB keep-model caches are not copied. Verified byte-identical play
# (same digest, same resolved depth/budget) when the value.json matches the real one.
make_variant_deck() {   # $1 dest  $2 src-deck-dir  $3 stem  $4 value.json
    local dest=$1 src=$2 stem=$3 val=$4 f b
    rm -rf "$dest"; mkdir -p "$dest"
    for f in "$src"/*; do
        b=$(basename "$f")
        [ "$b" = "$stem.value.json" ] && continue
        ln -sf "$(realpath "$f")" "$dest/$b"
    done
    cp "$val" "$dest/$stem.value.json"
}
variant_deck_file() { ls "$1/$2".cod "$1/$2".txt 2>/dev/null | head -1; }

# ------------------------------------------------------------------------ stage 0: freeze + build
stage_freeze() {
    done_p 00_freeze && return 0
    if ! git diff --quiet -- src/ || ! git diff --cached --quiet -- src/; then
        log "ABORT: uncommitted changes under src/ -- the frozen commit would not describe the binary."
        return 1
    fi
    src_fingerprint > "$VLQ/freeze.src"
    git rev-parse --short HEAD > "$VLQ/freeze.commit"
    log "FROZEN at $(cat "$VLQ/freeze.commit")  src-tree $(cut -c1-12 "$VLQ/freeze.src")"
    bash build.sh >> "$VLQ/build.log" 2>&1 || { log "ABORT: build failed, see $VLQ/build.log"; return 1; }
    log "build OK"
    mark 00_freeze
}

# ------------------------------------------------------- per-deck stage 1: rows (calibrate + dump)
# Sized from MEASUREMENT, not extrapolation: a short pooled run over COMPLETE games gives an honest
# rows/game, which is what turns a row target into a game count. The row RATE is a different quantity
# and is optimistic on any short run (turn-1/2 positions are cheap, so every dump starts fast and
# collapses once games reach mid-game) -- judge duration by the sustained rate, never the first minute.
d_rows() {   # key dir stem K rollout
    local key=$1 dir=$2 stem=$3 K=$4 ro=$5
    local rows=$ROWDIR/${stem}.rows
    done_p "${key}_10_rows" && return 0
    if ! done_p "${key}_10_cal"; then
        # A row file with no calibration marker is a leftover from an attempt that did not finish --
        # possibly on a different engine. Archive rather than append: mixing engine states in one
        # training set is unrecoverable once trained, and rows carry no engine fingerprint.
        if [ -s "$rows" ]; then
            mv "$rows" "$rows.orphaned.$(date +%s)"
            log "$key: archived an orphaned row file (no calibration marker -- provenance unknown)"
        fi
        log "$key: calibrating with $CAL_GAMES games (K=$K rollout=$ro)"
        local t0=$SECONDS
        K=$K ROLLOUT=$ro bash scripts/valueleaf_row_dump.sh "$dir" "$stem" "$CAL_GAMES" "$rows" 30030 \
            >> "$VLQ/${key}_dump.log" 2>&1
        local r; r=$(grep -vc '^#' "$rows" 2>/dev/null || echo 0)
        [ "$r" -gt 0 ] || { log "$key ABORT: calibration produced 0 rows (see $VLQ/${key}_dump.log)"; return 1; }
        awk -v r="$r" -v g="$CAL_GAMES" -v s="$((SECONDS-t0))" \
            'BEGIN{printf "rows_per_game=%.4f\nsec_per_game=%.3f\n", r/g, s/g}' > "$VLQ/$key.calibration"
        log "$key: $r rows / $CAL_GAMES games in $((SECONDS-t0))s -> $(tr '\n' ' ' < "$VLQ/$key.calibration")"
        mark "${key}_10_cal"
    fi
    # TOP-UP LOOP -- TAIL-BOUNDED. rows/game from a short calibration slice OVER-ESTIMATES the true
    # yield (the sample is biased toward short games), so one sized dump undershoots: measured on auras,
    # 4.875 rows/game predicted 405 and produced 341. The naive fix -- keep topping up until the target
    # is met -- REINTRODUCES THE EXACT TAIL the pooled batch exists to remove, because ROUNDS ARE
    # BARRIERS: round N+1 cannot start until round N's SLOWEST game finishes, and the rounds shrink fast
    # (auras went 75 -> 13 -> 2 games). A 13-game round on a deck with a pathological-game tail means
    # hours with 23 of 24 cores idle -- the very failure documented in the batch-dump header. So:
    #   * oversize each round by MARGIN so one round normally OVERSHOOTS instead of iterating,
    #   * cap at 2 rounds, and
    #   * stop once within SHORTFALL_OK of the target -- the last few percent of rows are worth far
    #     less than another round's tail, and the trainer floor is half the target anyway.
    # Overshooting is free: the extra games ride in the SAME pooled batch, so they add no tail at all.
    local MARGIN=13 SHORTFALL_OK=10          # MARGIN/10 = 1.3x sizing; accept landing within 10%
    local have rpg need base=40040 round=0 games_done=0 floor_ok
    floor_ok=$(( ROW_TARGET - ROW_TARGET * SHORTFALL_OK / 100 ))
    while :; do
        have=$(grep -vc '^#' "$rows" 2>/dev/null || echo 0)
        if [ "$have" -ge "$floor_ok" ]; then
            [ "$have" -lt "$ROW_TARGET" ] && log "$key: $have rows is within ${SHORTFALL_OK}% of $ROW_TARGET -- stopping rather than paying another round's tail"
            break
        fi
        if [ "$round" -ge 2 ]; then
            log "$key: STOPPING top-up at $have/$ROW_TARGET rows after $round rounds (another round costs a full tail for few rows)"
            break
        fi
        # Yield estimate: observed rows/game once there is real dump data, else the calibration.
        rpg=$(awk -v h="$have" -v g="$games_done" -v c="$(sed -n 's/^rows_per_game=//p' "$VLQ/$key.calibration")" \
              'BEGIN{print (g>0 && h>0) ? h/g : c}')
        need=$(awk -v t="$ROW_TARGET" -v h="$have" -v r="$rpg" -v m="$MARGIN" \
               'BEGIN{n=(t-h)/r*m/10; print (n<1?1:int(n+0.999))}')
        local eta; eta=$(awk -v n="$need" -v f="$VLQ/$key.calibration" \
            'BEGIN{while((getline l < f)>0) if (l ~ /^sec_per_game=/){split(l,a,"=");printf "%.1f", n*a[2]/3600}}')
        log "$key: round $round -- $have/$ROW_TARGET rows; queueing $need games (yield $rpg rows/game, ${MARGIN}/10 margin, rough ETA ${eta}h)"
        K=$K ROLLOUT=$ro bash scripts/valueleaf_row_dump.sh "$dir" "$stem" "$need" "$rows" "$base" \
            >> "$VLQ/${key}_dump.log" 2>&1
        base=$(( base + need + 1000 ))     # margin so consecutive rounds cannot overlap
        games_done=$(( games_done + need ))
        round=$(( round + 1 ))
    done
    log "$key: rows $(grep -vc '^#' "$rows" 2>/dev/null || echo 0) (target $ROW_TARGET)"
    mark "${key}_10_rows"
}

# ------------------------------------------------------------------- per-deck stage 2: train STAGED
# Trains eval_model and merges ONLY that key into a COPY of the live sidecar, so value_play, the old
# table and the old crossover survive until the new table is measured and the A/B has run.
d_train() {   # key dir stem
    local key=$1 dir=$2 stem=$3
    done_p "${key}_20_train" && return 0
    local rows=$ROWDIR/${stem}.rows
    local sorted=$ROWDIR/${stem}.sorted.rows
    local staged=logs/eval/${stem}.value.STAGED.json
    local live="$dir/$stem.value.json"
    # The dump is multi-threaded, so row order varies run to run; sort (keeping the header first) or
    # training is not reproducible. Dedupe on (seed,turn) -- the last two fields -- keyed off NF so
    # adding a feature to the row cannot silently break it.
    { grep -h '^#' "$rows" | head -1
      grep -v '^#' "$rows" | awk '!seen[$(NF-1)" "$NF]++' | sort
    } > "$sorted"
    # Floor scales with the target so a deliberately small run (smoke) is not blocked, while a real
    # run that produced far fewer rows than asked for still stops rather than training on thin data.
    local n floor; n=$(grep -vc '^#' "$sorted" 2>/dev/null || echo 0)
    floor=$(( ROW_TARGET / 2 ))
    [ "$n" -ge "$floor" ] || { log "$key ABORT: only $n unique rows (floor $floor = half the target)"; return 1; }
    log "$key: training on $n unique rows -> $staged"
    python3 scripts/attic/train_eval_gbdt.py --rows "$sorted" --out "$staged.raw" \
        --regression --trees 120 --depth 4 --lr 0.15 --min-leaf 20 >> "$VLQ/${key}_train.log" 2>&1
    [ -s "$staged.raw" ] || { log "$key ABORT: trainer produced nothing (see $VLQ/${key}_train.log)"; return 1; }
    python3 - "$live" "$staged.raw" "$staged" "$(cat "$VLQ/freeze.commit")" "$n" <<'PY'
import json, sys
live, raw, out, commit, n = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5])
L = json.load(open(live)); R = json.load(open(raw))
L["eval_model"] = R["eval_model"]          # ONLY the model; table/crossover/value_play stay until measured
L.setdefault("provenance", {}).update({
    "regenerated_commit": commit, "rows": n,
    "note": "eval_model retrained on rows dumped at SHIPPED play WITH the deck profile "
            "(so the exhaustive keep model is live). value_leaf_table/crossover in this file are "
            "still the OLD ones until the regenerated matrix lands.",
})
json.dump(L, open(out, "w"), indent=1)
PY
    rm -f "$staged.raw"
    mark "${key}_20_train"
}

# --------------------------------------------------------------------- per-deck stage 3: the matrix
# The proper generator: incremental, batched, tractability-aware. Every cell advances in 25-game
# batches round-robin, so the WHOLE table exists at 25 games, then 50, ...; each batch is written the
# instant it lands (<out>.cells.json = resume). A cell measured slower than --intractable-sec-per-game
# is capped at a small reference sample instead of burning the box on a cell no production run could
# use. Run with the box to ITSELF -- the cutoff is wall-clock based, so a loaded box misclassifies
# slow cells as intractable and silently truncates the table.
d_matrix() {   # key matrix-key
    local key=$1 mkey=$2
    done_p "${key}_30_matrix" && return 0
    local out=logs/eval/valueleaf_depth_${key}_regen.txt
    log "$key: matrix ${mkey}_staged H1-5 x V1-5, target $MATRIX_TARGET/cell, workers $WORKERS"
    python3 scripts/attic/valueleaf_depth_matrix.py --incremental --decks "${mkey}_staged" \
        --hdepths 1 2 3 4 5 --vdepths 1 2 3 4 5 --seeds 8008 9009 10010 11011 \
        --target "$MATRIX_TARGET" --reference-target "$MATRIX_REF" --batch 25 --workers "$WORKERS" \
        --value-min-depth 0 --intractable-sec-per-game 3.0 --out "$out" >> "$VLQ/${key}_matrix.log" 2>&1
    [ -s "$out" ] || { log "$key ABORT: no matrix output"; return 1; }
    log "$key: matrix done"
    mark "${key}_30_matrix"
}
d_meta() {   # key matrix-key
    local key=$1 mkey=$2
    done_p "${key}_40_meta" && return 0
    log "$key: table -> metadata (crossover + trust depth) into the staged sidecar"
    python3 scripts/attic/valueleaf_table_to_metadata.py "logs/eval/valueleaf_depth_${key}_regen.txt" \
        --decks "${mkey}_staged" --average-seeds 2>&1 | tee -a "$VLQ/driver.log"
    mark "${key}_40_meta"
}

# ------------------------------------------------- per-deck stage 4: A/B staged vs live (MEASURED)
# Both arms in ONE pooled batch. Bases are spaced by exactly AB_GAMES so the arms tile the seed space
# once each: game identity is base+game_index, so bases spaced closer than games-per-job make jobs
# REPLAY games -- which once turned a 1.3-sigma result into a fake -14.4 sigma (rule 7).
d_ab() {   # key dir stem
    local key=$1 dir=$2 stem=$3
    done_p "${key}_50_ab" && return 0
    local vdir=$VLQ/ab_$key out=$VLQ/ab_$key/manifest.json s
    rm -rf "$vdir"; mkdir -p "$vdir"
    make_variant_deck "$vdir/live"   "$dir" "$stem" "$dir/$stem.value.json"
    make_variant_deck "$vdir/staged" "$dir" "$stem" "logs/eval/$stem.value.STAGED.json"
    { for s in $AB_SEEDS; do
        h_job "live_s$s"   "$(variant_deck_file "$vdir/live" "$stem")"   "$vdir/live/$stem.profile.json"   "$AB_GAMES" "$s"
        h_job "staged_s$s" "$(variant_deck_file "$vdir/staged" "$stem")" "$vdir/staged/$stem.profile.json" "$AB_GAMES" "$s"
      done; } | h_manifest "$out" >/dev/null
    log "$key: A/B staged-vs-live, ${AB_GAMES}g x $(echo $AB_SEEDS | wc -w) seeds per arm"
    ./build/Release/mtg --batch "$out" > "$VLQ/ab_$key.log" 2> "$VLQ/ab_$key.err"
    echo "--- $key: regenerated value-leaf vs live ---" | tee -a "$VLQ/driver.log"
    python3 scripts/vlq_ab_report.py "$VLQ/ab_$key.log" live 2>&1 | tee -a "$VLQ/driver.log"
    mark "${key}_50_ab"
}

# ------------------------------------------ per-deck stage 5: play profile (value_play target depth)
# The play profile is the value_play block, and the depth is the part the table actually informs --
# Hinata's block still says "PROVISIONAL d5 default; revise after mulligan profile + depth table".
# Sweep target_depth around the shipped one on the REGENERATED model, all arms in one pooled batch.
# escalation_cap tracks target_depth because it is set equal to it on every deck (measured: the cap
# never binds, its live role is selecting the single-pass path), so moving depth alone would silently
# change what the cap does.
d_play() {   # key dir stem
    local key=$1 dir=$2 stem=$3
    done_p "${key}_60_play" && return 0
    local staged=logs/eval/$stem.value.STAGED.json
    local base; base=$(python3 -c "
import json; print((json.load(open('$staged')).get('value_play') or {}).get('target_depth') or 5)")
    local vdir=$VLQ/play_$key out=$VLQ/play_$key/manifest.json d s
    rm -rf "$vdir"; mkdir -p "$vdir"
    local depths=""
    for d in $((base-1)) $base $((base+1)); do [ "$d" -ge 3 ] && depths="$depths $d"; done
    for d in $depths; do
        python3 - "$staged" "$vdir/d$d.value.json" "$d" <<'PY'
import json, sys
src, out, d = sys.argv[1], sys.argv[2], int(sys.argv[3])
v = json.load(open(src)); vp = v.setdefault("value_play", {})
vp["target_depth"] = d
vp["escalation_cap"] = d
json.dump(v, open(out, "w"), indent=1)
PY
        make_variant_deck "$vdir/d$d" "$dir" "$stem" "$vdir/d$d.value.json"
    done
    { for d in $depths; do for s in $PLAY_SEEDS; do
        h_job "d${d}_s$s" "$(variant_deck_file "$vdir/d$d" "$stem")" "$vdir/d$d/$stem.profile.json" "$PLAY_GAMES" "$s"
      done; done; } | h_manifest "$out" >/dev/null
    log "$key: play-profile sweep target_depth ∈{$depths } (shipped $base), ${PLAY_GAMES}g x $(echo $PLAY_SEEDS | wc -w) seeds"
    ./build/Release/mtg --batch "$out" > "$VLQ/play_$key.log" 2> "$VLQ/play_$key.err"
    echo "--- $key: play-profile target_depth sweep (baseline d$base = shipped) ---" | tee -a "$VLQ/driver.log"
    python3 scripts/vlq_ab_report.py "$VLQ/play_$key.log" "d$base" 2>&1 | tee -a "$VLQ/driver.log"
    # Cost per arm: the pooled batch cannot attribute wall time, so time one small job per arm
    # single-threaded. Quality is the primary metric; this is the tie-breaker when quality is flat.
    for d in $depths; do
        local t0=$SECONDS
        ./build/Release/mtg "$(variant_deck_file "$vdir/d$d" "$stem")" \
            --profile "$vdir/d$d/$stem.profile.json" --seed 620000 --games 50 --threads 1 >/dev/null 2>&1
        log "$key: cost d$d = $((SECONDS-t0))s / 50 games single-thread"
    done
    mark "${key}_60_play"
}

case "${1:-status}" in
run)
    log "=== value-leaf + play-profile regeneration queue: START ==="
    check_freeze || exit 1
    stage_freeze || exit 1
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey K ro <<< "$row"
        if [ -n "$ONLY" ] && ! grep -qw "$key" <<< "$ONLY"; then continue; fi
        log ">>> DECK $key ($stem)"
        for st in "d_rows $key $dir $stem $K $ro" "d_train $key $dir $stem" "d_matrix $key $mkey" \
                  "d_meta $key $mkey" "d_ab $key $dir $stem" "d_play $key $dir $stem"; do
            check_freeze || exit 1
            $st || { log "STOPPED in $key at: $st"; exit 1; }
        done
        log "<<< DECK $key COMPLETE (staged only -- nothing adopted)"
    done
    log "=== QUEUE COMPLETE -- review the A/B + sweep reports; adoption is a separate step ==="
    ;;
status)
    echo "frozen at : $(cat "$VLQ/freeze.commit" 2>/dev/null || echo '(not started)')"
    echo "src now   : $(src_fingerprint | cut -c1-12)   frozen: $( [ -e "$VLQ/freeze.src" ] && cut -c1-12 "$VLQ/freeze.src" || echo - )"
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey K ro <<< "$row"
        printf "%-12s rows=%-7s stages: %s\n" "$key" \
            "$(grep -vc '^#' "$ROWDIR/${stem}.rows" 2>/dev/null || echo 0)" \
            "$(ls "$DONE" 2>/dev/null | grep "^${key}_" | sed "s/^${key}_//" | tr '\n' ' ')"
    done
    echo "mtg procs : $(pgrep -c -x mtg 2>/dev/null || echo 0)"
    tail -6 "$VLQ/driver.log" 2>/dev/null
    ;;
*) echo "usage: $0 {run|status}"; exit 2 ;;
esac
