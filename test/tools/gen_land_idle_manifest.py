#!/usr/bin/env python3
"""MTG_LAND_IDLE_TAPPED_FIRST, THE WHOLE QUESTION, IN ONE POOLED QUEUE.

The rule: on the FIRST land drop of the game, when the untapped land's mana provably cannot be spent
this turn, play the TAPPED land instead. Deferring is free then and buys a full extra mana on every
later turn. USER ruling, 2026-09-05: "Wow, that is surprising on the untapped land T1. That is never
a good idea."

WHY IT IS MEASURED SUITE-WIDE AND NOT JUST ON FLUCTUATOR. The rule lives in GreedyLandChoiceIndex --
the ONE ranker behind all three land-drop sites (the executor's greedy drop, the enumeration's
last-resort plan-ordering tiebreak, and the rollout playout when MTG_ROLLOUT_LAND_RANKER is on) --
so it is a global heuristic change, not a deck rule. Every deck whose opening hand can hold both a
tapped and an untapped land is in its domain.

BUILT-IN NEGATIVE CONTROLS. Decks whose manabase cannot present the choice must read EXACTLY 0.0000:
burn's greedy drop is 100% forced (one legal land name) and stompy/goblins are effectively
mono-basic. A nonzero delta there means the rule is doing something other than what it claims.

CELLS. Fluctuator at all three of its shipped settings (d0/b0, d3/b10, d5/b20) because the deck's
whole gate is turn-two mana and d0 is where a ranker change shows up undamped; every other deck at
d3/b10, the suite's own searched gate. PLAY SETTINGS throughout -- an off-settings diagnosis has
lied by 700x in this repo.

The deck map is READ FROM test/regression_cases.sh rather than copied. A hand-maintained duplicate
of that map is exactly what had to be rescued out of test/viewer_protocol_check.py, and the older
copy in test/tools/kitty_ab/gen_land_arc_manifest.py has already fallen behind (it predates
dragons, breaching and fluctuator).
"""
import json
import pathlib
import re
import sys

_ROOT = pathlib.Path(__file__).resolve().parents[2]


def _decks():
    """(name -> (deck, profile)) parsed out of regression_cases.sh's two associative arrays."""
    text = (_ROOT / "test" / "regression_cases.sh").read_text()
    out = {}
    for arr, slot in (("DECK_FILE", 0), ("DECK_PROF", 1)):
        block = re.search(r"declare -A %s=\((.*?)\n\)" % arr, text, re.S)
        if not block:
            raise SystemExit(f"could not find {arr} in test/regression_cases.sh")
        for name, val in re.findall(r"\[(\w+)\]=\"?([^\"\n]+)\"?", block.group(1)):
            out.setdefault(name, ["", ""])[slot] = val.strip()
    missing = [k for k, v in out.items() if not v[0] or not v[1]]
    if missing:
        raise SystemExit(f"deck rows missing a half: {missing}")
    return {k: tuple(v) for k, v in out.items()}


ARMS = {"base": {}, "idle": {"MTG_LAND_IDLE_TAPPED_FIRST": True}}

# Two disjoint blocks so the winner is confirmed on seeds it was not chosen on, and both are
# disjoint from the suite's own seeds (smoke 1001, regression 2002/3003, overnight 4004-7007).
# Spaced far wider than games-per-job: base seeds spaced UNDER the job size REPLAY games, and the
# tell is zero variance between blocks.
BLOCKS = {"train": 610001, "hold": 1220001}

# (depth, budget_ms, games) -- games sized off measured per-game core cost: d0 ~10us, d3 ~2.5s,
# d5 ~5.2s. Fluctuator carries the extra cells; the rest of the suite is the regression check.
FLUCT_CELLS = [(0, 0, 6000), (3, 10, 1000), (5, 20, 500)]
SUITE_CELLS = [(3, 10, 400)]


def main():
    scale = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    decks = _decks()
    jobs = []
    for deck, (path, prof) in sorted(decks.items()):
        cells = FLUCT_CELLS if deck == "fluctuator" else SUITE_CELLS
        for depth, budget, games in cells:
            for arm, flags in ARMS.items():
                for block, seed in BLOCKS.items():
                    job = {
                        "name": f"{arm}.{deck}.d{depth}.{block}",
                        "deck": path,
                        "profile": prof,
                        "games": max(1, int(games * scale)),
                        "seed": seed,
                        "depth": depth,
                        "budget_ms": budget,
                        "ignore_play_profile": True,
                        "weight": 0,
                    }
                    if flags:
                        job["flags"] = dict(flags)
                    jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
