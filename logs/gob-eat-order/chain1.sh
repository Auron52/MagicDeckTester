#!/bin/bash
# gob-eat-order chain 1 (under the box lock): build; then units + check-mode pool + 131075 probe in parallel.
S=/workspaces/MagicDeckTester/build/scratch/claude_gob_order
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-ae8f56a1352ef506f
cd $W || exit 1
echo "build start $(date -u)" > $S/chain1.status
./build.sh > $S/build1.log 2>&1 || { echo "BUILD FAILED $(date -u)" >> $S/chain1.status; touch $S/chain1.DONE; exit 1; }
echo "build ok $(date -u)" >> $S/chain1.status
BIN=$W/build/Release/mtg
( $W/build/Release/mtg-test > $S/units1.log 2>&1; echo "units rc=$? end $(date -u)" >> $S/chain1.status ) &
( env MTG_RSS_CAP_GB=6 MTG_MANA_ORACLE=2 MTG_MANA_ORACLE_COST_TRACE=1 MTG_MANA_ORACLE_ONLY_MENU=446 MTG_MANA_ORACLE_DECISION_S=1200 \
    $BIN --batch $S/u131075.json --threads 1 --game-log-dir $S/logs_u > $S/u131075.out 2> $S/u131075.err
  echo "probe131075 rc=$? end $(date -u)" >> $S/chain1.status ) &
( env MTG_RSS_CAP_GB=28 MTG_MANA_ORACLE=2 MTG_MANA_ORACLE_COST_TRACE=1 MTG_ORACLE_EAT_ORDER_FOLD_CHECK=2 \
    $BIN --batch $S/pool.json --threads 29 --game-log-dir $S/logs_check > $S/check.out 2> $S/check.err
  echo "check rc=$? end $(date -u)" >> $S/chain1.status ) &
wait
touch $S/chain1.DONE
