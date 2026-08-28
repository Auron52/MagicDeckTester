#!/usr/bin/env python3
"""Re-play every DISAGREEING Mirrorwing game of the land arc at escalated budget AND depth.
ONE pooled manifest.

Gate 2 of the standing method: "unrecoverable" requires escalating BOTH budget and depth, and
escalating BOTH ARMS -- escalating only one compares two different configurations and answers
nothing. A budget-only escalation has proved nothing twice in this repo, while ONE extra depth ply
fixed two of three games.

What survives 100x budget AND +1 ply is a genuinely DELETED line: the arm prefers a worse line, not
merely a diluted one. That is the class the no-lossy-truncation bar rejects outright.

AND IT IS THE WHOLE TEST FOR A PRUNE, per the USER's own reasoning: a prune that only removes casts
the search genuinely considered and declined CANNOT lose at 100x budget, so every survivor is a
FALSE PREMISE rather than a tuning cost. That argument is what found bug 8; it applies to
MTG_BP_CONDEMN_LAND unchanged, and it is why condemnation fire counts are not the scorer here (they
have predicted the wrong sign four times in this arc).

Mirrorwing ships depth=5 budget=20ms, so the cells are 100x budget and 100x budget + 1 ply.

Usage: gen_land_escalate_manifest.py <wins-dir> <arm> <arm> [<arm>...] > manifest.json
       (arm names are keys of COND_ARMS in gen_land_arc_manifest.py, e.g. base cond cond_land)
"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from gen_land_arc_manifest import COND_ARMS, BLOCKS, MW   # noqa: E402

MAX_TURNS    = 8
NATIVE_DEPTH = 5
CELLS = [("b2000", 2000, 0), ("b2000d6", 2000, 1)]


def load(path):
    out = {}
    for line in path.read_text().splitlines():
        p = line.split()
        if len(p) >= 2:
            out[int(p[0])] = MAX_TURNS + 1 if int(p[1]) < 0 else int(p[1])
    return out


def main():
    root = pathlib.Path(sys.argv[1])
    arms = sys.argv[2:]
    if len(arms) < 2:
        sys.exit("need at least two arms")
    for a in arms:
        if a not in COND_ARMS:
            sys.exit(f"unknown arm {a!r}; known: {sorted(COND_ARMS)}")

    jobs, changed = [], []
    for block, seed_base in BLOCKS.items():
        wins = {a: load(root / f"A.{a}.{block}.wins") for a in arms}
        common = set.intersection(*(set(w) for w in wins.values()))
        for gi in sorted(common):
            turns = {a: wins[a][gi] for a in arms}
            if len(set(turns.values())) == 1:
                continue                       # every arm agrees -> nothing to escalate
            changed.append((block, gi, turns))
            for cell, budget, ddelta in CELLS:
                for arm in arms:
                    job = {
                        "name":       f"E.{arm}.{block}_{cell}_g{gi}",
                        "deck":       MW[0],
                        "profile":    MW[1],
                        "games":      1,
                        "seed":       seed_base + gi,
                        "game_index": gi,
                        "budget_ms":  budget,
                    }
                    # DEPTH is the play POLICY and an enabled value_play block owns it, so only the
                    # depth cell opts out; the budget cell keeps the shipped policy.
                    if ddelta:
                        job["depth"] = NATIVE_DEPTH + ddelta
                        job["ignore_play_profile"] = True
                    if COND_ARMS[arm]:
                        job["flags"] = dict(COND_ARMS[arm])
                    jobs.append(job)

    print(f"# {len(changed)} disagreeing games over arms {arms} -> {len(jobs)} jobs",
          file=sys.stderr)
    for blk, gi, turns in changed:
        print(f"#   {blk} gi={gi}  " + "  ".join(f"{a}={turns[a]}" for a in arms), file=sys.stderr)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
