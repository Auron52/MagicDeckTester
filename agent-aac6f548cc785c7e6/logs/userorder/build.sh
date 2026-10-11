#!/usr/bin/env bash
# Low-priority Release build of THIS worktree (never the primary checkout): taskset 0-5, nice 19.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6
tag="${1:-build}"
taskset -c 0-5 nice -n 19 "$WT/build.sh" > "$WT/logs/userorder/$tag.log" 2>&1
rc=$?
echo "EXIT $rc"
tail -3 "$WT/logs/userorder/$tag.log"
exit $rc
