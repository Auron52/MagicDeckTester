#!/usr/bin/env python3
"""Census the UNRECOVERABLE regressions of a Snow arm pair. ONE pooled manifest.

Gate 2 of the standing method (see test/tools/kitty_ab/gen_gr_escalate_manifest.py, which this
mirrors for Snow): a game that a test arm plays WORSE is only evidence of a deleted line if it is
still worse after escalating BOTH budget and depth, and after escalating BOTH ARMS. A budget-only
escalation has proved nothing twice in this repo, and one extra depth ply has fixed games outright.

THESE CELLS SCREEN; THEY DO NOT ADJUDICATE. USER 2026-09-16: *"To be clear unrecoverable means not
recoverable at unlimited budget (0) and depth 8."* So what survives 100x budget AND +1 ply is a
CANDIDATE for the unrecoverable label -- cheap enough to run over every disagreeing game, which is
what this tool is for -- and the verdict needs its own `--budget-ms 0 --depth 8` cell per survivor.
Do not report a survivor here as unrecoverable; that overclaims, and it has been done in this arc.

The converse trap is worse and is the reason the screen exists at all: A MISSING CELL NEVER READS AS
SURVIVAL. A d8/b0 Snow game can run for hours (44 h repros are on record), so an adjudication run
will have cells that never land. Report the strongest bound actually measured for those; never let
an absent result stand in for a recovery.

Condemnation FIRE COUNTS are not the scorer: "volume is not harm" has been the wrong predictor four
times in this arc.

Snow ships depth=5 budget=20ms, so the cells are 100x budget and 100x budget + 1 ply. Every arm is
escalated at every disagreeing game, in ONE pooled queue, so the box sees a single tail.

Usage: gen_condemn_escalate_manifest.py <wins-dir> <seed-base> <arm> <arm> [<arm>...] > m.json
       gen_condemn_escalate_manifest.py <wins-dir> <seed-base> --from <play.manifest.json> <arm>...
       (arm names are the `<deck>__<arm>` suffixes the play batch used)

PREFER `--from`: it reads each arm's flags out of the play manifest that produced the wins files,
so the escalation cannot run an arm under different levers than the run it is adjudicating.
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


def arms_from_play_manifest(path):
    """Arm -> flags, read from the PLAY manifest that produced the wins files.

    The census must escalate each arm under the flags it actually played, and a hand-maintained
    table drifts silently: nothing downstream can tell "cond escalated without its lever" from
    "cond recovered", so a drift reads as a clean recovery and retires a real deleted line. Reading
    the play manifest makes that class of mistake unrepresentable.
    """
    spec = json.loads(pathlib.Path(path).read_text())
    out = {}
    for j in spec["jobs"]:
        name = j["name"]
        out[name[len("snow__"):] if name.startswith("snow__") else name] = j.get("flags", {})
    return out


def main():
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    root = pathlib.Path(sys.argv[1])
    seed_base = int(sys.argv[2])
    arms = sys.argv[3:]

    # `--from <play-manifest.json>` anywhere in the arm list replaces the static ARMS table.
    if "--from" in arms:
        k = arms.index("--from")
        ARMS.clear()
        ARMS.update(arms_from_play_manifest(arms[k + 1]))
        del arms[k:k + 2]
    missing = [a for a in arms if a not in ARMS]
    if missing:
        sys.exit(f"no flag block for arm(s) {missing}; known: {sorted(ARMS)}")

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
# Budget-sweep arm names (`b<ms>_<arm>`) resolve to the same flag blocks, plus the cell's budget.
# The escalation cells override budget_ms anyway -- the point of an escalation is that it is NOT
# the shipped budget -- so only the flags carry over.
for _b in (5, 10, 20, 40):
    for _a in ("base", "cond", "condraw"):
        ARMS[f"b{_b}_{_a}"] = dict(ARMS["base"] if _a == "base" else ARMS["cond"])
        if _a == "condraw":
            ARMS[f"b{_b}_{_a}"]["MTG_BP_CONDEMN_PLAN_CAST"] = False


if __name__ == "__main__":
    main()
