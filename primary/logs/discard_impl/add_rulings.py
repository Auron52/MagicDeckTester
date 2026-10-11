#!/usr/bin/env python3
"""One-shot: record the USER's 2026-10-10 rulings on the implementation readings in every discard doc."""
from pathlib import Path

REPO = Path("/workspaces/MagicDeckTester/docs/design")
CROSS = [
    "#### USER rulings (2026-10-10) on the implementation readings",
    "",
    "Cross-deck (verbatim):",
    "- *\"Colour beats the land count is correct.\"* -- a land that gives a KEPT spell a colour nothing on the",
    "  board or already kept provides is kept even past the land quota (Creature Giving, Melira Pod and",
    "  EldraziDisplacerFlicker already do this; every deck follows it).",
    "- *\"Creature quotas are less worried about the board than land quotas. This is because you can always add",
    "  more creatures and sometimes this speeds up the clock.\"* -- creature / threat / payoff quotas are filled",
    "  from the hand WITHOUT subtracting creatures already in play. Land and mana quotas still count the board;",
    "  engine, combo-piece and duplicate-legend rules keep the board rules their design states.",
    "- *\"For Distance to castable I think it makes sense to have a smarter model if we can.\"* -- the shared",
    "  model in docs/design/discard-distance-model.md replaces the per-deck crude proxies.",
]
DECK = {
    "dragonstorm": ["- Dragonstorm: *\"Apex should indeed go first before Dragonstorm as it is almost strictly worse.\"* --",
                    "  the SHED-line reading stands: an Apex held beside a Dragonstorm sheds before the soft 2nd Medallion."],
    "goblins": ["- Goblins: *\"For Goblins a hand lackey is only really surplus in the later game where you have nothing",
                "  expensive in hand.\"* -- a Goblin Lackey in hand is NOT made surplus by a Skirk / Warchief in play;",
                "  it is surplus only late in the game with nothing expensive in hand."],
    "selesnyalifegain": ["- SelesnyaLifegain: *\"For SelesnyaLifegain you want to keep the cheapest effective payoffs if mana is",
                         "  really poor.\"* -- when no payoff is reachable, the payoff quota is still kept, cheapest effective",
                         "  (enabled) payoffs first; poor mana does not empty the bucket."],
    "auras": ["- Auras: *\"In auras we should keep Light-Paws as it is the superior engine.\"* -- a hand Light-Paws stays",
              "  protected with a Kor in play (the implemented reading (a) is confirmed)."],
    "eldrazi-flicker": ["- EldraziDisplacerFlicker: *\"For EDF that is fine as long as we keep our only win-con/draw source even",
                        "  if it is a land.\"* -- a SPARE draw land sheds as a land (reading R8 confirmed), but the ONLY",
                        "  win con / draw source is kept in the win-con slot even when it is a land."],
}


def block(slug):
    out = CROSS[:]
    if slug in DECK:
        out += ["", "Deck-specific (verbatim):"] + DECK[slug]
    return "\n".join(out) + "\n\n"


for slug in ["angels", "auras", "breaching-dragonstorm", "burn", "creature-giving", "critterlifegain",
             "dragonstorm", "eldrazi-flicker", "fluctuator", "giants", "goblins", "knights", "melira-pod",
             "slivers-vial"]:
    p = REPO / f"{slug}-discard-policy-proposal.md"
    t = p.read_text()
    if "USER rulings (2026-10-10)" in t:
        continue
    anchor = "## The original proposal (2026-09-23"
    i = t.index(anchor)
    j = t.rfind("\n---\n", 0, i)          # insert before the separator that precedes the proposal
    pos = j + 1 if j >= 0 else i
    p.write_text(t[:pos] + block(slug) + t[pos:])
    print("updated", p.name)

for name, slug, head in [("fungus-cleanup-discard.md", "fungus", "## 7. USER rulings (2026-10-10)"),
                         ("selesnyalifegain-discard-policy.md", "selesnyalifegain", None),
                         ("whiteknights-discard-policy.md", "whiteknights", None)]:
    p = REPO / name
    t = p.read_text()
    if "USER rulings (2026-10-10)" in t:
        continue
    b = block(slug)
    if head:
        b = b.replace("#### USER rulings (2026-10-10) on the implementation readings", head)
    p.write_text(t.rstrip() + "\n\n" + b)
    print("updated", p.name)
