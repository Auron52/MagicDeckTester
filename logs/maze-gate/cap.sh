#!/bin/bash
# usage: cap.sh <binary-tag> <out-tag> <gi:turn>...   CAPPED-DECISION profile: one turn audited per game, in parallel
# lanes (1 slot, 6 GB each). Env passes through (MTG_MANA_ORACLE etc. are set here).
cd /workspaces/MagicDeckTester/.claude/worktrees/agent-a02c2671f9e6fcd10 || exit 1
S=/workspaces/MagicDeckTester/build/scratch/claude_maze_gate
bin=$S/bin/mtg.$1; tag=$2; shift 2
pids=()
for gt in "$@"; do
  g=${gt%%:*}; t=${gt##*:}
  ( MTG_MANA_ORACLE=2 MTG_MANA_ORACLE_ONLY_TURN=$t MTG_MANA_ORACLE_HEAVY=1 MTG_MANA_ORACLE_COST_TRACE=1 \
    bash scripts/box_lane.sh --slots 1 --cap-gb 6 -- /usr/bin/time -f "%e s wall %M KB" "$bin" "decks/Maze's End/Maze's End.cod" \
    --profile "decks/Maze's End/Maze's End.profile.json" --games 1 --seed $((30000000 + g)) --game-index "$g" \
    > "$S/${tag}_g${g}t$t.out" 2> "$S/${tag}_g${g}t$t.err" ) &
  pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
echo DONE > "$S/${tag}.DONE"
