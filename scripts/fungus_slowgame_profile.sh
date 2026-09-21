#!/usr/bin/env bash
# Profile the WORST game of the Fungus value-leaf run, to find where the wall clock actually goes.
#
# WHY. The slow-game log says the unit accounting has lost contact with wall clock:
#
#   expected (NODES_PER_VIRTUAL_MS=900)          ~900,000 units/s
#   median of the 496 least-slow slow games         39,856 units/s   (22x low)
#   median of the 50 worst                           3,786 units/s   (238x low)
#   the single worst game (25.93 h, 4.94M units)         53 units/s   (17,000x low)
#
# A search that budgets in units therefore cannot see most of what it spends. The wall-clock
# backstop bounds the symptom; this run is about the cause. The standing hypothesis (flagged
# UNMEASURED in docs/design/per-game-wall-clock-backstop.md) is that PLAN ENUMERATION is uncharged:
# units are booked at rollout_step / la_cand / greedy_fallback / fs_pre, and enumeration is not one
# of them. A profile settles it in one read.
#
# WHY SAMPLE A WINDOW rather than run to completion. The target game takes 25.93 h. Profiling it to
# the end is not on. So the game is launched UNBOUNDED and perf samples a fixed window of it with
# `perf record -p <pid> -- sleep N`. Nothing is truncated to produce a number: the deliverable is a
# distribution over stack samples, which a window measures honestly. The game is then killed -- it is
# this script's own experiment, not user-requested work.
#
# SAFETY: builds only build/Profile (never build/Release), so the value-leaf freeze on HEAD:src and
# the running driver's binary are both untouched.

set -u
cd /workspaces/MagicDeckTester2

OUT=logs/fungus_opt
mkdir -p "$OUT"
LOG=$OUT/profile.log
log() { echo "[$(date -u '+%m-%d %H:%M:%S')] $*" | tee -a "$LOG"; }

WAIT_PIDS=${WAIT_PIDS:-}
SAMPLE_SEC=${SAMPLE_SEC:-150}
WARMUP_SEC=${WARMUP_SEC:-60}

log "=== Fungus slow-game profile START ==="

# ---- 1. Wait for the box ---------------------------------------------------------------------
for p in $WAIT_PIDS; do
    if kill -0 "$p" 2>/dev/null; then
        log "waiting for pid $p to exit (box must be quiet -- a shared box mis-attributes samples)"
        while kill -0 "$p" 2>/dev/null; do sleep 60; done
        log "  pid $p exited"
    fi
done
sleep 20
busy=$(ps -eo pcpu --no-headers | awk '{s+=$1} END {printf "%.0f", s}')
log "box load after wait: ${busy}% of $(nproc)00%"

# ---- 2. Build the profile binary (Release codegen + symbols) ----------------------------------
# build/Profile is a SEPARATE directory; build/Release/mtg is not rewritten, so a resumed driver
# keeps the binary its freeze names.
log "--- building build/Profile (-O3 + symbols) ---"
./build.sh profile >> "$OUT/build.log" 2>&1
brc=$?
BIN=build/Profile/mtg
log "build rc=$brc  binary: $(ls -la $BIN 2>/dev/null || echo MISSING)"
[ -x "$BIN" ] || { log "ABORT: $BIN missing -- see $OUT/build.log"; exit 1; }

# ---- 3. The target ----------------------------------------------------------------------------
# fungus_staged_H2_s8008_off150 gi=157: 25.93 h for 4.94M units -- 53 units/s, the worst
# unit-to-wall ratio in the run, and it is DEPTH 2, which should be among the cheapest cells.
# The same underlying game is catastrophic at H3 (4.34 h) and H4 (10.16 h) too, so it is a property
# of the position, not of one depth's search.
# Flags mirror the manifest's H2 job exactly (ignore_play_profile, max_turns 8, budget 0,
# ladder_value_leaf via the now-live sidecar).
ARGS=(decks/Fungus/Fungus.cod
      --profile decks/Fungus/Fungus.profile.json
      --depth 2 --budget-ms 0 --max-turns 8 --ignore-play-profile
      --seed 8165 --game-index 157 --games 1 --threads 1)
log "target: --seed 8165 --game-index 157 (H2 cell, 25.93 h, 4.94M units, 53 units/s)"

# ---- 4. Launch unbounded, sample a window ------------------------------------------------------
log "--- launching the game (unbounded) ---"
MTG_BATCH_HEARTBEAT=0 "$BIN" "${ARGS[@]}" > "$OUT/game.log" 2>&1 &
GPID=$!
log "game pid $GPID; warming up ${WARMUP_SEC}s so startup/deck-load is not in the sample"
sleep "$WARMUP_SEC"
if ! kill -0 "$GPID" 2>/dev/null; then
    log "NOTE: game already exited during warmup -- it is not reproducing as slow. See $OUT/game.log"
    tail -25 "$OUT/game.log" | tee -a "$LOG"
    exit 3
fi

PERFDATA=/tmp/fungus_slow.perf.data     # /tmp, never the repo tree (perf needs a writable fs)
log "--- perf record, ${SAMPLE_SEC}s window, call graph (dwarf) ---"
perf record -F 199 --call-graph dwarf -p "$GPID" -o "$PERFDATA" -- sleep "$SAMPLE_SEC" \
    >> "$OUT/perf_record.log" 2>&1
prc=$?
log "perf record rc=$prc"

log "--- killing the target (own experiment; it would otherwise run ~26 h) ---"
kill "$GPID" 2>/dev/null; sleep 2; kill -9 "$GPID" 2>/dev/null

# ---- 5. Report ---------------------------------------------------------------------------------
if [ -s "$PERFDATA" ]; then
    perf report -i "$PERFDATA" --stdio --no-children -g none --percent-limit 0.5 \
        > "$OUT/perf_flat.txt" 2>/dev/null
    perf report -i "$PERFDATA" --stdio --children -g graph,0.5,caller --percent-limit 1.0 \
        > "$OUT/perf_tree.txt" 2>/dev/null
    log "--- TOP SELF TIME (the answer to 'what is uncharged') ---"
    head -40 "$OUT/perf_flat.txt" | tee -a "$LOG"
    log "full reports: $OUT/perf_flat.txt (self), $OUT/perf_tree.txt (callers)"
else
    log "NO PERF DATA -- perf may be restricted here. Falling back to the engine's own counters."
    log "  try: MTG_UNIT_SITES=1 plus the [rollout-stats] units.* lines in $OUT/game.log"
fi

log "=== profile COMPLETE ==="
