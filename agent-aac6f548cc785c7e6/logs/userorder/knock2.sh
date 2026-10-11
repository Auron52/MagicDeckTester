#!/usr/bin/env bash
# knock2.sh <seed> <gi> <depth> <turn> -- per knockout mask, the cont decisions of <turn>
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
BIN="$WT/build/Release/mtg"
seed="$1"; gi="$2"; depth="$3"; turn="$4"
for mask in 0 1 2 4 8 16 32 9 24 25; do
  echo "== mask=$mask"
  env MTG_UO_DBG_OFF=$mask MTG_USER_ORDER_TRACE=1 nice -n 19 "$BIN" "$WT/decks/Dragonstorm/Dragonstorm.cod" \
      --profile "$WT/decks/Dragonstorm/Dragonstorm.profile.json" --seed "$seed" --game-index "$gi" --games 1 \
      --depth "$depth" --budget-ms 10 --ignore-play-profile --threads 1 2>&1 | grep -E "uo-order|avg" \
      | grep -E "t=$turn m=1|avg" | awk '{print $5, $6, $7, $12, $NF}'
done
