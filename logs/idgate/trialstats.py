#!/usr/bin/env python3
"""trialstats.py <batch err>: start-gate trial outcomes from the [idg] census (MTG_IDG_CENSUS=1), rollout-leaf ladders (vl 0/4), budgeted."""
import re, sys, collections
BIG = 1 << 60
T = collections.defaultdict(lambda: collections.Counter())
for l in open(sys.argv[1]):
    if not l.startswith('[idg]'): continue
    d = dict(kv.split('=', 1) for kv in l.split()[1:] if '=' in kv)
    if int(d['vl']) not in (0, 4) or int(d['rem']) >= BIG: continue
    arm = d['j'].split('__')[0]; fit = int(d['fit']); st = int(d['st']); act = int(d['act']); rem = int(d['rem'])
    c = T[arm]; c['consult'] += 1
    if fit == 0: c['refused'] += 1; c['refused_rem'] += rem
    if fit == 2:
        c['trial'] += 1; c['trial_rem'] += rem
        if st == 0: c['done'] += 1; c['done_units'] += act; c['better'] += int(d['w']) < int(d['pw'])
        else: c['abort'] += 1; c['abort_units'] += act; c['rescued'] += (st == 2)
for arm, c in sorted(T.items()):
    print(arm, dict(c))
    if c['trial']:
        print(f"  trials {c['trial']}: finished {c['done']} ({c['done']/c['trial']:.1%}, {c['better']} with a better win), aborted {c['abort']} ({c['rescued']} rescued)")
        print(f"  refused-pass budget (rem at the gate) {c['trial_rem']:,} units: {c['done_units']:,} ({c['done_units']/c['trial_rem']:.1%}) spent in FINISHED passes, {c['abort_units']:,} ({c['abort_units']/c['trial_rem']:.1%}) in aborted trials")
