#!/usr/bin/env bash
# WHY: the value-leaf cost probe measured the matrix's H5 cell (depth 5, UNBOUNDED budget 0) at
# 8 games in 7 ms -- ~600x CHEAPER than H1 -- while playing WORSE (avg 4.875 vs 4.50-4.55 at every
# shallower depth). A deeper search cannot be cheaper and worse; that is a broken measurement or a
# real defect, and either way the cost projection built on it is void until it is explained.
#
# Four suspects, isolated one at a time on the SAME 8 games (seed 8008), all at depth 5 / budget 0:
#
#   sidecar   the deck as it now ships (value.json with leaf:none + alpha:relaxed) + MTG_LAZY_LEAF=1
#   nolazy    same, MTG_LAZY_LEAF=0                -> is it the lazy-leaf pass?
#   nosidecar value.json moved aside, lazy on      -> is it the adopted shape?
#   plain     no sidecar, no lazy                  -> the true matrix-H baseline
#
# The matrix driver runs its H cells against a SCRATCH deck dir with no sidecar (make_variant_deck),
# so `plain` is what phase C would actually pay. If `plain` is expensive and `sidecar` is not, the
# probe was measuring the wrong thing -- and separately, the shape's behaviour at budget 0 becomes a
# question about the adoption, because the screen only ever tested BUDGETED cells (d0b0 has no search
# at all, so it cannot speak to this).
set -euo pipefail
cd /workspaces/MagicDeckTester
OUT=$PWD/logs/wk_h5; mkdir -p "$OUT"
DECK=$PWD/decks/WhiteKnights/WhiteKnights.cod
PROF=$PWD/decks/WhiteKnights/WhiteKnights.profile.json
SIDE=$PWD/decks/WhiteKnights/WhiteKnights.value.json
STASH=$SIDE.H5_STASH
GAMES=${GAMES:-8}

restore () { [ -f "$STASH" ] && mv -f "$STASH" "$SIDE" || true; }
trap restore EXIT   # never leave the shipping sidecar moved aside

run_arm () {  # run_arm <tag> <lazy:0|1> <sidecar:0|1>
  local tag=$1 lazy=$2 side=$3
  [ "$side" = 0 ] && [ -f "$SIDE" ] && mv -f "$SIDE" "$STASH"
  [ "$side" = 1 ] && [ -f "$STASH" ] && mv -f "$STASH" "$SIDE"
  local env_pfx=()
  [ "$lazy" = 1 ] && env_pfx=(env MTG_LAZY_LEAF=1) || env_pfx=(env MTG_LAZY_LEAF=0)
  printf '%-10s ' "$tag"
  "${env_pfx[@]}" MTG_DUMP_UNITS=1 ./build/Release/mtg "$DECK" --profile "$PROF" \
      --games "$GAMES" --seed 8008 --depth 5 --budget-ms 0 --ignore-play-profile \
      > "$OUT/$tag.out" 2> "$OUT/$tag.err"
  awk '/^avg \(turns\)/ {printf "avg=%-9s", $4}' "$OUT/$tag.out"
  awk '/^Games played/ {printf "games=%-4s", $4}' "$OUT/$tag.out"
  # wall from `time`-free source: the binary prints nothing, so use the units counters as the
  # deterministic work axis and a coarse wall from the shell.
  echo
}

echo "== H5 anomaly isolation: depth 5, budget 0 (unbounded), $GAMES games, seed 8008 =="
echo "   (avg win turn is the tell: a real depth-5 search should be <= the shallower cells)"
echo
for spec in "sidecar 1 1" "nolazy 0 1" "nosidecar 1 0" "plain 0 0"; do
  # shellcheck disable=SC2086
  set -- $spec
  /usr/bin/time -f "   wall=%es cpu=%Us" ./build/Release/mtg --help >/dev/null 2>&1 || true
  s=$(date +%s.%N)
  run_arm "$1" "$2" "$3"
  e=$(date +%s.%N)
  printf '           wall %.2fs\n' "$(echo "$e - $s" | bc)"
done
restore
echo
echo "Reference from the cost probe (sidecar present, lazy on, via --batch): 8 games in 7 ms, avg 4.875."
echo "Shallower matrix cells for comparison: H1 avg 4.550, H3 avg 4.525, H4 avg 4.500."
echo
echo "READING: whichever arm is EXPENSIVE and plays avg <= 4.50 is the real H5 cell. If that is"
echo "'plain'/'nosidecar', the cost projection must be rebuilt from it. If the cheap-and-worse"
echo "behaviour follows the SIDECAR, that is a finding about the adopted shape at budget 0."
