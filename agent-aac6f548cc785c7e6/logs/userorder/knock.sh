#!/usr/bin/env bash
# knock.sh <seed> <gi> <depth|ship> -- win turn with the lever on, each component knocked out in turn.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
BIN="$WT/build/Release/mtg"
seed="$1"; gi="$2"; depth="$3"
if [ "$depth" = ship ]; then dargs=(--budget-ms 20); else dargs=(--depth "$depth" --budget-ms 10 --ignore-play-profile); fi
for mask in 0 1 2 4 8 16 32 64 127; do
  r=$(env MTG_UO_DBG_OFF=$mask nice -n 19 "$BIN" "$WT/decks/Dragonstorm/Dragonstorm.cod" \
      --profile "$WT/decks/Dragonstorm/Dragonstorm.profile.json" --seed "$seed" --game-index "$gi" --games 1 \
      "${dargs[@]}" --threads 1 2>/dev/null | grep 'avg (turns)' | awk '{print $4}')
  echo "mask=$mask -> $r"
done
r=$(env MTG_DRAGONSTORM_USER_ORDER=0 nice -n 19 "$BIN" "$WT/decks/Dragonstorm/Dragonstorm.cod" \
    --profile "$WT/decks/Dragonstorm/Dragonstorm.profile.json" --seed "$seed" --game-index "$gi" --games 1 \
    "${dargs[@]}" --threads 1 2>/dev/null | grep 'avg (turns)' | awk '{print $4}')
echo "lever off -> $r"
