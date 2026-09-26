#!/usr/bin/env bash
# Proves the Acclaimed Contender etb_dig_subtypes widening (Knight -> Knight/Aura/Equipment)
# is PLAY-NEUTRAL for decks/Knights/ (that list holds no Aura and no Equipment, so the dig
# candidate set is unchanged). Per-game digest equality is the proof; a sign test is not needed.
#   docs/design/analysis-WhiteKnights.md, "Audits"
#
# NOTE: Knights ships a value_play sidecar that PINS target_depth=5, so --depth is refused.
# We therefore run the deck at its own shipped settings (the configuration that matters), and
# separately at d0/d3 with --ignore-play-profile to widen the coverage.
set -euo pipefail
cd /workspaces/MagicDeckTester

OUT=logs/wk_neutrality
mkdir -p "$OUT"
git show HEAD:src/cards/data/cards.json > "$OUT/cards.old.json"

DECK=decks/Knights/Knights.cod
PROF=decks/Knights/Knights.profile.json

run () {           # run <label> <cards-json> <seed> <games> <extra args...>
  local label="$1" cj="$2" s="$3" g="$4"; shift 4
  MTG_DUMP_WINS=1 ./build/Release/mtg "$DECK" --profile "$PROF" --cards-json "$cj" \
      --games "$g" --seed "$s" --lookahead-bottoming --threads 1 "$@" \
      > "$OUT/${label}.out" 2> "$OUT/${label}.err" || true
  # the per-game win-turn stream IS the play fingerprint we compare
  grep '^\[win\]' "$OUT/${label}.err" > "$OUT/${label}.wins" || true
  wc -l < "$OUT/${label}.wins"
}

cmp_arm () {       # cmp_arm <name> <seed> <games> <extra...>
  local name="$1" s="$2" g="$3"; shift 3
  printf '### knights %s (seed %s, %s games)\n' "$name" "$s" "$g"
  printf '  old: %s games\n' "$(run "k_${name}_old" "$OUT/cards.old.json"     "$s" "$g" "$@")"
  printf '  new: %s games\n' "$(run "k_${name}_new" src/cards/data/cards.json "$s" "$g" "$@")"
  if diff -q "$OUT/k_${name}_old.wins" "$OUT/k_${name}_new.wins" >/dev/null 2>&1 \
     && [ -s "$OUT/k_${name}_old.wins" ]; then
    echo "  VERDICT: IDENTICAL per-game play"
  else
    echo "  VERDICT: *** DIFFERS (or empty) ***"
    diff "$OUT/k_${name}_old.wins" "$OUT/k_${name}_new.wins" | head -20 || true
  fi
}

cmp_arm shipped 1001 250
cmp_arm d0      1001 500 --ignore-play-profile --depth 0 --budget-ms 0
cmp_arm d3      1001 250 --ignore-play-profile --depth 3 --budget-ms 10
