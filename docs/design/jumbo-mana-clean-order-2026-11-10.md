# Jumbo Cactuar mana-clean: same-host Aura order

The 5,000-game fast gate at `logs/mana_clean/lastweek-20261110-fast/report.md`
reported 2,948 Jumbo issues: 529 ENUM-GAP, 992 APPLY-DROP-ORDER, 421
APPLY-DROP, 613 DOMINATED-CREATURE, 291 ORACLE-GAP, and 102 UNMODELED.
This change addresses one concentrated APPLY-DROP-ORDER family; it is not a
replacement for a full mana-clean gate.

## Reproduction and cause

On source `d5baba49a`, seed 30013421, game-index 13421, turn 4 main 1
(`MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4`) emitted 500
APPLY-DROP-ORDER lines. The most common plan casts two Overgrowths and a
Utopia Sprawl on the same Forest before Tooth and Nail and Finale of
Devastation. Jumbo's fixed cast ranks put the one-mana Sprawl before
Overgrowth. That order strands Finale; the oracle pays every cast with
Overgrowth, Overgrowth, Sprawl, Tooth, Finale. On this already stacked Forest,
the Overgrowth bonuses before the last untaps matter more than Sprawl's lower
initial cost.

The Jumbo provider now tries the Overgrowth-first alternative only when both
Aura types target the same land, the ranked order fails the exact sequential
payment walk, and the alternative passes it. The ordinary Sprawl-first order
remains when it pays. The provider owns the adjustment; the generic cast-order
machinery is unchanged.

The focused unit case recreates the Forest with Utopia Sprawl and Wild Growth,
two Voyaging Satyrs, and the five planned casts. It verifies that the ranked
order fails and the provider's order pays (14 assertions passed). Replaying
the exact seed on the fixed Release binary emitted zero ENUM-GAP, APPLY-DROP,
or APPLY-DROP-ORDER lines and retained the turn-four win. This is a targeted
result, not a measured 5,000-game reduction.

## Remaining adjacent issues

Two different top signatures still reproduce on the fixed binary:

| seed / game-index | turn | class | lines | example |
| --- | ---: | --- | ---: | --- |
| 30013505 / 13505 | 3 | ENUM-GAP | 19 | Tooth and Nail + Surrak and Goreclaw + Arbor Elf |
| 30010653 / 10653 | 3 | APPLY-DROP | 40 | Green Sun's Zenith + Finale of Devastation + Utopia Sprawl |

Neither sample casts both same-host Aura types. Their enumeration and payment
mechanisms need separate diagnosis before a mana-clean gate can be accepted.
