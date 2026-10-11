#!/usr/bin/env python3
"""Build the held-out pooled manifest: EDF only, three cells, one arm per lever-off plus all-on and all-off.

usage: mk_manifest.py <out.json> <d0_games> <d3_games> <ship_games> [arms]
arms: comma list from {on,L1,L2a,L2b,L3,L4,off}; default all seven.
Seeds (held out of ab3, whose cells start at 1645001 / 1650001): d0 5300001, d3 5310001, ship 5320001.
"""
import json, sys

ROOT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a090b37745a53c3a3"
DECK = ROOT + "/decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod"
PROF = ROOT + "/decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json"
LEVERS = {
    "L1":  "MTG_SOLVE_UNTAP_CREDIT",
    "L2a": "MTG_PREPAY_ENERGY_RESTORE",
    "L2b": "MTG_SOLVE_AURA_PAYER_CHECK",
    "L3":  "MTG_EDF_ROUTE_BOARD_DEPLOY",
    "L4":  "MTG_EDF_GOFF_VERIFY_UNPROJECTED",
}

def arm_flags(arm):
    if arm == "on":
        return {}
    if arm == "off":
        return {v: False for v in LEVERS.values()}
    return {LEVERS[arm]: False}

def main():
    out = sys.argv[1]
    n0, n3, ns = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
    arms = sys.argv[5].split(",") if len(sys.argv) > 5 else ["on", "L1", "L2a", "L2b", "L3", "L4", "off"]
    cells = [("d0", n0, 5300001, {"depth": 0, "ignore_play_profile": True}),
             ("d3", n3, 5310001, {"depth": 3, "budget_ms": 10, "ignore_play_profile": True}),
             ("ship", ns, 5320001, {"budget_ms": 20})]
    jobs = []
    # Heaviest cell first in the manifest (the runner schedules LPT anyway; this only orders the file).
    for cell, n, seed, extra in reversed(cells):
        if n <= 0:
            continue
        for arm in arms:
            if arm == "off" and cell != "d0":
                continue   # the all-off arm (= the tip) only where it is cheap
            j = {"name": "edf_%s_%s" % (cell, arm), "deck": DECK, "profile": PROF,
                 "games": n, "seed": seed}
            j.update(extra)
            f = arm_flags(arm)
            if f:
                j["flags"] = f
            jobs.append(j)
    json.dump({"jobs": jobs}, open(out, "w"), indent=1)
    print("wrote %s: %d jobs" % (out, len(jobs)))

if __name__ == "__main__":
    main()
