#!/bin/bash
# Lever-ON census + digauto trace for the decks with an in-window residual. Usage: census2.sh <binary> <outdir> [games]
BIN=$1; OUT=$2; N=${3:-40}
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-ac800b84e49fb7cf6
P=/workspaces/MagicDeckTester
mkdir -p "$OUT"
while read -r key deck prof; do
  env -C "$WT" MTG_DIG_CENSUS=1 MTG_TRACE=digauto taskset -c 0-5 nice -n 19 "$BIN" "$P/$deck" --profile "$P/$prof" \
    --seed 8700001 --games "$N" --depth 3 --budget-ms 10 --ignore-play-profile --threads 6 > "$OUT/$key.out" 2> "$OUT/$key.err"
  echo "$key $(grep -h '\[dig-census\]' "$OUT/$key.err")"
done <<'EOF'
fivecolour decks/FiveColour/FiveColour.cod decks/FiveColour/FiveColour.profile.json
pirates decks/Pirates/Pirates.cod decks/Pirates/Pirates.profile.json
soldiers decks/Soldiers/soldiers.cod decks/Soldiers/soldiers.profile.json
th decks/treasure_hunt/treasure_hunt.txt decks/treasure_hunt/treasure_hunt.profile.json
auras decks/Auras/Auras.cod decks/Auras/Auras.profile.json
fluctuator decks/Fluctuator/Fluctuator.cod decks/Fluctuator/Fluctuator.profile.json
EOF
