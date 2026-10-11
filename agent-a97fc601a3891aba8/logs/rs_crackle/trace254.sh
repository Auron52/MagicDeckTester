#!/usr/bin/env bash
# trace254.sh <bin> <outfile> [ENV...] : 2HG gi 254 at d0 with the executor traces on.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
BIN=$1; OUT=$2; shift 2
cd "$WT" || exit 1
env MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2 MTG_USER_ORDER_TRACE=1 MTG_EXEC_TAP_TRACE=1 "$@" nice -n 19 \
  "$BIN" "$WT/decks/Hinata2/Hinata2.cod" --profile "$WT/decks/Hinata2/Hinata2.profile.json" \
  --seed 8130255 --game-index 254 --games 1 --threads 1 --depth 0 --ignore-play-profile > "$OUT" 2>&1
grep -c "" "$OUT"
