#!/usr/bin/env bash
# MTG_SITE9_TRACE=3 on 2HG gi 254 under B on and base: the executor's committed plan at turn 3.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
cd "$WT" || exit 1
for arm in bon base; do
  ev=(); [ $arm = bon ] && ev=(MTG_HINATA_HOLD_NEEDS_HINATA=1)
  env MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2 MTG_SITE9_TRACE=3 "${ev[@]}" nice -n 19 \
    "$R/mtg_hold1" "$WT/decks/Hinata2/Hinata2.cod" --profile "$WT/decks/Hinata2/Hinata2.profile.json" \
    --seed 8130255 --game-index 254 --games 1 --threads 1 --depth 0 --ignore-play-profile 2>&1 | grep "site9" | cut -c1-400
  echo ----
done
