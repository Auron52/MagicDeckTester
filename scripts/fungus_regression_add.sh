#!/usr/bin/env bash
# Add Fungus to the regression suite at HINATA'S SIZING, run it, accept its baseline. LOCAL ONLY.
#
# SIZING (user call, 2026-09-20): mirror Hinata exactly rather than probing for a bespoke size.
# Hinata is the suite's existing deep-search deck, so its counts are a known-good conservative tier.
# The alternative -- measuring per-game wall first -- costs idle-box time that is better spent on
# the mulligan stage, and the downside it protects against is cheap to undo here: this change is
# NEVER PUSHED, so if Fungus turns out to be slow in the suite we shrink the counts before anyone
# else sees them. Measure-then-size stays the right call for a change that WILL be pushed.
#
# WHAT IS DELIBERATELY NOT DONE: no push. The commit stays local so the sizing can be revised (or
# dropped) without rewriting shared history. Pushing is a human decision after the timings below
# have been read.
#
# TIMING IS THE DELIVERABLE HERE. Because we did not probe, the smoke run's own per-case timings are
# the first real evidence of what this deck costs the suite. They are extracted into the log
# explicitly so the "fix it after if it is slow" step has numbers to work from.
#
# WHY BEFORE ADOPTION. Ground truth accepted here is on the HEURISTIC engine -- the expensive path --
# so a case that fits now still fits once the value leaf is adopted, and a later re-run's GT delta
# ISOLATES the value leaf instead of confounding it with the deck's first appearance in the suite.
#
# Safe to re-run: idempotent on the cases file; skips straight to run/accept if already present.

#
# MODES:
#   (no arg)     add + baseline SMOKE and REGRESSION. Runs pre-adoption.
#   --overnight  add + baseline OVERNIGHT. Runs LAST, after the mulligan stage, because Hinata's
#                overnight block is ~4x the regression d3 volume and ~6x at d5 -- hours of box time
#                that would otherwise delay the mulligan gen. The cost of deferring it is that this
#                tier's baseline is taken on the ADOPTED engine, so it yields no value-leaf delta;
#                smoke+regression already provide that, and a long-run tier arguably wants its
#                baseline on the shipped configuration anyway.

set -u
MODE=${1:-core}
KEY=fungus
DECKDIR=decks/Fungus
STEM=Fungus
CASES=test/regression_cases.sh
BIN=build/Release/mtg
OUT=logs/${STEM}_regadd
mkdir -p "$OUT"
LOG=$OUT/regadd.log
log() { echo "[$(date -u '+%m-%d %H:%M:%S')] $*" | tee -a "$LOG"; }

log "=== fungus regression-add START (mode=$MODE, Hinata sizing, local-only) ==="
[ -x "$BIN" ] || { log "ABORT: $BIN missing (do NOT rebuild under a freeze)"; exit 1; }
[ -e "$DECKDIR/$STEM.cod" ] && [ -e "$DECKDIR/$STEM.profile.json" ] \
    || { log "ABORT: deck or profile missing under $DECKDIR"; exit 1; }

log "--- inserting Hinata-sized entries into $CASES (idempotent, per array) ---"
python3 - "$CASES" "$MODE" <<'PY'
import re, sys
path, mode = sys.argv[1], sys.argv[2]
src = open(path).read()

def add_map(src, name, value):
    m = re.search(r'declare -A %s=\(\n' % name, src)
    if not m: sys.exit("could not find %s" % name)
    end = src.index(')', m.end())
    if '[fungus]=' in src[m.end():end]: return src
    return src[:m.end()] + '  [fungus]=%s\n' % value + src[m.end():]

src = add_map(src, 'DECK_FILE', 'decks/Fungus/Fungus.cod')
src = add_map(src, 'DECK_PROF', 'decks/Fungus/Fungus.profile.json')

# Mirrors of the hinata block in each array, same depths/seeds/counts/budgets.
SMOKE = """  # fungus: Hinata's sizing, adopted wholesale rather than probed (user call 2026-09-20).
  # Revise from the measured per-case timings if it proves slow -- this entry is deliberately
  # unpushed so that is a cheap edit, not a history rewrite.
  "fungus  0 1001 1000 0"
  "fungus  3 1001  150 10"
  "fungus  5 1001   75 20"
"""
REGRESSION = """  # fungus: Hinata's sizing (d0 full + d3/d5 at both seeds).
  "fungus  0 2002 1000 0"
  "fungus  3 2002  200 10"
  "fungus  3 3003  200 10"
  "fungus  5 2002  100 20"
  "fungus  5 3003  100 20"
"""

# Mirror of hinata's 12-case overnight block: d0 x4 @2000, d3 x4 @400 b10, d5 x4 @300 b20.
OVERNIGHT = """  # fungus: Hinata's overnight sizing. Baselined AFTER the mulligan stage (see script header):
  # this block is ~4x the regression d3 volume and ~6x at d5, so running it first would delay the
  # mulligan gen for a tier that is run rarely.
  "fungus  0  4004 2000 0"
  "fungus  0  6006 2000 0"
  "fungus  0  8008 2000 0"
  "fungus  0 10010 2000 0"
  "fungus  3  4004  400 10"
  "fungus  3  5005  400 10"
  "fungus  3  6006  400 10"
  "fungus  3  7007  400 10"
  "fungus  5  4004  300 20"
  "fungus  5  5005  300 20"
  "fungus  5  6006  300 20"
  "fungus  5  7007  300 20"
"""

def add_cases(src, array, block):
    """Insert once, per array -- an array that already lists fungus is left untouched."""
    m = re.search(r'%s=\(\n' % array, src)
    if not m: sys.exit("could not find %s" % array)
    end = src.index('\n)', m.end())
    if '"fungus ' in src[m.end():end]:
        print("  %s: already present, untouched" % array)
        return src
    print("  %s: inserted" % array)
    return src[:m.end()] + block + src[m.end():]

if mode == 'overnight':
    src = add_cases(src, 'OVERNIGHT_CASES', OVERNIGHT)
else:
    src = add_cases(src, 'SMOKE_CASES', SMOKE)
    src = add_cases(src, 'REGRESSION_CASES', REGRESSION)
open(path, 'w').write(src)
PY
rc=$?
[ $rc -eq 0 ] || { log "ABORT: cases edit failed rc=$rc"; exit 1; }
if ! bash -n "$CASES"; then
    log "ABORT: $CASES no longer parses -- reverting the edit"
    git checkout -- "$CASES"; exit 1
fi
log "fungus entries now in $CASES:"; grep -n "\"fungus " "$CASES" | tee -a "$LOG"

# ---- Run and baseline BOTH modes we added cases to --------------------------------------------
# Adding cases to a mode without accepting that mode leaves it un-baselined, so the next person to
# run it sees Fungus as missing ground truth. We added smoke AND regression cases, so both get run
# and accepted here. (Accept is per-mode: --smoke --accept promotes only the smoke keys.)
baseline_mode() {   # baseline_mode <flag> <label>
    local flag=$1 label=$2 rc arc
    log "--- running $label (fungus only) ---"
    bash test/regression.sh "$flag" --deck=fungus >> "$OUT/$label.log" 2>&1
    rc=$?
    log "$label rc=$rc (see $OUT/$label.log)"
    log "--- $label per-case timings (THE deliverable: the input to any resizing) ---"
    grep -iE "^[[:space:]]*fungus|elapsed|SLOWER|took|[0-9]+\.[0-9]+ ?s\b" "$OUT/$label.log" \
        | tail -40 | tee -a "$LOG"
    tail -20 "$OUT/$label.log" | tee -a "$LOG"
    [ $rc -ne 0 ] && log "NOTE: non-zero $label rc is EXPECTED for a new deck -- it has no GT yet."
    log "--- accepting $label baseline (narrowed: creates its .wins, promotes no other deck) ---"
    bash test/regression.sh "$flag" --deck=fungus --accept >> "$OUT/accept_$label.log" 2>&1
    arc=$?
    log "$label accept rc=$arc (see $OUT/accept_$label.log)"
    return $arc
}
if [ "$MODE" = overnight ]; then
    baseline_mode --overnight overnight \
        || { log "STOP: overnight accept failed -- NOT committing"; exit 6; }
else
    baseline_mode --smoke smoke || { log "STOP: smoke accept failed -- NOT committing"; exit 6; }
    baseline_mode --regression regression \
        || { log "STOP: regression accept failed -- NOT committing"; exit 6; }
fi

python3 test/check_gt_logs.py >> "$OUT/check_gt.log" 2>&1
log "check_gt_logs rc=$? (see $OUT/check_gt.log)"

# ---- Commit LOCALLY. No push, by explicit instruction. ----------------------------------------
git add "$CASES" test/regression_gt.txt test/gt_logs 2>/dev/null
if git diff --cached --quiet; then
    log "nothing staged -- no commit"
else
    git commit -q -m "test(fungus): add Fungus to the suite at Hinata's sizing ($MODE)

Sized by mirroring Hinata, the suite's existing deep-search deck, rather than by
a bespoke wall-clock probe -- the idle box is better spent on the mulligan stage,
and this entry is deliberately NOT PUSHED, so shrinking the counts later is a
cheap local edit rather than a history rewrite.

Accept narrowed with --deck=fungus, so it creates this deck's baseline and
promotes no other deck's numbers.

core mode (smoke+regression) is baselined on the HEURISTIC engine BEFORE
value-leaf adoption, so a later re-run's GT delta isolates the value leaf.
overnight mode runs LAST, after the mulligan stage: Hinata's overnight block is
~4x the regression d3 volume and ~6x at d5, so baselining it first would delay
the mulligan gen for a tier that is run rarely. Its baseline is therefore taken
on the adopted engine and yields no value-leaf delta -- smoke+regression already
provide that.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
    log "committed LOCALLY (not pushed): $(git log --oneline -1)"
fi
log "=== fungus regression-add COMPLETE -- LOCAL commit only; review timings before pushing ==="
