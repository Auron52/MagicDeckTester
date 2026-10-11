#!/usr/bin/env python3
"""ONE pooled batch: every USER discard design, arm off (=0) vs on (=1), paired game by game.

    python3 logs/discard_impl/make_ab.py [--scale 1.0] [--out logs/discard_impl/ab1]
    -> <out>/manifest.json; run with
       MTG_DUMP_WINS=1 build/Release/mtg --batch <out>/manifest.json --threads N 2> <out>/batch.err
    -> python3 logs/discard_impl/make_ab.py --analyse <out>/batch.err

Cells per deck (suite-shaped, FRESH seeds 210001+): the shipped value_play-driven cell (no depth key,
budget 20), d3 b10, d0, and the 2HG d3 b10 variant where the suite has one. Every arm of a cell plays
the same seed / game_index chunks, interleaved arm-minor so the pool has one tail. Unwon scores 9.
"""
import argparse, json, math, os, re, sys
from collections import defaultdict

REPO = "/workspaces/MagicDeckTester"
# suite key -> (heurarm env flag, has a 2HG suite variant)
DECKS = {
    "angels": ("MTG_ANGELS_BUCKET_DISCARD", True),
    "auras": ("MTG_AURA_BUCKET_DISCARD", True),
    "breaching": ("MTG_BREACH_BUCKET_DISCARD", True),
    "burn": ("MTG_BURN_BUCKET_DISCARD", True),
    "creature_giving": ("MTG_CG_BUCKET_DISCARD", True),   # off = the shipped DotH-last patch
    "critter": ("MTG_CRITTER_BUCKET_DISCARD", True),
    "dragonstorm": ("MTG_DRAGONSTORM_BUCKET_DISCARD", True),
    "edf": ("MTG_EDF_BUCKET_DISCARD", False),               # not a suite deck
    "fluctuator": ("MTG_FLUCT_BUCKET_DISCARD", True),
    "fungus": ("MTG_FUNGUS_BUCKET_DISCARD", False),
    "fungusb": ("MTG_FUNGUS_BUCKET_DISCARD", False),
    "giants": ("MTG_GIANTS_BUCKET_DISCARD", False),
    "goblins": ("MTG_GOBLIN_BUCKET_DISCARD", True),
    "knights": ("MTG_KNIGHTS_BUCKET_DISCARD", True),
    "melira": ("MTG_POD_BUCKET_DISCARD", True),
    "selesnya": ("MTG_SELESNYA_BUCKET_DISCARD", False),
    "slivers": ("MTG_SLIVERS_BUCKET_DISCARD", True),
    "whiteknights": ("MTG_WK_BUCKET_DISCARD", True),
}
EXTRA_FILES = {"edf": ("decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod",
                       "decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json")}
CELLS = [  # name, depth (None = value_play-driven), budget, games
    ("ship", None, 20, 300),
    ("d3", 3, 10, 400),
    ("d0", 0, 0, 2000),
]
CELL_2HG = ("2hg", 3, 10, 200)
CHUNK = 50


def suite_maps():
    txt = open(os.path.join(REPO, "test/regression_cases.sh")).read()
    out = {}
    for name in ("DECK_FILE", "DECK_PROF"):
        block = txt.split("declare -A %s=(" % name, 1)[1].split("\n)", 1)[0]
        m = {}
        for line in block.splitlines():
            mm = re.match(r'\s*\[(\w+)\]=(.*)$', line)
            if mm:
                m[mm.group(1)] = mm.group(2).strip().strip('"')
        out[name] = m
    return out


DIST_DECKS = {"angels", "critter", "auras", "giants", "goblins", "selesnya"}
EXTRA_KNOCKOUTS = {   # arm name -> (deck keys, the lever turned OFF on top of the shipped default)
    "nodist": (DIST_DECKS, "MTG_DISCARD_DIST_MODEL"),
    "w6":     ({"giants"}, "MTG_GIANTS_TUTOR_WIDTH"),
    "vialle": ({"goblins"}, "MTG_GOBLIN_TUTOR_VIAL_EXACT"),
    "noseq":  ({"auras"}, "MTG_AURA_RESTRICTED_NEW_CREATURE"),
}
# (arm, baseline) pairs the analysis reports
COMPARISONS = [("on", "off"), ("gen", "off"), ("on", "gen"),
               ("on", "nodist"), ("on", "w6"), ("on", "vialle"), ("on", "noseq")]


def arms_for(key, flag):
    """'off' = the deck's =0 fallback, 'on' = everything shipped default ON, plus one knockout arm per
    sub-lever (compared against 'on')."""
    if key == "creature_giving":   # the USER's three arms: patch (off) / buckets (on) / generic
        arms = [("off", {flag: False, "MTG_CG_DOTH_LAST": True}), ("on", {flag: True}),
                ("gen", {flag: False, "MTG_CG_DOTH_LAST": False})]
    else:
        arms = [("off", {flag: False}), ("on", {flag: True})]
    for arm, (decks, lever) in EXTRA_KNOCKOUTS.items():
        if key in decks:
            arms.append((arm, {flag: True, lever: False}))
    return arms


def build(args):
    maps = suite_maps()
    jobs = []
    for i, (key, (flag, has2hg)) in enumerate(DECKS.items()):
        deck, prof = EXTRA_FILES.get(key) or (maps["DECK_FILE"][key], maps["DECK_PROF"][key])
        cells = list(CELLS) + ([CELL_2HG] if has2hg else [])
        for ci, (cname, depth, bud, games) in enumerate(cells):
            games = max(CHUNK, int(round(games * args.scale / CHUNK)) * CHUNK)
            # DISJOINT seeds per (deck, cell): a game's deal is seeded from the job seed + its local game
            # number (BatchRunner), so bases must be >= the cell's game count apart (ab1 used 1000*i +
            # 100*ci and its cells replayed each other's deals). args.seed_base moves a rerun off ab1.
            base = args.seed_base + 20000 * i + 5000 * ci
            for c in range(0, games, CHUNK):
                for arm, fl in arms_for(key, flag):
                    j = {"name": "%s_%s_%s.c%d" % (key, cname, arm, c), "deck": deck, "profile": prof,
                         "games": CHUNK, "seed": base + c, "game_index": c, "budget_ms": bud,
                         "flags": dict(fl)}
                    if depth is not None:
                        j["depth"] = depth
                        j["ignore_play_profile"] = True
                    if cname == "2hg":
                        j["starting_life"] = 30
                        j["opponent_heads"] = 2
                    jobs.append(j)
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, "manifest.json")
    json.dump({"jobs": jobs}, open(path, "w"), indent=1)
    print("wrote %s: %d jobs, %d games" % (path, len(jobs), sum(j["games"] for j in jobs)))


def analyse(err_path):
    pat = re.compile(r"\[win\] job=(\S+?)_(ship|d3|d0|2hg)_(off|on|gen|nodist|w6|vialle|noseq)\.c\d+ gi=(\d+) wt=(-?\d+)")
    res = defaultdict(dict)   # (deck, cell) -> arm -> gi -> score
    for line in open(err_path, errors="ignore"):
        m = pat.search(line)
        if not m:
            continue
        deck, cell, arm, gi, wt = m.group(1), m.group(2), m.group(3), int(m.group(4)), int(m.group(5))
        res[(deck, cell)].setdefault(arm, {})[gi] = 9 if wt < 0 else wt
    print("%-28s %-5s %5s %8s %9s %7s %6s %6s" % ("deck[arm-base]", "cell", "n", "base", "arm-base", "t", "faster", "slower"))
    per_deck = defaultdict(lambda: [0, 0.0, 0, 0])
    for (deck, cell) in sorted(res):
        a = res[(deck, cell)]
        for other, base in COMPARISONS:
          if base not in a or other not in a:
            continue
          gis = sorted(set(a[base]) & set(a[other]))
          d = [a[other][g] - a[base][g] for g in gis]
          n = len(d)
          if n == 0:
            continue
          mean = sum(d) / n
          var = sum((x - mean) ** 2 for x in d) / max(1, n - 1)
          se = math.sqrt(var / n) if n > 1 else 0.0
          t = mean / se if se > 0 else 0.0
          fast, slow = sum(1 for x in d if x < 0), sum(1 for x in d if x > 0)
          off_mean = sum(a[base][g] for g in gis) / n
          label = deck if (other, base) == ("on", "off") else "%s[%s-%s]" % (deck, other, base)
          print("%-28s %-5s %5d %8.4f %+9.4f %7.2f %6d %6d" % (label, cell, n, off_mean, mean, t, fast, slow))
          pd = per_deck[label]
          pd[0] += n; pd[1] += sum(d); pd[2] += fast; pd[3] += slow
    print("\nper deck (all cells pooled): n, sum(arm-base), faster, slower")
    for deck, (n, s, f, sl) in sorted(per_deck.items()):
        print("  %-28s %6d %+8.0f %6d %6d" % (deck, n, s, f, sl))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--seed-base", type=int, default=210001)
    ap.add_argument("--out", default=os.path.join(REPO, "logs/discard_impl/ab1"))
    ap.add_argument("--analyse", default=None)
    a = ap.parse_args()
    analyse(a.analyse) if a.analyse else build(a)
