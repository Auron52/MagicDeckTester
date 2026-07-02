#!/usr/bin/env bash
# Scenario sanity suite: run every test/scenarios/*.json through `mtg --scenario` and pass/fail on
# the fixture's own `expect_win_turn` assertion (exit 0 = PASS, 1 = FAIL/regressed, 2 = harness error).
# These are hand-built board fixtures (see docs/design/scenario-harness.md) that guard specific
# interactions the seed-driven regression suite can't target deterministically. Cheap (< a few
# seconds each), so the regression tester runs this as a sanity gate before the batch run.
#
# Usage:  bash test/scenarios.sh            # run all fixtures
#         MTG_BIN=path bash test/scenarios.sh
set -u
BIN="${MTG_BIN:-./build/Release/mtg}"
[ -f "$BIN" ] || BIN=./build/Release/mtg.exe
if [ ! -f "$BIN" ]; then echo "ERROR: $BIN not found -- build Release first." >&2; exit 2; fi

dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scenarios"
shopt -s nullglob
files=("$dir"/*.json)
if [ ${#files[@]} -eq 0 ]; then echo "no scenarios in $dir"; exit 0; fi

pass=0; fail=0; err=0
for f in "${files[@]}"; do
  out="$("$BIN" --scenario "$f" 2>&1)"; rc=$?
  line="$(printf '%s\n' "$out" | grep '^scenario: win_turn' | head -1)"
  name="$(basename "$f" .json)"
  case $rc in
    0) echo "  PASS  $name    ${line#scenario: }"; pass=$((pass+1)) ;;
    1) echo "  FAIL  $name    ${line#scenario: }"; fail=$((fail+1)) ;;
    *) echo "  ERROR $name    $(printf '%s' "$out" | tail -1)"; err=$((err+1)) ;;
  esac
done
echo "Scenarios: $pass passed, $fail failed, $err error  (${#files[@]} total)"
[ $fail -eq 0 ] && [ $err -eq 0 ]
