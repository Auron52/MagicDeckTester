#!/bin/bash
# Dig census per deck (MTG_DIG_CENSUS), lever ON vs =0, small d3 b10 sample. Diagnostic only.
# Usage: census.sh <binary> <outdir> [games]
BIN=$1; OUT=$2; N=${3:-40}
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-ac800b84e49fb7cf6
P=/workspaces/MagicDeckTester
mkdir -p "$OUT"
while read -r key deck prof; do
  for arm in 1 0; do
    env -C "$WT" MTG_DIG_CENSUS=1 MTG_DIG_SEARCHED_CHOICE=$arm taskset -c 0-5 nice -n 19 "$BIN" "$P/$deck" --profile "$P/$prof" \
      --seed 8700001 --games "$N" --depth 3 --budget-ms 10 --ignore-play-profile --threads 6 > "$OUT/$key.$arm.out" 2> "$OUT/$key.$arm.err"
    echo "$key lever=$arm $(grep -h 'avg (turns)' "$OUT/$key.$arm.out" "$OUT/$key.$arm.err" | head -1) $(grep -h '\[dig-census\]' "$OUT/$key.$arm.err")"
  done
done <<'EOF'
fivecolour decks/FiveColour/FiveColour.cod decks/FiveColour/FiveColour.profile.json
pirates decks/Pirates/Pirates.cod decks/Pirates/Pirates.profile.json
edf decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json
soldiers decks/Soldiers/soldiers.cod decks/Soldiers/soldiers.profile.json
whiteknights decks/WhiteKnights/WhiteKnights.cod decks/WhiteKnights/WhiteKnights.profile.json
th decks/treasure_hunt/treasure_hunt.txt decks/treasure_hunt/treasure_hunt.profile.json
auras decks/Auras/Auras.cod decks/Auras/Auras.profile.json
fluctuator decks/Fluctuator/Fluctuator.cod decks/Fluctuator/Fluctuator.profile.json
dragons decks/Dragons/Dragons.cod decks/Dragons/Dragons.profile.json
EOF
