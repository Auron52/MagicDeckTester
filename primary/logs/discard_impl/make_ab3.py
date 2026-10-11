#!/usr/bin/env python3
"""ONE pooled batch: the 2026-10-10 bug-fix round, suite-wide, paired.

    python3 logs/discard_impl/make_ab3.py [--extra MTG_A,MTG_B ...]   -> logs/discard_impl/ab3/manifest.json
    MTG_DUMP_WINS=1 <bin> --batch logs/discard_impl/ab3/manifest.json --threads N 2> .../batch.err > .../batch.out
    python3 logs/discard_impl/make_ab3.py --analyse logs/discard_impl/ab3/batch.err

Arms, every deck: 'new' = the tip's defaults; 'old' = every FIX lever =0 (the tip with this round's bug
fixes off). Per-lever knockouts ('ko_<LEVER>' = new with that one lever =0) only on the decks it was
written for. Extra arms: 'fc_on' (MTG_EXEC_FAILED_CAST_NO_RESOLVE=1, default OFF) on dragonstorm;
'alch_off' (MTG_SELESNYA_ALCHEMIST_BURST=0, the USER's package) on selesnya.
Seeds are DISJOINT per (deck, cell) and from ab1 / ab2 (a game's deal = job seed + local game number).
"""
import argparse, json, math, os, re, sys
from collections import defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_ab

REPO = make_ab.REPO
FIX_LEVERS = [
    "MTG_DRAGONSTORM_ORDER_OPAQUE", "MTG_PAYABLE_ORDER_RITUALS",                       # Dragonstorm / rituals
    "MTG_COLOR_EXACT_SCALED", "MTG_ATTACK_TAP_NEED_EXACT", "MTG_ATTACK_TAP_ABILITY_DISCOUNT",   # Selesnya / attack
    "MTG_ROLLOUT_LAND_ACT_COLOR",                                                       # playout land drop
]
KNOCKOUTS = {   # deck key -> levers to knock out one at a time
    "dragonstorm": ["MTG_DRAGONSTORM_ORDER_OPAQUE", "MTG_PAYABLE_ORDER_RITUALS"],
    "selesnya":    ["MTG_COLOR_EXACT_SCALED", "MTG_ATTACK_TAP_NEED_EXACT", "MTG_ATTACK_TAP_ABILITY_DISCOUNT"],
    "edf":         [],   # filled from --edf-levers
}
TWOHG = set("angels antilife auras breaching bruna burn creature_giving critter dragons dragonstorm fivecolour "
            "fluctuator goblins hinata kitty knights melira minotaur mirrorwing pd pirates slivers soldiers "
            "stompy th whiteknights".split())
CELLS = [("ship", None, 20, 300), ("d3", 3, 10, 400), ("d0", 0, 0, 2000), ("2hg", 3, 10, 200)]
CHUNK = 50
SEED0 = 1000001


def arms_for(key):
    arms = [("new", {}), ("old", {l: False for l in FIX_LEVERS})]
    for lv in KNOCKOUTS.get(key, []):
        arms.append(("ko_" + lv[4:].lower(), {lv: False}))
    if key == "dragonstorm":
        arms.append(("fc_on", {"MTG_EXEC_FAILED_CAST_NO_RESOLVE": True}))
    if key == "selesnya":
        arms.append(("alch_off", {"MTG_SELESNYA_ALCHEMIST_BURST": False}))
    return arms


def build(out):
    maps = make_ab.suite_maps()
    keys = sorted(maps["DECK_FILE"]) + ["edf"]
    jobs = []
    for i, key in enumerate(keys):
        deck, prof = make_ab.EXTRA_FILES.get(key) or (maps["DECK_FILE"][key], maps["DECK_PROF"][key])
        for ci, (cname, depth, bud, games) in enumerate(CELLS):
            if cname == "2hg" and key not in TWOHG:
                continue
            base = SEED0 + 20000 * i + 5000 * ci
            for c in range(0, games, CHUNK):
                for arm, fl in arms_for(key):
                    j = {"name": "%s_%s_%s.c%d" % (key, cname, arm, c), "deck": deck, "profile": prof,
                         "games": CHUNK, "seed": base + c, "game_index": c, "budget_ms": bud}
                    if fl:
                        j["flags"] = dict(fl)
                    if depth is not None:
                        j["depth"] = depth
                        j["ignore_play_profile"] = True
                    if cname == "2hg":
                        j["starting_life"] = 30
                        j["opponent_heads"] = 2
                    jobs.append(j)
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, "manifest.json")
    json.dump({"jobs": jobs}, open(path, "w"), indent=1)
    print("wrote %s: %d jobs, %d games, %d decks" % (path, len(jobs), sum(j["games"] for j in jobs), len(keys)))


def analyse(err_path):
    pat = re.compile(r"\[win\] job=(\S+?)_(ship|d3|d0|2hg)_([a-z0-9_]+)\.c(\d+) gi=(\d+) wt=(-?\d+)")
    res = defaultdict(lambda: defaultdict(dict))
    for line in open(err_path, errors="ignore"):
        m = pat.search(line)
        if m:
            res[(m.group(1), m.group(2))][m.group(3)][int(m.group(5))] = 9 if int(m.group(6)) < 0 else int(m.group(6))
    maps = make_ab.suite_maps()
    keys = sorted(maps["DECK_FILE"]) + ["edf"]
    print("%-24s %-5s %5s %8s %9s %7s %6s %6s" % ("deck[arm-base]", "cell", "n", "base", "arm-base", "t", "faster", "slower"))
    per = defaultdict(lambda: [0, 0, 0, 0])
    slower = []
    for (deck, cell) in sorted(res):
        a = res[(deck, cell)]
        pairs = [("new", "old")] + [(x, "new") for x in a if x not in ("new", "old")]
        for x, b in pairs:
            if x not in a or b not in a:
                continue
            gis = sorted(set(a[x]) & set(a[b]))
            d = [a[x][g] - a[b][g] for g in gis]
            n = len(d)
            if not n:
                continue
            mean = sum(d) / n
            var = sum((v - mean) ** 2 for v in d) / max(1, n - 1)
            se = math.sqrt(var / n) if n > 1 else 0.0
            f, s_ = sum(1 for v in d if v < 0), sum(1 for v in d if v > 0)
            lab = deck if (x, b) == ("new", "old") else "%s[%s-%s]" % (deck, x, b)
            print("%-24s %-5s %5d %8.4f %+9.4f %7.2f %6d %6d" % (lab, cell, n, sum(a[b][g] for g in gis) / n,
                  mean, mean / se if se > 0 else 0.0, f, s_))
            p = per[lab]; p[0] += n; p[1] += sum(d); p[2] += f; p[3] += s_
            if (x, b) == ("new", "old"):
                ci = [c[0] for c in CELLS].index(cell)
                base = SEED0 + 20000 * keys.index(deck) + 5000 * ci
                for g in gis:
                    if a["new"][g] > a["old"][g]:
                        slower.append((deck, cell, g, a["old"][g], a["new"][g], base + g))
    print("\nper deck, all cells pooled (n, sum(arm-base), faster, slower):")
    for lab, (n, sm, f, s_) in sorted(per.items()):
        print("  %-26s %6d %+6d %5d %5d" % (lab, n, sm, f, s_))
    print("\nSLOWER games, new vs old (deck cell gi old new seed):")
    for r in slower:
        print("  %-16s %-4s gi=%-5d old=%d new=%d seed=%d" % r)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "logs/discard_impl/ab3"))
    ap.add_argument("--edf-levers", default="", help="comma-separated EDF fix levers (added to FIX_LEVERS + EDF knockouts)")
    ap.add_argument("--analyse", default=None)
    a = ap.parse_args()
    if a.edf_levers:
        for lv in a.edf_levers.split(","):
            FIX_LEVERS.append(lv)
            KNOCKOUTS["edf"].append(lv)
    analyse(a.analyse) if a.analyse else build(a.out)
