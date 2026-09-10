#!/usr/bin/env bash
# COMBO OFF offer-condition check -- does the viewer's "⚡ Combo Off" button appear on the boards the
# USER says it should, and does the plan behind it actually win?
#
# Every fixture in test/combo_off/*.json carries `expect_combo_off` (see RunScenario's header): the
# board is enumerated through TurnSolver::EnumerateMainPlans under MTG_HUMAN_PLAY -- the viewer's own
# entry point -- and the flagged plan is then re-APPLIED through TurnSolver::ApplyPlan and required
# to have actually killed the opponent. Both halves matter and neither substitutes for the other:
#
#  * offered-but-false is the seed-7 regression ("COMBO OFF: wins this turn", ten blinks, opponent
#    still on 20), which only the re-apply can catch;
#  * absent-but-winnable is the USER's 2026-09-10 report ("we simply do not get the Combo Off button
#    in enough cases"), which only the positive fixtures can catch.
#
# The NEGATIVE fixtures are not filler -- they are the whole reason a widening here is safe to ship.
# A change that made the button appear everywhere would pass every positive fixture.
#
# Kept OUT of test/scenarios/ deliberately: scenarios.sh asserts win turns on the autonomous engine,
# and these fixtures set MTG_HUMAN_PLAY and never run a turn. Separate suite, separate gate.
#
# Usage:  bash test/combo_off_check.sh              # run all fixtures
#         MTG_BIN=path bash test/combo_off_check.sh
#         CO_ENV="MTG_UNTAP_C_STARVED=0" bash test/combo_off_check.sh   # one-binary A/B of a lever
set -u
BIN="${MTG_BIN:-./build/Release/mtg}"
[ -f "$BIN" ] || BIN=./build/Release/mtg.exe
if [ ! -f "$BIN" ]; then echo "ERROR: $BIN not found -- build Release first." >&2; exit 2; fi

dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/combo_off"
shopt -s nullglob
files=("$dir"/*.json)
if [ ${#files[@]} -eq 0 ]; then echo "no combo_off fixtures in $dir"; exit 0; fi

pass=0; fail=0; err=0
for f in "${files[@]}"; do
  out="$(env ${CO_ENV:-} "$BIN" --scenario "$f" 2>&1)"; rc=$?
  line="$(printf '%s\n' "$out" | grep '^scenario: combo_off ' | head -1)"
  name="$(basename "$f" .json)"
  case $rc in
    0) echo "  PASS  $name    ${line#scenario: }"; pass=$((pass+1)) ;;
    1) echo "  FAIL  $name    ${line#scenario: }"
       printf '%s\n' "$out" | grep '^scenario: FAIL' | sed 's/^/        /'
       fail=$((fail+1)) ;;
    *) echo "  ERROR $name    $(printf '%s' "$out" | tail -1)"; err=$((err+1)) ;;
  esac
done
echo "Combo-off fixtures: $pass passed, $fail failed, $err error  (${#files[@]} total)"
[ $fail -eq 0 ] && [ $err -eq 0 ]
