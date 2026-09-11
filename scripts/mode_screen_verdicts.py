#!/usr/bin/env python3
"""Consolidate every mode screen into ONE verdict table.  scripts/mode_screen_verdicts.py [--csv]

Reads all (winsdir, run.out) screens, computes per-(deck,arm) delta avg-win-turn with a per-block
se, outcome better/worse counts, units and wall ratios and digest agreement, then CLASSIFIES.

Cost is judged on UNITS, not wall.  Wall is contaminated here: these screens share a box, and the
plan-cache pool is now GLOBAL, so one arm's monster enumeration can draw pool another arm wanted.
Units are the work actually done.  Where units say 1.000x and wall says 0.82x, that is noise --
breaching `cap` and `capctl` are both byte-identical at units 1.000x, so neither is a real speedup.

Verdict rules (Delta < 0 == fewer turns to win == BETTER):
  REJECT     quality significantly worse (mean > 2se), OR null quality bought with >1% more units
  CLEAN WIN  quality better-or-equal AND units not worse  (free gain, or free work cut)
  TRADE      quality significantly better but units worse -- needs a ruling
  NULL       no significant movement either way
"""
import os, glob, re, math, sys

SCREENS = [
    ('logs/modes/wins',      'logs/modes/run.out',       'modes'),
    ('logs/modes/wins',      'logs/modes/run_frac.out',  'frac'),
    ('logs/modes/wins',      'logs/modes/run_fix.out',   'fix'),
    ('logs/modes/wins',      'logs/modes/run_nlcm.out',  'nlcm'),
    ('logs/modes/wins',      'logs/modes/run_trust.out', 'trust'),
    ('logs/modes/wins',      'logs/modes/run_kitty.out', 'kitty'),
    ('logs/modes/winsmel',   'logs/modes/run_mel.out',   'mel'),
    ('logs/modes/winsfinal', 'logs/modes/run_final.out', 'FINAL'),
]

def rd(p):
    d = {}
    for ln in open(p):
        f = ln.split()
        if len(f) >= 2: d[int(f[0])] = int(f[1])
    return d

def ru(p):
    return sum(int(l.split()[1]) for l in open(p)) if os.path.exists(p) else 0

PAT = re.compile(r'^(.*)_([A-Za-z0-9]+)_s(\d+)\.wins$')

def screen(wd, run):
    if not os.path.exists(run): return []
    ms = {}
    for ln in open(run):
        m = re.match(r'(\S+): played=(\d+) avg=([\d.]+) digest=(\w+) ms=(\d+)', ln)
        if m: ms[m.group(1)] = dict(avg=float(m.group(3)), dig=m.group(4), ms=int(m.group(5)))
    jobs = {}
    for p in glob.glob(wd + '/*.wins'):
        m = PAT.match(os.path.basename(p))
        if m and m.group(0)[:-5] in ms:
            jobs.setdefault(m.group(1), {}).setdefault(m.group(2), {})[m.group(3)] = p
    out = []
    for deck, arms in sorted(jobs.items()):
        if 'ship' not in arms: continue
        for tag in sorted(a for a in arms if a != 'ship'):
            ds = []; B = W = same = 0; us = uc = mS = mC = 0
            for s, p in sorted(arms[tag].items()):
                sp = arms['ship'].get(s)
                if not sp: continue
                ks, kc = os.path.basename(sp)[:-5], os.path.basename(p)[:-5]
                ds.append(ms[kc]['avg'] - ms[ks]['avg'])
                us += ru(sp[:-5] + '.units'); uc += ru(p[:-5] + '.units')
                mS += ms[ks]['ms']; mC += ms[kc]['ms']
                if ms[ks]['dig'] == ms[kc]['dig']: same += 1
                a, b = rd(sp), rd(p)
                for gi in set(a) & set(b):
                    ka = a[gi] if a[gi] > 0 else 99
                    kb = b[gi] if b[gi] > 0 else 99
                    if kb < ka: B += 1
                    elif kb > ka: W += 1
            if not ds: continue
            mean = sum(ds) / len(ds)
            sd = math.sqrt(sum((x - mean) ** 2 for x in ds) / (len(ds) - 1)) if len(ds) > 1 else 0.0
            se = sd / math.sqrt(len(ds)) if len(ds) > 1 else 0.0
            u = uc / us if us else 0.0
            out.append(dict(deck=deck, arm=tag, n=len(ds), mean=mean, se=se, B=B, W=W,
                            units=u, wall=(mC / mS if mS else 0.0), same=same))
    return out

def verdict(r):
    """Strongest evidence first.  B==W==0 means NO GAME changed outcome -- that PROVES neutrality
    outright and needs no significance test (the digest-equality lesson); cost then decides alone.
    Only where outcomes did move do we fall back to the noisy mean, and there a wide band is
    reported as LOW POWER rather than asserted as a null -- `melira single` at +0.0032 +/- 0.0034
    cannot be called neutral just because 0.0032 < 2se."""
    cheaper  = r['units'] < 0.99
    costlier = r['units'] > 1.01
    if r['B'] == 0 and r['W'] == 0:                      # outcomes provably unchanged
        if cheaper:  return 'CLEAN WIN'                  # free work cut
        if costlier: return 'REJECT'                     # pure waste, identical play
        return 'NULL'                                    # no-op
    sig    = r['se'] > 0 and abs(r['mean']) > 2 * r['se']
    better = sig and r['mean'] < 0
    worse  = sig and r['mean'] > 0
    if worse:               return 'REJECT'
    if better and costlier: return 'TRADE'
    if better:              return 'CLEAN WIN'
    # Outcomes MOVED but quality is not significantly better.  A CLEAN WIN must not regress on ANY
    # axis, so a cell where some games got worse does NOT qualify however cheap it is -- labelling
    # `dragons nlcm` (0 better / 3 worse, 0.398x units) a clean win is exactly the error of
    # classifying a bundle by its cleanest part.  These are cost-for-quality trades.
    if cheaper:             return 'CHEAPER, Q UNPROVEN'
    if costlier:            return 'REJECT'
    if 2 * r['se'] > 0.002: return 'LOW POWER'
    return 'NULL'

rows = []
for wd, run, name in SCREENS:
    for r in screen(wd, run):
        r['screen'] = name; rows.append(r)

if '--csv' in sys.argv:
    print('screen,deck,arm,blocks,d_turn,se,better,worse,units,wall,digest_same,verdict')
    for r in rows:
        print('%s,%s,%s,%d,%.4f,%.4f,%d,%d,%.3f,%.3f,%d/%d,%s' % (
            r['screen'], r['deck'], r['arm'], r['n'], r['mean'], r['se'], r['B'], r['W'],
            r['units'], r['wall'], r['same'], r['n'], verdict(r)))
    raise SystemExit

for v in ('CLEAN WIN', 'TRADE', 'CHEAPER, Q UNPROVEN', 'LOW POWER', 'REJECT', 'NULL'):
    sel = [r for r in rows if verdict(r) == v]
    if not sel: continue
    print('\n################ %s  (%d)' % (v, len(sel)))
    print('  %-8s %-13s %-10s %10s %8s %5s %5s %8s %9s' %
          ('screen', 'deck', 'arm', 'd_turn', '+/-se', 'bett', 'wors', 'units', 'digest'))
    key = (lambda r: r['units']) if v == 'CLEAN WIN' else (lambda r: r['mean'])
    for r in sorted(sel, key=key):
        print('  %-8s %-13s %-10s %+10.4f %8.4f %5d %5d %7.3fx %6d/%-3d' % (
            r['screen'], r['deck'], r['arm'], r['mean'], r['se'], r['B'], r['W'],
            r['units'], r['same'], r['n']))
