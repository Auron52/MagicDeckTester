#!/bin/bash
# Overnight: regenerate every suite deck's mulligan profile with the JOINT
# land-params x hand-score-gate analyzer (AnalyzerEngine.cpp), validate each, and
# A/B it against the committed profile at the regression suite's own depths/budgets.
#
# NON-DESTRUCTIVE: the analyzer writes <stem>.profile.json next to its INPUT deck
# file (see analyzer/main.cpp), so we run it on TEMP COPIES of the deck files. The
# committed decks/*.profile.json are never touched. New profiles are STAGED under
# logs/profile_regen/ for the user to inspect and adopt manually -- this script does
# NOT adopt anything.
#
# Usage:  bash test/regen_profiles_overnight.sh
# Output: logs/profile_regen/  (staged profiles, per-deck analyzer logs, A/B report)
set -uo pipefail
cd "$(dirname "$0")/.."

BIN=./build/Release/mtg
ANALYZE=./build/Release/mtg-analyze
CARDS=src/cards/data/cards.json
SEED=20260623                       # fixed -> reproducible profiles
AB_SEEDS=(9001 7777)                # out-of-suite eval seeds
OUT=logs/profile_regen
WORK=$OUT/work
REPORT=$OUT/REPORT.txt
mkdir -p "$WORK"
: > "$REPORT"

# deck key -> source deck file (matches test/regression_cases.sh DECK_FILE)
KEYS=(burn th antilife slivers knights)         # burn first (known reference)
declare -A SRC=(
  [burn]=decks/test_deck.txt
  [th]=decks/treasure_hunt.txt
  [antilife]="decks/Anti-Lifegain.cod"
  [slivers]=decks/slivers_vial.txt
  [knights]=decks/Knights.cod
)
declare -A PROF=(
  [burn]=decks/test_deck.profile.json
  [th]=decks/treasure_hunt.profile.json
  [antilife]=decks/Anti-Lifegain.profile.json
  [slivers]=decks/slivers_vial.profile.json
  [knights]=decks/Knights.profile.json
)

log(){ echo "$@" | tee -a "$REPORT"; }

# Run N games and print the metric avg (mean turn-to-win, unwon = max_turns+1; lower is better).
# Win/loss is not used -- avg already folds unwon games in at the horizon.
ab(){ # profile games seed depth budget
  local out avg
  out=$("$BIN" "$DECKTXT" --profile "$1" --games "$2" --seed "$3" \
        --depth "$4" --budget-ms "$5" --max-turns 8 --threads 0 2>/dev/null)
  avg=$(echo "$out" | grep -oE 'avg \(turns\) *: *[0-9.]+' | grep -oE '[0-9.]+$')
  [ -z "$avg" ] && { echo "ERR"; return; }
  echo "$avg"
}

log "=== Joint-gate profile regen + A/B  (seed=$SEED, $(date)) ==="
log "Decks: ${KEYS[*]}"
log ""

GRAND_START=$SECONDS
for key in "${KEYS[@]}"; do
  src=${SRC[$key]}; committed=${PROF[$key]}
  stem=$(basename "$src"); stem=${stem%.*}
  tmpdeck="$WORK/$(basename "$src")"
  newprof="$WORK/$stem.profile.json"
  staged="$OUT/$key.profile.json"
  DECKTXT="$src"                    # A/B always plays from the real deck file

  log "----------------------------------------------------------------"
  log "[$key] $src   ($(date +%H:%M:%S))"
  cp "$src" "$tmpdeck"
  rm -f "$newprof"

  t0=$SECONDS
  "$ANALYZE" "$tmpdeck" --cards-json "$CARDS" --max-turns 8 \
      --seed "$SEED" > "$WORK/$key.stdout.json" 2> "$OUT/$key.analyze.log"
  rc=$?
  dt=$((SECONDS - t0))
  if [ $rc -ne 0 ] || [ ! -s "$newprof" ]; then
    log "[$key] ANALYZER FAILED (rc=$rc, ${dt}s) -- see $OUT/$key.analyze.log; skipping"
    continue
  fi
  # Validate JSON + threshold finite + mulligan_profile present.
  if ! python3 - "$newprof" <<'PY'
import json,sys,math
p=json.load(open(sys.argv[1]))
mp=p["mulligan_profile"]
t=p.get("hand_score_threshold",0.0)
assert math.isfinite(t), f"threshold not finite: {t}"
assert "min_lands" in mp and "stop_at" in mp
PY
  then
    log "[$key] PROFILE VALIDATION FAILED -- skipping"; continue
  fi
  cp "$newprof" "$staged"

  # Param + gate summary
  read nmin nmax nstop nthr < <(python3 -c "
import json;p=json.load(open('$newprof'));m=p['mulligan_profile']
print(m['min_lands'],m['max_lands'],m['stop_at'],p.get('hand_score_threshold',0.0))")
  read cmin cmax cstop cthr < <(python3 -c "
import json;p=json.load(open('$committed'));m=p['mulligan_profile']
print(m.get('min_lands','?'),m.get('max_lands','?'),m.get('stop_at','?'),p.get('hand_score_threshold','?'))")
  log "[$key] analyzed in ${dt}s.  NEW min/max/stop/thr = $nmin/$nmax/$nstop/$nthr   COMMITTED = $cmin/$cmax/$cstop/$cthr"

  # A/B at the suite's own settings: d3/budget-10 (1000g) and d5/budget-20 (500g).
  # Metric = avg (lower is better); NEW regresses if its avg is higher than COMMITTED.
  log "[$key]   depth seed   NEW avg     COMMITTED avg    delta"
  drop=0
  for s in "${AB_SEEDS[@]}"; do
    for spec in "3 10 1000" "5 20 500"; do
      set -- $spec; d=$1; b=$2; g=$3
      n=$(ab "$staged" "$g" "$s" "$d" "$b")
      c=$(ab "$committed" "$g" "$s" "$d" "$b")
      flag=""; delta="?"
      if [ "$n" != ERR ] && [ "$c" != ERR ]; then
        delta=$(python3 -c "print(f'{$n-$c:+.4f}')")
        if python3 -c "import sys; sys.exit(0 if $n > $c + 1e-6 else 1)"; then flag="  <- NEW higher avg (worse)"; drop=1; fi
      fi
      log "[$key]   d$d  $s   ${n} (${g}g)   ${c} (${g}g)   ${delta}${flag}"
    done
  done
  if [ $drop -eq 0 ]; then log "[$key] VERDICT: NEW avg <= COMMITTED at every depth/seed -- candidate to adopt";
  else                    log "[$key] VERDICT: NEW avg regresses somewhere -- inspect before adopting"; fi
  log ""
done
log "=== done in $((SECONDS - GRAND_START))s.  Staged profiles: $OUT/<key>.profile.json ==="
log "Adopt a deck by copying its staged profile over the committed one, then re-baseline GT."
