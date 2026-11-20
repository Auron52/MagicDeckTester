#!/bin/bash
# build; probe diag (MTG_LKR_TIGHT_CHECK) + plain; then the full A/B (tag ab3)
S=/workspaces/MagicDeckTester/build/scratch/claude_rock_plan; W=/workspaces/MagicDeckTester/.claude/worktrees/agent-ade0a1bc23ae96aa7
cd $W
if ! ./build.sh > $S/ab3.build.log 2>&1; then echo FAIL > $S/ab3.done; exit 1; fi
mkdir -p $S/logs_probe8d $S/logs_probe8 $S/logs_ab3
MTG_RSS_CAP_GB=36 MTG_LKR_TIGHT_CHECK=1 MTG_LINE_REDUCER_KEEP_STATS=1 build/Release/mtg --batch $S/manifest_probe_diag.json --threads $(nproc) --game-log-dir $S/logs_probe8d > $S/probe8d.out 2> $S/probe8d.err
MTG_RSS_CAP_GB=36 MTG_LINE_REDUCER_KEEP_STATS=1 build/Release/mtg --batch $S/manifest_probe.json --threads $(nproc) --game-log-dir $S/logs_probe8 > $S/probe8.out 2> $S/probe8.err
if grep -q "lkr-tight-bad" $S/probe8d.err; then echo "ab3 SKIPPED: tight refusals" >> $S/ab3.status; echo DONE > $S/ab3.done; exit 0; fi
echo "ab3 start $(date)" >> $S/ab3.status
MTG_RSS_CAP_GB=40 MTG_PGO=0 build/Release/mtg --batch $S/manifest_ab.json --threads $(nproc) --game-log-dir $S/logs_ab3 > $S/ab3.out 2> $S/ab3.err
echo "ab3 rc=$? $(date)" >> $S/ab3.status
python3 $W/logs/rock-in-plan/compare.py $S/logs_ab3 $S/ab3.out > $S/ab3.compare.txt 2>&1
echo DONE > $S/ab3.done
