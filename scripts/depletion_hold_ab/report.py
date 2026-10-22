"""Paired report for the MTG_PAY_DEPLETION_HOLD cross-deck A/B (make_manifest.py). Logic copied from
the Niv round-3 report (build/scratch/niv/ab3_report.py): every comparison is PAIRED on game index over the games BOTH arms finished
(a game abandoned in either arm is dropped from that pair); lower win turn is better and an unwon game
counts as 9 (the suite's metric: max_turns 8 + 1). The .wins files write an unwon game as win_turn <= 0, so
it is mapped to 9 here -- read raw (as ab3_report.py does) a loss would score as a turn-0 WIN. `play`
counts the paired games whose decision-stream digest differs (the payer moved the game even where the
win turn did not). Per deck: each cell
(d3 b10, play b20), then the deck aggregate over both cells (the adoption bar is per DECK: net <= 0), with
the core-s and units ratios on/off. usage: report.py <dir> [d3_seed play_seed]   (<dir>/batch.log, <dir>/wins/;
the seeds default to the A/B's 500000 / 600000)"""
import math
import os
import re
import sys

D = sys.argv[1]
CELLS = (("d3", int(sys.argv[2]) if len(sys.argv) > 3 else 500000),
         ("play", int(sys.argv[3]) if len(sys.argv) > 3 else 600000))
DECKS = ("niv", "bruna", "th", "dragonstorm", "mirrorwing", "breaching", "galadriel", "fungusv3",
         "angels", "critter", "kittyv3", "whiteknights")


LOSS = 9   # max_turns 8 + 1


def read_wins(path):
    """{game_index: (win_turn with a loss as LOSS, digest)}"""
    out = {}
    if os.path.exists(path):
        for line in open(path):
            f = line.split()
            if len(f) >= 2:
                wt = int(f[1])
                out[int(f[0])] = (wt if wt > 0 else LOSS, f[2] if len(f) > 2 else "")
    return out


def read_aband(path):
    return {int(x) for x in open(path).read().split()} if os.path.exists(path) else set()


def stats(d):
    if not d:
        return 0.0, 0.0, 0.0
    md = sum(d) / len(d)
    sd = math.sqrt(sum((x - md) ** 2 for x in d) / max(1, len(d) - 1))
    se = sd / math.sqrt(len(d))
    return md, se, (md / se) if se else 0.0


cost = {}
for line in open(os.path.join(D, "batch.log"), errors="replace"):
    m = re.match(r"^(\S+): played=(\d+) avg=([\d.]+) digest=\S+ ms=(\d+) units=(\d+)", line.strip())
    if m:
        cost[m.group(1)] = (int(m.group(2)), float(m.group(3)), int(m.group(4)), int(m.group(5)))

tot = {"off": [0, 0], "on": [0, 0]}
print("%-13s %-5s %6s %8s %8s %8s %6s %6s %6s %5s %5s %5s %7s %7s"
      % ("deck", "cell", "n", "off", "on", "on-off", "se", "t", "drop", "play", "fast", "slow", "core-s", "units"))
for deck in DECKS:
    agg = []
    agg_cost = {"off": [0, 0], "on": [0, 0]}
    for cell, seed in CELLS:
        nm = {a: "dh_%s_%s_%s_s%d" % (deck, a, cell, seed) for a in ("off", "on")}
        if nm["off"] not in cost and not os.path.exists(os.path.join(D, "wins", nm["off"] + ".wins")):
            continue
        w = {a: read_wins(os.path.join(D, "wins", nm[a] + ".wins")) for a in nm}
        ab = {a: read_aband(os.path.join(D, "wins", nm[a] + ".abandoned")) for a in nm}
        common = sorted((set(w["off"]) & set(w["on"])) - ab["off"] - ab["on"])
        dropped = len(ab["off"] | ab["on"])
        d = [w["on"][g][0] - w["off"][g][0] for g in common]
        moved = sum(1 for g in common if w["on"][g][1] != w["off"][g][1])
        agg += d
        md, se, t = stats(d)
        c = {a: cost.get(nm[a], (0, 0.0, 0, 0)) for a in nm}
        for a in nm:
            agg_cost[a][0] += c[a][2]; agg_cost[a][1] += c[a][3]
            tot[a][0] += c[a][2]; tot[a][1] += c[a][3]
        mo = sum(w["off"][g][0] for g in common) / max(1, len(common))
        mn = sum(w["on"][g][0] for g in common) / max(1, len(common))
        print("%-13s %-5s %6d %8.4f %8.4f %+8.4f %6.4f %+6.2f %6d %5d %5d %5d %6.3fx %6.3fx"
              % (deck, cell, len(d), mo, mn, md, se, t, dropped, moved, sum(1 for x in d if x < 0),
                 sum(1 for x in d if x > 0), c["on"][2] / max(1, c["off"][2]), c["on"][3] / max(1, c["off"][3])))
    if agg:
        md, se, t = stats(agg)
        print("%-13s %-5s %6d %8s %8s %+8.4f %6.4f %+6.2f %6s %5s %5d %5d %6.3fx %6.3fx   <- DECK (net <= 0 is the bar)"
              % (deck, "ALL", len(agg), "", "", md, se, t, "", "", sum(1 for x in agg if x < 0),
                 sum(1 for x in agg if x > 0), agg_cost["on"][0] / max(1, agg_cost["off"][0]),
                 agg_cost["on"][1] / max(1, agg_cost["off"][1])))
print("TOTAL core-s off %.0f on %.0f (%.3fx)   units off %d on %d (%.3fx)"
      % (tot["off"][0] / 1000.0, tot["on"][0] / 1000.0, tot["on"][0] / max(1, tot["off"][0]),
         tot["off"][1], tot["on"][1], tot["on"][1] / max(1, tot["off"][1])))
