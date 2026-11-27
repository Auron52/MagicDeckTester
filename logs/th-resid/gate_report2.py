#!/usr/bin/env python3
# gate_report2.py <outroot> [tag ...]: per-binary issue counts of the Treasure Hunt oracle gate (run_gate2.sh), with the
# DROP-COVERED verdicts the new oracle rules made out of would-be issues counted apart (so "before" = issue + moved).
#   AD/ADO-won: MTG_ORACLE_DROP_WON; AD-undecl-cov: MTG_ORACLE_UNDECL_MENU_COVER. ENUM-GAP's provider prune emits no line.
import os, re, sys
from collections import Counter
root = sys.argv[1]
tags = sys.argv[2:] or sorted(d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d)))
pat = re.compile(r'^\[mana-oracle\] (\S+) seed=(\d+) job=\S+ gi=-?\d+ (T\d+ m\d) plan=')
cols = ['APPLY-DROP', 'AD-undecl', 'APPLY-DROP-ORDER', 'ENUM-GAP', 'ORACLE-GAP', 'HIDDEN-INFO',
        'AD-won', 'ADO-won', 'AD-undecl-cov', 'DROP-COVERED']
print('%-10s %s' % ('binary', ' '.join('%14s' % c for c in cols)))
for t in tags:
    c = Counter()
    f = os.path.join(root, t, 'flags.err')
    if not os.path.exists(f):
        continue
    for line in open(f, errors='ignore'):
        m = pat.match(line)
        if not m:
            continue
        cls = m.group(1)
        c[cls] += 1
        if cls == 'APPLY-DROP' and 'not declared' in line:
            c['AD-undecl'] += 1
        if cls == 'DROP-COVERED':
            if '(would-be APPLY-DROP-ORDER)' in line: c['ADO-won'] += 1
            elif 'wins here' in line and '(would-be APPLY-DROP)' in line: c['AD-won'] += 1
            elif 'was not declared in the plan, and every board' in line: c['AD-undecl-cov'] += 1
    print('%-10s %s' % (t, ' '.join('%14d' % c[k] for k in cols)))
