#!/usr/bin/env python3
"""For each Goblins tutor regression: the STATE we searched in, what we searched for then, and what
we search for now.

The point (user, 2026-08-04): "list each regression game and the state in which we searched
originally including what we searched for. Then we can list what the new rules would search in that
situation and try to figure out how we can adjust it."

Aggregate deltas say a ranking is worse; they never say WHICH rule to change. This prints, per game,
the board/hand/mana at the moment Goblin Matron's ETB resolved, the card each arm fetched, and the
ranked candidate list the current rules produce -- so a rule adjustment can be argued from the state
rather than from a turn-count delta.

Both arms are the SAME binary; the arms are env flags (MTG_GOBLIN_SWING_LETHAL /
MTG_GOBLIN_ENABLER_RANK), so there is no risk of comparing two different builds.

Usage:
    python3 test/goblins_tutor_states.py                # the tracked searched regressions
    python3 test/goblins_tutor_states.py --d0           # the d0 cluster as well (clean diagnostic:
                                                        #   d0 takes cands[0], so no search churn)
"""
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build/Release/mtg")
DECK = "decks/Goblins/Goblins.cod"
PROF = "decks/Goblins/Goblins.profile.json"

# (label, seed_base, game_index, depth, budget_ms, was, now).  Budgets mirror regression_cases.sh.
SEARCHED = [
    ("overnight d3_s4004 gi573", 4004, 573, 3, 20, 3, 4),
    ("overnight d3_s7007 gi849", 7007, 849, 3, 20, 5, 6),
    ("smoke     d3_s1001 gi44",  1001,  44, 3, 10, 4, 5),
    ("regr      d3_s2002 gi289", 2002, 289, 3, 10, 5, 6),   # churn (recovers with budget)
    ("regr      d3_s3003 gi112", 3003, 112, 3, 10, 4, 5),   # churn
]
D0 = [
    ("overnight d0_s4004 gi871", 4004, 871, 0, 0, 5, 6),
    ("overnight d0_s4004 gi1045", 4004, 1045, 0, 0, 5, 6),
    ("overnight d0_s6006 gi126", 6006, 126, 0, 0, 5, 6),
    ("overnight d0_s8008 gi243", 8008, 243, 0, 0, 5, 6),
    ("smoke     d0_s1001 gi19",  1001,  19, 0, 0, 5, 6),
    ("smoke     d0_s1001 gi222", 1001, 222, 0, 0, 5, 6),
]

OLD_ENV = {"MTG_GOBLIN_SWING_LETHAL": "0", "MTG_GOBLIN_ENABLER_RANK": "0"}


def run(base, gi, depth, budget, env_extra):
    tmp = tempfile.mkdtemp(prefix="tutorstate_")
    try:
        env = dict(os.environ, **env_extra)
        subprocess.run(
            [BIN, DECK, "--profile", PROF, "--games", "1", "--seed", str(base + gi),
             "--game-index", str(gi), "--depth", str(depth), "--budget-ms", str(budget),
             "--ignore-play-profile", "--log-dir", tmp],
            cwd=ROOT, capture_output=True, text=True, env=env)
        f = glob.glob(os.path.join(tmp, "*.json"))
        return json.load(open(f[0])) if f else None
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def names(numbering, nums):
    out = []
    for n in nums:
        for cn, ns in numbering.items():
            if n in ns:
                out.append(cn)
                break
    return out


def tutor_state(g):
    """The state entering the phase where Matron's ETB resolved, + what it fetched."""
    if not g:
        return None
    numbering = g.get("cardNumbering", {})
    segs = g.get("turns", [])
    for i, seg in enumerate(segs):
        for a in seg.get("actions", []):
            if a.get("type") != "REVEAL" or "searched" not in a.get("source", ""):
                continue
            prev = segs[i - 1].get("boardAfter", {}) if i else {}
            bf = prev.get("battlefield", [])
            pre = [x for x in seg["actions"]
                   if x is not a and x.get("type") in ("PLAY_LAND", "CAST_SPELL")
                   and seg["actions"].index(x) < seg["actions"].index(a)]
            return dict(
                turn=seg.get("turn"),
                fetched=[c["cardName"] for c in a.get("lookedAt", [])],
                lands=[p["cardName"] for p in bf if p.get("isLand")],
                creatures=[p["cardName"] for p in bf if not p.get("isLand")],
                hand=names(numbering, prev.get("hand", [])),
                opp_life=prev.get("opponentLife"),
                before=[f"{x['type'].split('_')[0].lower()} {x['cardName']}" for x in pre],
            )
    return None


def block(label, was, now, old, new):
    print(f"\n=== {label}   T{was} -> T{now} ===")
    if not new:
        print("   (no tutor resolved in the new arm -- the divergence is upstream of the fetch)")
    for tag, st in (("WAS (pre-ranking)", old), ("NOW (shipped)", new)):
        if not st:
            print(f"  {tag:<20} (no Matron ETB resolved)")
            continue
        print(f"  {tag:<20} turn {st['turn']}  ->  FETCHED: {', '.join(st['fetched'])}")
        print(f"    {'':<18} board:  {', '.join(st['creatures']) or '(none)'}"
              f"   | lands {len(st['lands'])} | opp {st['opp_life']}")
        print(f"    {'':<18} hand:   {', '.join(st['hand']) or '(empty)'}")
        if st["before"]:
            print(f"    {'':<18} earlier this turn: {'; '.join(st['before'])}")


def main():
    cases = list(SEARCHED)
    if "--d0" in sys.argv:
        cases += D0
    if not os.path.exists(BIN):
        print(f"missing {BIN} -- run ./build.sh first", file=sys.stderr)
        return 2
    for label, base, gi, depth, budget, was, now in cases:
        old = tutor_state(run(base, gi, depth, budget, OLD_ENV))
        new = tutor_state(run(base, gi, depth, budget, {}))
        block(label, was, now, old, new)
    print("\nranked list for any state above:  MTG_TUTOR_RANK_DUMP=1 <same command>  "
          "(dump is deduped by situation; match on the printed inputs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
