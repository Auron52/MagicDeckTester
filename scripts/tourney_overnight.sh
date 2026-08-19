#!/usr/bin/env bash
# Wait for the pooled keep table, then run the whole card-elimination tournament unattended.
#
#   nohup bash scripts/tourney_overnight.sh <gen-pid> [games] > logs/tourney/overnight.log 2>&1 &
#
# Order matters, and each step is a gate on the next:
#   1. wait for the generator to EXIT (not for the file to appear -- the profile is written at the
#      end and a half-written file would be attached as if it were a table)
#   2. regression smoke: the engine gained MTG_DUMP_CARDS since the last green run, and CLAUDE.md
#      requires the byte-identity check after any engine change. A 9-hour measurement on an engine
#      that quietly moved is 9 hours of garbage, so this refuses rather than warns.
#   3. the measurement: ONE pooled batch, 60 arms x 2 life totals, one queue and one tail
#   4. the report (three numbers per comparison, per life total)
#   5. case logs for the headline comparison of each test, at both life totals
#
# No step is wrapped in a timeout: a truncated run reads as a result (CLAUDE.md).
set -uo pipefail
cd "$(dirname "$0")/.."

GEN_PID="${1:?usage: tourney_overnight.sh <gen-pid> [games]}"
GAMES="${2:-10000}"
SEED=1200000
OUT=logs/tourney
mkdir -p "$OUT/run"

say() { printf '\n=== %s  %s ===\n' "$(date '+%F %T')" "$*"; }

say "waiting for keep-table generation (pid $GEN_PID)"
while kill -0 "$GEN_PID" 2>/dev/null; do sleep 60; done
say "generation exited"
tail -3 "$OUT/pool/gen.log"

if ! ls "$OUT"/pool/pool.keepmodel.exhaustive.profile.json* >/dev/null 2>&1; then
  say "ABORT: generation left no keep table -- nothing downstream is worth running"; exit 1
fi

say "rebuilding (the tournament binary must be the one the smoke certifies)"
./build.sh || { say "ABORT: build failed"; exit 1; }

say "regression smoke (byte-identity after the MTG_DUMP_CARDS change)"
if ! bash test/regression.sh --smoke > "$OUT/smoke_regression.log" 2>&1; then
  say "ABORT: smoke regression FAILED -- see $OUT/smoke_regression.log"
  tail -40 "$OUT/smoke_regression.log"; exit 1
fi
tail -12 "$OUT/smoke_regression.log"

say "measurement: 60 arms x 2 life totals x $GAMES games, one pooled batch"
python3 scripts/tourney_run.py run --games "$GAMES" --seed "$SEED" || { say "ABORT: batch"; exit 1; }

say "report"
python3 scripts/tourney_report.py --err "$OUT/run/tourney.err" --games "$GAMES" \
    --tsv "$OUT/run/tourney.tsv" > "$OUT/run/REPORT.md" 2>&1
tail -5 "$OUT/run/REPORT.md"

# The headline comparison of each test, both life totals. These are the pairs the user named:
# Twinflame vs Libation, Anger vs Oracle, and Scale vs Draught for the contested slot.
for life in 20 30; do
  for spec in "tf tf3lib0 tf0lib3" "ao a4o0 a0o4" "slot scale draught" "slot draught entrance"; do
    set -- $spec
    say "case logs: $1 $2 vs $3 at $life life"
    tag="$1_$2_vs_$3_L$life"
    if python3 scripts/tourney_cases.py --err "$OUT/run/tourney.err" --factor "$1" --a "$2" \
         --b "$3" --life "$life" --games "$GAMES" --seed "$SEED"; then
      python3 scripts/tourney_analyse.py "$OUT/cases/$tag/cases.json" \
          > "$OUT/cases/$tag/MECHANISM.md" 2>&1 || say "mechanism failed for $tag"
    else
      say "case logs failed for $spec@$life"
    fi
  done
done

say "DONE -- report at $OUT/run/REPORT.md, case logs under $OUT/cases/"
