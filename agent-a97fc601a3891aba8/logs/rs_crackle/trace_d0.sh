#!/usr/bin/env bash
# trace_d0.sh <bin> <gi> <seed> <outfile> [ENV...] : one Hinata2 d0 game (20 life) with the executor traces on.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
BIN=$1; GI=$2; SEED=$3; OUT=$4; shift 4
cd "$WT" || exit 1
env MTG_USER_ORDER_TRACE=1 MTG_EXEC_TAP_TRACE=1 "$@" nice -n 19 \
  "$BIN" "$WT/decks/Hinata2/Hinata2.cod" --profile "$WT/decks/Hinata2/Hinata2.profile.json" \
  --seed "$SEED" --game-index "$GI" --games 1 --threads 1 --depth 0 --ignore-play-profile > "$OUT" 2>&1
grep "avg (turns)" "$OUT"
