#!/usr/bin/env bash
# Regression tester for the MTG simulator. Three modes, each with its own time
# budget, cadence, and (deliberately disjoint) seeds so coverage compounds when
# you run more than one:
#
#   --smoke       fast gate, target < 15 min, run frequently / before every push
#   (default)     pre-commit regression, target < 45 min, run before committing
#   --overnight   deep multi-seed sweep, target < 8 h, run while you sleep
#
# Each case is (deck x depth x seed x games x budget), defined in
# regression_cases.sh. For every case we:
#   * write the binary's FULL output to test/logs/<mode>/<key>.log (+ .err),
#   * record wall time (per case and total),
#   * record the fingerprint "<games_won>/<avg_win_turn>" to
#     test/results/<mode>.env,
#   * compare that fingerprint to the committed ground truth in regression_gt.txt
#     (keyed <deck>_<mode>_d<depth>_s<seed>).
# The won-count catches win<->loss flips that barely move the avg win turn
# (important for decks like Treasure Hunt that do not always win).
#
# Usage (run from repo root, after building Release):
#   bash test/regression.sh            # regression mode (default)
#   bash test/regression.sh --smoke    # fast smoke gate
#   bash test/regression.sh --overnight
#
# Update ground truth from a run you have inspected and ACCEPT (no re-run):
#   bash test/regression.sh --smoke --accept     # promote last smoke results
# Accept reuses test/results/<mode>.env from the most recent run of that mode and
# merges it into regression_gt.txt, leaving the other modes untouched.
#
# Thread count defaults to hardware_concurrency (--threads 0). Results are
# thread-invariant, so the ground truth is valid at any thread count.
#   THREADS=2 bash test/regression.sh
#
# Exit code: 0 = all pass (NEW keys do not fail), 1 = any mismatch.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN=./build/Release/mtg.exe
GT=test/regression_gt.txt
THREADS=${THREADS:-0}

MODE=regression
ACCEPT=0
for arg in "$@"; do
  case "$arg" in
    --smoke)     MODE=smoke ;;
    --overnight) MODE=overnight ;;
    --fast)      MODE=smoke ;;      # back-compat alias
    --accept)    ACCEPT=1 ;;
    *) echo "unknown arg: $arg" >&2; exit 2 ;;
  esac
done

LOGDIR=test/logs/$MODE
OUT=test/regression_result_${MODE}.txt
RESULTS=test/results/${MODE}.env
mkdir -p "$LOGDIR" test/results

# Flag that selects this mode on the command line (regression is the default).
case "$MODE" in
  smoke)     MODEFLAG="--smoke" ;;
  overnight) MODEFLAG="--overnight" ;;
  *)         MODEFLAG="" ;;
esac

# shellcheck source=regression_cases.sh
source "$HERE/regression_cases.sh"

case "$MODE" in
  smoke)      CASES=( "${SMOKE_CASES[@]}" ) ;;
  regression) CASES=( "${REGRESSION_CASES[@]}" ) ;;
  overnight)  CASES=( "${OVERNIGHT_CASES[@]}" ) ;;
esac

# ---- accept: promote the last run's results into ground truth -------------
# Rebuilds regression_gt.txt in canonical matrix order, pulling each value from
# (existing ground truth) overlaid with (this mode's just-accepted results), so
# only the accepted mode changes. No binary is run.
if [ "$ACCEPT" = 1 ]; then
  if [ ! -f "$RESULTS" ]; then
    echo "ERROR: $RESULTS not found. Run 'bash test/regression.sh $MODEFLAG' first, inspect it, then --accept." >&2
    exit 1
  fi
  # shellcheck disable=SC1090
  [ -f "$GT" ] && source "$GT" 2>/dev/null || true   # existing values for all modes
  # shellcheck disable=SC1090
  source "$RESULTS"                                   # override the accepted mode
  emit_mode() {
    local mode=$1; local -n arr=$2
    echo ""
    echo "# --- $mode ---"
    local spec deck depth seed key val
    for spec in "${arr[@]}"; do
      # shellcheck disable=SC2086
      set -- $spec; deck=$1; depth=$2; seed=$3
      key="${deck}_${mode}_d${depth}_s${seed}"
      val="${!key-}"
      [ -n "$val" ] && [ "$val" != "TODO" ] && echo "$key=$val"
    done
  }
  {
    echo "# Regression ground truth -- commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)  date $(date +%Y-%m-%d)"
    echo "# Promoted from accepted runs by 'regression.sh --accept' -- do not hand-edit."
    echo "# Key: <deck>_<mode>_d<depth>_s<seed> = <games_won>/<avg_win_turn>"
    echo "# Modes: smoke (<15m), regression (<45m), overnight (<8h); seeds disjoint."
    emit_mode smoke      SMOKE_CASES
    emit_mode regression REGRESSION_CASES
    emit_mode overnight  OVERNIGHT_CASES
  } > "$GT.tmp"
  mv "$GT.tmp" "$GT"
  echo "Accepted $MODE results into $GT."
  exit 0
fi

# ---- run a mode and compare ----------------------------------------------
if [ ! -f "$BIN" ]; then
  echo "ERROR: $BIN not found. Run cmake --build build --config Release first." >&2
  exit 1
fi
[ -f "$GT" ] && source "$GT" 2>/dev/null || true

PASS=0; FAIL=0; NEW=0
TOTAL_START=$(date +%s)

log() { echo "$1"; echo "$1" >> "$OUT"; }

: > "$OUT"
: > "$RESULTS"
log "=== REGRESSION ($MODE) $(date) ==="
log "(threads=$THREADS; binary=$BIN; logs in $LOGDIR)"

run_case() {
  local deck=$1 depth=$2 seed=$3 games=$4 budget=$5
  local file=${DECK_FILE[$deck]} prof=${DECK_PROF[$deck]}
  local key="${deck}_${MODE}_d${depth}_s${seed}"
  local logf="$LOGDIR/${key}.log"

  local flags=(--games "$games" --seed "$seed" --threads "$THREADS" --depth "$depth")
  if [ "$depth" -gt 0 ]; then
    flags+=(--budget-ms "$budget" --lookahead-bottoming)
  fi

  local t0 t1 out won awt got expected wall status
  t0=$(date +%s)
  out=$("$BIN" "$file" --profile "$prof" "${flags[@]}" 2>"$LOGDIR/${key}.err")
  t1=$(date +%s)
  printf '%s\n' "$out" > "$logf"
  wall=$((t1 - t0))

  won=$(printf '%s\n' "$out" | grep "Games won"    | sed -E 's/.*won *: *([0-9]+).*/\1/')
  awt=$(printf '%s\n' "$out" | grep "Avg win turn" | sed 's/.*: //')
  got="${won}/${awt}"
  expected="${!key-}"

  if [ -z "$won" ] || [ -z "$awt" ]; then
    status="FAIL"; got="(no output)"; FAIL=$((FAIL+1))
  else
    echo "$key=$got" >> "$RESULTS"           # record for a later --accept
    if [ -z "$expected" ]; then
      status="NEW "; expected="<none>"; NEW=$((NEW+1))
    elif [ "$expected" = "$got" ]; then
      status="PASS"; PASS=$((PASS+1))
    else
      status="FAIL"; FAIL=$((FAIL+1))
    fi
  fi
  log "$(printf '  %s  %-26s exp=%-12s got=%-12s %5ss' "$status" "$key" "$expected" "$got" "$wall")"
}

CUR_DECK=""
for spec in "${CASES[@]}"; do
  # shellcheck disable=SC2086
  set -- $spec
  if [ "$1" != "$CUR_DECK" ]; then CUR_DECK="$1"; log ""; log "-- $CUR_DECK --"; fi
  run_case "$@"
done

TOTAL=$(( $(date +%s) - TOTAL_START ))
log ""
log "Result: $PASS passed, $FAIL failed, $NEW new   (total wall ${TOTAL}s = $((TOTAL/60))m$((TOTAL%60))s)"
if [ "$FAIL" -eq 0 ]; then
  [ "$NEW" -gt 0 ] && log "ALL PASS ($NEW new key(s); inspect, then 'bash test/regression.sh $MODEFLAG --accept' to record)" \
                   || log "ALL PASS"
  exit 0
else
  log "REGRESSION DETECTED"
  exit 1
fi
