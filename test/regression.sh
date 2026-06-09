#!/usr/bin/env bash
# Regression test: compare simulator metrics against stored ground truth.
# Run after any significant change to catch unintended behaviour shifts.
# Ground truth is in regression_gt.txt (committed); regenerate with gen_regression_gt.sh.
#
# Usage (run from repo root):
#   bash test/regression.sh           # full suite
#   bash test/regression.sh --fast    # d0 + d3 only, ~30 s
#
# Thread count: defaults to hardware_concurrency (--threads 0).
# Results are thread-invariant so the ground truth is valid at any thread count.
# Override: THREADS=2 bash test/regression.sh
#
# Exit code: 0 = all pass, 1 = any failure.

set -uo pipefail

BIN=./build/Release/mtg.exe
GT=test/regression_gt.txt
OUT=test/regression_result.txt
THREADS=${THREADS:-0}
PASS=0
FAIL=0
SKIP=0
FAST=0

for arg in "$@"; do
  case "$arg" in --fast) FAST=1 ;; esac
done

if [ ! -f "$BIN" ]; then
  echo "ERROR: $BIN not found. Run cmake --build build --config Release first." >&2
  exit 1
fi
if [ ! -f "$GT" ]; then
  echo "ERROR: $GT not found." >&2
  exit 1
fi

# shellcheck disable=SC1091
source "$GT"

run() { "$BIN" "$@" 2>/dev/null | grep "Avg win turn" | sed 's/.*: //' || true; }

check() {
  local key="$1" expected="$2" actual="$3" line
  if [ -z "$actual" ]; then
    line="  FAIL  $key: (no output from binary)"
    FAIL=$((FAIL+1))
  elif [ "$expected" = "$actual" ]; then
    line="  PASS  $key: $actual"
    PASS=$((PASS+1))
  else
    line="  FAIL  $key: expected=$expected  got=$actual"
    FAIL=$((FAIL+1))
  fi
  echo "$line"
  echo "$line" >> "$OUT"
}

header() { echo "$1"; echo "$1" >> "$OUT"; }

: > "$OUT"
header "=== REGRESSION $(date) ==="
header "(threads=$THREADS; fast=$FAST)"
header ""

# ---- SLIVERS ----
header "-- slivers_vial --"
SDECK=slivers_vial.txt
SPROF=(--profile slivers_vial.profile.json)

check "slivers d0  s=1001 1000g" "$slivers_d0_s1001" \
  "$(run $SDECK "${SPROF[@]}" --games 1000 --seed 1001 --threads $THREADS --depth 0)"

check "slivers d3  s=1001  500g" "$slivers_d3_s1001" \
  "$(run $SDECK "${SPROF[@]}" --games 500 --seed 1001 --threads $THREADS \
      --depth 3 --budget-ms 100 --lookahead-bottoming)"

check "slivers d3  s=2002  500g" "$slivers_d3_s2002" \
  "$(run $SDECK "${SPROF[@]}" --games 500 --seed 2002 --threads $THREADS \
      --depth 3 --budget-ms 100 --lookahead-bottoming)"

if [ "$FAST" -eq 0 ]; then
  check "slivers d5  s=1001  500g" "$slivers_d5_s1001" \
    "$(run $SDECK "${SPROF[@]}" --games 500 --seed 1001 --threads $THREADS \
        --depth 5 --budget-ms 200 --lookahead-bottoming)"
  check "slivers d5  s=2002  500g" "$slivers_d5_s2002" \
    "$(run $SDECK "${SPROF[@]}" --games 500 --seed 2002 --threads $THREADS \
        --depth 5 --budget-ms 200 --lookahead-bottoming)"
else
  SKIP=$((SKIP+2))
  echo "  SKIP  slivers d5 tests (--fast)"; echo "  SKIP  slivers d5 tests (--fast)" >> "$OUT"
fi

header ""

# ---- BURN ----
header "-- test_deck (burn) --"
BDECK=test_deck.txt

check "burn   d0  s=1001 1000g" "$burn_d0_s1001" \
  "$(run $BDECK --games 1000 --seed 1001 --threads $THREADS --depth 0)"

check "burn   d3  s=1001  500g" "$burn_d3_s1001" \
  "$(run $BDECK --games 500 --seed 1001 --threads $THREADS \
      --depth 3 --budget-ms 100)"

check "burn   d3  s=2002  500g" "$burn_d3_s2002" \
  "$(run $BDECK --games 500 --seed 2002 --threads $THREADS \
      --depth 3 --budget-ms 100)"

if [ "$FAST" -eq 0 ]; then
  check "burn   d5  s=1001  500g" "$burn_d5_s1001" \
    "$(run $BDECK --games 500 --seed 1001 --threads $THREADS \
        --depth 5 --budget-ms 200)"
else
  SKIP=$((SKIP+1))
  echo "  SKIP  burn d5 tests (--fast)"; echo "  SKIP  burn d5 tests (--fast)" >> "$OUT"
fi

# ---- SUMMARY ----
header ""
SUMMARY="Result: $PASS passed, $FAIL failed, $SKIP skipped"
header "$SUMMARY"
if [ "$FAIL" -eq 0 ]; then
  header "ALL PASS"
  exit 0
else
  header "REGRESSION DETECTED"
  exit 1
fi
