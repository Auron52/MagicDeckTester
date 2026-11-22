#!/usr/bin/env python3
"""Pooled held-out A/B, agent 3 (after the rebase onto the 1 Drop Burn update: MTG_FIREBLAST_FLOAT,
MTG_ONEDROPBURN_FIREBLAST_EARLY and MTG_ONEDROPBURN_M2_HELD are origin defaults ON, so every arm carries them).
Arms over the levers that survive: dmg = MTG_PO_DAMAGE_ORDER, pins = MTG_FIREBLAST_COPY_PINS, lava =
MTG_LAVAMANCER_TARGET_PRUNE (USER-ordered), all = the three. Each deck only gets the arms that can move it:
Creature Burn holds Fireblast but no Needle Drop / spectacle / Incinerator / Lavamancer; Modern RW Burn holds
spectacle + Vexing Devil but no Fireblast / Lavamancer. Burn holds none of the three (inert, not run).
Fresh seeds, never the gate's 30000000+ games. usage: mk_ab3.py <out.json> [scale] [seed]"""
import json, sys
out = sys.argv[1]; scale = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
SEED = int(sys.argv[3]) if len(sys.argv) > 3 else 71001
F = {"onedropburn": ("decks/1 Drop Burn/1 Drop Burn.cod", "decks/1 Drop Burn/1 Drop Burn.profile.json"),
     "midrangeburn": ("decks/Midrange Burn/Midrange Burn.cod", "decks/Midrange Burn/Midrange Burn.profile.json"),
     "creatureburn": ("decks/Creature Burn/Creature Burn.cod", "decks/Creature Burn/Creature Burn.profile.json"),
     "modernrwburn": ("decks/Modern RW Burn/Modern RW Burn.cod", "decks/Modern RW Burn/Modern RW Burn.profile.json")}
L = ("MTG_PO_DAMAGE_ORDER", "MTG_FIREBLAST_COPY_PINS", "MTG_LAVAMANCER_TARGET_PRUNE")
def arm(*on): return {k: (k in on) for k in L}
A = {"base": arm(), "dmg": arm(L[0]), "pins": arm(L[1]), "lava": arm(L[2]), "all": arm(*L)}
ARMS = {"onedropburn": ["base", "dmg", "pins", "lava", "all"], "midrangeburn": ["base", "dmg", "pins", "lava", "all"],
        "creatureburn": ["base", "pins"], "modernrwburn": ["base", "dmg"]}
# (deck key, base, depth, budget, games)
CELLS = []
for b in ("onedropburn", "midrangeburn", "creatureburn"):
    CELLS += [(b, b, 0, 0, 2000), (b, b, 3, 10, 2000), (b, b, 5, 20, 2000), (b + "2hg", b, 5, 20, 400)]
CELLS += [("modernrwburn", "modernrwburn", 3, 10, 1000), ("modernrwburn", "modernrwburn", 5, 20, 1000)]
jobs = []
for deck, base, depth, bud, games in CELLS:
    g = max(1, int(games * scale))
    extra = {"starting_life": 30, "opponent_heads": 2} if deck.endswith("2hg") else {}
    for a in ARMS[base]:
        j = {"name": f"{deck}_d{depth}_s{SEED}__{a}", "deck": F[base][0], "profile": F[base][1], **extra,
             "games": g, "seed": SEED, "budget_ms": bud, "weight": 0, "flags": A[a]}
        if depth != 5: j["depth"] = depth; j["ignore_play_profile"] = True
        jobs.append(j)
json.dump({"jobs": jobs}, open(out, "w"), indent=1)
print(f"{len(jobs)} jobs, {sum(j['games'] for j in jobs)} games")
