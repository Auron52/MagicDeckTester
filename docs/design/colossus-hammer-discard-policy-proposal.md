# Colossus Hammer: DRAFT cleanup-discard buckets (AI-drafted 2026-10-17 — awaiting the USER's review)

**Status: PROPOSAL** (analyze-deck 5i: the AI authors its best shot, the USER amends and approves before
anything ships). Implemented as `ColossusHammerProvider::CleanupDiscardCandidates` behind
`MTG_COLOSSUS_BUCKET_DISCARD`, **default OFF** until approved; off = `GenericProvider`'s max-MV
fallback. The evidence pass (`scripts/analyze_deck.py --discard-analysis`) result is appended when run.

## How this deck wins, which is what the buckets protect

Colossus Hammer (+10/+10) on a double striker is 22+ damage in one swing, and the deck plays five ways
to dodge the {8} equip. So a hand is live when it holds (or the board already has) **one Hammer (or a
tutor for it), one way to attach it cheaply, and one double-strike host**, plus enough mana to cast
them. Everything else is redundancy or a backup plan (Loxodon Warhammer + Speaker of the Heavens).

## Buckets (quota-first; every permanent quota is NET OF THE BOARD; one index returned)

| bucket | members | quota (hand owes, after the board) |
|---|---|---|
| **MANA** | lands | lands on board + hand up to **4** (curve tops at 3: Forge Anew, Ardenn, Open the Armory + a 1-drop); colour first: at least one R source and two W sources counting the board. A 5th land is soft (Loxodon Warhammer's equip {3}). |
| **HAMMER** | Colossus Hammer; Steelshaper's Gift / Open the Armory stand in for it | **1** unless a Hammer is on the battlefield (then 0). A tutor fills the slot only when no Hammer is in hand. |
| **CHEAT** | Sigarda's Aid, Magnetic Theft, Forge Anew (Raubahn / Ardenn ON THE BOARD count) | **1** unless an Aid / Raubahn / Ardenn is already on the battlefield (then 0). Preference inside: Sigarda's Aid ({W}, permanent) > Magnetic Theft ({R}, once) > Forge Anew ({2}{W}). |
| **HOST** | Swiftblade Vindicator, Kor Duelist, Lizard Blades | **1** double striker, net of the board's double strikers. Preference: Swiftblade (native) > Kor Duelist ({W}) > Lizard Blades. |
| **OVERFLOW** (shed first → last) | everything past its quota | Path to Exile → Dragonfire Blade → Speaker of the Heavens → Loxodon Warhammer → lands past the quota → a second tutor → Raubahn → Ardenn → a second cheat → a second host → a second Hammer |

Distance-to-playable orders the shed inside an over-full bucket (the doctrine's rule 4): a card whose
colour the board + hand cannot produce sheds before a castable twin.

## Why this order, in the places it is a judgement call

* **Path to Exile sheds first.** Its only live goldfish line is self-target ramp (a body for a tapped
  basic); it is the least likely card to matter.
* **Speaker of the Heavens before Loxodon Warhammer.** Speaker needs +7 life, which in practice needs a
  lifelinker swinging — i.e. the Warhammer; the Warhammer without Speaker is still +3 power and lifelink.
* **A second Hammer is the LAST overflow shed.** With five cheats and two tutors in the deck, a spare
  Hammer is a second kill threat; a spare host or cheat is more replaceable.

## Questions for the USER

1. Is 4 the right land cap (counting the board), or 3 for this curve?
2. Should Forge Anew rank above Magnetic Theft in CHEAT when the graveyard already holds an Equipment?
