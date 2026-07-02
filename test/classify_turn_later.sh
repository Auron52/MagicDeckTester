#!/usr/bin/env bash
# Auto-classify every SEARCHED-depth turn-later game the audit flagged for <mode>, by re-running
# that ONE game at higher search budget (the classification the skill makes mandatory before
# --accept, generated instead of done by hand):
#
#   * recovers to the OLD win turn at higher budget -> "churn"    (search-truncation at the case's
#                                                                   budget; the fast line is still
#                                                                   reachable -- benign)
#   * persists at the new (slower) turn             -> "PERSISTS" (NOT budget churn: either
#                                                                   draw-divergence variance if the
#                                                                   deck shuffles/fetches, or a real
#                                                                   same-draws slowdown -- diff the
#                                                                   two lines with --log-dir to tell)
#
# Cost: two single-game runs per turn-later game (dozens, not thousands). Deterministic virtual
# budget, so results are reproducible. See docs/design/auto-audit-integration.md and the
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

BIN=./build/Release/mtg.exe; [ -f "$BIN" ] || BIN=./build/Release/mtg
[ -f "$BIN" ] || { echo "ERROR: $BIN not found -- build first (cmake --build build --config Release)." >&2; exit 1; }

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

# Pull the SEARCHED-depth turn-later entries from the audit output. Lines look like:
#   "    <key> gi<N>: <old>-><new>"   (both numeric -- a turn-later, not a win->loss).
# Reset the grab flag at the next section header so the d0 block is never mis-read.
audit=$(python3 "$HERE/audit_changed_games.py" "$MODE" 2>&1)
list=$(printf '%s\n' "$audit" | awk '
  /SEARCHED-depth turn-later/{grab=1; next}
  /^d0 |^GATE|^\*\*\*/{grab=0}
  grab && /gi[0-9]+: *[0-9]+->[0-9]+/{print}
')
if [ -z "$list" ]; then
  echo "no searched-depth turn-later games for $MODE -- nothing to classify."
  exit 0
fi

# Single game: reproduce game <gi> of a case by seeding at base_seed+gi and shifting the spawn
# pattern with --game-index gi (see GoldFishRunner::Run -- SetupGame(base_seed+gi), spawn uses
# base_game_index+gi). MTG_DUMP_WINS prints "[win] gi=0 wt=<N>" without perturbing play.
run_wt() { # deck_file game_seed gi depth budget -> win turn (or -1 loss)
  MTG_DUMP_WINS=1 "$BIN" "$1" --seed "$2" --game-index "$3" --games 1 --depth "$4" --budget-ms "$5" 2>&1 \
    | grep -oP 'wt=\K-?[0-9]+' | head -1
}

echo "=== classify searched turn-later ($MODE) -- re-run each at 4x and 16x its case budget ==="
printf '%-40s %-5s %-5s  %s\n' "GAME" "OLD" "NEW" "CLASSIFICATION"
churn=0; persist=0
printf '%s\n' "$list" | while read -r key gi_field old_new; do
  gi=${gi_field#gi}; gi=${gi%:}
  old=${old_new%%->*}; new=${old_new##*->}
  spec="${CASE_OF[$key]:-}"
  if [ -z "$spec" ]; then
    printf '%-40s %-5s %-5s  %s\n' "$key gi$gi" "$old" "$new" "?? no case row for key"
    continue
  fi
  # shellcheck disable=SC2086
  set -- $spec; deck=$1; depth=$2; seed=$3; budget=$5
  file=${DECK_FILE[$deck]}
  gseed=$(( seed + gi ))
  b4=$(( budget * 4 )); b16=$(( budget * 16 ))
  wt4=$(run_wt "$file" "$gseed" "$gi" "$depth" "$b4")
  wt16=$(run_wt "$file" "$gseed" "$gi" "$depth" "$b16")
  if [ "$wt4" = "$old" ] || [ "$wt16" = "$old" ]; then
    cls="churn (recovers to $old: 4x=$wt4 16x=$wt16)"
  else
    cls="PERSISTS (4x=$wt4 16x=$wt16) -- variance if $deck shuffles, else same-draws slowdown"
  fi
  printf '%-40s %-5s %-5s  %s\n' "$key gi$gi" "$old" "$new" "$cls"
done
