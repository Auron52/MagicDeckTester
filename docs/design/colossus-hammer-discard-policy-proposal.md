# Colossus Hammer: DRAFT cleanup-discard buckets (AI-drafted 2026-10-17 — awaiting the USER's review)

**Status: PROPOSAL** (analyze-deck 5i: the AI authors its best shot, the USER amends and approves before
anything ships). Implemented as `ColossusHammerProvider::CleanupDiscardCandidates` behind
`MTG_COLOSSUS_BUCKET_DISCARD` (heurarm `COLOSSUS_BUCKET_DISCARD`), **default OFF** until approved; off = `GenericProvider`'s max-MV
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
| **HAMMER** | Colossus Hammer (keyed as the Equipment with `equip_power_bonus >= 10`); Steelshaper's Gift / Open the Armory stand in for it | **1** unless a Hammer is on the battlefield (then 0). A tutor fills the slot only when no Hammer is in hand. |
| **CHEAT** | Sigarda's Aid, Magnetic Theft, Forge Anew; Raubahn / Ardenn only as a fallback (below) | **1** unless a PERMANENT cheat is already on the battlefield — Sigarda's Aid, Forge Anew (its first-equip waiver works every turn), Raubahn, Ardenn (then 0). Preference inside: Sigarda's Aid ({W}, permanent) > Magnetic Theft ({R}, once) > Forge Anew ({2}{W}); Raubahn > Ardenn fill the slot only when the hand holds none of those three. |
| **HOST** | Swiftblade Vindicator, Kor Duelist, Lizard Blades | **1** double striker, net of the board's double strikers. Preference: Swiftblade (native) > Kor Duelist ({W}) > Lizard Blades. |
| **OVERFLOW** (shed first → last) | everything past its quota | Path to Exile → Dragonfire Blade → Speaker of the Heavens → Loxodon Warhammer → lands past the quota → a second tutor → Raubahn → Ardenn → a second cheat → a second host → a second Hammer |

Distance-to-playable orders every pick and shed inside one role (the doctrine's rule 4): a card whose
COLOURS the board plus the kept lands cannot make loses to one whose colours they can; then the role's
preference above; then the shared `discard_dist` castability factor; then the cheaper card. Colour
coverage is "reachable", not a castability race, on purpose: a pure factor sort would always keep Kor
Duelist ({W}) over Swiftblade Vindicator ({R}{W}) and erase the preference.

**Implementation:** `ColossusHammerProvider::CleanupDiscardCandidates` (ONE index through
`CleanupDiscardRankingWithOrder`, re-asked after every shed; `MTG_COLOSSUS_DISCARD_FAN=1`, testing only,
returns the whole order). A card off this list sheds before every listed card.

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

## Evidence and measurement (2026-10-17)

* **Evidence pass** (`analyze_deck.py`, Stage 4): 7 cleanup decisions in 400 games at d3, every one a tie
  (all options equally optimal) -- STATUS_QUO_OK. The deck empties its hand fast; cleanup discards are rare
  and, when they happen, free.
* **Paired A/B** (same batch as the cast-order draft, held-out seeds): play settings +0.0003 (se .0003, 0
  faster / 1 slower, 3000 games); d3 +0.0000 (1 / 1, 4000); d0 +0.0001 (2 / 3, 20000); 2HG +0.0010 (0 / 1,
  1000). Every searched slower game recovers at stage 1 (budget 100). I.e. the buckets are neutral: there is
  almost nothing for a discard rule to decide in this deck.

**Status: still OFF.** The USER decides whether a neutral rule ships for doctrine and rollout fidelity (the
Landfall precedent) or the generic max-MV fallback stays.
