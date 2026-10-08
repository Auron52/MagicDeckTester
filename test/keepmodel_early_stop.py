#!/usr/bin/env python3
"""EARLY STOP for the keep/bottom A/B (test/keepmodel_exhaustive_ab.sh), user 2026-10-08.

Polled while the pooled batch runs. Reads the per-job result lines ("s<seed>@<tag>: played=N avg=X ...")
flushed as each job finishes, pairs the seeds both arms have finished, and exits 0 -- printing why --
when the run may stop now; exit 1 means keep going.

ONE-SIDED, BY DESIGN: it stops only when arm B (the candidate: the exhaustive keep, the blind
bottoming, the new table) is CLEARLY BETTER. A candidate that looks worse always runs the full planned
sample, because a reject quarantines a live profile and scripts/mullgen.sh's minimum-sample rule exists
for exactly that direction (a 2-seed smoke once deactivated a real profile on noise).

The rule: at least MIN_PAIRS complete seed pairs, and the paired t = mean(B-A)/se <= -T. The per-seed
deltas are means of 1000 paired games, so they are close to normal and t has k-1 df. With T=8 checked
at every k from 4 to 16, the chance of EVER stopping when B is not actually better is at most 0.31%
(union bound: P(t3 > 8) = 0.0020, P(t4 > 8) = 0.0007, P(t5 > 8) = 0.0003, ...). A keep table that beats
the static rules by 0.1t or more (per-seed sd ~0.02) stops after 4 pairs; one at 0.05t after ~11; a
close call runs the full 16.

The delta reported at an early stop comes from fewer seeds and, like any early-stopped estimate, is
somewhat exaggerated on average. The DECISION is what the rule guarantees, not the size.

Only the longest PREFIX of the seed list (manifest order) that both arms have finished is judged, so
which jobs happen to finish first -- cheap ones, under a small game count -- cannot select the sample.
With the default 1000-game jobs the batch works through about one job at a time and this is all of the
finished pairs anyway. The judged seeds are written to <seeds-out> so the report uses exactly them.

usage: keepmodel_early_stop.py <batch.log> <A-tag> <B-tag> <games> <min-pairs> <T> <min-abs>
                               "<seed seed ...>" <seeds-out>
"""
import math
import re
import sys

log, A, B = sys.argv[1], sys.argv[2], sys.argv[3]
games, min_pairs = int(sys.argv[4]), int(sys.argv[5])
t_stop, min_abs = float(sys.argv[6]), float(sys.argv[7])
order, seeds_out = sys.argv[8].split(), sys.argv[9]

res = {A: {}, B: {}}
pat = re.compile(r"s(\d+)@(\S+): played=(\d+) avg=([\d.]+)")
try:
    with open(log) as f:
        for ln in f:
            m = pat.match(ln)
            # A short job (abandoned / condemned games) is left out of the decision: conservative.
            if m and m.group(2) in res and int(m.group(3)) == games:
                res[m.group(2)][m.group(1)] = float(m.group(4))
except FileNotFoundError:
    sys.exit(1)

used = []
for s in order:
    if s not in res[A] or s not in res[B]:
        break
    used.append(s)
d = [res[B][s] - res[A][s] for s in used]
k = len(d)
if k < max(2, min_pairs):
    sys.exit(1)
mean = sum(d) / k
sd = math.sqrt(sum((x - mean) ** 2 for x in d) / (k - 1))
se = sd / math.sqrt(k)
t = mean / se if se > 0 else (-math.inf if mean < 0 else math.inf)
if mean < 0 and t <= -t_stop and -mean >= min_abs:
    better = sum(1 for x in d if x < 0)
    with open(seeds_out, "w") as f:
        f.write("\n".join(used) + "\n")
    print(f"{k} seed pairs: delta {mean:+.4f}t (se {se:.4f}, t {t:.1f}; {B} better on {better}/{k}) "
          f"-- clears t <= -{t_stop:g}, so {B} is clearly better; the rest of the sample is not needed")
    sys.exit(0)
sys.exit(1)
