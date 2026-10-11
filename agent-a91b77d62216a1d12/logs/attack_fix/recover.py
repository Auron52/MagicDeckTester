#!/usr/bin/env python3
"""recover.py mk <outdir> <arm>=<LEVER,LEVER..|-> ...   build ONE pooled manifest: every game x every arm
   recover.py report <outdir>                           per-game table from <outdir>/err ([win] lines)

An arm's lever list names the levers forced OFF (false) for that arm; '-' = none (all defaults ON).
Games come from games.txt (deck cell gi seed old new src) beside this script; GAMES=<file> overrides,
DECKS=fivecolour,stompy filters."""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DECK = {
    'fivecolour': 'decks/FiveColour/FiveColour',
    'stompy': 'decks/StompySurprise/StompySurprise',
    'selesnya': 'decks/SelesnyaLifegain/SelesnyaLifegain',
    'bruna': 'decks/Bruna/Bruna',
    'snow': 'decks/Snow/Snow',
}


def games():
    fn = os.environ.get('GAMES', os.path.join(HERE, 'games.txt'))
    keep = set(filter(None, os.environ.get('DECKS', '').split(',')))
    out = []
    for ln in open(fn):
        p = ln.split()
        if len(p) < 6:
            continue
        if keep and p[0] not in keep:
            continue
        out.append((p[0], p[1], int(p[2]), int(p[3]), p[4], p[5], p[6] if len(p) > 6 else ''))
    return out


def cell_fields(cell):
    if cell == 'd0':
        return {'depth': 0, 'budget_ms': 0, 'ignore_play_profile': True}
    if cell == 'd3':
        return {'depth': 3, 'budget_ms': 10, 'ignore_play_profile': True}
    if cell == 'ship':
        return {'budget_ms': 20}
    raise SystemExit('bad cell ' + cell)


def mk(outdir, arms):
    os.makedirs(outdir, exist_ok=True)
    jobs = []
    for (deck, cell, gi, seed, old, new, src) in games():
        for arm, levers in arms:
            j = {'name': '%s_%s_g%d_%s' % (deck, cell, gi, arm), 'deck': DECK[deck] + '.cod',
                 'profile': DECK[deck] + '.profile.json', 'games': 1, 'seed': seed, 'game_index': gi}
            j.update(cell_fields(cell))
            if levers:
                j['flags'] = {l: False for l in levers}
            jobs.append(j)
    json.dump({'jobs': jobs}, open(os.path.join(outdir, 'm.json'), 'w'))
    json.dump(arms, open(os.path.join(outdir, 'arms.json'), 'w'))
    print('%d jobs -> %s' % (len(jobs), os.path.join(outdir, 'm.json')))


def report(outdir):
    arms = [a for a, _ in json.load(open(os.path.join(outdir, 'arms.json')))]
    wt = {}
    for ln in open(os.path.join(outdir, 'err'), errors='replace'):
        m = re.search(r'\[win\] job=(\S+) gi=(\d+) wt=(-?\d+)', ln)
        if m:
            w = int(m.group(3))
            wt[m.group(1)] = w if w > 0 else 9
    hdr = '%-10s %-4s %5s %-7s %3s %3s  ' % ('deck', 'cell', 'gi', 'src', 'old', 'new') + ' '.join('%6s' % a for a in arms)
    print(hdr)
    tot = {a: 0 for a in arms}
    rec = {a: [0, 0] for a in arms}
    for (deck, cell, gi, seed, old, new, src) in games():
        row = []
        for a in arms:
            v = wt.get('%s_%s_g%d_%s' % (deck, cell, gi, a))
            row.append('%6s' % ('?' if v is None else v))
            if v is not None:
                tot[a] += v
                rec[a][0] += 1
                if v <= int(old):
                    rec[a][1] += 1
        print('%-10s %-4s %5d %-7s %3s %3s  ' % (deck, cell, gi, src, old, new) + ' '.join(row))
    print('sum: ' + ' '.join('%s=%d' % (a, tot[a]) for a in arms))
    print('reached old-or-better: ' + ' '.join('%s=%d/%d' % (a, rec[a][1], rec[a][0]) for a in arms))


if __name__ == '__main__':
    if sys.argv[1] == 'mk':
        arms = []
        for spec in sys.argv[3:]:
            a, l = spec.split('=', 1)
            arms.append((a, [] if l == '-' else l.split(',')))
        mk(sys.argv[2], arms)
    else:
        report(sys.argv[2])
