#!/usr/bin/env bash
# dcombo.sh <bin> <DeckFolder> <seed> <gi> <depth> <env-set>... -- win turn per env set ("A=1,B=0"; "-" = defaults)
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
BIN="$1"; deck="$2"; seed="$3"; gi="$4"; depth="$5"; shift 5
for set in "$@"; do
  envs=(MTG_PGO=0)
  if [ "$set" != "-" ]; then IFS=',' read -r -a envs <<< "$set"; fi
  r=$(env "${envs[@]}" nice -n 19 "$BIN" "$WT/decks/$deck/$deck.cod" --profile "$WT/decks/$deck/$deck.profile.json" \
      --seed "$seed" --game-index "$gi" --games 1 --depth "$depth" --budget-ms 10 --ignore-play-profile --threads 1 2>/dev/null \
      | grep 'avg (turns)' | awk '{print $4}')
  echo "$deck seed=$seed gi=$gi d$depth [$set] -> $r"
done
