#!/usr/bin/env bash
# Plan dump of 2HG gi 254 turn 3 under B on and base.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
cd "$WT" || exit 1
for arm in bon base; do
  ev=(); [ $arm = bon ] && ev=(MTG_HINATA_HOLD_NEEDS_HINATA=1)
  env MTG_PGO=0 MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2 MTG_PLAN_DUMP=3 MTG_PLAN_DUMP_MAX=15 "${ev[@]}" nice -n 19 \
    "$R/mtg_hold1" "$WT/decks/Hinata2/Hinata2.cod" --profile "$WT/decks/Hinata2/Hinata2.profile.json" \
    --seed 8130255 --game-index 254 --games 1 --threads 1 --depth 0 --ignore-play-profile > "$R/dump254_$arm.txt" 2>&1
done
