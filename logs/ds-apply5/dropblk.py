#!/usr/bin/env python3
# dropblk.py <err> [nth]: the engine-apply block (MTG_TAPDBG + MTG_DS2_PAYDBG trace) behind the nth APPLY-DROP flag:
# the last ORACLE-ENGINE-APPLY of the flag's plan before it, its taps and payments.
import re, sys
fn = sys.argv[1]; nth = int(sys.argv[2]) if len(sys.argv) > 2 else 1
L = open(fn, errors='ignore').read().split('\n')
k = 0; fl = None
for i, l in enumerate(L):
    if l.startswith('[mana-oracle] APPLY-DROP seed'):
        k += 1
        if k == nth: fl = i; break
plan = re.search(r' (T\d+) m\d plan=\{(.*?)\} \|', L[fl])
key = '[tapdbg] ORACLE-ENGINE-APPLY %s plan={%s} root' % (plan.group(1), plan.group(2))
st = max(i for i in range(fl) if L[i].startswith(key))
print(L[fl][:300])
for l in L[st:fl]:
    if l.startswith('[tapdbg] ORACLE-ENGINE-APPLY END'): print(l[:300]); break
    if l.startswith('[ds2pay]') or l.startswith('[tapdbg] tap ') or l.startswith('[tapdbg] ORACLE') or 'TFCD cost' in l: print(l[:420])
