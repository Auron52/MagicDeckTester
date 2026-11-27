#!/usr/bin/env python3
"""List suite decks holding a creature IsLordPermanent() accepts (SpellEffects.h): the decks MTG_ATTACK_LORD_CAST can touch."""
import json, re, sys
sys.path.insert(0, "scripts")
import deck_compare as dc
cards = json.load(open("src/cards/data/cards.json"))
cards = cards.get("cards", cards)
byname = {c["name"]: c for c in (cards.values() if isinstance(cards, dict) else cards)}
def is_lord(c):
    if c.get("template") == "lord_effect": return True
    p = c.get("parameters", {})
    return ("Creature" in c.get("types", [])
            and (p.get("power_bonus", 0) or p.get("tough_bonus", 0))
            and (p.get("subtypes_affected") or p.get("affects_all_creatures")
                 or p.get("lord_affects_chosen_subtype") or p.get("lord_affects_mana_ability_creatures")))
txt = open("test/regression_cases.sh").read()
blk = re.search(r"DECK_FILE=\((.*?)\n\)", txt, re.S).group(1)
files = dict(re.findall(r'^\s*\[(\w+)\]=("?[^"\n]+"?)\s*$', blk, re.M))
for k, f in sorted(files.items()):
    f = f.strip('"')
    try: names = [n for _, n in dc.read_decklist(f)]
    except Exception as e: print("ERR", k, f, e); continue
    lords = sorted({n for n in names if n in byname and is_lord(byname[n])})
    if lords: print(k, "|", f, "|", ", ".join(lords))
