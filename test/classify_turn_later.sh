#!/usr/bin/env bash
# Auto-classify every SEARCHED-depth SLOWER game the audit flagged for <mode>, by re-running
# that ONE game at higher search budget (the metric is the loss-penalized avg, so a slower game
# is any worse score -- a bigger win turn OR a game becoming unwon, which is just the maximal slowdown):
#
#   * reaches AT LEAST the old win turn at higher budget -> "churn" (search-truncation at the case's
#                                                                   budget; the fast line is still
#                                                                   reachable -- benign). "At least",
#                                                                   not "exactly": a re-run that comes
#                                                                   back BETTER than the baseline is
#                                                                   the opposite of a regression and
#                                                                   must not be reported as one.
#   * persists at the new (slower) turn             -> "PERSISTS" (NOT budget churn: either
#                                                                   draw-divergence variance if the
#                                                                   deck shuffles/fetches, or a real
#                                                                   same-draws slowdown -- diff the
#                                                                   two lines with --log-dir to tell)
#
# Cost: two single-game runs per turn-later game, pooled into ONE `mtg --batch` (see below).
# Deterministic virtual budget, so results are reproducible. See docs/design/auto-audit-integration.md and the
# "MANDATORY before --accept" section of .claude/skills/regression-testing.md.
#
# Usage: bash test/classify_turn_later.sh <smoke|regression|overnight>
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT"

MODE="${1:-}"
case "$MODE" in
  smoke|regression|overnight) ;;
  *) echo "usage: classify_turn_later.sh <smoke|regression|overnight>" >&2; exit 2 ;;
esac

# Resolve the binary through the SHARED resolver, exactly as regression.sh does -- this script is
# part of that suite's pre-accept gate, so it must classify under the SAME binary the run it is
# classifying used. It previously hand-rolled the lookup and so could only ever read
# ./build/Release/mtg; when that tree was pinned to a different commit (a long batch holding it, a
# worktree A/B, a bisect) the classifier silently graded the wrong engine and reported churn-vs-
# persists for a binary nobody ran. harness_bin() also honours MTG_BIN, which is the documented way
# to point the suite at a worktree build.
# shellcheck source=lib/harness.sh
. "$HERE/lib/harness.sh"
BIN=$(harness_bin) || exit 1

# shellcheck source=regression_cases.sh
source "$HERE/regression_cases.sh"
case "$MODE" in
  smoke)      CASES=( "${SMOKE_CASES[@]}" ) ;;
  regression) CASES=( "${REGRESSION_CASES[@]}" ) ;;
  overnight)  CASES=( "${OVERNIGHT_CASES[@]}" ) ;;
esac

# key -> full case spec ("deck depth seed games budget"), so we can recover each game's budget.
declare -A CASE_OF
for spec in "${CASES[@]}"; do
  # shellcheck disable=SC2086
  set -- $spec; deck=$1; depth=$2; seed=$3
  CASE_OF["${deck}_${MODE}_d${depth}_s${seed}"]="$spec"
done

# Pull the SEARCHED-depth SLOWER entries from the audit output. Lines look like:
#   "    <key> gi<N>: <old>-><new>"   where <old> is the (numeric) prior win turn and <new> is
#   either a slower win turn or "loss" (a game becoming unwon = the maximal slowdown under loss=9).
# Reset the grab flag at the next section header so the d0 block is never mis-read.
audit=$(python3 "$HERE/audit_changed_games.py" "$MODE" 2>&1)
list=$(printf '%s\n' "$audit" | awk '
  /SEARCHED-depth SLOWER/{grab=1; next}
  /^d0 |^REVIEW|^\*\*\*/{grab=0}
  grab && /gi[0-9]+: *[0-9]+->(loss|[0-9]+)/{print}
')
if [ -z "$list" ]; then
  echo "no searched-depth slower games for $MODE -- nothing to classify."
  exit 0
fi


# Single game: reproduce game <gi> of a case by seeding at base_seed+gi and shifting the spawn
# pattern by gi (see GoldFishRunner::Run -- SetupGame(base_seed+gi), spawn uses base_game_index+gi).
# In a manifest that is a one-game CHUNK: "seed": base_seed+gi, "game_index": gi (BatchRunner
# Job::game_index), which plays exactly the game `--seed base+gi --game-index gi --games 1` plays.
#
# ALL the re-runs go into ONE pooled `mtg --batch` (two one-game jobs per slower game, at 4x and 16x
# the case budget). This used to be a serial loop of single-game processes, one core for the whole
# step: the 2026-10-22 overnight rebaseline's 706 slower games took ~35 min on 1 of 24 cores. Pooled,
# the step is one batch with one tail (CLAUDE.md: long / multi-item runs batch into ONE queue).
#
# Each re-run is the CASE's own job at 4x/16x its budget, emitted exactly as regression.sh emits it.
# A depth-5 case is the value_play-driven case: it carries NO depth, so the deck's value_play block
# decides the depth (FiveColour plays d6) and the hybrid/escalation stay on. It used to be replayed at
# an explicit depth 5 with ignore_play_profile, which on a d6 deck plays a DIFFERENT configuration
# from the one that slowed down -- so its churn/PERSISTS verdict answered a question nobody asked
# (found 2026-10-22 on FiveColour's overnight d5 cells). The d0/d3 cases pin their shallow depth and
# carry ignore_play_profile, as in the suite.
WORK="$ROOT/test/logs/$MODE/classify"
mkdir -p "$WORK"
rm -f "$WORK"/wins/*.wins 2>/dev/null
MANIFEST="$WORK/manifest.json"
INDEX="$WORK/index.tsv"     # job_stem key gi old new deck  (one line per slower game, audit order)
: > "$INDEX"
{
  echo '{ "jobs": ['
  first=1
  while read -r key gi_field old_new; do
    gi=${gi_field#gi}; gi=${gi%:}
    old=${old_new%%->*}; new=${old_new##*->}
    spec="${CASE_OF[$key]:-}"
    if [ -z "$spec" ]; then
      printf '%s\t%s\t%s\t%s\t%s\t%s\n' "-" "$key" "$gi" "$old" "$new" "-" >> "$INDEX"
      continue
    fi
    # shellcheck disable=SC2086
    set -- $spec; deck=$1; depth=$2; seed=$3; budget=$5
    # "<deck>2hg" 2HG variant cases (see regression_cases.sh): same deck file, but the repro must
    # match the case's game setup -- starting_life 30 + opponent_heads 2 -- or the re-run plays a
    # different game and the churn/PERSISTS verdict is meaningless.
    base=$deck; extra=""
    # A budget-0 case (the UNBUDGETED BACKSTOP rows) re-runs at 0 x 4 = 0 x 16 = unlimited, so its
    # re-run gets the same deterministic per-game ceiling regression.sh gives the case: a game that
    # turned degenerate is abandoned (no win line -> scored unwon -> PERSISTS), never left to hang.
    [ "$budget" -eq 0 ] && extra='"abandon_units": 2000000, '
    if [[ "$deck" == *2hg ]]; then
      base=${deck%2hg}; extra="${extra}\"starting_life\": 30, \"opponent_heads\": 2, "
    fi
    file=${DECK_FILE[$base]}; prof=${DECK_PROF[$base]}
    stem="${key}_gi${gi}"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$stem" "$key" "$gi" "$old" "$new" "$deck" >> "$INDEX"
    for x in 4 16; do
      [ $first -eq 1 ] && first=0 || printf ',\n'
      if [ "$depth" -eq 5 ]; then
        printf '  { "name": "%s_x%s", "deck": "%s", "profile": "%s", %s"games": 1, "seed": %s, "game_index": %s, "budget_ms": %s }' \
          "$stem" "$x" "$file" "$prof" "$extra" "$(( seed + gi ))" "$gi" "$(( budget * x ))"
      else
        printf '  { "name": "%s_x%s", "deck": "%s", "profile": "%s", %s"games": 1, "seed": %s, "game_index": %s, "depth": %s, "budget_ms": %s, "ignore_play_profile": true }' \
          "$stem" "$x" "$file" "$prof" "$extra" "$(( seed + gi ))" "$gi" "$depth" "$(( budget * x ))"
      fi
    done
  done <<< "$list"
  printf '\n] }\n'
} > "$MANIFEST"

"$BIN" --batch "$MANIFEST" --threads "${THREADS:-0}" --game-log-dir "$WORK/wins" \
  > "$WORK/batch.out" 2> "$WORK/batch.err"
rc=$?
if [ $rc -ne 0 ]; then
  echo "classify batch FAILED (rc=$rc) -- see $WORK/batch.err" >&2
  exit 1
fi

# Win turn of a one-game job from its per-game log ("<gi> <wt> [digest]"); empty if it did not run.
wt_of() { awk 'NR==1{print $2}' "$WORK/wins/$1.wins" 2>/dev/null; }

echo "=== classify searched slower games ($MODE) -- re-run each at 4x and 16x its case budget ==="
printf '%-40s %-5s %-5s  %s\n' "GAME" "OLD" "NEW" "CLASSIFICATION"
# Loss-penalized ORDER key, matching audit_changed_games.py / audit_cells.py: a won game scores its
# win turn, anything unwon ("loss", "-1", empty) ranks worse than every win. Only the ordering is
# used, so the sentinel just has to sit above any plausible win turn.
score_of() {
  case "$1" in
    ''|*[!0-9]*) echo 10000 ;;
    0)           echo 10000 ;;
    *)           echo "$1"  ;;
  esac
}
while IFS=$'\t' read -r stem key gi old new deck; do
  if [ "$stem" = "-" ]; then
    printf '%-40s %-5s %-5s  %s\n' "$key gi$gi" "$old" "$new" "?? no case row for key"
    continue
  fi
  wt4=$(wt_of "${stem}_x4"); wt16=$(wt_of "${stem}_x16")
  # SCORE the re-runs, don't string-compare them. The old test was `wt == old`, which asks
  # "did it come back to exactly the baseline turn" -- so a re-run that lands BETTER than the
  # baseline failed it and got reported as PERSISTS, i.e. as the very thing it is the opposite
  # of. That fired on both non-churn games of the 2026-09-10 overnight rebaseline (antilife d5
  # s7007 gi959: 7 -> 8 at the case budget but 6 at 4x/16x; hinata d5 s4004 gi5: 6 -> 7 but 5),
  # and it reads as a regression in a report whose whole job is to tell regressions apart from
  # churn. What actually matters is whether more budget buys back at LEAST the baseline.
  # An unwon re-run scores as a loss so it can never satisfy the test (see score()).
  s_old=$(score_of "$old"); s4=$(score_of "$wt4"); s16=$(score_of "$wt16")
  if [ "$s4" -le "$s_old" ] || [ "$s16" -le "$s_old" ]; then
    if [ "$s4" -lt "$s_old" ] || [ "$s16" -lt "$s_old" ]; then
      cls="churn -- and BETTER than baseline $old at higher budget (4x=$wt4 16x=$wt16)"
    else
      cls="churn (recovers to $old: 4x=$wt4 16x=$wt16)"
    fi
  else
    cls="PERSISTS (4x=$wt4 16x=$wt16) -- variance if $deck shuffles, else same-draws slowdown"
  fi
  printf '%-40s %-5s %-5s  %s\n' "$key gi$gi" "$old" "$new" "$cls"
done < "$INDEX"
