#!/usr/bin/env bash
# combo.sh <bin> <seed> <gi> <depth> <env-set>... -- win turn for each env set ("A=1,B=0" comma lists; "-" = defaults)
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
BIN="$1"; seed="$2"; gi="$3"; depth="$4"; shift 4
for set in "$@"; do
  envs=(MTG_NONE=0)
  if [ "$set" != "-" ]; then IFS=',' read -r -a envs <<< "$set"; fi
  r=$(env "${envs[@]}" nice -n 19 "$BIN" "$WT/decks/Dragonstorm/Dragonstorm.cod" --profile "$WT/decks/Dragonstorm/Dragonstorm.profile.json" \
      --seed "$seed" --game-index "$gi" --games 1 --depth "$depth" --budget-ms 10 --ignore-play-profile --threads 1 2>/dev/null \
      | grep 'avg (turns)' | awk '{print $4}')
  echo "seed=$seed gi=$gi [$set] -> $r"
done
