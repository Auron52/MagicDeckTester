#!/usr/bin/env bash
# Scenario sanity suite: run every test/scenarios/*.json through `mtg --scenario` and pass/fail on
# the fixture's own `expect_win_turn` assertion (exit 0 = PASS, 1 = FAIL/regressed, 2 = harness error).
# These are hand-built board fixtures (see docs/design/scenario-harness.md) that guard specific
# interactions the seed-driven regression suite can't target deterministically. Cheap (< a few
# seconds each), so the regression tester runs this as a sanity gate before the batch run.
#
# PARALLEL (2026-11-14). The fixtures are independent single-threaded processes, and run one after
# another the ~540 of them cost ~90 s of wall on an otherwise idle box -- serially, in front of every
# smoke and regression tier, i.e. 23 of 24 cores idle for a minute and a half each time. They now run
# SCEN_JOBS at once (default: THREADS when >0, else nproc) into per-fixture output files, and the
# report is printed afterwards IN FIXTURE ORDER, so the output and the exit code are exactly what the
# serial loop produced. SCEN_JOBS=1 restores the serial run.
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

jobs=${SCEN_JOBS:-${THREADS:-0}}
[ "$jobs" -gt 0 ] 2>/dev/null || jobs=$(nproc 2>/dev/null || echo 1)

# Per-fixture output: <tmp>/<i>.out (combined stdout+stderr) and <tmp>/<i>.rc. Regenerable scratch.
tmp=$(mktemp -d "${TMPDIR:-/tmp}/scenarios.XXXXXX") || { echo "ERROR: mktemp failed" >&2; exit 2; }
trap 'rm -f "$tmp"/*.out "$tmp"/*.rc "$tmp"/*.ms; rmdir "$tmp" 2>/dev/null' EXIT
# LONGEST FIRST: a few valve fixtures cost ~45 s apiece (kittyv2_*_valve_*), so in file order the run
# waited on them alone at the end (67 s on 24 cores, 2026-11-14). Each fixture's last wall (ms) is kept
# in test/logs/scenario_times.tsv (regenerable scratch) and fixtures start in descending cost, unknown
# first. Only the START order changes: the report below is printed in fixture order.
times="$(dirname "$dir")/logs/scenario_times.tsv"
export BIN tmp
for i in "${!files[@]}"; do
  n=$(basename "${files[$i]}")
  c=$(awk -F'\t' -v n="$n" '$1==n{print $2; exit}' "$times" 2>/dev/null)
  printf '%s\t%s\t%s\n' "${c:-999999999}" "$i" "${files[$i]}"
done | sort -t$'\t' -k1,1nr | while IFS=$'\t' read -r _c i f; do printf '%s\0%s\0' "$i" "$f"; done \
  | xargs -0 -n 2 -P "$jobs" bash -c '
      # MTG_JOBSERVER (test/regression.sh runs this beside the game batch): hold one shared CPU token per
      # fixture, the same protocol as the batch workers (src/runner/BatchRunner.cpp JobServer).
      js=""; if [ -n "${MTG_JOBSERVER:-}" ] && [ -p "$MTG_JOBSERVER" ] && exec 3<>"$MTG_JOBSERVER"; then
        IFS= read -r -n 1 -u 3 _tok && js=1; fi
      s=$(date +%s%N); "$BIN" --scenario "$2" > "$tmp/$1.out" 2>&1; echo $? > "$tmp/$1.rc"
      echo $(( ($(date +%s%N) - s) / 1000000 )) > "$tmp/$1.ms"
      [ -n "$js" ] && printf + >&3; exit 0' _
mkdir -p "$(dirname "$times")" 2>/dev/null
for i in "${!files[@]}"; do
  [ -f "$tmp/$i.ms" ] && printf '%s\t%s\n' "$(basename "${files[$i]}")" "$(cat "$tmp/$i.ms")"
done > "$times.$$" 2>/dev/null && mv -f "$times.$$" "$times" 2>/dev/null

pass=0; fail=0; err=0
for i in "${!files[@]}"; do
  f=${files[$i]}
  out="$(cat "$tmp/$i.out" 2>/dev/null)"; rc=$(cat "$tmp/$i.rc" 2>/dev/null || echo 2)
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
