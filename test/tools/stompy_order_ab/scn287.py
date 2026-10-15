#!/usr/bin/env python3
"""gi287's T3 board (stompy ship): Forest, Forest, Fyndhorn Elves x2, Priest of Titania (all untapped,
unsick), hand Vaultborn Tyrant + Turntimber Symbiosis + Elvish Archdruid, the T3 draw a second Archdruid,
then Sol Ring. Old line: Turntimber as the land, Archdruid x2 (6 mana: three lands + Priest), both
Fyndhorns swing for 6; T4 swing 15 kills. One scenario per position of Priest among the elves.
usage: scn287.py <out_dir>"""
import json
import os
import sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for k, elves in enumerate((["Priest of Titania", "Fyndhorn Elves", "Fyndhorn Elves"],
                           ["Fyndhorn Elves", "Priest of Titania", "Fyndhorn Elves"],
                           ["Fyndhorn Elves", "Fyndhorn Elves", "Priest of Titania"])):
    bf = [{"name": "Forest", "controller": 0, "tapped": False}, {"name": "Forest", "controller": 0, "tapped": False}]
    bf += [{"name": e, "controller": 0, "tapped": False, "sick": False} for e in elves]
    j = {"deck": "decks/StompySurprise/StompySurprise.cod",
         "profile": "decks/StompySurprise/StompySurprise.profile.json",
         "turn": 3, "on_the_play": True, "max_turns": 9, "active_life": 20, "opponent_life": 20,
         "battlefield": bf, "hand": ["Vaultborn Tyrant", "Turntimber Symbiosis", "Elvish Archdruid"],
         "library_top": ["Elvish Archdruid", "Sol Ring", "Call of the Wild", "Forest", "Forest"],
         "library_filler": "Forest", "library_size": 25, "depth": 3, "budget_ms": 0,
         "_order": elves}
    json.dump(j, open(os.path.join(out, "g287_%d.json" % k), "w"), indent=1)
print("ok")
