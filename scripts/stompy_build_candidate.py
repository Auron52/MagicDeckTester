#!/usr/bin/env python3
"""Materialise a StompySurprise candidate as a self-contained deck folder.

Tonight's freeze (2026-09-16) needs each candidate list to carry its OWN keep table, which means
each needs its own folder: the engine resolves every sibling model directory-relative off the
profile path, so `decks/<Stem>/<Stem>.profile.json` is what binds a list to its table.

Three things this gets right that a hand-copy would not:

* **The play profile must be the POOLED one.** `decks/StompySurprise/StompySurprise.profile.json`
  has no `card_scores` for Fyndhorn Elves / World War Hulk / Apex Altisaur -- all three are new to
  the list. An unscored card is scored as an EMPTY SLOT, and that penalty falls only on the list
  that plays it. `logs/deckcmp/.../pool/` is the profile every screen this week measured under, and
  it leaves the 18 shipped scores untouched, so generation stays comparable to the screening.

* **The value sidecar is carried, NOT regenerated.** `value_play.leaf: "none"` makes
  AttachValueSidecar (src/ai/MulliganProfileIO.h:1211) swap the learned model for
  MidGameEvaluator::Constant() and force trust depth 0 -- the 120 fitted trees are discarded at
  load. Fitting a new one would buy an object the engine throws away. The FILE must still exist
  (presence gates the H-cell ladder), and mullgen reads mull_gen_depth/mull_gen_budget_ms from it.

* **expected_buckets is STRIPPED.** It is a guard, not a setting: ExhaustiveKeep.cpp REFUSES to
  generate when it is set and K differs, and merely records K when it is unset. These lists drop
  Mirri's Guile and Wirewood Lodge, so K falls 15 -> 13; carrying 15 over would hard-fail the run.
  Leaving it unset is also the "K as discovered, no policy" rule -- a bucket ruling is the user's.
"""
import json, os, shutil, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POOL_PROFILE = os.path.join(REPO, "logs/deckcmp/StompySurprise/pool/StompySurprise.profile.json")
SRC_VALUE    = os.path.join(REPO, "decks/StompySurprise/StompySurprise.value.json")

# Everything the screens settled and that is IDENTICAL in both candidates. Only the mana axis and
# the two fat counts move, so any difference between the lists is the axis under test.
FIXED = [
    ("Natural Order", 4), ("World War Hulk", 3), ("Call of the Wild", 2),
    ("Worldly Tutor", 4), ("Turntimber Symbiosis", 4), ("Sol Ring", 1),
    ("Apex Altisaur", 1), ("Vaultborn Tyrant", 1), ("Hornet Queen", 1),
    ("Elderscale Wurm", 1), ("Craterhoof Behemoth", 2),
]
BIG_DORKS = [("Elvish Archdruid", 4), ("Priest of Titania", 4)]
ONE_DORKS = [("Llanowar Elves", 4), ("Elvish Mystic", 4)]


def build(stem, fyndhorn, forest, worldspine, terastodon, symbiosis=None, hulk=None):
    cards = dict(FIXED)
    if symbiosis is not None: cards["Turntimber Symbiosis"] = symbiosis
    if hulk is not None:      cards["World War Hulk"] = hulk
    cards.update(dict(BIG_DORKS)); cards.update(dict(ONE_DORKS))
    cards["Fyndhorn Elves"] = fyndhorn
    cards["Forest"] = forest
    cards["Worldspine Wurm"] = worldspine
    cards["Terastodon"] = terastodon
    cards = {k: v for k, v in cards.items() if v > 0}

    total = sum(cards.values())
    if total != 60:
        raise SystemExit(f"{stem}: {total} cards, not 60 -- {cards}")
    mana_creatures = sum(cards.get(n, 0) for n, _ in BIG_DORKS + ONE_DORKS) + cards.get("Fyndhorn Elves", 0)

    d = os.path.join(REPO, "decks", stem)
    os.makedirs(d, exist_ok=True)

    # Cockatrice deck. File order defines the base numbering, so keep it stable and deliberate.
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<cockatrice_deck version="1">',
             f'    <deckname>{stem}</deckname>', '    <comments></comments>',
             '    <zone name="main">']
    for name in sorted(cards):
        lines.append(f'        <card number="{cards[name]}" name="{name}"/>')
    lines += ['    </zone>', '</cockatrice_deck>', '']
    with open(os.path.join(d, f"{stem}.cod"), "w") as f:
        f.write("\n".join(lines))

    shutil.copyfile(POOL_PROFILE, os.path.join(d, f"{stem}.profile.json"))

    v = json.load(open(SRC_VALUE))
    vp = v.get("value_play", {})
    vp.pop("expected_buckets", None)            # guard, not a setting -- K is rediscovered per list
    vp["leaf_note"] = (vp.get("leaf_note", "") +
                       f"  || CARRIED to {stem} 2026-09-16 without refitting: leaf 'none' makes "
                       "AttachValueSidecar replace the model with Constant(), so the fitted trees are "
                       "never consulted. The file is kept because presence gates the H-cell ladder, "
                       "and mullgen reads mull_gen_depth/budget from value_play.")
    v["value_play"] = vp
    json.dump(v, open(os.path.join(d, f"{stem}.value.json"), "w"), indent=1)

    print(f"{stem:22s} 60 cards | {mana_creatures:2d} mana creatures | {cards.get('Forest')} Forest | "
          f"WS {cards.get('Worldspine Wurm',0)} / Tera {cards.get('Terastodon',0)} | "
          f"Sym {cards['Turntimber Symbiosis']} | Hulk {cards['World War Hulk']}")
    return cards


if __name__ == "__main__":
    print("import and call build(); see --help in the docstring")
