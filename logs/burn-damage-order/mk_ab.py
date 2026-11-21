#!/usr/bin/env python3
"""Pooled held-out A/B for MTG_PO_DAMAGE_ORDER / MTG_FIREBLAST_FLOAT (burn-damage-order). One manifest, every arm,
paired seeds (fresh base, never the mana-clean gate's 30000000+ games). usage: mk_ab.py <out.json> [scale]"""
import json, sys
out = sys.argv[1]; scale = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
SEED = int(sys.argv[3]) if len(sys.argv) > 3 else 71001
F = {"onedropburn": ("decks/1 Drop Burn/1 Drop Burn.cod", "decks/1 Drop Burn/1 Drop Burn.profile.json"),
     "midrangeburn": ("decks/Midrange Burn/Midrange Burn.cod", "decks/Midrange Burn/Midrange Burn.profile.json"),
     "burn": ("decks/Burn/Burn.txt", "decks/Burn/Burn.profile.json"),
     "modernrwburn": ("decks/Modern RW Burn/Modern RW Burn.cod", "decks/Modern RW Burn/Modern RW Burn.profile.json")}
A = {"base":  {"MTG_FIREBLAST_FLOAT": False, "MTG_PO_DAMAGE_ORDER": False, "MTG_ONEDROPBURN_FIREBLAST_EARLY": False},
     "float": {"MTG_FIREBLAST_FLOAT": True,  "MTG_PO_DAMAGE_ORDER": False, "MTG_ONEDROPBURN_FIREBLAST_EARLY": False},
     "dmg":   {"MTG_FIREBLAST_FLOAT": False, "MTG_PO_DAMAGE_ORDER": True,  "MTG_ONEDROPBURN_FIREBLAST_EARLY": False},
     "fdmg":  {"MTG_FIREBLAST_FLOAT": True,  "MTG_PO_DAMAGE_ORDER": True,  "MTG_ONEDROPBURN_FIREBLAST_EARLY": False},
     "early": {"MTG_FIREBLAST_FLOAT": True,  "MTG_PO_DAMAGE_ORDER": False, "MTG_ONEDROPBURN_FIREBLAST_EARLY": True}}
ARMS = {"onedropburn": ["base", "float", "dmg", "fdmg", "early"], "midrangeburn": ["base", "float", "dmg", "fdmg"],
        "burn": ["base", "fdmg"], "modernrwburn": ["base", "fdmg"]}
# (deck key, base, depth, budget, games)
CELLS = [("onedropburn", "onedropburn", 0, 0, 2000), ("onedropburn", "onedropburn", 3, 10, 2000),
         ("onedropburn", "onedropburn", 5, 20, 2000), ("onedropburn2hg", "onedropburn", 5, 20, 400),
         ("midrangeburn", "midrangeburn", 0, 0, 2000), ("midrangeburn", "midrangeburn", 3, 10, 2000),
         ("midrangeburn", "midrangeburn", 5, 20, 2000), ("midrangeburn2hg", "midrangeburn", 5, 20, 400),
         ("burn", "burn", 3, 10, 1000), ("burn", "burn", 5, 20, 1000),
         ("modernrwburn", "modernrwburn", 3, 10, 1000), ("modernrwburn", "modernrwburn", 5, 20, 1000)]
jobs = []
for deck, base, depth, bud, games in CELLS:
    g = max(1, int(games * scale))
    extra = {"starting_life": 30, "opponent_heads": 2} if deck.endswith("2hg") else {}
    for arm in ARMS[base]:
        j = {"name": f"{deck}_d{depth}_s{SEED}__{arm}", "deck": F[base][0], "profile": F[base][1], **extra,
             "games": g, "seed": SEED, "budget_ms": bud, "weight": 0, "flags": A[arm]}
        if depth != 5: j["depth"] = depth; j["ignore_play_profile"] = True
        jobs.append(j)
json.dump({"jobs": jobs}, open(out, "w"), indent=1)
print(f"{len(jobs)} jobs, {sum(j['games'] for j in jobs)} games")
