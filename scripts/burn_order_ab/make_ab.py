#!/usr/bin/env python3
"""burn: the USER's cast order + main-2 rule + the spectacle plan credit (2026-10-15), paired A/B.

docs/design/burn-cast-order-2026-10-15.md is the record. Two binaries, so two pooled batches:
  * BASE  = the integration tip 16dfb7e4 (build/scratch/burn-order/bin/mtg.base): arm "old" on
    burn + burn2hg, plus the identity jobs for every other suite deck;
  * NEW   = this branch: every arm below on burn + burn2hg (ONE pooled batch, arms interleaved per
    chunk), plus the same identity jobs with every new lever ON.
Arms are heurarm job flags (src/ai/HeuristicArm.h), so one process carries all of them. Every arm of a
cell runs the SAME games (seed = base + gi, game_index = gi).

  python3 make_ab.py --out DIR --which base|new [--scale 1.0]
  python3 make_ab.py --analyse DIR_BASE DIR_NEW          (loss = 9; paired per game; units ratio)
  python3 make_ab.py --recovery DIR_BASE DIR_NEW --out DIR   (one job per slower game, stages 1 + 2)
"""
import argparse, json, os, re, sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # the checkout
BURN_DECK = "decks/burn/burn.txt"
BURN_PROF = "decks/burn/burn.profile.json"

O, M2, CR, NA = "MTG_BURN_ORDER", "MTG_BURN_M2_RULE", "MTG_SPECTACLE_PLAN_CREDIT", "MTG_BURN_M2_NARROW"
BF = "MTG_BURN_BLOOD_FIRST"   # the PROPOSED amendment (Searing Blood before Searing Blaze), a USER question
SO = "MTG_SPECTACLE_CREDIT_SEARCH_ONLY"   # the credit in the search's enumeration only (not Solve)
ARMS = [
    ("off",   {O: False, M2: False, CR: False, NA: False, BF: False, SO: False}),   # new binary, every lever off == base?
    ("o1",    {O: True,  M2: False, CR: False, NA: False, BF: False, SO: False}),
    ("o12",   {O: True,  M2: True,  CR: False, NA: False, BF: False, SO: False}),
    ("o123",  {O: True,  M2: True,  CR: True,  NA: False, BF: False, SO: False}),
    ("o13",   {O: True,  M2: False, CR: True,  NA: False, BF: False, SO: False}),
    ("o123n", {O: True,  M2: True,  CR: True,  NA: True,  BF: False, SO: False}),   # the narrower "played off" reading
    ("o1b",   {O: True,  M2: False, CR: False, NA: False, BF: True,  SO: False}),   # order + the Blood-first amendment
    ("o123b", {O: True,  M2: True,  CR: True,  NA: False, BF: True,  SO: False}),
    ("o123s", {O: True,  M2: True,  CR: True,  NA: False, BF: False, SO: True}),    # credit search-only
]
ALL_ON = {O: True, M2: True, CR: True, NA: False, BF: True, SO: False}

# Disjoint blocks inside 11600001..11699999 (the walk's allocation for burn). Sized above the brief's
# floor (>= 4000 shipped / 6000 d3 / 20000 d0 for burn, half for burn2hg): every cell is cheap here.
SEEDS = {("burn", "ship"): 11600001, ("burn", "d3"): 11610001, ("burn", "d0"): 11630001,
         ("burn2hg", "ship"): 11670001, ("burn2hg", "d3"): 11675001, ("burn2hg", "d0"): 11681001}
SIZES = {("burn", "ship"): 8000, ("burn", "d3"): 12000, ("burn", "d0"): 40000,
         ("burn2hg", "ship"): 4000, ("burn2hg", "d3"): 6000, ("burn2hg", "d0"): 15000}
CELL = {"ship": {"budget_ms": 20},
        "d3": {"depth": 3, "budget_ms": 10, "ignore_play_profile": True},
        "d0": {"depth": 0, "budget_ms": 0, "ignore_play_profile": True}}
CHUNK = {"ship": 25, "d3": 50, "d0": 500}
COST = {"ship": 100.0, "d3": 10.0, "d0": 1.0}
TWO_HG = {"starting_life": 30, "opponent_heads": 2}

ID_SEED = 11697001
ID_GAMES = {"ship": 20, "d3": 30, "d0": 100}


def suite_decks():
    """The suite's DECK_FILE / DECK_PROF maps (test/regression_cases.sh), minus burn."""
    txt = open(os.path.join(ROOT, "test", "regression_cases.sh")).read()
    out = {}
    for which in ("DECK_FILE", "DECK_PROF"):
        m = re.search(r"declare -A %s=\((.*?)\n\)" % which, txt, re.S)
        for line in m.group(1).splitlines():
            mm = re.match(r'\s*\[(\w+)\]=(?:"([^"]+)"|(\S+))', line)
            if mm:
                out.setdefault(mm.group(1), {})[which] = mm.group(2) or mm.group(3)
    return {k: v for k, v in out.items() if k != "burn"}


def job(name, deck, prof, cell, games, seed, gi, flags=None, extra=None):
    j = {"name": name, "deck": os.path.join(ROOT, deck), "profile": os.path.join(ROOT, prof),
         "games": games, "seed": seed, "game_index": gi}
    j.update(CELL[cell])
    if extra:
        j.update(extra)
    if flags:
        j["flags"] = dict(flags)
    return j


def build(a):
    jobs = []
    arms = [("old", None)] if a.which == "base" else ARMS
    for (deck, cell), n0 in SIZES.items():
        n = int(n0 * a.scale)
        # --floor: the searched cells at the brief's floor (half these sizes) so a 9-arm NEW batch fits the
        # fleet's ~3 h per-batch limit; d0 stays full (it is nearly free). The base ran the full sizes --
        # the analysis pairs only the games both sides played.
        if a.floor and cell != "d0":
            n //= 2
        ch = CHUNK[cell]
        extra = TWO_HG if deck == "burn2hg" else None
        for c in range(0, n, ch):
            for arm, fl in arms:
                # The "off" arm is the identity check (new binary, every lever off == base): a quarter of
                # the searched chunks is plenty for a digest comparison.
                if arm == "off" and cell != "d0" and (c // ch) % 4 != 0:
                    continue
                jobs.append(job("%s.%s.%s.c%d" % (deck, cell, arm, c), BURN_DECK, BURN_PROF, cell,
                                min(ch, n - c), SEEDS[(deck, cell)] + c, c, fl, extra))
    if not a.no_identity:
        for key, v in sorted(suite_decks().items()):
            for cell, g in ID_GAMES.items():
                jobs.append(job("id.%s.%s" % (key, cell), v["DECK_FILE"], v["DECK_PROF"], cell, g,
                                ID_SEED, 0, ALL_ON if a.which == "new" else None))
    def cost(j):
        p = j["name"].split(".")
        return COST[p[2]] * 10 if p[0] == "id" else COST[p[1]]   # identity decks can be far dearer
    jobs.sort(key=lambda j: -cost(j))   # stable: expensive cells first
    os.makedirs(a.out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(a.out, "manifest.json"), "w"), indent=1)
    print("wrote %s/manifest.json: %d jobs, %d games" % (a.out, len(jobs), sum(j["games"] for j in jobs)))


WIN = re.compile(r"\[win\] job=(\S+) gi=(\d+) wt=(-?\d+)")
RES = re.compile(r"^(\S+): played=(\d+) avg=(\S+) digest=([0-9a-f]+) ms=(\d+) units=(\d+)")


def load(d):
    wins = defaultdict(dict)      # (deck, cell, arm) -> gi -> score (loss = 9)
    for line in open(os.path.join(d, "batch.err"), errors="ignore"):
        m = WIN.search(line)
        if not m:
            continue
        parts = m.group(1).split(".")
        if parts[0] == "id":
            continue
        wt = int(m.group(3))
        wins[(parts[0], parts[1], parts[2])][int(m.group(2))] = 9 if wt < 0 else wt
    res = {}
    for line in open(os.path.join(d, "batch.out"), errors="ignore"):
        m = RES.match(line)
        if m:
            res[m.group(1)] = dict(played=int(m.group(2)), avg=float(m.group(3)), digest=m.group(4),
                                   ms=int(m.group(5)), units=int(m.group(6)))
    return wins, res


def units_by(res):
    u = defaultdict(int)
    ms = defaultdict(int)
    for name, r in res.items():
        p = name.split(".")
        if p[0] == "id":
            continue
        u[(p[0], p[1], p[2])] += r["units"]
        ms[(p[0], p[1], p[2])] += r["ms"]
    return u, ms


def analyse(dbase, dnew):
    wb, rb = load(dbase)
    wn, rn = load(dnew)
    ub, msb = units_by(rb)
    un, msn = units_by(rn)
    out = []
    pr = out.append
    pr("loss = 9; paired per game vs the BASE binary (16dfb7e4); faster/slower/net = arm - old")
    pr("%-8s %-5s %-6s %6s %8s %8s %6s %6s %6s %8s %8s" % ("deck", "cell", "arm", "n", "mean", "old",
                                                         "fast", "slow", "net", "units", "ms"))
    slower = []
    for deck in ("burn", "burn2hg"):
        for cell in ("ship", "d3", "d0"):
            base = wb.get((deck, cell, "old"), {})
            for arm, _ in ARMS:
                w = wn.get((deck, cell, arm), {})
                gis = sorted(set(w) & set(base))
                if not gis:
                    continue
                dd = {g: w[g] - base[g] for g in gis}
                fast = [g for g in gis if dd[g] < 0]
                slow = [g for g in gis if dd[g] > 0]
                ma = sum(w[g] for g in gis) / len(gis)
                mb = sum(base[g] for g in gis) / len(gis)
                # Work is paired by CHUNK: only the chunks both sides ran (the base ran the full sizes).
                chunks = [nm.split(".")[3] for nm in rn if nm.startswith("%s.%s.%s." % (deck, cell, arm))]
                pu = [(rn["%s.%s.%s.%s" % (deck, cell, arm, c)], rb.get("%s.%s.old.%s" % (deck, cell, c)))
                      for c in chunks]
                pu = [(x, y) for x, y in pu if y is not None]
                bu = sum(y["units"] for _, y in pu)
                bm = sum(y["ms"] for _, y in pu)
                ur = sum(x["units"] for x, _ in pu) / bu if bu else 0.0
                mr = sum(x["ms"] for x, _ in pu) / bm if bm else 0.0
                pr("%-8s %-5s %-6s %6d %8.4f %8.4f %6d %6d %+6d %8.3f %8.3f" % (
                    deck, cell, arm, len(gis), ma, mb, len(fast), len(slow), sum(dd.values()), ur, mr))
                for g in slow:
                    slower.append((deck, cell, arm, g, base[g], w[g]))
    pr("\n== identity (non-burn suite decks; new binary with every lever ON vs base) ==")
    nid = 0
    bad = []
    for name in sorted(rb):
        if not name.startswith("id."):
            continue
        nid += 1
        if name not in rn:
            bad.append("%s: missing in new" % name)
        elif rn[name]["digest"] != rb[name]["digest"] or rn[name]["avg"] != rb[name]["avg"]:
            bad.append("%s: base %s/%s new %s/%s" % (name, rb[name]["avg"], rb[name]["digest"],
                                                     rn[name]["avg"], rn[name]["digest"]))
    pr("%d identity jobs, %d differ" % (nid, len(bad)))
    for b in bad:
        pr("  " + b)
    pr("\n== off arm (new binary, every lever off) vs base, digest per chunk ==")
    noff = 0
    offbad = []
    for name in sorted(rn):
        p = name.split(".")
        if p[0] == "id" or p[2] != "off":
            continue
        bname = "%s.%s.old.%s" % (p[0], p[1], p[3])
        noff += 1
        if bname not in rb or rb[bname]["digest"] != rn[name]["digest"]:
            offbad.append(name)
    pr("%d off chunks, %d differ%s" % (noff, len(offbad), (": " + ", ".join(offbad[:20])) if offbad else ""))
    pr("\n== slower games (deck cell arm gi seed old new) ==")
    for deck, cell, arm, g, ob, nw in slower:
        pr("  %-8s %-5s %-6s gi=%-6d seed=%d  old=%d new=%d" % (deck, cell, arm, g, SEEDS[(deck, cell)] + g, ob, nw))
    text = "\n".join(out)
    print(text)
    open(os.path.join(dnew, "analysis.txt"), "w").write(text + "\n")
    json.dump([list(s) for s in slower], open(os.path.join(dnew, "slower.json"), "w"))


def recovery(dbase, dnew, out):
    """One job per slower game: stage 1 (depth W = max(6, old win turn), budget 100) and stage 2
    (depth 8, budget 0), both under the arm's flags, both ignore_play_profile (the depth is pinned)."""
    slower = json.load(open(os.path.join(dnew, "slower.json")))
    flags = dict(ARMS)
    jobs = []
    for deck, cell, arm, g, ob, nw in slower:
        extra = dict(TWO_HG) if deck == "burn2hg" else {}
        w = max(6, ob if ob < 9 else 6)
        for stage, depth, bud in (("s1", w, 100), ("s2", 8, 0)):
            j = {"name": "rec.%s.%s.%s.g%d.%s" % (deck, cell, arm, g, stage),
                 "deck": os.path.join(ROOT, BURN_DECK), "profile": os.path.join(ROOT, BURN_PROF),
                 "games": 1, "seed": SEEDS[(deck, cell)] + g, "game_index": g,
                 "depth": depth, "budget_ms": bud, "ignore_play_profile": True, "flags": dict(flags[arm])}
            j.update(extra)
            jobs.append(j)
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("wrote %s/manifest.json: %d jobs" % (out, len(jobs)))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--which", choices=["base", "new"])
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--no-identity", action="store_true")
    ap.add_argument("--floor", action="store_true", help="searched cells at the brief's floor sizes")
    ap.add_argument("--analyse", nargs=2)
    ap.add_argument("--recovery", nargs=2)
    a = ap.parse_args()
    if a.analyse:
        analyse(*a.analyse)
    elif a.recovery:
        recovery(a.recovery[0], a.recovery[1], a.out)
    else:
        build(a)
