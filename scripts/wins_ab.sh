#!/usr/bin/env bash
# EXACT per-game A/B: run one pooled batch twice and diff the per-game win-turn logs.
#
#   bash scripts/wins_ab.sh <outdir> <env-assignments...>
#   e.g. bash scripts/wins_ab.sh logs/winsab/bu900 MTG_BOTTOM_EVAL_UNITS=900
#
# WHY THIS EXISTS. A sweep compares per-chunk MEAN win turns, and two games that swap turns inside
# one chunk leave the mean bit-identical -- so "0 chunks differ" is strong evidence but not a proof
# that no game moved. `--game-log-dir` writes one win turn per game (the same artifact the
# regression harness promotes as `test/gt_logs/<key>.wins`), so diffing two runs' logs answers
# "did ANY game play differently" exactly. Use it before calling a lever quality-neutral.
#
# ONE POOLED BATCH PER ARM (CLAUDE.md), not a loop of per-seed invocations. The arms run
# SEQUENTIALLY on purpose: each gets the whole box, so neither is measured under the other's load.
# Win turns are deterministic, so sequencing costs nothing but wall clock.
set -u
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${MTG_BIN:-$root/build/Release/mtg}"
[ -f "$BIN" ] || { echo "ERROR: no binary -- run ./build.sh first" >&2; exit 2; }

OUT="${1:?outdir}"; shift
DECK=${DECK:-decks/Snow/Snow.cod}
PROF=${PROF:-decks/Snow/Snow.profile.json}
DEPTH=${DEPTH:-3}
BUDGET=${BUDGET:-10}
GAMES=${GAMES:-75}
CHUNKS=${CHUNKS:-8}
BASE=${BASE:-920000}
THREADS=${THREADS:-$(nproc)}

mkdir -p "$OUT"
MAN="$OUT/manifest.json"
{
  echo '{ "jobs": ['
  for ((c=0; c<CHUNKS; ++c)); do
    s=$((BASE + c * GAMES))
    [ "$c" -gt 0 ] && printf ',\n'
    # ignore_play_profile mirrors the regression harness's d0/d3 coverage cases: the deck's
    # value_play block LOCKS the play depth, and a pinned depth has to bypass it or the job runs a
    # depth the arm did not ask for.
    printf '  { "name": "wab_d%s_s%s", "deck": "%s", "profile": "%s", "games": %s, "seed": %s, "depth": %s, "budget_ms": %s, "ignore_play_profile": true }' \
      "$DEPTH" "$s" "$DECK" "$PROF" "$GAMES" "$s" "$DEPTH" "$BUDGET"
  done
  printf '\n] }\n'
} > "$MAN"

for arm in base arm; do
  rm -rf "$OUT/$arm"; mkdir -p "$OUT/$arm"
  if [ "$arm" = base ]; then envs=(); else envs=("$@"); fi
  echo ">>> $arm ${envs[*]:-(no env)}"
  # shellcheck disable=SC2086
  env ${envs[*]:-} "$BIN" --batch "$MAN" --threads "$THREADS" \
      --game-log-dir "$OUT/$arm" > "$OUT/$arm.log" 2>&1
done

echo
same=0; diff_n=0; games_moved=0
for f in "$OUT/base"/*.wins; do
  b="$(basename "$f")"
  g="$OUT/arm/$b"
  if [ ! -f "$g" ]; then echo "MISSING in arm: $b"; diff_n=$((diff_n+1)); continue; fi
  if cmp -s "$f" "$g"; then same=$((same+1));
  else
    diff_n=$((diff_n+1))
    n=$(diff <(cat "$f") <(cat "$g") | grep -c '^[<>]' || true)
    games_moved=$((games_moved + n / 2))
    echo "DIFFERS: $b  ($((n/2)) game line(s))"
  fi
done
echo
echo "per-game win logs: $same identical, $diff_n differ; approx $games_moved game(s) moved"
echo "logs under $OUT  (base/ vs arm/)"
