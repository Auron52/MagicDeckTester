# Merfolk: keep unrestricted blue for Spreading Seas

The fresh Merfolk mana-clean extension (`lastweek-20261110-fast`, games
110000–114999) recorded 15 APPLY-DROP lines. They collapse to five distinct
seeds: 30110325, 30111189, 30111177, 30112007, and 30113994. Every line
drops a Spreading Seas cast on turn four. The first two seeds start with Seas;
the other three cast Silvergill Adept, then reach a Seas through the continuation.

Seed 30110325 shows the mechanism clearly. Two Secluded Courtyards and two
Islands can pay two Seas costs (`{1}{U}` each): each Courtyard makes `{C}` for
one generic pip, and each Island makes `{U}`. The engine instead spends both
Islands on the first Seas, so the second has no legal blue source. The oracle
finds the two legal payments. Disabling the existing CCO line reserve leaves
the miss unchanged, so this is the per-cast source order after a one-cast
batch-prepay decline, not the earlier mixed-batch prepay defect.

`MerfolkProvider::ManaSourceRank` now moves a creature-only land to the early
generic rank while Spreading Seas is in hand. That land can provide blue to a
creature or colourless toward Seas, while an Island provides blue to Seas. The
deck-scoped rank change also makes Silvergill Adept use Courtyard blue when a
Seas is waiting, preserving the unrestricted Island. It changes ordering only;
all previously legal payment options remain available.

## Verification

The modified Release binary reports zero APPLY-DROP and `lost=0` for all five
single-game oracle reproductions (`MTG_MANA_ORACLE=3`, turn four), at the exact
seeds and game indices above. Oracle-on and oracle-off runs agree on each game's
win turn (5, 4, 4, 5, 5 respectively). A direct payment unit covers two Seas
and Adept followed by Seas. The official next 5,000-game PGO gate remains for
after integration.
