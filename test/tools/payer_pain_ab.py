#!/usr/bin/env python3
"""The payer's pain-last + narrow-restricted-first levers, measured on every deck they move, in ONE pooled
`mtg --batch` queue (Vampires sweep F + L, 2026-10-18; docs/design/analysis-Vampire.md "Sweep fixes").

ARMS (per-job "flags", heurarm slots, so both arms share one queue and one tail):
    on   MTG_PAY_PAIN_LAST=1 MTG_PAY_NARROW_RESTRICTED_FIRST=1   (as shipped, default ON)
    off  MTG_PAY_PAIN_LAST=0 MTG_PAY_NARROW_RESTRICTED_FIRST=0   (the historical payer, byte-identical)
DECKS: the eight decks whose smoke rows the levers moved (keys from test/regression_cases.sh).
CELLS: std d0 / d3 b10 / d5 b20 and 2hg d3 b10 (the suite's gate budgets). SEEDS: two held-out blocks far
from every suite seed and from the cast-order A/B blocks (12,180,001 / 12,480,001).

Usage:
    payer_pain_ab.py manifest [--scale X] > build/scratch/payer-ab/manifest.json
    MTG_PGO=0 build/Release/mtg --batch build/scratch/payer-ab/manifest.json --threads 20 \\
        --game-log-dir build/scratch/payer-ab/wins 2> build/scratch/payer-ab/batch.err
    payer_pain_ab.py report build/scratch/payer-ab/wins [--max-turns 8]
"""
import collections
import json
import pathlib
import re
import sys
from math import sqrt

DECKS = ["vampires", "auras", "creature_giving", "minotaur", "pirates", "selesnya", "soldiers", "th"]
ARMS = {
    "on":  {"MTG_PAY_PAIN_LAST": True,  "MTG_PAY_NARROW_RESTRICTED_FIRST": True},
    "off": {"MTG_PAY_PAIN_LAST": False, "MTG_PAY_NARROW_RESTRICTED_FIRST": False},
}
BASELINE = "off"
BLOCKS = {"A": 13_180_001, "B": 13_480_001}
CELLS = [("std", 0, 0, 2000), ("std", 3, 10, 300), ("std", 5, 20, 150), ("2hg", 3, 10, 100)]
FORMAT_FIELDS = {"std": {}, "2hg": {"starting_life": 30, "opponent_heads": 2}}
JOB_RE = re.compile(r"^(?P<deck>[a-z_]+)\.(?P<arm>on|off)\.(?P<fmt>std|2hg)\.d(?P<depth>\d+)\.b(?P<budget>\d+)\.(?P<block>[A-Z])$")


def deck_maps():
    text = pathlib.Path("test/regression_cases.sh").read_text(encoding="utf-8")
    maps = {}
    for var in ("DECK_FILE", "DECK_PROF"):
        block = re.search(r"declare -A " + var + r"=\((.*?)\n\)", text, re.S)
        if not block:
            block = re.search(var + r"=\((.*?)\n\)", text, re.S)
        m = {}
        for k, v in re.findall(r'\[([a-z_0-9]+)\]=("[^"]*"|\S+)', block.group(1)):
            m[k] = v.strip('"')
        maps[var] = m
    # Vampires is not a suite deck (the 3x cost gate, 2026-10-20), so its paths are given here.
    maps["DECK_FILE"].setdefault("vampires", "decks/Vampires/Vampire.cod")
    maps["DECK_PROF"].setdefault("vampires", "decks/Vampires/Vampire.profile.json")
    return maps["DECK_FILE"], maps["DECK_PROF"]


def manifest(argv):
    scale = float(argv[argv.index("--scale") + 1]) if "--scale" in argv else 1.0
    dfile, dprof = deck_maps()
    jobs = []
    for deck in DECKS:
        for fmt, depth, budget, games in CELLS:
            n = max(1, int(round(games * scale)))
            for block, seed in BLOCKS.items():
                for arm, flags in ARMS.items():
                    job = {"name": f"{deck}.{arm}.{fmt}.d{depth}.b{budget}.{block}",
                           "deck": dfile[deck], "profile": dprof[deck], "games": n, "seed": seed,
                           "budget_ms": budget, "flags": dict(flags), "weight": depth}
                    # The suite's convention (test/regression.sh): a d5 row omits the depth so an ENABLED
                    # value_play block owns it (else the built-in d5); a d0/d3 row overrides the profile.
                    if depth != 5:
                        job["depth"] = depth
                        job["ignore_play_profile"] = True
                    job.update(FORMAT_FIELDS[fmt])
                    jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")
    print(f"[payer_pain_ab] {len(jobs)} jobs, {sum(j['games'] for j in jobs)} games", file=sys.stderr)


def load_wins(path, max_turns):
    out = {}
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        gi, wt = int(parts[0]), int(parts[1])
        out[gi] = (max_turns + 1 if wt < 0 else wt, parts[2] if len(parts) > 2 else "")
    return out


def report(argv):
    root = pathlib.Path(argv[0])
    max_turns = int(argv[argv.index("--max-turns") + 1]) if "--max-turns" in argv else 8
    runs = collections.defaultdict(dict)   # (deck, fmt, depth, budget) -> (block, arm) -> wins
    for p in sorted(root.glob("*.wins")):
        m = JOB_RE.match(p.stem)
        if m:
            runs[(m["deck"], m["fmt"], int(m["depth"]), int(m["budget"]))][(m["block"], m["arm"])] = \
                load_wins(p, max_turns)
    print(f"{'cell':34s} {'n':>6s} {'faster':>6s} {'slower':>6s} {'net':>6s} {'mean':>9s} {'se':>8s} "
          f"{'t':>6s} {'dig':>5s}  off->on avg")
    deck_net = collections.Counter()
    slower_list = []
    for key in sorted(runs):
        deck, fmt, depth, budget = key
        d, dig, base_sum, arm_sum = [], 0, 0, 0
        for block in BLOCKS:
            b, a = runs[key].get((block, "off")), runs[key].get((block, "on"))
            if b is None or a is None:
                continue
            for gi in sorted(set(b) & set(a)):
                delta = a[gi][0] - b[gi][0]
                d.append(delta)
                base_sum += b[gi][0]
                arm_sum += a[gi][0]
                if b[gi][1] and b[gi][1] != a[gi][1]:
                    dig += 1
                if delta > 0:
                    slower_list.append((deck, fmt, depth, budget, block, gi, b[gi][0], a[gi][0]))
        n = len(d)
        if n == 0:
            continue
        mean = sum(d) / n
        var = sum((x - mean) ** 2 for x in d) / max(1, n - 1)
        se = sqrt(var / n) if n > 1 else 0.0
        t = mean / se if se > 0 else 0.0
        net = sum(d)
        deck_net[deck] += net
        print(f"{deck + '.' + fmt + '.d' + str(depth) + '.b' + str(budget):34s} {n:6d} "
              f"{sum(1 for x in d if x < 0):6d} {sum(1 for x in d if x > 0):6d} {net:+6d} {mean:+9.4f} "
              f"{se:8.4f} {t:+6.2f} {dig:5d}  {base_sum / n:.4f}->{arm_sum / n:.4f}")
    print("\nper-deck net (turns, on - off; negative = faster):")
    for deck in DECKS:
        print(f"  {deck:16s} {deck_net[deck]:+d}")
    print(f"\nSLOWER games ({len(slower_list)}):")
    for s in slower_list:
        deck, fmt, depth, budget, block, gi, wb, wa = s
        print(f"  {deck}.{fmt}.d{depth}.b{budget}.{block} gi={gi}: off {wb} -> on {wa}  "
              f"(repro: seed {BLOCKS[block] + gi}, game-index {gi % 10})")


if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in ("manifest", "report"):
        sys.exit(__doc__)
    (manifest if sys.argv[1] == "manifest" else report)(sys.argv[2:])
