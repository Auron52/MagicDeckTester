#!/usr/bin/env python3
"""Heliod placement-case and flip frequencies over executor main phases (MTG_USER_ORDER_TRACE notes).

  python3 -I heliod_census.py build OUT      -> OUT/manifest.json (critter + critter2hg, the A/B's first chunks)
  python3 -I heliod_census.py count OUT      -> per cell: mains, casts lists, heliod cases, flips
"""
import json, os, re, sys
from collections import Counter, defaultdict

WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a239469ed898c5c26"
DECK = os.path.join(WT, "decks/CritterLifegain/CritterLifegain.cod")
PROF = os.path.join(WT, "decks/CritterLifegain/CritterLifegain.profile.json")
CELLS = [("ship", None, None), ("d3", 3, 10), ("d0", 0, 0)]
N = {"critter": {"ship": 2000, "d3": 2000, "d0": 6000}, "critter2hg": {"ship": 1000, "d3": 1000, "d0": 3000}}
CHUNK = 50


def build(out):
    jobs = []
    for ci, (cname, depth, bud) in enumerate(CELLS):
        for k, key in enumerate(("critter", "critter2hg")):
            base = 11200001 + 100000 * k + 30000 * ci
            for c in range(0, N[key][cname], CHUNK):
                j = {"name": "%s_%s_new.c%d" % (key, cname, c), "deck": DECK, "profile": PROF, "games": CHUNK,
                     "seed": base + c, "game_index": c}
                if depth is not None:
                    j.update({"depth": depth, "budget_ms": bud, "ignore_play_profile": True})
                if key == "critter2hg":
                    j.update({"starting_life": 30, "opponent_heads": 2})
                jobs.append(j)
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("%d jobs" % len(jobs))


def count(out):
    # The trace has no job name; game seeds identify the cell (11200001 + 100000k + 30000c + gi).
    # Only the NEW arm writes notes (the =0 arm's CastOrderTraceNote is empty); every other counter below counts
    # trace lines from whatever ran, so in a two-arm batch it is both arms together.
    def cell_of(gs):
        off = gs - 11200001
        if off < 0 or off >= 200000:
            return None                      # an identity deck's game
        k, r = divmod(off, 100000)
        c = r // 30000
        return ("critter", "critter2hg")[k], CELLS[c][0]
    # seq holds card names WITH spaces ("Heliod, Sun-Crowned|Ocelot Pride"): match it lazily up to the note.
    pat = re.compile(r"\[uo-order\] gs=(\d+) t=(\d+) m=(\d) route=(\S+) .*?seq=(.*?)(?: note=(\S+))?$")
    tot = defaultdict(Counter)
    seen = set()
    for line in open(os.path.join(out, "batch.err"), errors="ignore"):
        m = pat.search(line.rstrip("\n"))
        if not m:
            continue
        gs, t, mm, route, seq, note = int(m.group(1)), int(m.group(2)), m.group(3), m.group(4), m.group(5), m.group(6)
        key = cell_of(gs)
        if key is None:
            continue
        n = len([x for x in seq.split("|") if x]) if seq else 0
        c = tot[key]
        c["lines"] += 1
        if (gs, t, mm, route) not in seen:
            seen.add((gs, t, mm, route))
        if n >= 2:
            c["lists>=2"] += 1
        if "Heliod" in seq and n >= 2:
            c["heliod_lists"] += 1
        if note:
            for part in note.split(","):
                c[part] += 1
        c["route=" + route] += 1
    for key in sorted(tot):
        c = tot[key]
        print("%-11s %-5s %s" % (key[0], key[1], " ".join("%s:%d" % kv for kv in sorted(c.items()))))


if __name__ == "__main__":
    {"build": build, "count": count}[sys.argv[1]](sys.argv[2])
