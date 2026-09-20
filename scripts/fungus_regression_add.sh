#!/usr/bin/env bash
# Add Fungus to the regression suite, SIZED BY MEASUREMENT, then run and accept its baseline.
#
# WHY SIZED BY MEASUREMENT. The suite has shared per-mode wall budgets (smoke < 15 min, regression
# < 45 min) across ~20 decks, so a new deck may only take a small slice. Fungus cannot be sized by
# analogy: its per-game cost is pathological and -- this is the trap -- a `--budget-ms 20` case is
# NOT bounded to 20 ms of wall. The budget is denominated in work UNITS (900 units per virtual ms),
# and Fungus burns units at a fraction of the calibrated rate, so a nominally-cheap case can run for
# minutes. The only honest sizing input is measured wall at the exact case shape.
#
# WHY IT RUNS BEFORE ADOPTION. Ground truth accepted here is on the HEURISTIC engine. That is the
# expensive path, so a case that fits here still fits after the value leaf is adopted; and it means
# a later re-run's GT delta ISOLATES the value leaf's effect instead of confounding it with the
# deck's first appearance in the suite.
#
# Safe to re-run: it is idempotent on the cases file and skips straight to run/accept if already
# present. Accepting here CREATES Fungus's baseline (no prior GT exists) and is narrowed with
# --deck=fungus so it cannot promote any other deck's numbers.

set -u
KEY=fungus
DECKDIR=decks/Fungus
STEM=Fungus
DECK=$DECKDIR/$STEM.cod
PROF=$DECKDIR/$STEM.profile.json
CASES=test/regression_cases.sh
BIN=build/Release/mtg
OUT=logs/${STEM}_regadd
mkdir -p "$OUT"
LOG=$OUT/regadd.log
log() { echo "[$(date -u '+%m-%d %H:%M:%S')] $*" | tee -a "$LOG"; }

# Per-case wall targets (seconds). Deliberately small: this deck is a guest in a shared budget.
SMOKE_TARGET=${SMOKE_TARGET:-90}
REG_TARGET=${REG_TARGET:-150}
MIN_GAMES=${MIN_GAMES:-25}        # below this a case is statistically pointless -- drop it

log "=== fungus regression-add START ==="
[ -x "$BIN" ] || { log "ABORT: $BIN missing (do NOT rebuild under a freeze)"; exit 1; }
[ -e "$DECK" ] && [ -e "$PROF" ] || { log "ABORT: deck or profile missing"; exit 1; }

if grep -q "^\s*\"$KEY " "$CASES"; then
    log "$KEY already present in $CASES -- skipping probe/edit, going straight to run+accept"
else
    # ---- 1. Probe: measured wall per game at each case shape ---------------------------------
    # Probe seed 9001 is disjoint from every suite range (smoke 1001, regression 2002/3003,
    # overnight 4004-10010) so the sizing run can never be mistaken for, or reused as, ground truth.
    log "--- probing per-game wall (threads=hw, probe seed 9001) ---"
    declare -A PERGAME
    probe() {   # probe <depth> <budget_ms> <games>
        local d=$1 b=$2 n=$3 t0 t1 el
        t0=$(date +%s.%N)
        "$BIN" "$DECK" --profile "$PROF" --depth "$d" --budget-ms "$b" \
               --games "$n" --seed 9001 --threads 0 > "$OUT/probe_d${d}_b${b}.log" 2>&1
        local rc=$?
        t1=$(date +%s.%N)
        el=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.3f", b-a}')
        if [ $rc -ne 0 ]; then log "  d$d/b$b PROBE FAILED rc=$rc -- see $OUT/probe_d${d}_b${b}.log"; return 1; fi
        PERGAME["${d}_${b}"]=$(awk -v e="$el" -v n="$n" 'BEGIN{printf "%.4f", e/n}')
        log "  d$d/b$b : $n games in ${el}s -> ${PERGAME[${d}_${b}]}s/game"
        return 0
    }
    probe 0 0  24 || { log "ABORT: d0 probe failed"; exit 1; }
    probe 3 10 12 || { log "ABORT: d3 probe failed"; exit 1; }
    probe 5 20  8 || log "  NOTE: d5 probe failed -- d5 case will be dropped"

    # ---- 2. Size each case, capped at the suite's conservative tier ---------------------------
    # Caps mirror `th`, the existing conservative slow deck. Measurement may only make Fungus
    # CHEAPER than that tier, never more expensive.
    size() {    # size <pergame_key> <target_s> <cap>
        local pg=${PERGAME[$1]:-} tgt=$2 cap=$3
        [ -n "$pg" ] || { echo 0; return; }
        awk -v pg="$pg" -v t="$tgt" -v c="$cap" 'BEGIN{
            n = (pg > 0) ? int(t/pg) : 0; if (n > c) n = c; print n }'
    }
    S0=$(size 0_0   "$SMOKE_TARGET" 1000); S3=$(size 3_10 "$SMOKE_TARGET" 150); S5=$(size 5_20 "$SMOKE_TARGET" 75)
    R0=$(size 0_0   "$REG_TARGET"   1000); R3=$(size 3_10 "$REG_TARGET"   500); R5=$(size 5_20 "$REG_TARGET"  300)
    log "--- sized (min $MIN_GAMES to keep a case) ---"
    log "  smoke:      d0=$S0 d3=$S3 d5=$S5"
    log "  regression: d0=$R0 d3=$R3 d5=$R5"
    if [ "$S0" -lt "$MIN_GAMES" ]; then
        log "ABORT: even d0 cannot reach $MIN_GAMES games inside ${SMOKE_TARGET}s."
        log "       That is a PERFORMANCE BLOCKER -- report it rather than shrinking the suite's"
        log "       budget to fit. The deck is not ready for the shared suite."
        exit 5
    fi

    # ---- 3. Edit the cases file --------------------------------------------------------------
    log "--- inserting entries into $CASES ---"
    python3 - "$CASES" "$S0" "$S3" "$S5" "$R0" "$R3" "$R5" "$MIN_GAMES" <<'PY'
import re, sys
path, s0, s3, s5, r0, r3, r5, floor = sys.argv[1], *map(int, sys.argv[2:9])
src = open(path).read()

def add_map(src, name, value):
    m = re.search(r'(declare -A %s=\(\n)' % name, src)
    if not m: sys.exit("could not find %s" % name)
    if '[fungus]=' in src[m.end():src.index(')', m.end())]: return src
    return src[:m.end()] + '  [fungus]=%s\n' % value + src[m.end():]

src = add_map(src, 'DECK_FILE', 'decks/Fungus/Fungus.cod')
src = add_map(src, 'DECK_PROF', 'decks/Fungus/Fungus.profile.json')

def add_cases(src, array, rows):
    rows = [r for r in rows if r[2] >= floor]
    if not rows: return src
    m = re.search(r'(%s=\(\n)' % array, src)
    if not m: sys.exit("could not find %s" % array)
    block = ''.join('  "fungus  %d %5d %4d %2d"\n' % r for r in rows)
    return src[:m.end()] + block + src[m.end():]

src = add_cases(src, 'SMOKE_CASES',      [(0, 1001, s0, 0), (3, 1001, s3, 10), (5, 1001, s5, 20)])
src = add_cases(src, 'REGRESSION_CASES', [(0, 2002, r0, 0), (3, 2002, r3, 10), (5, 2002, r5, 20)])
open(path, 'w').write(src)
print("inserted")
PY
    [ $? -eq 0 ] || { log "ABORT: cases edit failed"; exit 1; }
    bash -n "$CASES" || { log "ABORT: $CASES no longer parses -- reverting"; git checkout -- "$CASES"; exit 1; }
    log "entries now in $CASES:"; grep -n "fungus" "$CASES" | tee -a "$LOG"
fi

# ---- 4. Run the deck's own smoke cases, then promote its baseline -----------------------------
log "--- running smoke (fungus only) ---"
bash test/regression.sh --smoke --deck=fungus >> "$OUT/smoke.log" 2>&1
rc=$?
log "smoke rc=$rc (see $OUT/smoke.log)"
tail -30 "$OUT/smoke.log" | tee -a "$LOG"
if [ $rc -ne 0 ] && ! grep -q "no ground-truth" "$OUT/smoke.log"; then
    log "NOTE: smoke returned non-zero. For a NEW deck that is expected -- it has no GT yet."
fi
log "--- accepting fungus baseline (narrowed; creates .wins, promotes no other deck) ---"
bash test/regression.sh --smoke --deck=fungus --accept >> "$OUT/accept.log" 2>&1
arc=$?
log "accept rc=$arc (see $OUT/accept.log)"
if [ $arc -ne 0 ]; then log "STOP: accept failed -- NOT committing"; exit 6; fi

python3 test/check_gt_logs.py >> "$OUT/check_gt.log" 2>&1
log "check_gt_logs rc=$? (see $OUT/check_gt.log)"

# ---- 5. Commit (local only; pushing needs a rebase onto a moving origin -- left to a human) ----
git add "$CASES" test/regression_gt.txt test/gt_logs 2>/dev/null
if git diff --cached --quiet; then
    log "nothing staged -- no commit"
else
    git commit -q -m "test(fungus): add Fungus to smoke+regression, sized by measured wall

Sized from a measured per-game probe rather than by analogy: a --budget-ms case
is denominated in work UNITS, not wall, so this deck's nominal budget does not
bound its wall. Counts are capped at the conservative (th) tier and reduced
where measurement demanded it.

Baseline accepted on the HEURISTIC engine, BEFORE value-leaf adoption, so a
later re-run's GT delta isolates the value leaf.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
    log "committed: $(git log --oneline -1)"
fi
log "=== fungus regression-add COMPLETE ==="
