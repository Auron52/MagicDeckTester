#!/usr/bin/env python3
# blost.py <err>...: payments that spend an owed floating {B} on generic (before B>0, the bill has no {B} pip, the
# float after holds fewer {B} than before and than the line still owes), grouped; with the preceding [unseen-scarce]
# line when there is one (MTG_UNSEEN_SCARCE_TRACE).
import re, sys
from collections import Counter
P = re.compile(r'\[ds2pay\] (.*?) paid=(\d) fc=\d cost g(\d+) B(\d+) R(\d+) G(\d+) lu g(\d+) B(\d+) R(\d+) G(\d+) .*?unseen=(\d) hook=(\d) before B(\d+) R(\d+) G(\d+) fl B(\d+) R(\d+) G(\d+) C\d+ W\d+ ->(.*)')
for f in sys.argv[1:]:
    c = Counter(); prev = ''
    for l in open(f, errors='ignore'):
        if l.startswith('[unseen-scarce]'): prev = l.split(']', 1)[1].strip(); continue
        m = P.match(l)
        if not m: continue
        g = m.groups()
        name, paid = g[0], g[1]
        cB = int(g[3]); luB = int(g[7]); bB = int(g[12]); aB = int(g[15])
        if paid == '1' and bB > 0 and cB == 0 and aB < bB and aB < luB:
            unt = ' '.join(x for x in g[18].split() if not x.endswith('T'))
            c[(name, 'cost g%s R%s' % (g[2], g[4]), 'lu g%s B%s R%s' % (g[6], g[7], g[8]), 'B%d R%s -> B%d R%s' % (bB, g[13], aB, g[16]), 'u' + g[10], 'h' + g[11], unt, prev)] += 1
        prev = ''
    print('#####', f.split('/')[-1], sum(c.values()))
    for k, v in c.most_common(6): print('  x%d' % v, ' | '.join(k))
