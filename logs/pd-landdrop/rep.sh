#!/bin/bash
# rep.sh <tag> <manifest> [env assignments...] : one batch (2 threads) into $S/<tag>/, DONE marker $S/<tag>.DONE
S=/workspaces/MagicDeckTester/build/scratch/claude_pd_landdrop
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-aa57dd6400917806d
tag=$1; man=$2; shift 2
cd $W || exit 1
rm -f $S/$tag.DONE
mkdir -p $S/$tag
env "$@" $W/build/Release/mtg --batch $man --threads ${REP_THREADS:-2} --game-log-dir $S/$tag/wins --game-trace-dir $S/$tag/traces > $S/$tag.out 2> $S/$tag.err
echo "rc=$?" > $S/$tag.DONE
