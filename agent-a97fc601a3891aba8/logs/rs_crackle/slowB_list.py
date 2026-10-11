#!/usr/bin/env python3
"""Write the lever-B slower games (bon > base) of the held-out batch as a log_pairs.sh list."""
import sys
sys.path.insert(0, "/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8/logs/rs_crackle")
import make_hold as mh

wins, _, _, _ = mh.parse(sys.argv[1])
out = open(sys.argv[2], "w")
n = 0
for cname, depth, bud, games, base, chunk, extra in mh.CELLS:
    a, b = wins.get((cname, "bon"), {}), wins.get((cname, "base"), {})
    for g in sorted(set(a) & set(b)):
        if a[g] > b[g]:
            out.write("B %s %d %d\n" % (cname, g, base + g)); n += 1
print(n, "games")
