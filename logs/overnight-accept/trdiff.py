"""Old-vs-new case-setting traces: kept-hand / draw divergence per game, joined to the d8b0 class.
Usage: trdiff.py [-v] [deckprefix ...]"""
import sys, os, collections
sys.path.insert(0, '/workspaces/MagicDeckTester/.claude/worktrees/agent-a1088baf277f40b58/test')
import explain_game as eg
A = '/workspaces/MagicDeckTester/build/scratch/claude_ovn1120/agent'
cls = {}
for l in open(A + '/d8b0.classified'):
    f = l.split()
    cls[f[0]] = l.split(' d8b0 ')[1].split()[2] if ' d8b0 ' in l else '?'
args = [a for a in sys.argv[1:] if not a.startswith('-')]
verbose = '-v' in sys.argv
tot = collections.Counter()
per = collections.defaultdict(collections.Counter)
for l in open(A + '/d8b0.idx'):
    stem, key, gi, o, n = l.split()
    deck = key.split('_')[0]
    if args and deck not in args:
        continue
    po = f'{A}/trold/traces/{stem}_tr_gi{gi}.json'
    pn = f'{A}/trnew/traces/{stem}_tr_gi{gi}.json'
    if not (os.path.exists(po) and os.path.exists(pn)):
        div = 'notrace'
        first = ''
    else:
        old, new = eg.parse_log(po), eg.parse_log(pn)
        first = ''
        if sorted(old['opening']) != sorted(new['opening']):
            div = 'KEPT-DIFF'
        else:
            div = 'draws-same'
            for (ot, oc), (nt, nc) in zip(old['draws'], new['draws']):
                if oc != nc:
                    div = f'DRAW-DIFF-T{ot}'
                    break
            if div == 'draws-same':
                for tn in sorted(set(old['turns']) | set(new['turns'])):
                    a = eg.turn_str(old['turns'][tn]) if tn in old['turns'] else '-'
                    b = eg.turn_str(new['turns'][tn]) if tn in new['turns'] else '-'
                    if a != b:
                        first = f'T{tn} old: {a} || new: {b}'
                        break
    c = cls.get(stem, '?')
    kind = 'KEPT-DIFF' if div == 'KEPT-DIFF' else ('DRAW-DIFF' if div.startswith('DRAW') else div)
    tot[(c, kind)] += 1
    per[deck][(c, kind)] += 1
    if verbose and kind not in ('KEPT-DIFF',):
        print(f'{stem:44s} {o}->{n} {c:20s} {div:12s} {first[:260]}')
print('TOTAL', dict(tot))
for d in sorted(per):
    print(f'  {d:16s}', dict(per[d]))
