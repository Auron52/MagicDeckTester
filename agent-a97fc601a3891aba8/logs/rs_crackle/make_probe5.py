#!/usr/bin/env python3
"""The 5 slower Hinata d0 games of the ab3 round (rc3/hinata), as one pooled manifest.

    python3 make_probe5.py <out_dir> [--with-crackle]

Arms: old/new (the 2026-10-10 fix round's levers =0 / defaults) x (crackle lever off/on when
--with-crackle; the baseline binary does not know MTG_CRACKLE_COUNT_EXACT so it is omitted there).
"""
import json, os, sys

WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8"
DECK = WT + "/decks/Hinata2/Hinata2.cod"
PROF = WT + "/decks/Hinata2/Hinata2.profile.json"
OLD = ["MTG_DRAGONSTORM_ORDER_OPAQUE", "MTG_PAYABLE_ORDER_RITUALS", "MTG_COLOR_EXACT_SCALED",
       "MTG_ATTACK_TAP_NEED_EXACT", "MTG_ATTACK_TAP_ABILITY_DISCOUNT", "MTG_ROLLOUT_LAND_ACT_COLOR",
       "MTG_SOLVE_VERIFIED_WIN", "MTG_EDF_GORGE_RED_SIZING", "MTG_SOLVE_PENDING_AURA_EXACT"]
GAMES = [(1330763, 762), (1330874, 873), (1331155, 1154), (1331326, 1325), (1331550, 1549)]

out = sys.argv[1]
crk = "--with-crackle" in sys.argv
jobs = []
for seed, gi in GAMES:
    for arm in ("old", "new"):
        for cx in ((False, True) if crk else (None,)):
            fl = {k: False for k in OLD} if arm == "old" else {}
            if cx is not None:
                fl["MTG_CRACKLE_COUNT_EXACT"] = cx
            name = "p5_%s_%s_g%d" % (arm, "base" if cx is None else ("fix" if cx else "nofix"), gi)
            j = {"name": name, "deck": DECK, "profile": PROF, "games": 1, "seed": seed,
                 "game_index": gi, "budget_ms": 0, "depth": 0, "ignore_play_profile": True}
            if fl:
                j["flags"] = fl
            jobs.append(j)
os.makedirs(out, exist_ok=True)
json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
print("wrote", len(jobs), "jobs")
