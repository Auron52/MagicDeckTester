#!/usr/bin/env bash
# THE value-leaf pipeline. One script, one route, for ONE deck or the whole fleet.
#
#   bash scripts/valueleaf.sh run    decks/<Deck>     # build a model (new deck or regeneration)
#   bash scripts/valueleaf.sh status decks/<Deck>     # progress, touches nothing
#   bash scripts/valueleaf.sh run                     # the whole fleet
#
# THE INTERFACE IS THE DECK. There are no knobs -- every setting that matters is FIXED here rather
# than left to a caller to remember, and a retired one is REJECTED rather than ignored. Each has
# already gone wrong once:
#
#   * INCREMENTAL BATCHING, ALWAYS. Every phase is one pooled queue, resumable, with concurrent
#     batches per cell. The monolithic path is removed. A per-item loop pays a load-imbalance tail
#     PER ITEM, and one-batch-per-cell scheduling starved a live run to 3 of 24 cores for hours.
#   * NO CONDEMNATION AT d<=5. The H cells ARE the crossover -- they decide which evaluator
#     escalation uses at each rung it climbs to -- so condemning one leaves a HOLE in the answer
#     rather than saving cost. The guard is wall-clock based, so which cells it hits is partly luck
#     (H3 measured 219.8 s/game on one seed and ~30 on two others). Clamped, not just defaulted.
#   * THE PROFILE IS ALWAYS ATTACHED. Measuring profile-less describes a deck we do not ship; it
#     invalidated every value-leaf table in this repo once already.
#   * THE STAGED MODEL EXISTS BEFORE THE MATRIX. The H-cell ladder is guarded on the sidecar
#     existing, so a missing one does not error -- it silently runs every H cell on the slow path.
#   * SLOW GAMES AND UTILISATION ARE REPORTED, by the engine itself. `mtg --batch` defaults both ON:
#     any game over 30 s prints a self-contained repro (tagged by cell, mirrored to
#     <queue>/slow_games.log) and a `[batch] heartbeat` line every 10 min leads with workers busy.
#     They are not passed from here any more -- doing so is how they came to be OFF for every other
#     caller, which is why a 23-hour run at 3 of 20 cores produced no signal.
#
# NOTHING IS ADOPTED: artifacts land in logs/eval/<stem>.value.STAGED.json. Phase E produces the
# numbers an adoption decision needs; the decision is yours.
#
# New deck and regeneration are the SAME command -- the script detects whether a live sidecar exists
# and adapts (merge into a copy vs. use the fresh model directly; A/B against "no sidecar" vs. the
# old one). Resume is safe and incremental: finished phases are skipped via marker files, and rows
# dedupe on (seed,turn). See .claude/skills/value-leaf.md.
#
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
. test/lib/harness.sh

# ---------------------------------------------------------------- THE INTERFACE IS THE DECK ONLY
#
# `bash scripts/valueleaf.sh run decks/<Deck>` and nothing else. Every value below is FIXED, not a
# default, because each knob that existed here was a way to get a wrong answer that still looked
# like an answer: HDEPTHS silently truncated the ladder the crossover clamps to, MATRIX_TARGET
# resized the measurement, SLOW_GAME_MS could switch off the only instrument that names a
# pathological game, VLQ could point a single-deck run at the shared fleet directory (which is
# exactly how one run's slow games ended up in another's log).
#
# The ONE setting is NEVER_CONDEMN, kept deliberately (user, 2026-08-10): the d<=5 floor is the
# default we want, and raising it is a legitimate choice when a deeper ladder is being measured.
#
# A retired knob is REJECTED, not ignored -- silently dropping `HDEPTHS=1 2 3` would hand back a
# table that looks complete and is not. See RETIRED_KNOBS below.
RETIRED_KNOBS="VLQ WORKERS MATRIX_TARGET MATRIX_REF SLOW_GAME_MS SLOW_GAME_LOG VDEPTHS HDEPTHS
               INTRACTABLE_SPG AB_GAMES AB_SEEDS PLAY_GAMES PLAY_SEEDS ROW_K ROW_GAMES DECK_DIR"
_set_knobs=""
for _k in $RETIRED_KNOBS; do
    if [ -n "${!_k+x}" ]; then _set_knobs="$_set_knobs $_k"; fi
done
if [ -n "$_set_knobs" ]; then
    echo "REFUSING TO RUN: these settings were retired and are no longer honoured:$_set_knobs" >&2
    echo "  The interface is the deck: $0 run decks/<Deck>" >&2
    echo "  Every phase setting is fixed in the script on purpose -- a knob here produces a table" >&2
    echo "  that looks complete but measures something else. NEVER_CONDEMN (floor 5) is the one" >&2
    echo "  setting that remains. See .claude/skills/value-leaf.md." >&2
    exit 2
fi

VLQ=logs/vlq              # fleet default; single-deck mode scopes this to logs/vlq_<key> below
DONE=$VLQ/done
ROWDIR=$VLQ/rows          # queue-owned: logs/eval collides case-insensitively on this filesystem
                          # (Auras_value.rows resolves to the pre-merge auras_value.rows)
mkdir -p "$DONE" "$ROWDIR" logs/eval

# NO worker count is passed. The engine resolves it to AffinityCpuCount() -- every core the process
# is actually allowed to use -- which beats any number chosen here: it is affinity-aware (nproc and
# os.cpu_count() both over-report inside a cgroup with a restricted cpuset), and it cannot drift out
# of date with the box.
#
# The old value was 20 with the note "~1 GB/process on a 47 GB box". That reasoning expired when the
# matrix stopped spawning one PROCESS per cell: it is now ONE process whose workers SHARE each
# mulligan profile through a shared_ptr, and measured peak RSS at 24 threads on the heaviest keep
# model in the repo (Goblins) is 1,481 MB -- 3% of the box. There was never a reason to leave four
# cores idle on a run whose entire failure mode is idle cores. (User, 2026-08-10: "Why do we want
# less than the maximum workers? This makes no sense to me." It did not.)
WORKERS=auto
MATRIX_TARGET=400
MATRIX_REF=50                     # games cap for cells ruled intractable
# Protect the d<=5 ladder from the tractability guard. The H cells ARE the crossover: they decide
# which evaluator escalation uses at each rung it climbs to, so condemning one leaves a HOLE in the
# answer rather than saving cost. FiveColour 2026-08-08 is the worked example -- 8 capped cells, all
# of them H (H4 on every seed, H5 on three, one with zero games), while all 20 V cells finished at
# 400. That makes crossover entries for c=4..8 unfounded for a deck that ships at d5. The guard is
# also wall-clock based, so it can fire purely because the box was busy.
# THE ONE SETTING (user, 2026-08-10). Default 5 -- never condemn at or below the shipped play
# depth -- and raisable when a deeper ladder is being measured. It cannot go BELOW 5 (clamped
# just below), because that is the case that silently holes the answer rather than saving cost.
NEVER_CONDEMN=${NEVER_CONDEMN:-5}
# Clamped, not merely defaulted. The d<=5 H cells ARE the crossover -- they decide which evaluator
# escalation uses at each rung it climbs to -- so condemning one leaves a HOLE in the answer rather
# than saving cost, and the guard is wall-clock based so which cells it hits is partly luck. There is
# exactly ONE route here: incremental batching, no condemnation at d<=5.
if [ "$NEVER_CONDEMN" -lt 5 ] 2>/dev/null; then
    echo "NEVER_CONDEMN=$NEVER_CONDEMN is below the floor; forcing 5 (see docs/design/value-leaf-regeneration-queue.md)"
    NEVER_CONDEMN=5
fi
# Slow-game reporting is no longer set here: `mtg --batch` defaults it ON at 30 s and also prints a
# 10-minute `[batch] heartbeat` line leading with worker utilisation. Passing it from this script was
# how it came to be OFF for every caller that was not this script.
VDEPTHS="1 2 3 4 5 6 7 8"
# The HEURISTIC ladder, 1-5 -- H5 is the ESCALATION CAP, so it is also the deepest rung worth
# measuring (user, 2026-08-14). Escalation can never exceed H5, so H5 is the strongest fallback the
# runtime can take, so "trust the leaf" means "the leaf matches H5" and nothing deeper can inform a
# decision the runtime is incapable of making. The crossover's maxH+1 sentinel then reads as NEVER
# fall back, which is exactly the wanted behaviour.
#
# H6 WAS in this ladder and is removed. It has NEVER been completed on any deck in any run: every H6
# on disk is a small side-sample (fivecolour 4g, antilife 50g, dragonstorm 100g, all condemned; the
# 5-deck tables' H6 was merged in from a 150-GAME `d68` pass into tables declaring games=500 --
# `_antilife_d68_avg.txt` and `_5deck_combined.txt` both carry H6=4.6933, byte-identical). The
# numbers say the same: H4->H5 across all decks is 0.0000-0.0033 while H5->H6 ranges -0.6048 to
# +0.1525, i.e. noise. The old rationale here -- "H6 came in ~11x CHEAPER than H5" -- is contradicted
# by the 2026-08 FiveColour run: H5=822515.7ms vs H6=991283.1ms per game, 1.2x MORE expensive, and
# that measured only on the 4-50 games that tripped the one-hour guard, so it is a floor. Keeping it
# cost 47.4 core-h of that run for 111 unusable games, supplied the H6 row that flipped the derived
# fallback rule from "never" to "fall back at H6", and its ragged condemned chunks (n=4, n=7) held two
# seeds' banking hostage. See docs/design/depth-matrix-degenerate-games.md.
#
# Do NOT cut to H4: the two decks still improving at H3->H4 (dragonstorm +0.1100, hinata +0.0850) are
# exactly the two whose H5 was never measured -- dragonstorm's was ATTEMPTED AND CONDEMNED. Depth
# stays useful roughly as far as a deck's games run (antilife wins turn 4.09, dies at H2->H3;
# fivecolour 4.98, dies at H4->H5; hinata's games run to turn 6.00). H5 is the conservative ceiling.
#
# Note H_maxturns == V_maxturns is the SAME cell: at depth >= max_turns the horizon covers the whole
# game, so every leaf is terminal and the learned evaluator is never consulted. FiveColour has
# max_turns=8, so H8 == V8 -- never pay ~68 core-hours to re-measure a cell the V arm already has.
HDEPTHS="1 2 3 4 5"
# Tractability guard, seconds/game. This is a SAFETY VALVE against a genuinely exploding cell, not a
# budget: at 3.0 it condemned cells running at 4.33 s/game whose full fill cost 4 CORE-HOURS in total
# -- 0.3% of the run that skipped them -- and left H1-5 unmeasured on two decks, which is what the
# table is derived from. Set it high enough that only a real explosion trips it.
INTRACTABLE_SPG=60
AB_GAMES=1000
AB_SEEDS="600000 601000 602000 603000 604000 605000 606000 607000"
PLAY_GAMES=500
PLAY_SEEDS="610000 611000 612000 613000"
ROW_K=3

# key | deck-dir | stem | matrix-key | row-seed-base | row-games -- GENERATED, never edited.
#
# scripts/deck_registry.py discovers decks/*/ and derives every path from the folder, so a new deck
# needs no entry anywhere. Row seed bases are distinct multiples of 100k (row-games never approaches
# 100k), so `seed / 100000` recovers the deck when splitting the pooled file; the registry derives
# each from a hash of the deck key rather than from position, so adding or removing a deck cannot
# renumber the others and orphan rows already on disk.
#
# Games OVERSHOOT the ~11k-row knee (~5-6 rows/game measured on hinata and auras) because overshoot
# inside one pooled batch is free -- it adds no tail -- whereas undershooting needs a second batch and
# therefore a second tail.
ROW_GAMES=2500
mapfile -t DECK_TABLE < <(python3 scripts/deck_registry.py --shell "$ROW_GAMES")
[ ${#DECK_TABLE[@]} -gt 0 ] || { echo "no decks discovered under decks/ -- each needs <stem>.cod|.txt AND <stem>.profile.json"; exit 2; }
# SINGLE-DECK MODE. `run <deck-dir>` replaces the table above with one row derived from the folder,
# so a NEW deck goes through exactly this pipeline instead of a hand-rolled one. Nothing else differs:
# same phases, same staging, same freeze rule. Its own VLQ dir keeps it from colliding with a fleet run.
DECK_DIR=${2:-}
if [ -n "$DECK_DIR" ]; then
    _stem=$(basename "$DECK_DIR")
    _key=$(echo "$_stem" | tr '[:upper:]' '[:lower:]' | tr -c 'a-z0-9\n' '_')
    [ -d "$DECK_DIR" ] || { echo "no such deck dir: $DECK_DIR"; exit 2; }
    [ -e "$DECK_DIR/$_stem.profile.json" ] || { echo "no profile at $DECK_DIR/$_stem.profile.json -- run analyze_deck.py first"; exit 2; }
    DECK_TABLE=("$_key|$DECK_DIR|$_stem|$_key|900000|$ROW_GAMES")
    VLQ=logs/vlq_$_key
    DONE=$VLQ/done; ROWDIR=$VLQ/rows
    mkdir -p "$DONE" "$ROWDIR" logs/eval
fi

ALL_ROWS=$ROWDIR/all.rows
MATRIX_TXT=$VLQ/matrix.txt          # per-run: a single-deck run must not overwrite the fleet table
# Derived AFTER single-deck mode has finalised VLQ. Set beside SLOW_GAME_MS above, it captured the
# fleet default (logs/vlq/slow_games.log) and every single-deck run appended to the same shared file
# -- the FiveColour run's 17 slow games landed there while its own queue dir showed none.
SLOW_GAME_LOG=$VLQ/slow_games.log

log() { echo "[$(date '+%m-%d %H:%M:%S')] $*" | tee -a "$VLQ/driver.log"; }
done_p() { [ -e "$DONE/$1" ]; }
mark()   { date '+%Y-%m-%dT%H:%M:%S' > "$DONE/$1"; }
deck_file() { ls "$1/$2".cod "$1/$2".txt 2>/dev/null | head -1; }

src_fingerprint() { git rev-parse HEAD:src 2>/dev/null; }

# PLAY fingerprint: the smoke suite's per-case play digests, folded to one hash.
#
# What the freeze is actually protecting is that every cell of the table was measured by the same
# PLAY. `git rev-parse HEAD:src` is only a proxy for that, and it over-triggers badly: a comment, a
# docs block under src/, a card-data edit, or an opt-in flag that is byte-identical all move the tree
# hash and invalidate a table they cannot possibly have affected. That is not hypothetical -- it cost
# this pipeline a run on 2026-08-12, twice, for changes the suite proved play-neutral (33/33 configs,
# 0 games changed). If play does not change there is nothing to worry about (user, 2026-08-12).
#
# The digests in test/results/smoke.env ARE the repo's play-identity instrument: each is a fold of
# per-game decision-stream hashes over 33 configs spanning every suite deck at d0/d3/d5, and the
# regression harness already treats a digest move as a play change even when the average is identical.
# Folding them gives a single value that is stable across machines and thread counts (every run is
# deterministic and thread-invariant) and changes iff play changed within that coverage.
#
# COVERAGE IS THE LIMIT, and it is a sample, not a proof: a play change confined to a deck or depth
# the smoke matrix does not exercise would not show up here. So this is only ever used to ACCEPT a
# src move that the suite says is play-neutral -- never to reject one, and never to skip the src check
# when the fingerprint is missing. Failing that way round means the worst case is the old behaviour.
play_fingerprint() {
    bash test/regression.sh --smoke >/dev/null 2>&1 || true   # digests are recorded even on FAIL
    [ -f test/results/smoke.env ] || return 1
    # <key>=<avg>/<digest> -- keep the digest half only, so a pure timing/avg difference cannot move
    # this and a digest difference always does. Sorted so the fold is order-independent.
    sed -n 's/.*=\([0-9.]*\)\/\([0-9a-f]*\)$/\2/p' test/results/smoke.env | sort | sha256sum | cut -d' ' -f1
}

check_freeze() {
    local now frozen play_now play_frozen
    now=$(src_fingerprint); frozen=$(cat "$VLQ/freeze.src" 2>/dev/null || echo "")
    if [ -z "$frozen" ] || [ "$now" = "$frozen" ]; then return 0; fi

    # src moved. NEITHER outcome throws the run away (user, 2026-08-12) -- the two differ only in how
    # much is kept:
    #
    #   PLAY UNCHANGED -> keep EVERYTHING. The measurements are interchangeable, so the chunks are
    #     re-stamped to the new src and the matrix driver's resync sees nothing to absorb.
    #   PLAY CHANGED   -> keep the FULL SETS and drop the rest. Fall through to the driver, whose
    #     resync_engine_change keeps offsets below B (the completed common prefix -- every cell of the
    #     seed has them) and re-queues everything above it. That is the chunk rule the skill already
    #     documents; the blanket stop here was overriding it, which is what made a play change cost the
    #     whole run instead of one level. Set-completion ordering is what makes B worth keeping.
    play_frozen=$(cat "$VLQ/freeze.play" 2>/dev/null || echo "")
    log "src/ moved ($(echo "$frozen" | cut -c1-12) -> $(echo "$now" | cut -c1-12)); checking whether PLAY moved..."
    play_now=$(play_fingerprint || echo "")
    if [ -n "$play_frozen" ] && [ -n "$play_now" ] && [ "$play_now" = "$play_frozen" ]; then
        log "  PLAY UNCHANGED (smoke digests fold to $(echo "$play_now" | cut -c1-12)) -- keeping every game."
        # Re-stamp the CHUNKS too, not just the freeze: they were produced by a play-identical engine,
        # so leaving them on the old src would make the driver's resync drop them for no reason.
        python3 - "$MATRIX_TXT.cells.json" "$now" <<'PY' 2>/dev/null || true
import json, sys
p, new = sys.argv[1], sys.argv[2]
try:    cells = json.load(open(p))
except Exception: sys.exit(0)
for c in cells:
    for ch in c.get("chunks", []): ch["src"] = new
json.dump(cells, open(p, "w"))
PY
    else
        log "  PLAY CHANGED ($(echo "${play_frozen:-unstamped}" | cut -c1-12) -> $(echo "${play_now:-unavailable}" | cut -c1-12)) -- keeping"
        log "  the completed FULL SETS and re-running everything above them (see resync_engine_change)."
    fi
    src_fingerprint > "$VLQ/freeze.src"; git rev-parse --short HEAD > "$VLQ/freeze.commit"
    if [ -n "$play_now" ]; then printf '%s\n' "$play_now" > "$VLQ/freeze.play"; fi
    return 0
}

# Scratch deck folder differing from the real one ONLY in its value.json; siblings are symlinked so the
# ~600 MB keep-model caches are not copied. Verified byte-identical play when the value.json matches.
make_variant_deck() {   # dest src-dir stem value.json
    local dest=$1 src=$2 stem=$3 val=$4 f b
    rm -rf "$dest"; mkdir -p "$dest"
    for f in "$src"/*; do
        b=$(basename "$f"); [ "$b" = "$stem.value.json" ] && continue
        ln -sf "$(realpath "$f")" "$dest/$b"
    done
    # A missing/empty source means NO sidecar -- which is exactly the live arm for a deck that has
    # never had one. Placing nothing is correct: sidecar PRESENCE alone activates the depth-aware
    # hybrid in play, so copying an empty file would silently make the "live" arm the staged arm.
    [ -s "$val" ] && cp "$val" "$dest/$stem.value.json"
    return 0
}

# ------------------------------------------------------------------------- phase 0: freeze + build
phase_freeze() {
    done_p 00_freeze && return 0
    if ! git diff --quiet -- src/ || ! git diff --cached --quiet -- src/; then
        log "ABORT: uncommitted changes under src/ -- the frozen commit would not describe the binary."
        return 1
    fi
    src_fingerprint > "$VLQ/freeze.src"; git rev-parse --short HEAD > "$VLQ/freeze.commit"
    log "FROZEN at $(cat "$VLQ/freeze.commit")  src-tree $(cut -c1-12 "$VLQ/freeze.src")"
    bash build.sh >> "$VLQ/build.log" 2>&1 || { log "ABORT: build failed, see $VLQ/build.log"; return 1; }
    log "build OK"
    # Stamp the PLAY fingerprint alongside the src one, so a later src move can be judged on whether
    # it changed play rather than on whether it touched a file. Recorded AFTER the build so it
    # describes the binary the run will actually use. Best-effort: without it check_freeze simply
    # keeps the old src-only behaviour.
    if pf=$(play_fingerprint); then
        printf '%s\n' "$pf" > "$VLQ/freeze.play"
        log "play fingerprint $(echo "$pf" | cut -c1-12) (smoke digests)"
    else
        log "WARN: could not record a play fingerprint; a src move will stop the run as before"
    fi
    mark 00_freeze
}

# ------------------------------------------------------- PHASE A: every deck's rows, ONE pooled batch
phase_rows() {
    done_p A_rows && return 0
    local key dir stem mkey base games row b n
    { for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        b=0
        while [ "$b" -lt "$games" ]; do
            n=$(( games - b < 250 ? games - b : 250 ))
            h_job "${key}_rows_$(printf '%05d' "$b")" "$(deck_file "$dir" "$stem")" \
                  "$dir/$stem.profile.json" "$n" "$(( base + b ))"
            b=$(( b + 250 ))
        done
      done; } | h_manifest "$ALL_ROWS.manifest.json" >/dev/null
    # Counted from the manifest, not accumulated in the loop: the loop runs inside the pipeline's
    # SUBSHELL, so a variable incremented there is lost and the line logged "0 games".
    log "PHASE A: $(grep -c '"name"' "$ALL_ROWS.manifest.json") jobs across ${#DECK_TABLE[@]} decks in ONE queue (K=$ROW_K searched labels)."
    log "  One tail, at the very end. Hinata dominates; the other decks fill cores behind its slow games."
    # --threads 0 = let the engine resolve to every logical CPU it is actually allowed to use
    # (affinity-aware), exactly as phases C and E already do. The old literal 24 was the DEV box's
    # core count baked in as a number -- on a 32-thread machine it silently left a quarter of the
    # box idle through the most expensive phase.
    MTG_DUMP_VALUE_ROWS="$ALL_ROWS" MTG_EVAL_ROWS_K="$ROW_K" MTG_EVAL_ROWS_ROLLOUT=0 \
        ./build/Release/mtg --batch "$ALL_ROWS.manifest.json" --threads 0 \
        > "$VLQ/rows.batch.log" 2>&1
    log "PHASE A done: $(grep -vc '^#' "$ALL_ROWS" 2>/dev/null || echo 0) rows total"
    mark A_rows
}

# Split the pooled file by seed range, then dedupe + sort per deck. Dedupe is on (seed,turn) -- the last
# two fields, keyed off NF so adding a feature to the row cannot silently break it. Sorting matters
# because the dump is multi-threaded: row order varies run to run, and an unsorted training file makes
# training irreproducible.
phase_split() {
    done_p A_split && return 0
    local key dir stem mkey base games row hdr bucket
    hdr=$(grep -m1 '^#' "$ALL_ROWS")
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        bucket=$(( base / 100000 ))
        { echo "$hdr"
          awk -v b="$bucket" '!/^#/ && int($(NF-1)/100000)==b' "$ALL_ROWS" \
            | awk '!seen[$(NF-1)" "$NF]++' | sort
        } > "$ROWDIR/$stem.rows"
        log "  split $key: $(grep -vc '^#' "$ROWDIR/$stem.rows") unique rows"
    done
    mark A_split
}

# ------------------------------------------- PHASE B: train each deck's model (no games, so no tail)
# Merges ONLY eval_model into a COPY of the live sidecar, so value_play / the old table / the old
# crossover survive until the new table is measured and the A/B has run. A deck that fails here is
# skipped, not fatal -- the rest of the fleet still completes.
phase_train() {
    done_p B_train && return 0
    local key dir stem mkey base games row src staged n
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        done_p "B_train_$key" && continue
        src=$ROWDIR/$stem.rows; staged=logs/eval/$stem.value.STAGED.json
        n=$(grep -vc '^#' "$src" 2>/dev/null || echo 0)
        if [ "$n" -lt 1000 ]; then log "  $key SKIPPED: only $n rows"; continue; fi
        log "  training $key on $n rows"
        python3 scripts/attic/train_eval_gbdt.py --rows "$src" --out "$staged.raw" \
            --regression --trees 120 --depth 4 --lr 0.15 --min-leaf 20 >> "$VLQ/train_$key.log" 2>&1
        if [ ! -s "$staged.raw" ]; then log "  $key SKIPPED: trainer produced nothing"; continue; fi
        python3 - "$dir/$stem.value.json" "$staged.raw" "$staged" "$(cat "$VLQ/freeze.commit")" "$n" <<'PY'
import json, os, sys
live, raw, out, commit, n = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5])
R = json.load(open(raw))
if os.path.exists(live):
    L = json.load(open(live))          # REgeneration: keep value_play/table/crossover until measured
    L["eval_model"] = R["eval_model"]
else:
    L = R                              # FIRST model for this deck: nothing prior to preserve
L.setdefault("provenance", {}).update({
    "regenerated_commit": commit, "rows": n,
    "note": "eval_model retrained on rows dumped at SHIPPED play WITH the deck profile, so the "
            "exhaustive keep model was live. Labels are searched K=3 for every deck (a pooled dump "
            "forces one label mode). value_leaf_table/crossover here are the OLD ones until the "
            "regenerated matrix lands.",
})
json.dump(L, open(out, "w"), indent=1)
PY
        rm -f "$staged.raw"; mark "B_train_$key"
    done
    mark B_train
}

# ------------------------------------ PHASE C: every deck's matrix cells, ONE work-stealing pool
# Incremental, batched, tractability-aware: cells advance in 25-game batches submitted round-robin into
# a work-stealing pool, so a cell stuck on a long game holds ONE worker while the rest keep churning,
# and a cell slower than --intractable-sec-per-game is capped at a reference sample rather than burning
# the box on a cell no production run could use. Passing every deck at once means one pool and one tail
# instead of one per deck. Run with the box to ITSELF: the cutoff is wall-clock based, so a loaded box
# misclassifies slow cells as intractable and silently truncates the table.
staged_keys() {
    local row key dir stem mkey base games out=""
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        [ -s "logs/eval/$stem.value.STAGED.json" ] && out="$out ${mkey}_staged"
    done
    echo "$out"
}
phase_matrix() {
    done_p C_matrix && return 0
    local keys; keys=$(staged_keys)
    [ -n "$keys" ] || { log "PHASE C ABORT: no staged models"; return 1; }
    log "PHASE C: matrix over$keys -- ONE pool, all available cores, target $MATRIX_TARGET/cell, H=[$HDEPTHS], V=[$VDEPTHS], cutoff ${INTRACTABLE_SPG}s/game, never-condemn<=$NEVER_CONDEMN"
    MTG_SLOW_GAME_LOG="$SLOW_GAME_LOG" \
    python3 scripts/attic/valueleaf_depth_matrix.py --incremental --decks $keys \
        --hdepths $HDEPTHS --vdepths $VDEPTHS --seeds 8008 9009 10010 11011 \
        --target "$MATRIX_TARGET" --reference-target "$MATRIX_REF" --batch 25 \
        --value-min-depth 0 --intractable-sec-per-game "$INTRACTABLE_SPG" \
        --never-condemn-at-or-below "$NEVER_CONDEMN" \
        --out "$MATRIX_TXT" >> "$VLQ/matrix.log" 2>&1
    [ -s "$MATRIX_TXT" ] || { log "PHASE C ABORT: no matrix output"; return 1; }
    log "PHASE C done"
    mark C_matrix
}

# --------------------------------------------------- PHASE D: table -> metadata (no games, no tail)
phase_meta() {
    done_p D_meta && return 0
    local keys; keys=$(staged_keys)
    log "PHASE D: crossover + trust depth ->$keys"
    python3 scripts/attic/valueleaf_table_to_metadata.py "$MATRIX_TXT" \
        --decks $keys --average-seeds 2>&1 | tee -a "$VLQ/driver.log"
    mark D_meta
}

# --------------------- PHASE E: A/B + play sweeps for every deck, ONE pooled batch (MEASURED ONLY)
# Bases are spaced by exactly games-per-job so each arm tiles its seed space once: game identity is
# base+game_index, so closer spacing makes jobs REPLAY games -- which once turned 1.3 sigma into a fake
# -14.4 sigma (rule 7). Job names are "<deck>-<arm>_s<seed>" so the single log splits per deck after.
phase_measure() {
    done_p E_measure && return 0
    local vroot=$VLQ/variants; rm -rf "$vroot"; mkdir -p "$vroot"
    rm -f "$VLQ/play_baseline"
    local row key dir stem mkey base games staged s d bd drives bl
    { for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        staged=logs/eval/$stem.value.STAGED.json
        [ -s "$staged" ] || continue
        make_variant_deck "$vroot/$key/live"   "$dir" "$stem" "$dir/$stem.value.json"
        make_variant_deck "$vroot/$key/staged" "$dir" "$stem" "$staged"
        for s in $AB_SEEDS; do
            h_job "$key-live_s$s"   "$(deck_file "$vroot/$key/live" "$stem")"   "$vroot/$key/live/$stem.profile.json"   "$AB_GAMES" "$s"
            h_job "$key-staged_s$s" "$(deck_file "$vroot/$key/staged" "$stem")" "$vroot/$key/staged/$stem.profile.json" "$AB_GAMES" "$s"
        done
        # play-profile sweep: target_depth around the shipped one, on the REGENERATED model.
        # escalation_cap tracks target_depth because every deck ships them equal (measured: the cap
        # never binds), so moving depth alone would silently change what the cap does.
        read -r bd drives <<< "$(python3 -c "
import json; vp = json.load(open('$staged')).get('value_play') or {}
print(vp.get('target_depth') or 5, 1 if (vp.get('target_depth') and vp.get('enabled')) else 0)")"
        # A staged model with no ENABLED block ships the BUILT-IN default (d5, budget 20, no
        # escalation cap) -- exactly what the A/B above measured -- so THAT is the honest baseline
        # and the enabled arms below measure "is an adopted play policy worth it at all?", not just
        # which depth. For a regeneration the live block already drives, so d<bd> IS that baseline.
        if [ "$drives" = 0 ]; then
            make_variant_deck "$vroot/$key/pdflt" "$dir" "$stem" "$staged"
            for s in $PLAY_SEEDS; do
                h_job "$key-dflt_s$s" "$(deck_file "$vroot/$key/pdflt" "$stem")" "$vroot/$key/pdflt/$stem.profile.json" "$PLAY_GAMES" "$s"
            done
            echo "$key dflt" >> "$VLQ/play_baseline"
        else
            echo "$key d$bd" >> "$VLQ/play_baseline"
        fi
        for d in $((bd-1)) $bd $((bd+1)); do
            [ "$d" -ge 3 ] || continue
            python3 - "$staged" "$vroot/$key/d$d.value.json" "$d" <<'PY'
import json, sys
# ENABLED, deliberately. A value_play block steers play ONLY when enabled==true (ValuePlay::drives());
# writing target_depth alone leaves it a pure RECOMMENDATION, so the d-1/d/d+1 arms came out
# BYTE-IDENTICAL and the sweep reported "+0.00000, depth does not matter" having tested nothing
# (FiveColour 2026-08-09, its first model -- a deck WITH a live enabled block never showed the bug).
# budget_ms comes along because an enabled block OWNS the budget too: omit it and it resolves to 0,
# confounding the depth comparison with a resource change. 20 = BuiltinDefaultPlay().budget_ms.
v = json.load(open(sys.argv[1])); vp = v.setdefault("value_play", {})
vp["target_depth"] = int(sys.argv[3]); vp["escalation_cap"] = int(sys.argv[3])
vp["enabled"] = True
vp["budget_ms"] = vp.get("budget_ms") or 20
json.dump(v, open(sys.argv[2], "w"), indent=1)
PY
            make_variant_deck "$vroot/$key/pd$d" "$dir" "$stem" "$vroot/$key/d$d.value.json"
            for s in $PLAY_SEEDS; do
                h_job "$key-d${d}_s$s" "$(deck_file "$vroot/$key/pd$d" "$stem")" "$vroot/$key/pd$d/$stem.profile.json" "$PLAY_GAMES" "$s"
            done
        done
      done; } | h_manifest "$VLQ/measure.manifest.json" >/dev/null
    log "PHASE E: $(grep -c '"name"' "$VLQ/measure.manifest.json") jobs (A/B + sweeps, every deck) in ONE queue"
    ./build/Release/mtg --batch "$VLQ/measure.manifest.json" > "$VLQ/measure.log" 2> "$VLQ/measure.err"
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        grep "^$key-" "$VLQ/measure.log" > "$VLQ/m_$key.log" 2>/dev/null
        [ -s "$VLQ/m_$key.log" ] || continue
        sed -i "s/^$key-//" "$VLQ/m_$key.log"
        bl=$(awk -v k="$key" '$1==k{print $2}' "$VLQ/play_baseline" | tail -1)
        echo "=== $key: regenerated value-leaf vs live ===" | tee -a "$VLQ/driver.log"
        grep -E "^(live|staged)_s" "$VLQ/m_$key.log" > "$VLQ/m_${key}_ab.log"
        python3 scripts/vlq_ab_report.py "$VLQ/m_${key}_ab.log" live 2>&1 | tee -a "$VLQ/driver.log"
        echo "=== $key: play-profile target_depth sweep (baseline $bl = shipped) ===" | tee -a "$VLQ/driver.log"
        grep -E "^(d[0-9]+|dflt)_s" "$VLQ/m_$key.log" > "$VLQ/m_${key}_play.log"
        python3 scripts/vlq_ab_report.py "$VLQ/m_${key}_play.log" "$bl" 2>&1 | tee -a "$VLQ/driver.log"
    done
    mark E_measure
}

case "${1:-status}" in
finish|run)
    # `finish` is the escape hatch for the one downside of pooling: phase A is all-or-nothing, so
    # running out of time INSIDE it would otherwise leave rows but no models, tables or measurements
    # -- whereas per-deck staging banks finished decks as it goes. Rows are flushed per row and so are
    # durable at every instant, and every later phase works fine on fewer rows. So `finish` accepts the
    # rows on disk as final and runs B..E on them. Use it when the clock, not the data, is the limit.
    if [ "$1" = finish ]; then
        n=$(grep -vc '^#' "$ALL_ROWS" 2>/dev/null || echo 0)
        [ "$n" -gt 0 ] || { echo "no rows at $ALL_ROWS -- nothing to finish"; exit 1; }
        if pgrep -f "valueleaf_regen_queue.sh run" >/dev/null 2>&1; then
            echo "a 'run' is still going -- stop it first, or its batch will fight this one for the box"
            exit 1
        fi
        log "FINISH: accepting $n rows as final and running phases B..E on them"
        mark A_rows
    fi
    log "=== value-leaf + play-profile regeneration: START (3 pooled phases, 3 tails) ==="
    for ph in phase_freeze phase_rows phase_split phase_train phase_matrix phase_meta phase_measure; do
        check_freeze || exit 1
        $ph || { log "STOPPED at $ph"; exit 1; }
    done
    log "=== COMPLETE -- nothing adopted; review the A/B + sweep reports above ==="
    ;;
status)
    # A real progress report: where the run is, what is left, and how to resume. Reads only files the
    # phases already write, so it is safe to run against a live run.
    ph_label() { case $1 in
        00_freeze) echo "0 freeze+build";; A_rows)   echo "A rows        ";;
        A_split)   echo "A split       ";; B_train)  echo "B train       ";;
        C_matrix)  echo "C matrix      ";; D_meta)   echo "D metadata    ";;
        E_measure) echo "E measure     ";; esac; }
    echo "queue dir  : $VLQ"
    echo "frozen at  : $(cat "$VLQ/freeze.commit" 2>/dev/null || echo '(not started)')"
    now=$(src_fingerprint); frz=$(cat "$VLQ/freeze.src" 2>/dev/null || echo "")
    if [ -n "$frz" ] && [ "$now" != "$frz" ]; then
        # src moved. Whether that MATTERS is a play question, and `run` answers it by re-folding the
        # smoke digests (see check_freeze). status must not run the suite -- it is documented as
        # touching nothing -- so it reports what is pending rather than pre-judging it.
        echo "freeze     : src/ moved -- \`run\` re-checks the PLAY digests and CONTINUES either way:"
        echo "             unchanged (stamped $(cut -c1-12 "$VLQ/freeze.play" 2>/dev/null || echo 'none')) keeps every game;"
        echo "             changed keeps the completed full sets and re-runs the rest."
    elif [ -n "$frz" ]; then
        echo "freeze     : intact ($(cut -c1-12 "$VLQ/freeze.src"))"
    fi
    echo
    echo "phases:"
    for p in 00_freeze A_rows A_split B_train C_matrix D_meta E_measure; do
        if done_p "$p"; then printf "  [x] %s  %s\n" "$(ph_label "$p")" "$(cat "$DONE/$p")"
        else                 printf "  [ ] %s\n" "$(ph_label "$p")"; fi
    done
    echo
    for row in "${DECK_TABLE[@]}"; do
        IFS='|' read -r key dir stem mkey base games <<< "$row"
        r=$(grep -vc '^#' "$ROWDIR/$stem.rows" 2>/dev/null || echo 0)
        if [ "$r" = 0 ]; then
            r=$(awk -v b="$(( base / 100000 ))" '!/^#/ && int($(NF-1)/100000)==b' "$ALL_ROWS" 2>/dev/null | wc -l)
        fi
        if [ -e "$dir/$stem.value.json" ]; then sc="live sidecar=yes (regeneration)"
        else                                    sc="live sidecar=no (first model)"; fi
        printf "  %-14s %s labelled rows (from %s games)   staged=%-4s %s\n" \
            "$key" "$r" "$games" \
            "$( [ -s "logs/eval/$stem.value.STAGED.json" ] && echo yes || echo no )" "$sc"
        rmse=$(grep -oE 'heldout_RMSE=[0-9.]+' "$VLQ/train_$key.log" 2>/dev/null | tail -1)
        [ -n "$rmse" ] && printf "  %-14s %s\n" "" "$rmse"
    done
    echo
    echo "pooled rows: $(grep -vc '^#' "$ALL_ROWS" 2>/dev/null || echo 0)"
    # Matrix progress comes from the CELLS file, not from matrix.log. The log is append-only across
    # resumes, so its last line is whatever the PREVIOUS run said -- it reported "9 cells intractable"
    # for half an hour after a resume that had made condemnation impossible. The cells file is the
    # live state the scheduler itself reads.
    if [ -s "$MATRIX_TXT.cells.json" ]; then
        python3 - "$MATRIX_TXT.cells.json" "$MATRIX_TARGET" <<'PY'
import json, os, sys, collections
cells = json.load(open(sys.argv[1])); target = int(sys.argv[2])
done = sum(1 for c in cells if (c.get("games") or 0) >= target)
cond = sum(1 for c in cells if c.get("intractable"))
have = sum(c.get("games") or 0 for c in cells); want = target * len(cells)
print("matrix     : %d/%d cells at %d games, %d condemned  (%d/%d games, %.0f%%)"
      % (done, len(cells), target, cond, have, want, 100.0 * have / max(want, 1)))
# A cell with 0 games has no rate of its own; borrowing its depth's mean keeps it in the estimate
# instead of costing 0, which is how "123 core-h left" hid a 150 core-h job.
rate = {}
for c in cells:
    if c.get("games"):
        rate.setdefault((c["arm"], c["depth"]), []).append(c["ms"] / c["games"])
short = collections.defaultdict(list)
total = 0.0
for c in sorted(cells, key=lambda c: (c["arm"], c["depth"], c["seed"])):
    g = c.get("games") or 0
    if g >= target:
        continue
    peers = rate.get((c["arm"], c["depth"]), [])
    spg = c["ms"] / g if g else (sum(peers) / len(peers) if peers else 0.0)
    total += (target - g) * spg
    short["%s%d" % (c["arm"], c["depth"])].append((c["seed"], g, spg))
for k, v in short.items():
    left = sum((target - g) * s for _, g, s in v) / 3600.0
    print("             %-3s short on %d seed(s): %s   ~%.1f core-h left"
          % (k, len(v), " ".join("%s:%dg" % (s, g) for s, g, _ in v), left))
if total:
    # Cores the process may actually use, matching the engine's AffinityCpuCount() -- not a hardcoded
    # 20, which is what made a 275-core-hour job look like it might fit an evening. This ETA is the
    # multiplication that was skipped before the 2026-08-10 run: sizing the job first would have shown
    # a ~14 h floor and made 23 h at 5% complete obviously a scheduling failure, not an engine one.
    cores = len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else (os.cpu_count() or 8)
    print("             ~%.0f core-h remaining -> ~%.1f h wall at %d cores (FULL packing; if the"
          " heartbeat shows fewer busy, that is the problem)"
          % (total / 3600.0, total / 3600.0 / cores, cores))
PY
    fi
    [ -s "$MATRIX_TXT" ] && echo "             table -> $MATRIX_TXT"
    live=$(pgrep -c -x mtg 2>/dev/null || echo 0)
    if [ "$live" != 0 ]; then echo "running    : yes ($live mtg) -- a games phase is active"
    else                      echo "running    : no"; fi
    echo
    echo "resume     : bash $0 run ${DECK_DIR:-}"
    echo "             Re-running is SAFE and incremental: finished phases are skipped via marker"
    echo "             files in $DONE, and rows dedupe on (seed,turn), so an interrupted dump loses"
    echo "             only in-flight games. To redo one phase, delete its marker and re-run."
    echo "             If the CLOCK (not the data) ran out inside phase A, use 'finish' instead:"
    echo "             it accepts the rows on disk as final and runs B..E on them."
    tail -5 "$VLQ/driver.log" 2>/dev/null
    exit 0
    ;;
*) echo "usage: $0 {run|status} [deck-dir]"; echo "  $0 run                    # regenerate the whole fleet"; echo "  $0 run decks/FiveColour   # one deck, new or existing"; echo "  the deck is the ONLY input; NEVER_CONDEMN (floor 5) is the one setting"; exit 2 ;;
esac
