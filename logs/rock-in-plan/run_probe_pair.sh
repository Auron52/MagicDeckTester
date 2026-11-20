#!/bin/bash
# build; probe with MTG_LKR_TIGHT_CHECK=1 (diag) then plain (timing + digests). tag $1
S=/workspaces/MagicDeckTester/build/scratch/claude_rock_plan; W=/workspaces/MagicDeckTester/.claude/worktrees/agent-ade0a1bc23ae96aa7
cd $W; t=$1
if ! ./build.sh > $S/$t.build.log 2>&1; then echo FAIL > $S/$t.done; exit 1; fi
mkdir -p $S/logs_${t}d $S/logs_$t
MTG_RSS_CAP_GB=36 MTG_LKR_TIGHT_CHECK=1 MTG_LINE_REDUCER_KEEP_STATS=1 build/Release/mtg --batch $S/manifest_probe.json --threads $(nproc) --game-log-dir $S/logs_${t}d > $S/${t}d.out 2> $S/${t}d.err
MTG_RSS_CAP_GB=36 MTG_LINE_REDUCER_KEEP_STATS=1 build/Release/mtg --batch $S/manifest_probe.json --threads $(nproc) --game-log-dir $S/logs_$t > $S/$t.out 2> $S/$t.err
echo DONE > $S/$t.done
