#!/usr/bin/env bash
# The card-elimination tournament on the CHEAP apparatus, read one-sidedly.
#
#   nohup bash scripts/tourney_overnight.sh [games] [bracket-games] > logs/tourney/run.log 2>&1 &
#
# Why this shape (user, 2026-08-19): "just test with the original profile and flag any cases that
# are very close as potentially needing a generation test... Any that do better than the original
# with the same profile are expected to win out" and "we might be able to skip cases where the
# incumbent wins by a lot as well as long as we can get a measure of how high the bias should be."
#
# The apparatus is the SHIPPED Mirrorwing keep table (K=17, R=40) with the four new names aliased
# into the buckets they replace. It is fitted to the ORIGINAL list, so it tilts toward the
# incumbent card -- and that tilt is what makes a cheap test usable, because it is one-sided:
#
#   challenger ahead, |t|>=2   -> ADOPT.      It won against the tilt. Conservative, conclusive.
#   challenger ahead, weak     -> CONFIRM.    Still fighting the tilt, so the DIRECTION is already
#                                             evidence; only reproducibility is missing, and
#                                             held-out seeds buy that far cheaper than generation.
#   incumbent wins by > band   -> INCUMBENT.  The tilt cannot account for it. Nothing more to buy.
#   incumbent wins by <= band  -> GENERATE.   The ONLY apparatus-limited case: the tilt alone would
#                                             produce exactly this, and more seeds cannot separate
#                                             them because the tilt is systematic, not noise.
#
# `band` is not a guess: the bracket jobs run two decklists under the aliased table AND under no
# table at all, in this same pooled queue, giving (a) what the table is worth on this deck and
# (b) the fit tilt as a paired difference-in-differences, band = bias + 2se.
#
# Aliasing rather than "just use the profile unchanged" is deliberate: an unbucketed card makes
# ExhaustiveKeepPolicy answer present=false and drop that HAND to the generic heuristic, so the
# challenger arm would play 32-60% of its hands under a different mulligan policy than the
# incumbent (measured: 31.5% for 3 Libation, 39.9% for 4 Oracle). Aliasing keeps the tilt but
# removes the uncontrolled asymmetry.
#
# ONE pooled batch, arms and bracket together: a second concurrent batch is far worse than
# sequential on this box (measured 2026-08-19: lending 12 of 32 cores dropped a keepgen run from
# ~210 rollouts/s to ~16/s). No step is wrapped in a timeout -- a truncated run reads as a result.
set -uo pipefail
cd "$(dirname "$0")/.."

GAMES="${1:-10000}"
BRACKET="${2:-4000}"
SEED=1200000
OUT=logs/tourney

say() { printf '\n=== %s  %s ===\n' "$(date '+%F %T')" "$*"; }

cases_for() {   # reads $ERRF and $CSEED
  for life in 20 30; do
    for spec in "tf tf3lib0 tf0lib3" "ao a4o0 a0o4" "slot scale draught"; do
      set -- $spec
      tag="$1_$2_vs_$3_L$life"
      say "case logs: $1 $2 vs $3 at $life life"
      if python3 scripts/tourney_cases.py --err "$ERRF" --factor "$1" --a "$2" --b "$3" \
           --life "$life" --games "$GAMES" --seed "$CSEED"; then
        python3 scripts/tourney_analyse.py "$OUT/cases/$tag/cases.json" \
            > "$OUT/cases/$tag/MECHANISM.md" 2>&1 || say "mechanism failed for $tag"
      else
        say "case logs unavailable for $tag (no qualifying games)"
      fi
    done
  done
}

say "rebuild -- the measurement binary must be the one the smoke certifies"
./build.sh || { say "ABORT: build failed"; exit 1; }

# The last green smoke predates the LogDraw/CommitPhase changes to MTG_DUMP_CARDS, so it has to
# run again. A measurement on an engine that quietly moved is worthless, so this refuses.
say "regression smoke (byte-identity after the MTG_DUMP_CARDS changes)"
if ! bash test/regression.sh --smoke > "$OUT/smoke_regression.log" 2>&1; then
  say "ABORT: smoke regression FAILED -- see $OUT/smoke_regression.log"
  tail -40 "$OUT/smoke_regression.log"; exit 1
fi
tail -6 "$OUT/smoke_regression.log"

say "ONE pooled batch: 24 aliased arms x 2 life totals x $GAMES games, + the apparatus bracket"
python3 scripts/tourney_run.py run --apparatus alias --games "$GAMES" --seed "$SEED" \
    --bracket "$BRACKET" || { say "ABORT: batch"; exit 1; }

say "apparatus bracket -- what the table is worth, and the decision band"
python3 scripts/tourney_bracket.py --err "$OUT/run_alias/tourney.err" --games "$BRACKET" \
    > "$OUT/run_alias/BRACKET.md" 2>&1
cat "$OUT/run_alias/BRACKET.md"
BAND=$(grep -oP '(?<=decision band for the one-sided rule: \*\*)[0-9.]+' \
       "$OUT/run_alias/BRACKET.md" | sort -rn | head -1)
BAND=${BAND:-0}

say "report (one-sided, decision band = ${BAND}t)"
python3 scripts/tourney_report.py --err "$OUT/run_alias/tourney.err" --games "$GAMES" \
    --bias "$BAND" --seed "$SEED" --tsv "$OUT/run_alias/tourney.tsv" > "$OUT/run_alias/REPORT.md" 2>&1
grep -E '^\*\*(ADOPT|CONFIRM|INCUMBENT|GENERATE|MIXED)' "$OUT/run_alias/REPORT.md" || true

ERRF="$OUT/run_alias/tourney.err"; CSEED="$SEED"; cases_for

say "DONE -- results $OUT/run_alias/REPORT.md | bracket $OUT/run_alias/BRACKET.md | cases $OUT/cases/"
