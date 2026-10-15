#!/usr/bin/env python3
"""Pooled A/B builder + analyser for the breakpoint-completeness levers (docs/design/bp-empty-arm-deferred-site.md).

Arms: 'on' (shipped default: every new lever on) vs 'off' (every new lever =0 -- the committed tree's play),
plus optional per-lever attribution arms. Paired by (deck, cell, game index) on the SAME seeds.

  python3 make_ab.py build --out DIR --seed0 S [--decks k1,k2] [--cells ship,d3,d0] [--games ship=100,d3=200,d0=200]
                           [--arms on,off] [--chunk 25] [--scale-expensive]
  -> run:  MTG_PGO=0 MTG_DUMP_WINS=1 mtg --batch DIR/manifest.json --threads 6 > DIR/batch.out 2> DIR/batch.err
  python3 make_ab.py analyse DIR [--base off]

Seed of game gi in (deck, cell) = seed0 + 20000 * deck_index + 5000 * cell_index + gi. Unwon scores 9.
"""
import argparse, json, os, re, sys
from collections import defaultdict

# Root the deck/profile paths resolve against: BP_DATA_ROOT (stage_data.sh's copy on the build volume) or the
# primary checkout.
PRIMARY = os.environ.get("BP_DATA_ROOT", "/workspaces/MagicDeckTester")
ALL_DECKS = [  # (key, deck, profile) -- test/regression_cases.sh DECK_FILE / DECK_PROF, in that order
    ("fungus", "decks/Fungus/Fungus.cod", "decks/Fungus/Fungus.profile.json"),
    ("fungusb", "decks/Fungus/candidate-b-2026-09/Fungus.cod", "decks/Fungus/candidate-b-2026-09/Fungus.profile.json"),
    ("slivers", "decks/slivers_vial/slivers_vial.txt", "decks/slivers_vial/slivers_vial.profile.json"),
    ("burn", "decks/burn/burn.txt", "decks/burn/burn.profile.json"),
    ("th", "decks/treasure_hunt/treasure_hunt.txt", "decks/treasure_hunt/treasure_hunt.profile.json"),
    ("knights", "decks/Knights/Knights.cod", "decks/Knights/Knights.profile.json"),
    ("whiteknights", "decks/WhiteKnights/WhiteKnights.cod", "decks/WhiteKnights/WhiteKnights.profile.json"),
    ("antilife", "decks/Anti-Lifegain/Anti-Lifegain.cod", "decks/Anti-Lifegain/Anti-Lifegain.profile.json"),
    ("hinata", "decks/Hinata2/Hinata2.cod", "decks/Hinata2/Hinata2.profile.json"),
    ("dragonstorm", "decks/Dragonstorm/Dragonstorm.cod", "decks/Dragonstorm/Dragonstorm.profile.json"),
    ("auras", "decks/Auras/Auras.cod", "decks/Auras/Auras.profile.json"),
    ("goblins", "decks/Goblins/Goblins.cod", "decks/Goblins/Goblins.profile.json"),
    ("creature_giving", "decks/Creature Giving/Creature Giving.cod", "decks/Creature Giving/Creature Giving.profile.json"),
    ("mirrorwing", "decks/Mirrorwing Dragon/Mirrorwing Dragon.cod", "decks/Mirrorwing Dragon/Mirrorwing Dragon.profile.json"),
    ("fivecolour", "decks/FiveColour/FiveColour.cod", "decks/FiveColour/FiveColour.profile.json"),
    ("stompy", "decks/StompySurprise/StompySurprise.cod", "decks/StompySurprise/StompySurprise.profile.json"),
    ("minotaur", "decks/Minotaur/Minotaur.cod", "decks/Minotaur/Minotaur.profile.json"),
    ("kitty", "decks/KittyEquipment/KittyEquipment.cod", "decks/KittyEquipment/KittyEquipment.profile.json"),
    ("kittyv2", "decks/KittyEquipment/v2-puresteel-hammer/KittyEquipment.cod", "decks/KittyEquipment/v2-puresteel-hammer/KittyEquipment.profile.json"),
    ("kittyv3", "decks/KittyEquipment/v3-adelbert-gauntlets/KittyEquipment.cod", "decks/KittyEquipment/v3-adelbert-gauntlets/KittyEquipment.profile.json"),
    ("dragons", "decks/Dragons/Dragons.cod", "decks/Dragons/Dragons.profile.json"),
    ("breaching", "decks/BreachingDragonstorm/BreachingDragonstorm.cod", "decks/BreachingDragonstorm/BreachingDragonstorm.profile.json"),
    ("critter", "decks/CritterLifegain/CritterLifegain.cod", "decks/CritterLifegain/CritterLifegain.profile.json"),
    ("melira", "decks/Melira Pod/Melira Pod.cod", "decks/Melira Pod/Melira Pod.profile.json"),
    ("angels", "decks/Angels/Angels.cod", "decks/Angels/Angels.profile.json"),
    ("fluctuator", "decks/Fluctuator/Fluctuator.cod", "decks/Fluctuator/Fluctuator.profile.json"),
    ("snow", "decks/Snow/Snow.cod", "decks/Snow/Snow.profile.json"),
    ("giants", "decks/Giants/Giants.cod", "decks/Giants/Giants.profile.json"),
    ("pirates", "decks/Pirates/Pirates.cod", "decks/Pirates/Pirates.profile.json"),
    ("selesnya", "decks/SelesnyaLifegain/SelesnyaLifegain.cod", "decks/SelesnyaLifegain/SelesnyaLifegain.profile.json"),
    ("soldiers", "decks/Soldiers/soldiers.cod", "decks/Soldiers/soldiers.profile.json"),
    ("pd", "decks/Prevent Damage/Prevent Damage.cod", "decks/Prevent Damage/Prevent Damage.profile.json"),
    ("bruna", "decks/Bruna/Bruna.cod", "decks/Bruna/Bruna.profile.json"),
]
KEYS = [d[0] for d in ALL_DECKS]
LEVERS = ["MTG_BP_WAVE_EMPTY", "MTG_BP_WAVE_UNHOSTED_SITES", "MTG_BP_M2_WAVE_FIXPOINT", "MTG_BP_NODE_NESTED_SLOTS",
          "MTG_BP_NEW_ONLY_DEMOTE"]
# Every arm names MTG_BP_NEW_ONLY_DEMOTE explicitly: its default flipped to OFF on 2026-10-15 (USER decision
# pending), so "on" = all five levers and "nodemote" = F1-F4 = the shipped default, on any binary.
ARMS = {
    "on": {"MTG_BP_NEW_ONLY_DEMOTE": True},
    "ship": {},                                        # whatever the binary defaults to (F1-F4 since ee8a2644)
    "off": {k: False for k in LEVERS},
    "empty": {k: False for k in LEVERS if k != "MTG_BP_WAVE_EMPTY"},          # walker EMPTY alone
    "unhost": {k: False for k in LEVERS if k != "MTG_BP_WAVE_UNHOSTED_SITES"},
    "m2fix": {k: False for k in LEVERS if k != "MTG_BP_M2_WAVE_FIXPOINT"},
    "nested": {k: False for k in LEVERS if k != "MTG_BP_NODE_NESTED_SLOTS"},
    "noempty": {"MTG_BP_WAVE_EMPTY": False},
    "nounhost": {"MTG_BP_WAVE_UNHOSTED_SITES": False},
    "nom2fix": {"MTG_BP_M2_WAVE_FIXPOINT": False},
    "nonested": {"MTG_BP_NODE_NESTED_SLOTS": False},
    "nodemote": {"MTG_BP_NEW_ONLY_DEMOTE": False},
    # "onnl" / "nl14" (+ MTG_BP_WAVE_NEW_LAST) ran on r2 only: the lever was measured inert and deleted.
    "demote": dict({k: False for k in LEVERS if k != "MTG_BP_NEW_ONLY_DEMOTE"}, MTG_BP_NEW_ONLY_DEMOTE=True),
}
CELLS = [("ship", None, 20), ("d3", 3, 10), ("d0", 0, 0), ("x3", 3, 0), ("xs", None, 0)]
CELL_IX = {c[0]: i for i, c in enumerate(CELLS)}
# Rough per-game core-s at the shipped cell (test/suite_cost.json, regression worst case): used only to
# order the queue (expensive first) and to scale the expensive decks down.
COST = {"snow": 7.2, "bruna": 4.4, "hinata": 1.7, "pd": 1.5, "fivecolour": 1.2, "kittyv2": 1.0,
        "melira": 1.0, "fungusb": 0.56, "kitty": 0.34, "fungus": 0.32}


def seed_base(seed0, key, cell):
    return seed0 + 20000 * KEYS.index(key) + 5000 * CELL_IX[cell]


def build(a):
    decks = [d for d in ALL_DECKS if not a.decks or d[0] in a.decks.split(",")]
    cells = [c for c in CELLS if c[0] in a.cells.split(",")]
    games = dict((kv.split("=")[0], int(kv.split("=")[1])) for kv in a.games.split(","))
    arms = a.arms.split(",")
    jobs = []
    for cname, depth, bud in cells:
        for key, deck, prof in decks:
            base = seed_base(a.seed0, key, cname)
            n = games.get(cname, 100)
            if a.scale_expensive and cname != "d0" and COST.get(key, 0) >= 1.0:
                n = max(a.chunk, int(n * min(1.0, 1.5 / COST[key])) // a.chunk * a.chunk)
            for c in range(0, n, a.chunk):
                for arm in arms:   # interleaved: the arms' chunk c run back to back
                    j = {"name": "%s_%s_%s.c%d" % (key, cname, arm, c), "deck": os.path.join(PRIMARY, deck),
                         "profile": os.path.join(PRIMARY, prof), "games": min(a.chunk, n - c),
                         "seed": base + c, "game_index": c, "budget_ms": bud}
                    if ARMS[arm]:
                        j["flags"] = dict(ARMS[arm])
                    if depth is not None:
                        j["depth"] = depth
                        j["ignore_play_profile"] = True
                    jobs.append(j)

    def weight(j):
        key, cname = j["name"].split("_")[0], j["name"].split("_")[1]
        if cname == "d0":
            return 0.01
        return COST.get(key, 0.2) * (1.0 if cname == "ship" else 0.4)
    jobs.sort(key=lambda j: -weight(j))
    os.makedirs(a.out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(a.out, "manifest.json"), "w"), indent=1)
    json.dump({"seed0": a.seed0, "arms": arms}, open(os.path.join(a.out, "seeds.json"), "w"))
    print("wrote %s/manifest.json: %d jobs, %d games" % (a.out, len(jobs), sum(j["games"] for j in jobs)))


def load(d):
    wins = defaultdict(lambda: defaultdict(dict))     # (key, cell) -> arm -> gi -> wt
    pat = re.compile(r"\[win\] job=(\S+?)_(ship|d3|d0|x3|xs)_([a-z0-9]+)\.c(\d+) gi=(\d+) wt=(-?\d+)")
    for line in open(os.path.join(d, "batch.err"), errors="ignore"):
        m = pat.search(line)
        if m:
            wt = int(m.group(6))
            wins[(m.group(1), m.group(2))][m.group(3)][int(m.group(5))] = 9 if wt < 0 else wt
    jobs = defaultdict(lambda: defaultdict(dict))     # (key, cell) -> arm -> chunk -> (digest, ms, units, played)
    jpat = re.compile(r"^(\S+?)_(ship|d3|d0|x3|xs)_([a-z0-9]+)\.c(\d+): played=(\d+) avg=\S+ digest=(\S+) ms=(\d+) units=(\d+)")
    for line in open(os.path.join(d, "batch.out"), errors="ignore"):
        m = jpat.search(line)
        if m:
            jobs[(m.group(1), m.group(2))][m.group(3)][int(m.group(4))] = (
                m.group(6), int(m.group(7)), int(m.group(8)), int(m.group(5)))
    return wins, jobs


def analyse(a):
    d = a.dir
    wins, jobs = load(d)
    base = a.base
    rows = []
    print("%-16s %-4s %-8s %5s | %7s %7s %+8s | %4s %4s %+5s | %7s %7s | %s" % (
        "deck", "cell", "arm", "n", "base q", "arm q", "diff", "fast", "slow", "net", "units x", "ms x",
        "digests differ"))
    for (key, cell) in sorted(jobs, key=lambda kc: (KEYS.index(kc[0]) if kc[0] in KEYS else 99, CELL_IX[kc[1]])):
        arms = jobs[(key, cell)]
        if base not in arms:
            continue
        for arm in sorted(arms):
            if arm == base:
                continue
            w0, w1 = wins[(key, cell)].get(base, {}), wins[(key, cell)].get(arm, {})
            gis = sorted(set(w0) & set(w1))
            ch = sorted(set(arms[base]) & set(arms[arm]))
            dig = sum(1 for c in ch if arms[base][c][0] != arms[arm][c][0])
            u0 = sum(arms[base][c][2] for c in ch); u1 = sum(arms[arm][c][2] for c in ch)
            m0 = sum(arms[base][c][1] for c in ch); m1 = sum(arms[arm][c][1] for c in ch)
            if not gis:
                print("%-16s %-4s %-8s %5d | (no per-game wins) digests %d/%d" % (key, cell, arm, 0, dig, len(ch)))
                continue
            n = len(gis)
            q0 = sum(w0[g] for g in gis) / n
            q1 = sum(w1[g] for g in gis) / n
            dd = [w1[g] - w0[g] for g in gis]
            fast = sum(1 for v in dd if v < 0); slow = sum(1 for v in dd if v > 0)
            print("%-16s %-4s %-8s %5d | %7.4f %7.4f %+8.4f | %4d %4d %+5d | %7.4f %7.4f | %d/%d" % (
                key, cell, arm, n, q0, q1, q1 - q0, fast, slow, sum(dd),
                (u1 / u0) if u0 else float("nan"), (m1 / m0) if m0 else float("nan"), dig, len(ch)))
            rows.append({"deck": key, "cell": cell, "arm": arm, "n": n, "q_base": q0, "q_arm": q1,
                         "fast": fast, "slow": slow, "net": sum(dd), "units_x": (u1 / u0) if u0 else None,
                         "ms_x": (m1 / m0) if m0 else None, "digests_differ": dig, "chunks": len(ch),
                         "slower": [(g, w0[g], w1[g]) for g in gis if w1[g] > w0[g]],
                         "faster": [(g, w0[g], w1[g]) for g in gis if w1[g] < w0[g]]})
    json.dump(rows, open(os.path.join(d, "summary.json"), "w"), indent=1)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd")
    b = sub.add_parser("build")
    b.add_argument("--out", required=True)
    b.add_argument("--seed0", type=int, required=True)
    b.add_argument("--decks", default="")
    b.add_argument("--cells", default="ship,d3,d0")
    b.add_argument("--games", default="ship=100,d3=200,d0=200")
    b.add_argument("--arms", default="on,off")
    b.add_argument("--chunk", type=int, default=25)
    b.add_argument("--scale-expensive", action="store_true")
    an = sub.add_parser("analyse")
    an.add_argument("dir")
    an.add_argument("--base", default="off")
    a = ap.parse_args()
    if a.cmd == "build":
        build(a)
    elif a.cmd == "analyse":
        analyse(a)
    else:
        ap.print_help()
