# Opener line keeps a scarce named colour floating (2026-11-17) -- DEFAULT OFF, USER decision pending

Lever: `MTG_UNSEEN_KEEP_SCARCE` (heurarm slot `UNSEEN_KEEP_SCARCE`), **DEFAULT OFF**; `=1` enables. It is
committed off because the held-out A/B is a trade-off (below), and adopting a trade-off is the USER's call.

## The defect

Dragonstorm gate `claude-dragonstorm-swap-20261117`, seed `30083167` T7, plan
`{Desperate Ritual, Seething Song x2, Apex of Power, Irencrag Feat, Dragonlord Kolaghan}`: APPLY-DROP x14.

- Desperate Ritual's payment cracks Lotus Bloom for `{B}`, leaving `{B}{B}` plus Desperate's `{R}{R}{R}` floating.
- The plan casts Apex of Power, a breakpoint opener, so `g_line_opens_unseen` is set.
- With that flag set, the line-aware generic orders stand down (`LineGenericOrderLive`) and the drain takes the
  hand-pip / WUBRG order.
- So the first Seething Song's `{2}` drains `{B}{B}` before any red, with five untapped red sources on the board.
- Kolaghan, named in the same plan, then has no `{B}`.

The opener exclusion exists for a real case: TreasureHunt regression s3003 gi96. There, the line-aware order kept
the second Hunt's `{U}`, and the first Hunt revealed Land's Edge, which needed the `{R}` that order had spent. A named
line under an opener is only a lower bound on demand.

## The change

`SpendFloatingTowardCost` (SpellEffects.h), on an opener line only, reorders generic pips when a floating colour
meets all of these:
- the NAMED line still owes it (`LineOwedAfterOwnPips`);
- at least one other colour floats;
- no untapped permanent of the player's can make it in any mode, and no pay-sac lump (an uncracked Lotus Bloom or
  a Treasure) is on the board (`UnseenScarceNamedMask`, ManaPayment.cpp).

Such a colour pays generic last: every other colour first, then its units beyond what the line owes, then the rest.
It is a reorder of floating units only; the same total is spent.

## Measurement

Exact seeds (gate block 82,000-86,999):

| seed | lever on | lever off |
|---|---|---|
| s30083167 T7 | 0 | 14 |
| s30086717 T4 | 8 | 12 |

s30083522 and s30086338 are other shapes and do not move.

Held-out paired A/B, games 560,000+ (`build/scratch/claude_unseen_scarce_ab/`):

| sample | earlier | later | net | digest-only |
|---|---|---|---|---|
| 55 suite decks + Sea Creatures, 1,000 pairs each; Dragonstorm 5,000 | 0 | 0 | 0 | 0 |
| Merfolk Turns, 1,000 pairs (games 530,000+; see note) | 0 | 0 | 0 | 0 |
| Cyclone, 5,000 pairs (560,000-560,999 + 561,000-564,999) | 0 | 1 | **+1** | 2 |

Every deck except Cyclone is byte-identical, Dragonstorm included: the drops fixed are the oracle's, and none of them
decided a game in the sample.

The one later game is Cyclone gi 560506 (T4 -> T5). The arms diverge at T1 (Brainstorm vs Ponder), where no
two-colour float exists. That makes it a search butterfly: the rule moved the search's evaluation of a later turn.
At `--depth 8 --budget-ms 0 --ignore-play-profile` both arms win T4, so it recovers. It still leaves Cyclone net +1,
and the bar says +1 is not neutral.

Merfolk Turns' 560,000 block could not be measured. Game 560698 drives one process past 42 GB RSS within a minute
on both arms, and on the pre-rebase binary too. That is a pre-existing defect, separate from this lever.

## Recommendation

Adopt. The only cost is one Cyclone game that recovers at d8 b0, from a T1 butterfly, not a payment it got wrong.
The gain is a class of legal payment the engine provably misses (14 oracle drops on one seed, 4 on another).

Tests: `test_dragonstorm_followups.cpp`, "Opener line keeps a floating colour the named line owes and no untapped
source makes", covering both arms.
