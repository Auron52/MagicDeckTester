#!/usr/bin/env python3
"""Control batch for MTG_HINATA_CASTABLE_MANA: its consumers are all behind default-OFF levers
(MTG_HINATA_SUBSET_CREDIT -- Reality Spasm emission + Crackle X sizing -- and lever B), so the shipped
held-out run is byte-identical by construction. This shows the lever FUNCTIONS where it is consumed:
d0, Hinata2, seeds 8340001+, arms {on, off} x context {credit = SUBSET_CREDIT on, nhh = HOLD_NEEDS_HINATA on}.

    python3 make_cm_ctl.py build <dir> ; python3 make_cm_ctl.py analyse <dir>
"""
import json, math, os, re, sys
from collections import defaultdict

WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8"
DECK = WT + "/decks/Hinata2/Hinata2.cod"
PROF = WT + "/decks/Hinata2/Hinata2.profile.json"
M = "MTG_HINATA_CASTABLE_MANA"
CTX = {"credit": {"MTG_HINATA_SUBSET_CREDIT": True}, "nhh": {"MTG_HINATA_HOLD_NEEDS_HINATA": True}}
GAMES, CHUNK, BASE = 1000, 50, 8340001


def build(out):
    jobs = []
    for ctx, fl in CTX.items():
        for c in range(0, GAMES, CHUNK):
            for arm, v in (("on", True), ("off", False)):
                f = dict(fl); f[M] = v
                jobs.append({"name": "%s_%s.c%d" % (ctx, arm, c), "deck": DECK, "profile": PROF, "games": CHUNK,
                             "seed": BASE + c, "game_index": c, "budget_ms": 0, "depth": 0,
                             "ignore_play_profile": True, "flags": f})
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("wrote", len(jobs), "jobs", sum(j["games"] for j in jobs), "games")


def analyse(out):
    w = defaultdict(dict)
    pat = re.compile(r"\[win\] job=(\w+?)_(on|off)\.c(\d+) gi=(\d+) wt=(-?\d+)")
    for line in open(os.path.join(out, "batch.err"), errors="ignore"):
        m = pat.search(line)
        if m:
            t = int(m.group(5)); w[(m.group(1), m.group(2))][int(m.group(4))] = 9 if t < 0 else t
    for ctx in CTX:
        a, b = w[(ctx, "on")], w[(ctx, "off")]
        gis = sorted(set(a) & set(b))
        d = [a[g] - b[g] for g in gis]
        n = len(d); mean = sum(d) / n
        se = math.sqrt(sum((v - mean) ** 2 for v in d) / max(1, n - 1) / n)
        print("%-6s n=%d on-off=%+d mean=%+.4f t=%.2f faster=%d slower=%d" % (
            ctx, n, sum(d), mean, mean / se if se else 0, sum(1 for v in d if v < 0), sum(1 for v in d if v > 0)))
        print("  slower:", [(g, b[g], a[g], BASE + g) for g in gis if a[g] > b[g]])


if __name__ == "__main__":
    {"build": build, "analyse": analyse}[sys.argv[1]](sys.argv[2])
