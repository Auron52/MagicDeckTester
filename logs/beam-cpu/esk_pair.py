#!/usr/bin/env python3
"""Pair per-game units ([gdg]) and per-escalation [esk] lines between arm A (beam) and arm N (no beam).

usage: esk_pair.py <batch.err> [armA_prefix=cg_A30] [armN_prefix=cg_N30]
Job names are <prefix>_o<offset>; games pair on (offset, gi)."""
import re, sys, collections

err = sys.argv[1]
pa = sys.argv[2] if len(sys.argv) > 2 else "cg_A30"
pn = sys.argv[3] if len(sys.argv) > 3 else "cg_N30"
gdg = {}
esk = collections.defaultdict(list)
kv = re.compile(r"(\w+)=(\S+)")
for ln in open(err, errors="replace"):
    if ln.startswith("[gdg]"):
        d = dict(kv.findall(ln))
        gdg[(d["job"], int(d["gi"]))] = (int(d["u"]), d["dg"], int(d["wt"]))
    elif ln.startswith("[esk]"):
        d = dict(kv.findall(ln))
        esk[(d["job"], int(d["gi"]))].append(d)

def arm_of(job):
    for p, a in ((pa, "A"), (pn, "N")):
        if job.startswith(p + "_o") or job == p:
            return a
    return None

games = collections.defaultdict(dict)
for (job, gi), v in gdg.items():
    a = arm_of(job)
    if a: games[(job.rsplit("_o", 1)[-1], gi)][a] = v
tot = {"A": 0, "N": 0}; diffs = []; same_dg = 0; n = 0
for gi, d in sorted(games.items()):
    if "A" in d and "N" in d:
        n += 1; tot["A"] += d["A"][0]; tot["N"] += d["N"][0]
        same_dg += d["A"][1] == d["N"][1]
        diffs.append((d["N"][0] - d["A"][0], gi, d["A"][0], d["N"][0], d["A"][2], d["N"][2]))
print(f"paired games {n}: units A={tot['A']} N={tot['N']} ratio={tot['N']/max(1,tot['A']):.3f} same-digest={same_dg}")
diffs.sort(reverse=True)
print("top N-A unit deltas (delta gi uA uN wtA wtN):")
for r in diffs[:12]: print("  ", r)
print("most negative:")
for r in diffs[-5:]: print("  ", r)
# escalation totals per arm
for a, p in (("A", pa), ("N", pn)):
    eu = bu = cnt = 0; lim = 0; by_t = collections.Counter(); hc = collections.Counter(); taken = 0
    for (job, gi), lst in esk.items():
        if arm_of(job) != a: continue
        for d in lst:
            cnt += 1; eu += int(d["eu"]); bu += int(d["bu"]); lim += int(d["lim"])
            by_t[d["target"]] += 1; hc[d["hcom"]] += 1; taken += int(d["taken"])
    print(f"arm {a}: escalations={cnt} eu(sum gamework)={eu} bu(sum esc budget)={bu} lim(sum)={lim} taken={taken} "
          f"target={dict(by_t)} hcom={dict(hc)}")
