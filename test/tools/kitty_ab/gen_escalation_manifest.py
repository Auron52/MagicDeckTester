#!/usr/bin/env python3
"""UNRECOVERABLE-WIN-TURN CENSUS: which win-turn differences survive escalation?

USER 2026-08-28: "let's get as many games that have unrecoverable better win turns as we can."

THE TEST, and why it is the only one that settles a quality claim here. An aggregate delta mixes two
completely different things: BUDGET CHURN (the arm reached a worse line because the search spent its
20ms differently, and more budget recovers it) and a DELETED LINE (the arm cannot reach the better
win turn at ANY budget or depth). Only the second is a real quality loss, and only the second counts
against the no-lossy-truncation bar. Escalating BOTH arms at 100x budget AND +1 depth ply separates
them: a difference that survives both is a line one arm genuinely cannot express.

Hinata plays at depth 5 / 20ms (its value_play block), so the escalated cell is depth 6 / 2000ms.
Both arms are escalated -- escalating only the loser is the classic way to manufacture a false
"recovered", since the winner may also improve.

Emits one single-game job per (arm, game), using the repro convention that actually reproduces a
batch game: seed = base_seed + game_index AND game_index = gi. Getting only the seed right replays a
DIFFERENT game.
"""
import json
import pathlib
import sys

DECK = ("decks/Hinata2/Hinata2.cod", "decks/Hinata2/Hinata2.profile.json")
BLOCKS = {"train": 5500001, "hold": 6600001}
ESC_DEPTH, ESC_BUDGET = 6, 2000
ARMS = {
    "ord":       {"MTG_HINATA_ORDER_FULL": True},
    "ord_float": {"MTG_HINATA_ORDER_FULL": True, "MTG_HINATA_MANA_FLOAT_RANK": True},
    "cond_ng":   {"MTG_HINATA_ORDER_FULL": True, "MTG_BP_CLASSIFY": True, "MTG_BP_SITE3": True,
                  "MTG_BP_NO_GREEDY_CONT": True},
    "cond_ng_float": {"MTG_HINATA_ORDER_FULL": True, "MTG_BP_CLASSIFY": True, "MTG_BP_SITE3": True,
                      "MTG_BP_NO_GREEDY_CONT": True, "MTG_HINATA_MANA_FLOAT_RANK": True},
}


def wins(path):
    out = {}
    if not path.exists():
        return out
    for line in path.read_text().splitlines():
        q = line.split()
        if len(q) >= 2:
            out[int(q[0])] = 9 if int(q[1]) < 0 else int(q[1])
    return out


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "logs/landcondemn/ordv3")
    jobs, seen = [], set()
    for block, base_seed in BLOCKS.items():
        base = wins(root / f"base.hinata.{block}.wins")
        for arm, flags in ARMS.items():
            a = wins(root / f"{arm}.hinata.{block}.wins")
            if not a or not base:
                continue
            diff = [g for g in sorted(set(a) & set(base)) if a[g] != base[g]]
            for g in diff:
                # Both arms at the escalated cell -- the baseline too, exactly once per (block, game).
                for tag, fl in (("base", {}), (arm, flags)):
                    key = (block, g, tag)
                    if key in seen:
                        continue
                    seen.add(key)
                    job = {"name": f"{tag}.{block}.g{g}", "deck": DECK[0], "profile": DECK[1],
                           "games": 1, "seed": base_seed + g, "game_index": g,
                           "depth": ESC_DEPTH, "budget_ms": ESC_BUDGET,
                           # REQUIRED: this deck has an ENABLED value_play block, and pinning depth
                           # against one is refused outright ("omit --depth to use it, or
                           # --ignore-play-profile to override the depth too"). Probed before
                           # launching -- without it all 303 jobs abort on the first line. It bypasses
                           # only the depth/budget POLICY (ResolvePlaySettings); the value-leaf
                           # sidecar still loads directory-relative off the profile, so the escalated
                           # cell keeps the same evaluator as play and stays comparable.
                           "ignore_play_profile": True}
                    if fl:
                        job["flags"] = dict(fl)
                    jobs.append(job)
    json.dump({"jobs": jobs}, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
