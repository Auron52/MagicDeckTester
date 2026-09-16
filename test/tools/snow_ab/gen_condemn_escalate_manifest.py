#!/usr/bin/env python3
"""Census the UNRECOVERABLE regressions of a Snow arm pair. ONE pooled manifest.

Gate 2 of the standing method (see test/tools/kitty_ab/gen_gr_escalate_manifest.py, which this
mirrors for Snow): a game that a test arm plays WORSE is only evidence of a deleted line if it is
still worse after escalating BOTH budget and depth, and after escalating BOTH ARMS. A budget-only
escalation has proved nothing twice in this repo, and one extra depth ply has fixed games outright.

What survives 100x budget AND +1 ply is a line the arm cannot reach at any budget -- the class the
no-lossy-truncation bar rejects outright. Condemnation FIRE COUNTS are not the scorer: "volume is
not harm" has been the wrong predictor four times in this arc.

Snow ships depth=5 budget=20ms, so the cells are 100x budget and 100x budget + 1 ply. Every arm is
escalated at every disagreeing game, in ONE pooled queue, so the box sees a single tail.

Usage: gen_condemn_escalate_manifest.py <wins-dir> <seed-base> <arm> <arm> [<arm>...] > m.json
       (arm names are the `<deck>__<arm>` suffixes the play batch used)
"""
import json
import pathlib
import sys

DECK         = "decks/Snow/Snow.cod"
PROFILE      = "decks/Snow/Snow.profile.json"
MAX_TURNS    = 8
NATIVE_DEPTH = 5
CELLS = [("b2000", 2000, 0), ("b2000d6", 2000, 1)]


def load(path):
    out = {}
    if not path.exists():
        sys.exit(f"missing {path}")
    for line in path.read_text().splitlines():
        p = line.split()
        if len(p) >= 2:
            out[int(p[0])] = MAX_TURNS + 1 if int(p[1]) < 0 else int(p[1])
    return out


def main():
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    root = pathlib.Path(sys.argv[1])
    seed_base = int(sys.argv[2])
    arms = sys.argv[3:]

    wins = {a: load(root / f"snow__{a}.wins") for a in arms}
    common = set.intersection(*(set(w) for w in wins.values()))

    jobs, changed = [], []
    for gi in sorted(common):
        turns = {a: wins[a][gi] for a in arms}
        if len(set(turns.values())) == 1:
            continue                        # every arm agrees -> nothing to escalate
        changed.append((gi, turns))
        for cell, budget, ddelta in CELLS:
            for arm in arms:
                job = {
                    "name":       f"{arm}_{cell}_g{gi}",
                    "deck":       DECK,
                    "profile":    PROFILE,
                    "games":      1,
                    # The batch runner seeds game gi of a job as seed + gi, so a single-game
                    # re-play of gi must carry the SAME (seed_base + gi) the pooled run gave it.
                    "seed":       seed_base + gi,
                    "game_index": gi,
                    "depth":      NATIVE_DEPTH + ddelta,
                    "budget_ms":  budget,
                }
                flags = ARMS.get(arm)
                if flags:
                    job["flags"] = flags
                jobs.append(job)

    print(f"# {len(changed)} disagreeing games over arms {arms} -> {len(jobs)} jobs",
          file=sys.stderr)
    for gi, turns in changed:
        print(f"#   gi={gi}  " + "  ".join(f"{a}={turns[a]}" for a in arms), file=sys.stderr)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


# Arm -> per-job heurarm flag block, matching the play batch that produced the wins files.
ARMS = {
    "base":     {"MTG_SNOW_CAST_ORDER": True, "MTG_SNOW_CONDEMN": False},
    "cond":     {"MTG_SNOW_CAST_ORDER": True, "MTG_SNOW_CONDEMN": True},
    "condnost": {"MTG_SNOW_CAST_ORDER": True, "MTG_SNOW_CONDEMN": True,
                 "MTG_BP_CONDEMN_SAME_TURN": False},
}


if __name__ == "__main__":
    main()
