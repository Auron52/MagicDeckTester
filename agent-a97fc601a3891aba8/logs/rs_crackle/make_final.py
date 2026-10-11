#!/usr/bin/env python3
"""The final pooled run for MTG_HINATA_CASTABLE_MANA + MTG_RESTRICTOR_ORDER_EXACT (reject-only form).

    python3 make_final.py new <dir>   -> ONE manifest for the NEW binary:
        held-out Hinata2 (seeds 8300001+, the make_cm cells): arms base (both on) / roff (R off) everywhere,
            moff (M off) on the depth-only cells (d0, 2HG);
        control (seeds 8340001+, d0 1000): M on/off under MTG_HINATA_SUBSET_CREDIT=1 and under
            MTG_HINATA_HOLD_NEEDS_HINATA=1 -- the contexts where M is consumed;
        identity, new side: Hinata (both levers OFF via flags) + burn / dragonstorm / stompy (defaults).
    python3 make_final.py old <dir>   -> the identity jobs WITHOUT flags, for the 3d64d9a6 binary.
    python3 make_final.py analyse <newdir> <olddir>
"""
import json, math, os, re, sys
from collections import defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_cm as mc

WT = mc.WT
M, R = mc.M, mc.R
SC, NH = "MTG_HINATA_SUBSET_CREDIT", "MTG_HINATA_HOLD_NEEDS_HINATA"


def heldout():
    jobs = []
    for cname, depth, bud, games, base, chunk, extra in mc.CELLS:
        arms = [("base", {M: True, R: True}), ("roff", {M: True, R: False})]
        if depth == 0:
            arms.append(("moff", {M: False, R: True}))
        for c in range(0, games, chunk):
            for arm, fl in arms:
                j = {"name": "%s_%s.c%d" % (cname, arm, c), "deck": mc.DECK, "profile": mc.PROF,
                     "games": min(chunk, games - c), "seed": base + c, "game_index": c, "budget_ms": bud,
                     "flags": dict(fl)}
                if depth is not None:
                    j["depth"] = depth; j["ignore_play_profile"] = True
                j.update(extra)
                jobs.append(j)
    return jobs


def control():
    jobs = []
    for ctx, fl in (("ctlsc", {SC: True}), ("ctlnh", {NH: True})):
        for c in range(0, 1000, 50):
            for arm, v in (("on", True), ("off", False)):
                f = dict(fl); f[M] = v
                jobs.append({"name": "%s_%s.c%d" % (ctx, arm, c), "deck": mc.DECK, "profile": mc.PROF, "games": 50,
                             "seed": 8340001 + c, "game_index": c, "budget_ms": 0, "depth": 0,
                             "ignore_play_profile": True, "flags": f})
    return jobs


def identity(with_flags):
    jobs = []
    cap = {"ship": 50, "d3": 50, "2hg": 400, "d0": 1000}
    for cname, depth, bud, games, base, chunk, extra in mc.CELLS:
        for c in range(0, cap[cname], chunk):
            j = {"name": "idn_%s.c%d" % (cname, c), "deck": mc.DECK, "profile": mc.PROF, "games": chunk,
                 "seed": base + c, "game_index": c, "budget_ms": bud}
            if with_flags:
                j["flags"] = {M: False, R: False}
            if depth is not None:
                j["depth"] = depth; j["ignore_play_profile"] = True
            j.update(extra)
            jobs.append(j)
    for j in mc.other_jobs():
        j = dict(j); j["name"] = "oth_" + j["name"]
        jobs.append(j)
    return jobs


def write(out, jobs):
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("wrote %s: %d jobs, %d games" % (out, len(jobs), sum(j["games"] for j in jobs)))


def analyse(dnew, dold):
    # held-out
    print("##### HELD-OUT (seeds 8300001+)")
    mc.analyse(dnew)
    # control
    w = defaultdict(dict)
    pat = re.compile(r"\[win\] job=(ctl\w\w)_(on|off)\.c(\d+) gi=(\d+) wt=(-?\d+)")
    for line in open(os.path.join(dnew, "batch.err"), errors="ignore"):
        m = pat.search(line)
        if m:
            t = int(m.group(5)); w[(m.group(1), m.group(2))][int(m.group(4))] = 9 if t < 0 else t
    print("\n##### CONTROL: CASTABLE_MANA on vs off where it is consumed (d0, seeds 8340001+)")
    for ctx, label in (("ctlsc", "MTG_HINATA_SUBSET_CREDIT=1"), ("ctlnh", "MTG_HINATA_HOLD_NEEDS_HINATA=1")):
        a, b = w[(ctx, "on")], w[(ctx, "off")]
        gis = sorted(set(a) & set(b))
        if not gis:
            continue
        d = [a[g] - b[g] for g in gis]
        n = len(d); mean = sum(d) / n
        se = math.sqrt(sum((v - mean) ** 2 for v in d) / max(1, n - 1) / n)
        print("%-32s n=%d on-off=%+d mean=%+.4f t=%.2f faster=%d slower=%d" % (
            label, n, sum(d), mean, mean / se if se else 0, sum(1 for v in d if v < 0), sum(1 for v in d if v > 0)))
        print("   slower (gi off on seed):", [(g, b[g], a[g], 8340001 + g) for g in gis if a[g] > b[g]])
    # identity
    _, _, A = mc.parse(dnew)
    _, _, B = mc.parse(dold)
    for pre, label in (("idn_", "Hinata, both levers =0 vs 3d64d9a6"), ("oth_", "burn/dragonstorm/stompy, levers ON vs 3d64d9a6")):
        keys = sorted(k for k in set(A) & set(B) if k.startswith(pre))
        diff = [k for k in keys if A[k] != B[k]]
        print("\n##### IDENTITY: %s -- jobs %d, games %s, mismatches %d %s" % (label, len(keys), "-", len(diff), diff))


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "new":
        write(sys.argv[2], heldout() + control() + identity(True))
    elif cmd == "old":
        write(sys.argv[2], identity(False))
    elif cmd == "analyse":
        analyse(sys.argv[2], sys.argv[3])
