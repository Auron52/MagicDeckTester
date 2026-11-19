#!/bin/bash
# gob-eat-order chain 2 (under the box lock): build (no-op expected), off/on arms concurrently, then smoke.
S=/workspaces/MagicDeckTester/build/scratch/claude_gob_order
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-ae8f56a1352ef506f
cd $W || exit 1
echo "build start $(date -u)" > $S/chain2.status
./build.sh > $S/build2.log 2>&1 || { echo "BUILD FAILED $(date -u)" >> $S/chain2.status; touch $S/chain2.DONE; exit 1; }
echo "build ok $(date -u)" >> $S/chain2.status
BIN=$W/build/Release/mtg
T=$(( $(nproc) / 2 ))
arm() { local tag=$1 v=$2
  env MTG_RSS_CAP_GB=18 MTG_MANA_ORACLE=2 MTG_MANA_ORACLE_COST_TRACE=1 MTG_ORACLE_EAT_ORDER_FOLD=$v \
    $BIN --batch $S/pool.json --threads $T --game-log-dir $S/logs_$tag > $S/$tag.out 2> $S/$tag.err
  echo "arm $tag rc=$? end $(date -u)" >> $S/chain2.status; }
arm off 0 &
arm on 1 &
wait
echo "smoke start $(date -u)" >> $S/chain2.status
MTG_PGO=0 MTG_RSS_CAP_GB=40 bash test/regression.sh --smoke > $S/smoke.log 2>&1
echo "smoke rc=$? end $(date -u)" >> $S/chain2.status
touch $S/chain2.DONE
