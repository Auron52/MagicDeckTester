#!/usr/bin/env python3
"""Paired sanity A/B for ONE lever (NOT a verdict), ONE pooled batch.

    python3 make_ab2.py <outdir> <LEVER> <seed_base> [deck ...]     (deck keys: ds hin cyc)
    python3 make_ab2.py --analyse <outdir>/batch.err
Cells per deck: d0 300 games (seed_base + 100000*deck_idx), d3 b10 100 games (+ 50000).
"""
import json, os, re, sys
from collections import defaultdict

WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6"
DECKS = {
    "ds": (WT + "/decks/Dragonstorm/Dragonstorm.cod", WT + "/decks/Dragonstorm/Dragonstorm.profile.json"),
    "hin": (WT + "/decks/Hinata2/Hinata2.cod", WT + "/decks/Hinata2/Hinata2.profile.json"),
    "cyc": (WT + "/decks/Unpredictable Cyclone/Unpredictable Cyclone.cod",
            WT + "/decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json"),
}
for _k, _n in (("mw", "Mirrorwing Dragon"), ("dragons", "Dragons"), ("giants", "Giants"), ("snow", "Snow"),
               ("wk", "WhiteKnights"), ("angels", "Angels"), ("bruna", "Bruna"), ("kitty", "KittyEquipment"),
               ("stompy", "StompySurprise"), ("five", "FiveColour")):
    DECKS[_k] = (WT + "/decks/%s/%s.cod" % (_n, _n), WT + "/decks/%s/%s.profile.json" % (_n, _n))
CHUNK = 25
MAX_TURNS = 8


def build(out, lever, base, decks):
    jobs = []
    for di, dks in enumerate(decks):
        dk, _, only = dks.partition("@")   # "key@d0" -> the d0 cell only
        deck, prof = DECKS[dk]
        for cname, off, games, depth, bud in (("d0", 0, 300, 0, 0), ("d3", 50000, 100, 3, 10)):
            if only and cname != only:
                continue
            seed = base + 100000 * di + off
            for c in range(0, games, CHUNK):
                for arm, on in (("on", True), ("off", False)):
                    j = {"name": "%s_%s_%s.c%d" % (dk, cname, arm, c), "deck": deck, "games": min(CHUNK, games - c),
                         "seed": seed + c, "game_index": c, "budget_ms": bud, "depth": depth,
                         "ignore_play_profile": True, "flags": {lever: on}}
                    if os.path.exists(prof):
                        j["profile"] = prof
                    jobs.append(j)
    jobs.sort(key=lambda j: 0 if "_d3_" in j["name"] else 1)
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs, "meta": {"base": base, "decks": decks}}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("wrote %d jobs, %d games" % (len(jobs), sum(j["games"] for j in jobs)))


def analyse(err):
    out = os.path.dirname(err)
    meta = json.load(open(os.path.join(out, "manifest.json")))["meta"]
    pat = re.compile(r"\[win\] job=(\w+)_(d0|d3)_(on|off)\.c(\d+) gi=(\d+) wt=(-?\d+)")
    res = defaultdict(dict)
    for line in open(err, errors="replace"):
        m = pat.search(line)
        if not m:
            continue
        dk, cell, arm, gi, wt = m.group(1), m.group(2), m.group(3), int(m.group(5)), int(m.group(6))
        res[(dk, cell, gi)][arm] = wt if wt > 0 else MAX_TURNS + 1
    ms = defaultdict(int)
    for line in open(os.path.join(out, "batch.out"), errors="replace"):
        m = re.match(r"(\w+)_(d0|d3)_(on|off)\.c\d+: .* ms=(\d+) units=(\d+)", line)
        if m:
            ms[(m.group(1), m.group(2), m.group(3), "ms")] += int(m.group(4))
            ms[(m.group(1), m.group(2), m.group(3), "u")] += int(m.group(5))
    for di, dks in enumerate(meta["decks"]):
        dk = dks.partition("@")[0]
        for cell, off in (("d0", 0), ("d3", 50000)):
            keys = sorted(k for k in res if k[0] == dk and k[1] == cell and len(res[k]) == 2)
            if not keys:
                continue
            on = sum(res[k]["on"] for k in keys)
            off_ = sum(res[k]["off"] for k in keys)
            fa = [k for k in keys if res[k]["on"] < res[k]["off"]]
            sl = [k for k in keys if res[k]["on"] > res[k]["off"]]
            print("%s %s: %d paired; turns on %d off %d (delta %+d); faster %d slower %d; ms on/off %d/%d units %d/%d"
                  % (dk, cell, len(keys), on, off_, on - off_, len(fa), len(sl),
                     ms[(dk, cell, "on", "ms")], ms[(dk, cell, "off", "ms")],
                     ms[(dk, cell, "on", "u")], ms[(dk, cell, "off", "u")]))
            seed0 = meta["base"] + 100000 * di + off
            for k in sl:
                print("   SLOWER %s %s gi=%d seed=%d: on %d off %d" % (dk, cell, k[2], seed0 + k[2], res[k]["on"], res[k]["off"]))
            for k in fa:
                print("   faster %s %s gi=%d seed=%d: on %d off %d" % (dk, cell, k[2], seed0 + k[2], res[k]["on"], res[k]["off"]))


if __name__ == "__main__":
    if sys.argv[1] == "--analyse":
        analyse(sys.argv[2])
    else:
        build(sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4:] or ["ds"])
