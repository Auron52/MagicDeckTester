#!/usr/bin/env bash
# fd_pinned_games.sh -- replay single games that were once commit-the-line divergences, under
# MTG_FD_ORACLE=1, and fail if any of them diverges again or stops matching its ground truth.
#
#   bash test/fd_pinned_games.sh            # uses harness_bin (PGO when fresh, else Release)
#   MTG_BIN=build/Release/mtg bash test/fd_pinned_games.sh
#
# A divergence ([fd-diverge]) is a game whose realised win is later than a win the search had already
# PROVEN -- the executor did not play the line the rollout scored (docs/design/rollout-executor-
# lockstep.md). Each game below was root-caused to a specific lockstep defect; the list pins the fixes
# at the level that matters (the real game), alongside the mechanism-level unit tests in
# test/unit/test_staged_lockstep.cpp. Expected win turns are read from the committed per-game ground
# truth (test/gt_logs/<key>.wins), so a re-accepted tier never leaves this script stale: what it adds
# over GT is the oracle -- a game can keep its GT turn while the search proves an earlier one.
#
# ONE pooled --batch (one job per game, games:1 with an explicit game_index -- the repro contract:
# seed = base + gi AND game_index = gi). Seconds of wall. Exit 0 = all clean, 1 = a divergence or a
# GT mismatch, 2 = harness error.
set -u
cd "$(dirname "$0")/.." || exit 2
# shellcheck disable=SC1091
source test/lib/harness.sh
# shellcheck disable=SC1091
source test/regression_cases.sh

# <case key> <game index> <why it is pinned>
PINNED=(
  "hinata_overnight_d3_s4004 160 #14 deferred Gamble continuation replayed inline (fixed 60c168f8)"
  "hinata_overnight_d5_s4004 107 #5 Gamble random discard over a differently-ordered hand (fixed fef4a54b)"
  "hinata_overnight_d5_s4004 291 #14 deferred Gamble continuation replayed inline (fixed 60c168f8)"
  "hinata_regression_d5_s2002 25 #14 original repro (60c168f8)"
  "hinata_overnight_d5_s5005 13 #5 original repro (fef4a54b)"
  "hinata_regression_d3_s3003 24 #5 m2 follow-on pass layout (fef4a54b)"
)

BIN=${MTG_BIN:-$(harness_bin)}
[ -x "$BIN" ] || { echo "fd_pinned_games: no binary at $BIN (./build.sh first)" >&2; exit 2; }
OUT=${FD_PINNED_OUT:-build/scratch/fd_pinned}
mkdir -p "$OUT"
MAN="$OUT/manifest.json"

{
  echo '{ "jobs": ['
  first=1
  for row in "${PINNED[@]}"; do
    # shellcheck disable=SC2086
    set -- $row; key=$1; gi=$2
    deck=${key%%_*}; rest=${key#*_}; rest=${rest#*_}            # <deck>_<mode>_d<D>_s<S>
    depth=${rest%%_*}; depth=${depth#d}; seed=${rest#*_s}
    base=$deck; extra2hg=""
    if [[ "$deck" == *2hg ]]; then base=${deck%2hg}; extra2hg='"starting_life": 30, "opponent_heads": 2, '; fi
    file=${DECK_FILE[$base]}; prof=${DECK_PROF[$base]}
    # the case's budget, from the matrix (same row regression.sh would run)
    bud=""
    for spec in "${SMOKE_CASES[@]}" "${REGRESSION_CASES[@]}" "${OVERNIGHT_CASES[@]}"; do
      # shellcheck disable=SC2086
      set -- $spec
      if [ "$1" = "$deck" ] && [ "$2" = "$depth" ] && [ "$3" = "$seed" ]; then bud=$5; break; fi
    done
    [ -n "$bud" ] || { echo "fd_pinned_games: $key is not a suite case" >&2; exit 2; }
    [ "$depth" -gt 0 ] || bud=0
    name="${key}_gi${gi}"
    [ $first -eq 1 ] && first=0 || printf ',\n'
    if [ "$depth" -eq 5 ]; then   # value_play-driven, exactly as regression.sh emits it
      printf '  { "name": "%s", "deck": "%s", "profile": "%s", %s"games": 1, "seed": %s, "game_index": %s, "budget_ms": %s }' \
        "$name" "$file" "$prof" "$extra2hg" "$((seed + gi))" "$gi" "$bud"
    else
      printf '  { "name": "%s", "deck": "%s", "profile": "%s", %s"games": 1, "seed": %s, "game_index": %s, "depth": %s, "budget_ms": %s, "ignore_play_profile": true }' \
        "$name" "$file" "$prof" "$extra2hg" "$((seed + gi))" "$gi" "$depth" "$bud"
    fi
  done
  printf '\n] }\n'
} > "$MAN"

MTG_FD_ORACLE=1 "$BIN" --batch "$MAN" --threads 0 --game-log-dir "$OUT/games" \
    > "$OUT/batch.log" 2> "$OUT/batch.err" || { echo "fd_pinned_games: batch failed (see $OUT/batch.err)" >&2; exit 2; }

rc=0
for row in "${PINNED[@]}"; do
  # shellcheck disable=SC2086
  set -- $row; key=$1; gi=$2; shift 2; why="$*"
  name="${key}_gi${gi}"
  got=$(awk -v g="$gi" '$1==g {print $2}' "$OUT/games/$name.wins" 2>/dev/null)
  exp=$(awk -v g="$gi" '$1==g {print $2}' "test/gt_logs/$key.wins" 2>/dev/null)
  seedg=$(( ${key##*_s} + gi ))
  div=$(grep -m1 "^\[fd-diverge\] seed=$seedg " "$OUT/batch.err")
  status=ok
  [ -n "$got" ] || status="NO RESULT"
  [ "$status" = ok ] && [ -n "$exp" ] && [ "$got" != "$exp" ] && status="GT MISMATCH (gt=$exp)"
  [ "$status" = ok ] && [ -n "$div" ] && status="DIVERGED: ${div#\[fd-diverge\] }"
  [ "$status" = ok ] || rc=1
  printf '%-34s gi%-4s wt=%-3s %s   -- %s\n' "$key" "$gi" "${got:--}" "$status" "$why"
done
exit $rc
