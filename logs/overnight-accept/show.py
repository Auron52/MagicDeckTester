"""Print the old/new case-setting trace of one game side by side. Usage: show.py <stem> <gi> [maxturn]"""
import sys
sys.path.insert(0, '/workspaces/MagicDeckTester/.claude/worktrees/agent-a1088baf277f40b58/test')
import explain_game as eg
A = '/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent'
stem, gi = sys.argv[1], sys.argv[2]
mx = int(sys.argv[3]) if len(sys.argv) > 3 else 99
o = eg.parse_log(f'{A}/trold/traces/{stem}_tr_gi{gi}.json')
n = eg.parse_log(f'{A}/trnew/traces/{stem}_tr_gi{gi}.json')
print('opening old:', ', '.join(o['opening']))
print('opening new:', ', '.join(n['opening']))
print('win', o['win'], n['win'])
for t in sorted(set(o['turns']) | set(n['turns'])):
    if t > mx:
        break
    for lab, g in (('old', o), ('new', n)):
        e = g['turns'].get(t)
        if e:
            print(f'T{t} {lab}: draws={e["draws"]} | {eg.turn_str(e)}'[:400])
