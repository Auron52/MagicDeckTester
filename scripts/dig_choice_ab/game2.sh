#!/bin/bash
# One game, both lever arms, with the realised-dig trace and the committed-line trace.
# Usage: game2.sh <deck-key> <seed> <gi> <depth> <budget> <outdir> [extra env...]
KEY=$1; SEED=$2; GI=$3; DEPTH=$4; BUD=$5; OUT=$6; shift 6
B=/workspaces/MagicDeckTester/build/scratch/dig_agent
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-ac800b84e49fb7cf6
P=/workspaces/MagicDeckTester
case $KEY in
  fivecolour) D=decks/FiveColour/FiveColour.cod; PR=decks/FiveColour/FiveColour.profile.json;;
  th) D=decks/treasure_hunt/treasure_hunt.txt; PR=decks/treasure_hunt/treasure_hunt.profile.json;;
  fluctuator) D=decks/Fluctuator/Fluctuator.cod; PR=decks/Fluctuator/Fluctuator.profile.json;;
  pirates) D=decks/Pirates/Pirates.cod; PR=decks/Pirates/Pirates.profile.json;;
  *) echo "unknown deck"; exit 1;;
esac
mkdir -p "$OUT"
for lv in 1 0; do
  env -C "$WT" "$@" MTG_DIG_SEARCHED_CHOICE=$lv MTG_TRACE=dig MTG_FD_TRACE=1 taskset -c 0-5 nice -n 19 "$B/mtg.v6" "$P/$D" --profile "$P/$PR" \
    --seed "$SEED" --game-index "$GI" --games 1 --depth "$DEPTH" --budget-ms "$BUD" --ignore-play-profile --threads 1 \
    --log-dir "$OUT/log$lv" > "$OUT/out$lv.txt" 2> "$OUT/err$lv.txt"
  echo "lever=$lv $(grep -h 'avg (turns)' "$OUT/out$lv.txt" "$OUT/err$lv.txt" | head -1 | cut -c1-30) digs: $(grep -h '\[dig\]\|dig\] T' "$OUT/err$lv.txt" | grep -v fd | tr '\n' ' ' | cut -c1-300)"
done
