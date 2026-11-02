# LandfallElementals: provider narrowing for the fetch lands and Flamekin Harbinger (2026-10-23)

USER request: *"Let's implement narrowing heuristics for Terramorphic Expanse, Flamekin Harbinger and Myriad
Landscape in an attempt to reduce branching."* Both rules live in `LandfallElementalsProvider`
(`src/ai/DecisionProviders.cpp`). They RESTRICT and ORDER options; the search still branches over what they
admit. Human play and `MTG_UNPRUNE=landpick` / `MTG_UNPRUNE=tutor` keep the full lists.

## 1. Fetch order (Terramorphic Expanse, Myriad Landscape) -- `MTG_LFE_FETCH_ORDER`, default ON

USER: *"The order should prioritize Red -> Green -> Blue -> Second Red including mana from lands that can produce
all of these or SmokeBraider. Ideally Myriad Landscape would get Red or Green, though Red is preferable."* and
*"Maybe after the second red we can get a second green and then more red."*

- A basic search fetches the first colour, in the order R(1) -> G(1) -> U(1) -> R(2) -> G(2), whose source count is
  below the threshold; then Red. Myriad walks the same order without Blue. A colour with no basic left in the
  library is skipped. The pick is moved to the front (`LandSearchPickOrder`) and the list pruned to it
  (`LandSearchPickWidth` = 1); every state input is folded into `LandSearchPickOrderKey`.
- A source is any permanent we control whose `produces` holds the colour (basics, the duals, Secluded Courtyard /
  Unclaimed Territory, Smokebraider) and -- `MTG_LFE_FETCH_HAND`, default ON (USER: *"Yes, we should check the
  hand"*) -- any land in HAND (a coming land drop). Without it, T1 Terramorphic fetched Mountain when the Mountain
  in hand already covered red, losing the T2 Brushfire (5 of the 26 games the first version lost at d8 b0).
- Measured alone (held-out, 1,000 games per depth vs no rule): -8 / -3 turns at d3 b10 / d5 b20, -7% / -5% units.

## 2. The Harbinger rule -- `MTG_LFE_HARBINGER_RULE`, default ON

USER: *"getting Mossborn Hydra if we have none in hand and possibly also search a Brushfire Elemental if we need it
to curve out and already have a Hydra. Similarily we could consider Smokebraider if we are short on mana ... The
closest would probably be a Hellhound, Risen Reef or Incandescent Soulstoke ... Maybe the Soulstoke if it would make
the board go from non-lethal to lethal next turn."*

As shipped, the tutor is offered, in this order:

| Target | Condition | Lever |
|---|---|---|
| Smokebraider (first) | early ramp: <= 1 land on our battlefield, none held | `MTG_LFE_HARB_SMOKE_EARLY` + `MTG_LFE_HARB_SMOKE_FIRST` |
| Mossborn Hydra | none in hand | (rule) |
| Brushfire Elemental | always | `MTG_LFE_HARBINGER_BRUSHFIRE` |
| Incandescent Soulstoke | none held | `MTG_LFE_HARBINGER_WIDE` |
| Akoum Hellhound | always | `MTG_LFE_HARBINGER_WIDE` |
| Risen Reef | none in hand | `MTG_LFE_HARBINGER_WIDE` + `MTG_LFE_HARBINGER_INHAND` |
| Smokebraider (last) | short on mana: no land in hand, < 4 lands, none held | (rule) |
| Flamekin Harbinger | <= 1 land out, no OTHER Harbinger in hand | `MTG_LFE_HARB_CHAIN_EARLY` |

Never offered: Fury, Fulminator Mage, Foundation Breaker, and another Harbinger outside the early condition. All
levers are default ON except `MTG_LFE_HARBINGER_CHAIN` (another Harbinger + Smokebraider unconditionally: no
better, more work) and `MTG_LFE_HARB_FURY_SOULSTOKE` (Fury while a Soulstoke is held: measured 0 / -1).

How it got there (each held-out, 1,000-1,500 paired games per depth, fetch order on):
- **The rule as first written lost:** +55 / +34 turns vs searching every target, though -33% / -22% units. In the
  games it lost the unrestricted search fetched Brushfire 56 times and Hydra 5: most Harbinger casts are T1-T2,
  where the goldfish wants the 2-drop haste body, and a lone Hydra rarely kills before T4.
- **+Brushfire always:** +20 / -3. **+Hellhound / Reef / Soulstoke:** pooled over three samples -5 / -48.
- **Root-causing the 26 games that still lost at d8 b0** (turn-by-turn line diffs) gave three amendments:
  lands in hand count (above); Smokebraider as early ramp (8 games: T2 Smokebraider gives two Elemental mana on
  T3); the early Harbinger chain (5 games). Measured together: -6 / -1 at budget, and 19 of the 26 recover.
- **The chain is not a clairvoyance artifact.** Forcing the T1 target to each of the 10 Elementals in the four
  chain games at d8 b0: only another Harbinger reaches T4 (Foundation Breaker too in one), and it still does with
  the search's view of mid-game reshuffles DECOUPLED from the real deal (`shuffle_salt_search` 1-4) in 3 of the 4
  games. The USER's reading: the two Harbingers deal 5 by the end of T4, which lets a 16-power Hydra (one counter
  doubled by four landfalls) end the game; the second Harbinger also tutors that Hydra.
- **Smokebraider first:** ranked last, the budgeted search took Brushfire before scoring the ramp line
  (smoke d3 gi122). First: -12 / -12, +1.5% units.

## 3. Final measurements

- Held-out, final defaults vs both rules off (1,500 games per depth, seeds 240000+gi / 250000+gi): d3 70 faster /
  44 slower, net -26; d5 70 / 34, net -37; units -30% / -25%. Then Smokebraider first: a further -12 / -12.
- Unlimited budget: 15 of 78 slower games do not recover at d8 b0 -- these are provider PRUNES, which apply at every
  budget, so a line that needs an excluded target cannot be found there. Accepted by the USER for this deck.
- Keep-gen cost (the reason for the work): the 51 logged slow generation rollouts, both rules off vs on (first
  version): off aborted at the 20.9 GB cap, on finished all at 10.5 GB peak on 16 threads; on the 25 both finished,
  1,173 s -> 271 s. The remaining slow rollouts are Risen Reef + two Aether Vials (every Vial entry fans a look-top).
- References: the 10 USER references bench 0/10 short (search 4.30 vs human 4.80), and replay 10/10 strict.
- Smoke + regression accepted 2026-10-23 (only elementals keys moved; net faster in both). Overnight GT is left for
  the periodic overnight run.

## 4. Diagnostics added

`MTG_LFE_HARB_FORCE="<turn>:<card name>"` (unset = inert): every Harbinger tutor on that turn is offered only the
named card. Measurement only; never set in play or generation.
