#!/usr/bin/env bash
# waitbuild.sh <tag> -- block until logs/userorder/<tag>.log shows the build finished (done or an error)
U=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder
log="$U/$1.log"
while ! grep -qE ">>> done|error:|FAILED" "$log"; do sleep 15; done
grep -E "error:|FAILED" "$log" | head -20
tail -2 "$log"
