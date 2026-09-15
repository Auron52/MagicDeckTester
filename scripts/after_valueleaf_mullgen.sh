#!/usr/bin/env bash
# Unattended chain: WAIT for a value-leaf run to finish, decide adoption BY MEASUREMENT,
# then run phase F + the exhaustive mulligan generation. No AI in the loop.
#
#   bash scripts/after_valueleaf_mullgen.sh decks/<Deck> [complete|fast]
#   nohup bash scripts/after_valueleaf_mullgen.sh decks/Snow > logs/vlq_snow/chain.log 2>&1 &
#
# WHY THIS EXISTS. The pipeline is strictly serial (profile -> value leaf -> mulligan) and the
# mulligan generator reads mull_gen_depth / mull_gen_budget_ms / expected_buckets from the deck's
# LIVE <stem>.value.json. `valueleaf.sh` deliberately never writes a live sidecar, so its own
# phase F (mullgen_finalize.py) EXITS 1 with "the value leaf must exist first" and the driver stops
# there. That stop is expected, not a failure -- everything through phase E is done. This script is
# the piece that follows: it makes the install decision, then lets the pipeline finish itself.
#
# THE ADOPTION GATE IS MECHANICAL, AND IT USES THE RIGHT MEASUREMENT. Phase E's A/B compares the
# STAGED arm (sidecar present) against the LIVE arm (no sidecar at all). Sidecar PRESENCE is what
# activates the depth-aware hybrid, so that A/B *is* the install question -- there is no separate
# judgement to make. Creature Giving is the worked example of why presence is not free: installed
# with value_play.enabled=false, d5/s2002 still moved 4.792 -> 4.804.
#
# FAIL-SAFE, NOT FAIL-FORWARD. Anything ambiguous stops the chain instead of guessing. Generating a
# mulligan table at the wrong settings is the expensive failure mode (KittyEquipment spent weeks
# generating at the built-in d5/b20 default because an exit code was swallowed), and a stopped chain
# costs only idle cores.
set -uo pipefail
cd "$(dirname "$0")/.."

DECKDIR=${1:?usage: after_valueleaf_mullgen.sh decks/<Deck> [complete|fast]}
RECIPE=${2:-complete}
DECKDIR=${DECKDIR%/}
STEM=$(basename "$DECKDIR")
KEY=$(printf '%s' "$STEM" | tr '[:upper:]' '[:lower:]')
VLQ=logs/vlq_$KEY
STAGED=logs/eval/$STEM.value.STAGED.json
LIVE=$DECKDIR/$STEM.value.json

# Neutral-or-better on THE metric (avg turn-to-win; negative delta = better). 0.0020 is the paired
# tolerance the value-leaf pipeline already uses for equivalence at this sample size.
TOL=${ADOPT_TOL:-0.0020}

log() { printf '[%s] %s\n' "$(date '+%m-%d %H:%M:%S')" "$*"; }
die() { log "STOP: $*"; log "Nothing was generated. Fix the cause and re-run this script."; exit 1; }

log "=== after-value-leaf chain for $DECKDIR (recipe=$RECIPE, tol=$TOL) ==="

# ---------------------------------------------------------------- 1. wait for phase E + a free box
# Keyed on the DELIVERABLE (the E_measure marker) plus an idle box -- deliberately NOT on
# `pgrep -f "valueleaf.sh run"`. A -f match tests every process's full command line, so any
# long-lived shell that merely CONTAINS that string (a status command, an editor, this chain's own
# documentation) matches and the wait would never end. Verified live: `pgrep -f 'valueleaf.sh run'`
# matched an unrelated monitoring shell alongside the real driver.
#
# The marker is the honest condition anyway: the driver EXITS 1 at phase F for a first-model deck
# (no live sidecar yet), so "the driver is gone" and "phase E finished" are different facts, and it
# is the second one this chain needs. mtg-analyze counts as busy too -- phase F's K discovery uses it.
# EITHER phase E marker counts. `valueleaf.sh run` marks E_measure; `valueleaf.sh ab` (the
# matrix-free adoption A/B) marks E_ab_nomatrix instead. Both produce the staged-vs-live A/B this
# chain adopts on -- phase_measure is matrix-independent in code, and phase F does not need the
# matrix either (derive_mullgen_setting.py treats value_trust_depth as optional: "a CANDIDATE, not
# a short-circuit"). Accepting only E_measure would leave this chain waiting forever on a deck that
# took the `ab` route, which is exactly the route an unaffordable matrix forces.
log "waiting for a phase E marker (E_measure or E_ab_nomatrix) and the box to go idle..."
while true; do
    { [ -f "$VLQ/done/E_measure" ] || [ -f "$VLQ/done/E_ab_nomatrix" ]; } \
        && ! pgrep -x 'mtg|mtg\.run|mtg-analyze' >/dev/null 2>&1 && break
    sleep 60
done
log "phase E marker present ($(ls "$VLQ/done" | grep -E '^E_' | tr '\n' ' ')) and box idle."

# ---------------------------------------------------------------- 2. phases A..E must be complete
for m in A_rows A_split B_train C_matrix D_meta E_measure; do
    [ -f "$VLQ/done/$m" ] || die "phase $m never completed ($VLQ/done/$m missing). The value-leaf run
  did not get far enough; resume it with 'bash scripts/valueleaf.sh run $DECKDIR' first."
done
[ -s "$STAGED" ] || die "no staged model at $STAGED."
log "phases A..E complete; staged model present."

# ---------------------------------------------------------------- 3. adoption, BY MEASUREMENT
AB=$VLQ/m_${KEY}_ab.log
[ -s "$AB" ] || die "no phase E A/B log at $AB -- cannot decide adoption mechanically."

REPORT=$(python3 scripts/vlq_ab_report.py "$AB" live 2>/dev/null)
printf '%s\n' "$REPORT" | sed 's/^/    /'

# The report's OWN integrity flags, checked BEFORE the delta. A tiny delta computed over a broken
# pairing is the most dangerous output this chain can receive: it looks like a clean pass.
case "$REPORT" in
    *"ZERO VARIANCE"*) die "the A/B reports ZERO VARIANCE (suspect replayed games). That is the
  seed-overlap signature -- bases spaced closer than the game count replay the SAME games, so both
  arms score identically and any delta is meaningless. Refusing to adopt on it." ;;
esac
case "$REPORT" in
    *OK*) : ;;
    *) die "the A/B header did not report OK (arms/seeds/ids did not line up). Read $AB by hand." ;;
esac

DELTA=$(printf '%s\n' "$REPORT" \
        | awk '$1 ~ /staged/ && $3 ~ /^[-+]?[0-9]*\.?[0-9]+$/ { print $3; exit }')
case "$DELTA" in
    ''|*[!0-9.+-]*) die "could not parse a staged delta from $AB (got: '${DELTA:-<none>}').
  Read the A/B by hand, then install the sidecar yourself and re-run this script." ;;
esac

log "phase E A/B: staged-vs-live delta = $DELTA turns (negative = better), tolerance = $TOL"
if ! python3 -c "import sys; sys.exit(0 if float('$DELTA') <= float('$TOL') else 1)"; then
    cp "$STAGED" "$DECKDIR/$STEM.value.DISABLED.json"
    log "REJECTED: delta $DELTA exceeds $TOL, so installing the sidecar would cost play quality."
    log "  Shipped inert as $DECKDIR/$STEM.value.DISABLED.json (presence-gated: the .DISABLED name"
    log "  is what keeps it out of play)."
    die "no live sidecar => the mulligan generator has no mull_gen settings to read. Mulligan
  generation for $STEM needs either a better model or an explicit decision to ship this one."
fi

if [ -e "$LIVE" ]; then
    log "a live sidecar already exists at $LIVE -- leaving it alone (not overwriting)."
else
    cp "$STAGED" "$LIVE"
    log "ADOPTED: installed $STAGED -> $LIVE (delta $DELTA <= $TOL)."
    log "  Reversible: rm '$LIVE' restores the no-sidecar deck exactly."
fi

# ---------------------------------------------------------------- 4. phase F, via the pipeline
# Re-running the driver is better than calling mullgen_finalize directly: A..E are marked done and
# skipped, so this re-runs ONLY phase F, using the pipeline's own freeze/deck checks.
log "running phase F (mulligan-generation contract) via valueleaf.sh..."
bash scripts/valueleaf.sh run "$DECKDIR" >> "$VLQ/phaseF.log" 2>&1
[ -f "$VLQ/done/F_mullgen" ] || die "phase F did not complete -- see $VLQ/phaseF.log.
  Phase F FAILING is the guard working: without it the generation would silently run at the
  built-in d5/b20 default and cost days for a table nobody can ship."

# ---------------------------------------------------------------- 4b. FEASIBILITY, before hours
# The distinct-hand space is ~C(K+6,7) and it explodes: the skill's own guide is K=10 -> 7.8k hands,
# K=15 -> 114k, K=24 -> ~1M, K=60 (all 1-ofs) -> infeasible. Nothing downstream refuses an
# infeasible K -- ExhaustiveKeep only refuses a K that DISAGREES with the recorded one -- so an
# unattended chain would happily start a run that cannot finish this month. Checked here instead.
VERDICT=$(python3 - "$LIVE" "$RECIPE" "${MAX_HOURS:-72}" <<'PY'
import json, sys, math, os
vp = json.load(open(sys.argv[1])).get("value_play", {})
k  = vp.get("expected_buckets")
print("  mull_gen_depth=%s  mull_gen_budget_ms=%s  expected_buckets=%s"
      % (vp.get("mull_gen_depth"), vp.get("mull_gen_budget_ms"), k), file=sys.stderr)
if not k:
    print("NOK"); sys.exit(0)
hands = math.comb(int(k) + 6, 7)
R     = 30 if sys.argv[2] == "fast" else 40          # fast caps R30, complete caps R40
cores = len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else (os.cpu_count() or 8)
# .claude/skills/mulligan-profile.md: runtime ~= hands x 2(pd) x R / (~110 rollouts/s/core x cores)
hours = hands * 2 * R / (110.0 * cores) / 3600.0
print("  K=%d => ~%s distinct size-7 hands; recipe=%s (R=%d) on %d cores => ~%.1f h projected"
      % (k, f"{hands:,}", sys.argv[2], R, cores, hours), file=sys.stderr)
print("  (reference: FiveColour shipped at ~1,977,898 hands; Melira Pod at K=23)", file=sys.stderr)
print("OK" if hours <= float(sys.argv[3]) else "TOOLONG %.1f" % hours)
PY
)
case "$VERDICT" in
  TOOLONG*) die "projected generation time (${VERDICT#TOOLONG }h) exceeds MAX_HOURS=${MAX_HOURS:-72}.
  Options, in the order the skill prefers them: use the 'fast' recipe (R30, ~22-38% off), pool two
  fast chunks across machines to reach R=60, or lower R. Re-run with MAX_HOURS=<n> to override.
  Refusing to start a run that cannot finish in a sane window." ;;
  NOK)      die "expected_buckets was not recorded in $LIVE, so the hand space cannot be sized.
  Phase F reported success without recording K -- read $VLQ/phaseF.log before generating." ;;
esac

# ---------------------------------------------------------------- 5. generate + validate + gate
# mullgen.sh is one indivisible operation: it generates, then A/Bs, then QUARANTINES the profile to
# .DISABLED.json if it is at all worse on average. That self-gating is what makes it safe unattended.
log "starting exhaustive mulligan generation ($RECIPE) -- this owns the box for hours."
bash scripts/mullgen.sh run "$DECKDIR" "$RECIPE" >> "$VLQ/mullgen.log" 2>&1
rc=$?
log "mullgen.sh exited $rc -- see $VLQ/mullgen.log"
bash scripts/mullgen.sh status "$DECKDIR" 2>&1 | tail -20
log "=== chain complete ==="
exit $rc
