"""Per lever and deck: how many on/off job pairs have DIFFERENT digests (play perturbed at all)."""
import re, sys
from collections import defaultdict
out = sys.argv[1]
dig = {}
for line in open(out + '/batch.out', errors='replace'):
    m = re.match(r'(fb|ft)_(\w+)_(on|off)\.c(\d+): played=\d+ avg=[0-9.]+ digest=([0-9a-f]+)', line)
    if m:
        dig[(m.group(1), m.group(2), m.group(4), m.group(3))] = m.group(5)
diff = defaultdict(lambda: [0, 0])
for (k, tag, c, arm), d in dig.items():
    if arm != 'on':
        continue
    off = dig.get((k, tag, c, 'off'))
    if off is None:
        continue
    diff[(k, tag)][1] += 1
    if off != d:
        diff[(k, tag)][0] += 1
for (k, tag), (n, tot) in sorted(diff.items()):
    if n:
        print('%s %-24s %d / %d chunks differ' % (k, tag, n, tot))
print('lever totals:', {k: sum(v[0] for (kk, _), v in diff.items() if kk == k) for k in ('fb', 'ft')})
