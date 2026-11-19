#!/usr/bin/env bash
# wait for build2, then run the clean smoke+regression A/B (census=1, print only)
cd /workspaces/MagicDeckTester/.claude/worktrees/idgate
until [ -f logs/idgate/build2.done ]; do sleep 20; done
grep -q "rc=0" logs/idgate/build2.done || { echo "build failed" > logs/idgate/chain_sr.STOP; exit 1; }
O=/workspaces/MagicDeckTester/build/scratch/idgate/sr1; rm -rf $O; mkdir -p $O
/workspaces/MagicDeckTester/scripts/heavy_run.sh logs/idgate/run_batch.sh logs/idgate/sr_ab.json $O MTG_IDG_CENSUS=1 > $O/heavy.log 2>&1
