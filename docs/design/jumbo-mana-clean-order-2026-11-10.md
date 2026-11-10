# Jumbo Cactuar mana-clean: Aura order and haste enumeration

The 5,000-game fast gate at `logs/mana_clean/lastweek-20261110-fast/report.md`
reported 2,948 Jumbo issues: 529 ENUM-GAP, 992 APPLY-DROP-ORDER, 421
APPLY-DROP, 613 DOMINATED-CREATURE, 291 ORACLE-GAP, and 102 UNMODELED.
These changes address sampled APPLY-DROP-ORDER, ENUM-GAP, and APPLY-DROP families; they are
not a replacement for a full mana-clean gate.

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

## Same-plan haste enumeration

On the first fixed binary, seed 30013505, game-index 13505, turn 3 main 1
(`MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3`) emitted 19 ENUM-GAP lines for
plans containing Tooth and Nail, Surrak and Goreclaw, and Arbor Elf. The
original candidate bound saw 10 mana against their 14 total cost and pruned
the subset before the exact sequential payer ran. Surrak cast first makes the
new Arbor Elf hasty; that Elf untaps the Forest carrying Utopia Sprawl and
Overgrowth, adding four mana. The exact payer already supported this
interaction, but enumeration could not reach it.

The pending watcher plus hand-dork mana is now an upper bound for the scalar
odometer. A subset earns the exact-payment rescue only if it contains both
cards, and the sequential payer remains the legal arbiter. A focused unit
recreates the board and hand and confirms the three-cast plan is enumerated
(12 assertions passed). Replaying the exact seed on the second fixed Release
binary emitted zero ENUM-GAP, APPLY-DROP, or APPLY-DROP-ORDER lines and retained
the turn-three win. This is a targeted result, not a measured 5,000-game
reduction.

## Same-source X-spell floating color

The adjacent payment signature on the second fixed binary was:

| seed / game-index | turn | class | lines | example |
| --- | ---: | --- | ---: | --- |
| 30010653 / 10653 | 3 | APPLY-DROP | 40 | Green Sun's Zenith + Finale of Devastation + Utopia Sprawl |

In seed 30010653, game-index 10653, turn 3, the plan casts Utopia Sprawl,
Green Sun's Zenith at X=6, then Finale of Devastation. The engine's Zenith
payment taps Forest #10, Arbor Elf #1, and Voyaging Satyr #53 and leaves
`{W}{G}` floating, so Finale cannot pay `{G}{G}`. The oracle makes the same
source taps but floats `{G}{G}`, which pays Finale. Its log reports 40
APPLY-DROP lines on the second fixed binary. This is a payment color-selection
failure after an X spell, not a candidate-order or mana-bound failure.
`BatchPrepayMainCasts` declines to prepay an X spell (`PP_XSPELL`); the
per-cast payer must retain the useful green. The same-tap, different-float
choice also explains why the source-swap rollback does not recover the line.

The greedy payer spends each generic pip as it appears. In the directed
fixture, the last untap adds the second white Aura mana only after it spent
one green; an exact backtracker can use the same Forest, Arbor Elf, and
Voyaging Satyr taps while spending both white pips on Zenith. The new rescue
is limited to X-cost payments with a later named colored bill on a land-Aura
and untapper board. It runs only when the ordinary float cannot pay that bill,
and commits the alternative float only if the backtracker proves the later
bill payable with equal total mana, identical taps and counters, and no other
resource change. The standalone fixture now leaves `{G}{G}` and pays Finale.
The focused Zenith/Finale unit passed 10 assertions; all 13 Jumbo unit cases
passed 171 assertions on the fixed binary.
Replaying seed 30010653 at the same oracle setting reduced APPLY-DROP 40→0,
retained the turn-four win, and left PHANTOM=236, DOMINATED-CREATURE=4, and
DOMINATED-LAND=241 unchanged. This is a targeted result; a full mana-clean
gate remains necessary after integration.
