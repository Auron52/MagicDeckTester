#!/usr/bin/env python3
"""Remove the TEMP-DBG-ATKPROJ block (investigation only) from TurnSolver.cpp."""
W = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12'
p = W + '/src/ai/TurnSolver.cpp'
s = open(p).read()
a = s.index('        {   // TEMP-DBG-ATKPROJ')
b = s.index('        // A NON-DAMAGE win this turn (Dimensional Infiltrator decking them out). Rides the same')
blk = s[a:b]
assert blk.count('TEMP-DBG-ATKPROJ') == 1 and blk.rstrip().endswith('}'), blk[-200:]
s = s[:a] + s[b:]
assert 'TEMP-DBG' not in s and 'MTG_DBG_ATKPROJ' not in s
open(p, 'w').write(s)
print('removed %d chars' % len(blk))
