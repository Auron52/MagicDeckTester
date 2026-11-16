# Boars: Greaves in hand can haste a mana creature in the same line

The 5,000-game Boars mana gate on indices 5,000–9,999 found 667
`APPLY-DROP-ORDER` flags. On seed 30,005,044 T4, a plan with Lightning
Greaves, Flinthoof Boar, Herald of Ilharg, and Somberwald Sage dropped Sage.
The oracle paid every cast when it moved Sage before Herald: Greaves could
equip the newly cast Sage through the existing early-equip rule, and Sage's
three creature-only mana paid the later cast. The prior Boars haste-dork rank
only applied when Greaves was already on the battlefield.

`BoarsProvider::CastOrderRank` now places a 2–4 mana dork just after Greaves
and before Herald when Greaves is visible in hand or on the battlefield.
It is one deterministic order, not another searched permutation. The
`MTG_BOARS_GREAVES_HAND_DORK_RANK=0` hatch restores the previous rank.

Evidence:

- Exact seed 30,005,044 T4: 23 `APPLY-DROP-ORDER` flags to zero.
- The first 500 games of the fresh Boars block: order drops 107→41 and enum
  gaps 12→2; payment drops stay 20, and creature-dominance moves 137→138.
- Fresh paired solo seeds 9,940,001–9,942,000: 58 changed play digests, one
  game wins T4 instead of T5, none win later; mean 4.7650→4.7645.
- All 3,363 unit cases and 3,614,434 assertions pass. Boars/2HG smoke and
  regression have zero slower or faster searched games, 24 changed searched
  play digests at the same win turn, and zero strict viewer drift across 864
  references. Both required search wins pass.
- Three of 1,000 Boars depth-zero regression games become one turn slower.
  In each, promoting Druid or Sage before Herald loses Herald counters.
  Depth-zero is the deck's greedy runner, below searched play in the user's
  priority. This is the measured cost of the single-order fallback.

Boars remains unclean. Its fresh 5,000-game baseline has 3,258 issues before
this change, including creature-tap dominance, capped decisions, and the
remaining order/payment lines. A new 5,000-game block is required for a clean
verdict after further fixes.
