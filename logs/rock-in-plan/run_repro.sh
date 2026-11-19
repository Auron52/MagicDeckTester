#!/bin/bash
# usage: run_repro.sh <tag> <manifest>   (under the box lock)
# One hold: the oracle=2 batch over the flagged games + oracle=3 TAPDBG single repros (off and rock arms), in parallel.
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-ade0a1bc23ae96aa7
S=/workspaces/MagicDeckTester/build/scratch/claude_rock_plan
TAG="$1"; MAN="$2"
cd "$W" || exit 1
BIN=build/Release/mtg
if [ -n "$MAN" ]; then
  MTG_RSS_CAP_GB=16 MTG_MANA_ORACLE=2 $BIN --batch "$S/$MAN" --threads $(nproc) > "$S/$TAG.batch.out" 2> "$S/$TAG.batch.err" &
fi
for arm in off rock; do
  if [ $arm = rock ]; then AENV="MTG_LINE_KEEP_ROCK=1"; else AENV="MTG_LINE_KEEP_ROCK=0"; fi
  for spec in "Stuffy_Doll 3847 4" "Stuffy_Doll 27 4" "Stuffy_Doll 4614 5" "Dragons 94937 4" "Dragons 93961 4" "Dragons 93693 4"; do
    set -- $spec
    D="${1//_/ }"
    env $AENV MTG_RSS_CAP_GB=1 MTG_PREPAY_PROBE=1 MTG_LINE_REDUCER_KEEP_STATS=1 MTG_TAPDBG=1 MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=$3 \
      $BIN "decks/$D/$D.cod" --profile "decks/$D/$D.profile.json" --games 1 \
      --seed $((30000000 + $2)) --game-index $2 > "$S/$TAG.$arm.$1.s$2.out" 2> "$S/$TAG.$arm.$1.s$2.err" &
  done
done
wait
echo DONE > "$S/$TAG.done"
