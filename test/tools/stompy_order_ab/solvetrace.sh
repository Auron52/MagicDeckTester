#!/usr/bin/env bash
# One StompySurprise game (20-life), single-run CLI with --trace (per-candidate win-turn estimates),
# new binary at defaults and with MTG_STOMPY_ORDER=0. usage: solvetrace.sh <gi> <out_dir> [extra args...]
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9d0a1cbf3361f164
SC=/workspaces/MagicDeckTester/build/scratch/stompy-order
GI=$1; OUT=$2; shift 2
mkdir -p "$OUT"
cd "$WT" || exit 1
S=$((10900001 + GI))
for arm in new off; do
  if [ "$arm" = off ]; then export MTG_STOMPY_ORDER=0; else unset MTG_STOMPY_ORDER; fi
  MTG_FD_TRACE=1 nice -n 10 "$SC/bin/mtg.new" decks/StompySurprise/StompySurprise.cod \
      --profile decks/StompySurprise/StompySurprise.profile.json --cards-json src/cards/data/cards.json \
      --seed "$S" --game-index "$GI" --games 1 --threads 1 --trace "$@" > "$OUT/$arm.out" 2> "$OUT/$arm.err"
  grep "avg (turns)" "$OUT/$arm.out"
done
