#!/usr/bin/env python3
"""Recovery check for every SLOWER game of the A/B (the adoption bar), as ONE pooled batch.

  python3 -I recovery.py build NEW_DIR OUT     reads NEW_DIR/moved.json -> OUT/manifest.json
  python3 -I recovery.py analyse NEW_DIR OUT   per slower game: base, new, stage 1, stage 2 -> recovered?

Stage 1 = --depth W --budget-ms 100 with W = max(5 (the shipped depth), the base arm's win turn);
stage 2 = --depth 8 --budget-ms 0 (unlimited). Both on the NEW binary, profile attached (value leaf + keep
table), the game reproduced by seed = base + gi, game_index = gi. A game RECOVERS at a stage when that
stage wins at or before the base arm's turn.
"""
import json, os, re, sys

WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a239469ed898c5c26"
DECK = os.path.join(WT, "decks/CritterLifegain/CritterLifegain.cod")
PROF = os.path.join(WT, "decks/CritterLifegain/CritterLifegain.profile.json")


def stage_jobs(key, cell, gi, base_wt, seed, tag="new", flags=None):
    w = max(5, base_wt if base_wt < 9 else 8)
    out = []
    for st, depth, bud in (("s1", w, 100), ("s2", 8, 0)):
        j = {"name": "%s_%s_%s_gi%d_%s" % (key, cell, st, gi, tag), "deck": DECK, "profile": PROF, "games": 1,
             "seed": seed, "game_index": gi, "depth": depth, "budget_ms": bud, "ignore_play_profile": True}
        if key == "critter2hg":
            j.update({"starting_life": 30, "opponent_heads": 2})
        if flags:
            j["flags"] = flags
        out.append(j)
    return out


def build(ndir, out):
    moved = json.load(open(os.path.join(ndir, "moved.json")))
    jobs = []
    for key, cell, gi, b, n, seed in moved["slower"]:
        jobs += stage_jobs(key, cell, gi, b, seed)
    # Stage 2 is unlimited depth 8: the most expensive jobs first.
    jobs.sort(key=lambda j: 0 if j["name"].endswith("_s2_new") or "_s2_" in j["name"] else 1)
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("%d jobs" % len(jobs))


def analyse(ndir, out):
    moved = json.load(open(os.path.join(ndir, "moved.json")))
    wins = {}
    for line in open(os.path.join(out, "batch.err"), errors="ignore"):
        m = re.search(r"\[win\] job=(\S+) gi=(\d+) wt=(-?\d+)", line)
        if m:
            wt = int(m.group(3))
            wins[m.group(1)] = 9 if wt < 0 else wt
    units = {}
    for line in open(os.path.join(out, "batch.out"), errors="ignore"):
        m = re.match(r"^(\S+): played=\d+ avg=\S+ digest=\S+ ms=(\d+) units=(\d+)", line.strip())
        if m:
            units[m.group(1)] = (int(m.group(2)), int(m.group(3)))
    rec1 = rec2 = none = 0
    print("%-11s %-5s %6s %9s %4s %4s %4s %4s  %s" % ("deck", "cell", "gi", "seed", "base", "new", "s1", "s2", "verdict"))
    for key, cell, gi, b, n, seed in moved["slower"]:
        s1 = wins.get("%s_%s_s1_gi%d_new" % (key, cell, gi))
        s2 = wins.get("%s_%s_s2_gi%d_new" % (key, cell, gi))
        if s1 is not None and s1 <= b:
            v = "stage 1"; rec1 += 1
        elif s2 is not None and s2 <= b:
            v = "stage 2"; rec2 += 1
        else:
            v = "NOT RECOVERED"; none += 1
        print("%-11s %-5s %6d %9d %4d %4d %4s %4s  %s" % (key, cell, gi, seed, b, n, s1, s2, v))
    print("\nrecovered at stage 1: %d, at stage 2: %d, not recovered: %d" % (rec1, rec2, none))


if __name__ == "__main__":
    {"build": build, "analyse": analyse}[sys.argv[1]](sys.argv[2], sys.argv[3])
