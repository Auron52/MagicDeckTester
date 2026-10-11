#!/usr/bin/env python3
"""Stage lever M's commit: SpellEffects.h, the Hinata test file, and HeuristicArm.h WITHOUT the
RESTRICTOR_ORDER_EXACT slot (index-only blob; the working tree keeps both slots for lever R's commit)."""
import subprocess
WT = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8'


def git(*args, inp=None):
    return subprocess.run(['git', '-C', WT] + list(args), input=inp, capture_output=True, text=True, check=True).stdout


h = open(WT + '/src/ai/HeuristicArm.h').read()
lines = h.split('\n')
keep = [l for l in lines if not (l.lstrip().startswith('RESTRICTOR_ORDER_EXACT,') or l.strip() == '"MTG_RESTRICTOR_ORDER_EXACT",')]
assert len(lines) - len(keep) == 2, len(lines) - len(keep)
blob = git('hash-object', '-w', '--stdin', '--path', 'src/ai/HeuristicArm.h', inp='\n'.join(keep)).strip()
git('update-index', '--cacheinfo', '100644,%s,src/ai/HeuristicArm.h' % blob)
git('add', 'src/core/SpellEffects.h', 'test/unit/test_hinata_crackle_count.cpp')
print(git('diff', '--cached', '--stat'))
print(git('diff', '--cached', 'src/ai/HeuristicArm.h'))
