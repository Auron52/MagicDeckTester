#!/usr/bin/env bash
# Overnight full-depth (MTG_FULL_DEPTH) vs baseline A/B on the HELD-OUT overnight
# seeds (4004-7007), all 3 decks, d3/d5. Answers: does full-depth stay >= baseline
# everywhere after the verified-win fix (1256443)? Also measures full-depth wall time
# for the "does it fit the regression budget" question. Per-game .wins are diffed;
# a game is "worse" if full-depth wins LATER than baseline (a residual search bug).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
BIN=./build/Release/mtg.exe
OUT=logs/fd_overnight
rm -rf "$OUT" && mkdir -p "$OUT/fd" "$OUT/base"

SEEDS="4004 5005 6006 7007"
# deck=file=profile
DECKS=(
  "slivers=slivers_vial.txt=slivers_vial.profile.json"
  "burn=test_deck.txt=test_deck.profile.json"
  "th=treasure_hunt.txt=treasure_hunt.profile.json"
)
# depth=games=budget  (d0 omitted: full-depth==baseline at depth 0)
CFGS=( "3=1000=100" "5=600=200" )

{
  echo '{ "jobs": ['
  first=1
  for d in "${DECKS[@]}"; do
    name=${d%%=*}; rest=${d#*=}; file=${rest%%=*}; prof=${rest#*=}
    for seed in $SEEDS; do
      for c in "${CFGS[@]}"; do
        dep=${c%%=*}; rest2=${c#*=}; games=${rest2%%=*}; bud=${rest2#*=}
        [ $first -eq 1 ] && first=0 || printf ',\n'
        printf '  { "name": "%s_d%s_s%s", "deck": "%s", "profile": "%s", "games": %s, "seed": %s, "depth": %s, "budget_ms": %s, "lookahead_bottoming": true }' \
          "$name" "$dep" "$seed" "$file" "$prof" "$games" "$seed" "$dep" "$bud"
      done
    done
  done
  printf '\n] }\n'
} > "$OUT/manifest.json"

echo "[$(date +%H:%M:%S)] full-depth run starting"
S=$(date +%s)
MTG_FULL_DEPTH=1 "$BIN" --batch "$OUT/manifest.json" --threads 0 --game-log-dir "$OUT/fd" > "$OUT/fd.log" 2>"$OUT/fd.err"
FD_T=$(( $(date +%s) - S ))
echo "[$(date +%H:%M:%S)] full-depth done in ${FD_T}s"

echo "[$(date +%H:%M:%S)] baseline run starting"
S=$(date +%s)
MTG_LEGACY_SEARCH=1 "$BIN" --batch "$OUT/manifest.json" --threads 0 --game-log-dir "$OUT/base" > "$OUT/base.log" 2>"$OUT/base.err"
BASE_T=$(( $(date +%s) - S ))
echo "[$(date +%H:%M:%S)] baseline done in ${BASE_T}s"

SUM="$OUT/summary.txt"
{
  echo "=== full-depth vs baseline, overnight seeds 4004-7007 (verified-win fix 1256443) ==="
  echo "timing: full-depth=${FD_T}s  baseline=${BASE_T}s"
  echo ""
  printf "%-22s %-8s %-8s %-8s  %s\n" "job" "worse" "better" "same" "(worse = fd LATER than baseline)"
  total_worse=0
  for f in "$OUT"/fd/*.wins; do
    job=$(basename "$f" .wins)
    bf="$OUT/base/$job.wins"
    [ -f "$bf" ] || { echo "$job: MISSING baseline"; continue; }
    read -r w b s wl < <(paste "$f" "$bf" | awk '
      { fd=$2; bs=$4; if (fd>bs){worse++; wl=wl" "(NR-1)":"bs"->"fd} else if (fd<bs) better++; else same++ }
      END { print worse+0, better+0, same+0, wl }')
    printf "%-22s %-8s %-8s %-8s\n" "$job" "$w" "$b" "$s"
    if [ "$w" -gt 0 ]; then echo "    WORSE games (gi:base->fd):$wl"; total_worse=$((total_worse+w)); fi
  done
  echo ""
  echo "TOTAL worse games (full-depth regressions vs baseline): $total_worse"
  echo ""
  echo "=== full-depth aggregate fingerprints ==="
  grep -hE ": played=" "$OUT/fd.log"
  echo "=== baseline aggregate fingerprints ==="
  grep -hE ": played=" "$OUT/base.log"
} > "$SUM"
echo "DONE -- summary at $SUM"
cat "$SUM"
