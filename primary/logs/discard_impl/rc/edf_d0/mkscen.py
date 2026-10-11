#!/usr/bin/env python3
"""Scenario fixtures for the 1261 freeze: the real T6 board, the real remaining library (known draws on
top), pre-draw hands that differ only in the cleanup shed. Runs T6..T8 at depth 0."""
import json
import os

O = "/workspaces/MagicDeckTester/logs/discard_impl/rc/edf_d0/scen2"
os.makedirs(O, exist_ok=True)

main = {"Eldrazi Displacer": 3, "Emiel the Blessed": 4, "Cloud of Faeries": 3, "Peregrine Drake": 4,
        "Wild Growth": 4, "Overgrowth": 4, "Fertile Ground": 4, "Brushland": 4, "Yavimaya Coast": 4,
        "Adarkar Wastes": 2, "Mariposa Military Base": 3, "Trace of Abundance": 4, "Conservatory": 4,
        "Kitchen": 2, "Shivan Gorge": 1, "Training Grounds": 2, "Living Wish": 4, "Eladamri's Call": 1,
        "Aether Hub": 3}
assert sum(main.values()) == 60
# 1261: seen before the T6 draw = opening 7 + draws T2-T5 (Fertile, Fertile, Displacer, Wild Growth).
seen = ["Eldrazi Displacer", "Living Wish", "Kitchen", "Overgrowth", "Mariposa Military Base", "Aether Hub",
        "Trace of Abundance", "Fertile Ground", "Fertile Ground", "Eldrazi Displacer", "Wild Growth"]
rest = dict(main)
for c in seen:
    rest[c] -= 1
# Known order of the next draws (T6, T7, T8 draw steps, then OFF's T8 loop draws).
top = ["Wild Growth", "Wild Growth", "Trace of Abundance", "Peregrine Drake", "Overgrowth", "Kitchen",
       "Yavimaya Coast", "Peregrine Drake", "Adarkar Wastes", "Conservatory", "Shivan Gorge"]
for c in top:
    rest[c] -= 1
assert min(rest.values()) >= 0
library = list(top)
for c, k in rest.items():
    library += [c] * k
assert len(library) == 49, len(library)

base = {
    "deck": "decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod",
    "profile": "decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.profile.json",
    "turn": 6, "on_the_play": True,
    "active_life": 20, "opponent_life": 20, "opponent_library_size": 40,
    "library_top": library, "library_size": 0,
    "energy_counters": 0,
    "depth": 0, "budget_ms": 0, "max_turns": 8,
    "battlefield": [
        {"name": "Aether Hub", "controller": 0, "tapped": False},
        {"name": "Mariposa Military Base", "controller": 0, "tapped": False},
        {"name": "Kitchen", "controller": 0, "tapped": False},
    ],
}
# Pre-draw T6 hands (T5 cleanup already applied).
hands = {
    "on":  ["Cloud of Faeries", "Eldrazi Displacer", "Eldrazi Displacer", "Fertile Ground", "Fertile Ground",
            "Trace of Abundance", "Wild Growth"],
    "off": ["Cloud of Faeries", "Eldrazi Displacer", "Fertile Ground", "Fertile Ground", "Overgrowth",
            "Trace of Abundance", "Wild Growth"],
    "on_minus_disp": ["Cloud of Faeries", "Eldrazi Displacer", "Fertile Ground", "Fertile Ground",
                      "Trace of Abundance", "Wild Growth"],
    "on_no_disp": ["Cloud of Faeries", "Fertile Ground", "Fertile Ground", "Trace of Abundance", "Wild Growth"],
    "on_disp_to_wg": ["Cloud of Faeries", "Eldrazi Displacer", "Wild Growth", "Fertile Ground", "Fertile Ground",
                      "Trace of Abundance", "Wild Growth"],
}
for k, h in hands.items():
    s = dict(base)
    s["hand"] = h
    s["log_out"] = f"{O}/log_1261_{k}.json"
    json.dump(s, open(f"{O}/s1261_{k}.json", "w"), indent=1)
print("wrote", len(hands))
