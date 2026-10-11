#!/usr/bin/env python3
"""Replace the AttackTapCtx .. AttackTapLoss block with the stage-2 re-projection + Cannons helpers."""
import os, sys
W = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12'
p = os.path.join(W, 'src/ai/TurnSolver.cpp')
s = open(p).read()
start = s.index('struct AttackTapCtx\n{')
end = s.index('// Pre-computed per-permanent on-cast trigger: damage dealt to the caster when they')
new = open(os.path.join(W, 'logs/attack_fix/stage2_block.cpp')).read()
s = s[:start] + new + s[end:]
open(p, 'w').write(s)
print('patched', start, end)
