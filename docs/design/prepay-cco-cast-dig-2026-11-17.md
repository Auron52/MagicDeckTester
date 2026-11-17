# Creature-only prepay decline covers a hard-cast dig creature (2026-11-17)

Lever: `MTG_PREPAY_CCO_CAST_DIG` (heurarm slot `PREPAY_CCO_CAST_DIG`), DEFAULT ON; `=0` restores the Vial-only
scope byte-for-byte.

## The defect

Pirates mana-clean gate `claude-nearclean-20261117`, seed `30108757` T4, plan
`{land=Fiery Islet + cast:Goblin Tomb Raider + cast:Staunch Crewmate}`: APPLY-DROP x6 (every Pirates issue in
the gate).

The whole-turn prepay (`TurnSolver::BatchPrepayMainCasts`) paid both creatures jointly from Spirebluff Canal and
both Fiery Islets: every unrestricted red source. It left Secluded Courtyard, whose coloured mana pays creature
spells only. Staunch Crewmate's enters-the-battlefield dig opens a continuation, and that continuation casts the
Lightning Bolt already in hand. The Bolt was not in the joint bill, and only the Courtyard was left: dropped. The
oracle pays a creature with the Courtyard's red and keeps an unrestricted source for the Bolt.

The decline for exactly this shape already existed (`MTG_PREPAY_CCO_VIAL_DIG`, Pirates s30070533). It fires when:
- a creature-only land is up;
- an undeclared coloured noncreature in hand would be left short by the joint bill.

But it engaged only for an Aether Vial put of a dig creature. A HARD-CAST dig creature opens the same continuation.

## The change

The decline's trigger scan accepts a hand cast whose face has `etb_dig_count > 0`, as well as a Vial activation of
one. Everything after the trigger is unchanged. A declined prepay falls back to the per-cast payer, whose hand
demand keeps the source.

## Measurement

Exact seed: APPLY-DROP 6 -> 0 (lever off: 6).

Only Knights and Pirates in the suite hold an ETB-dig creature. Paired A/B, one pooled Release batch, both arms per
job via the heurarm flag, 5,000 pairs each, games 570,000+ (`build/scratch/claude_castdig_ab/`):

| deck | earlier | later | digest-only | work units |
|---|---|---|---|---|
| Knights (5,000) | 0 | 0 | 0 | identical |
| Pirates (5,000) | 0 | 0 | 0 | -0.00% |

Both decks are byte-identical in play: the shape never decided a game in this sample. It is adopted as a payment
correctness fix that removes every Pirates issue in the gate. Unit suite 3,464/3,464.

Test: `test_hand_demand_pay.cpp`, "CCO prepay: a hard-cast dig declines a bill that would spend Bolt's Mountain",
covers both arms. A first version cached the lever in a function-local `static const`; that froze the per-job
heurarm arm, and the test's lever-off arm caught it.
