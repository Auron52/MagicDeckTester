#!/usr/bin/env bash
# g1.sh <DeckFolder> <seed> <gi> <depth> <env-set>... -- one game's win turn per env set ("A=1,B=0"; "-" = defaults),
# the worktree's build/Release/mtg (build volume) under lowprio.sh. d>0 uses --budget-ms 10.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
deck="$1"; seed="$2"; gi="$3"; depth="$4"; shift 4
bud=0; if [ "$depth" != 0 ]; then bud=10; fi
for set in "$@"; do
  envs=(MTG_PGO=0)
  if [ "$set" != "-" ]; then IFS=',' read -r -a envs <<< "$set"; fi
  r=$(env "${envs[@]}" bash /workspaces/MagicDeckTester/build/scratch/lowprio.sh "$WT/build/Release/mtg" \
      "$WT/decks/$deck/$deck.cod" --profile "$WT/decks/$deck/$deck.profile.json" \
      --seed "$seed" --game-index "$gi" --games 1 --depth "$depth" --budget-ms "$bud" --ignore-play-profile --threads 1 2>/dev/null \
      | grep 'avg (turns)' | awk '{print $4}')
  echo "$deck seed=$seed gi=$gi d$depth [$set] -> $r"
done
