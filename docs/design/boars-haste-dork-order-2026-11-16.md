# Boars: mana dorks ahead of Flinthoof with Greaves in play

The approved Boars cast order ranks the cheap creatures by mana value. On a
board with Lightning Greaves already in play, that put Flinthoof Boar ahead of
Somberwald Sage or Zhur-Taa Druid even when the selected line could equip the
fresh mana creature and use its mana for Flinthoof. The first complete
5,000-game mana-clean gate reported 751 `APPLY-DROP-ORDER` flags for Boars;
seed 30,000,239 contains 23 flags, including Horn + Sage + Flinthoof + equip.

`MTG_BOARS_HASTE_DORK_RANK` (default on, `=0` control) moves a two- or
three-mana mana creature into the Birds/Hierarch tier only while its controller
already has Lightning Greaves on the battlefield. Herald's Horn and Herald of
Ilharg retain their earlier tiers. This is one visible cast order, not an
additional searched plan. A candidate fallback in `ArrangeCastOrder` was
discarded before adoption: that hook receives casts only and cannot tell
whether a Greaves equip is selected in the plan.

On seed 30,000,239, the oracle miss count falls from 23 to one. On the first
500 gate seeds, `APPLY-DROP-ORDER` falls from 99 to 69; other counted issue
classes are unchanged. Boars remains unclean, especially on double-Decimator
payment, attacker preservation, and capped Raggadragga observations. The
Release unit suite passes 3,363 cases / 3,614,434 assertions, including a
focused own-Greaves versus opponent-Greaves rank test.

On 500 fresh paired solo seeds (9,900,001–9,900,500), the new rank is two
turn-games faster and none slower (4.814 → 4.810 mean). On a separate 2,000
paired solo seeds (9,910,001–9,912,000), it changes 16 play lines but no win
turn; both arms average 4.823 and win 1,991 games by turn eight. Thus the
larger solo check is quality-neutral, not evidence of a speed gain.

The Boars/Boars2HG pooled smoke and regression run has no slower games. Smoke
has two depth-zero play changes at the same turn; regression has three more
depth-zero changes at the same turn and one searched solo play change at the
same turn. The searched two-headed row improves one game from turn six to
turn five. Its fetch sequence changes the later draw, so that one win is not a
like-for-like line comparison. The two inspected depth-zero changes cast Sage
before Flinthoof with identical hand/draw and the same turn-five win. All 561
scenarios, 864 strict viewer references (zero play drift/board divergence/enum
gaps), and both require-beat references passed. The affected ground truth was
accepted after the per-game audit.
