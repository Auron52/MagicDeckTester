# Hinata's hold-a-lone-Crackle was a gate at every budget (found 2026-10-13)

Status: the UNBUDGETED half is ON the branch (`MTG_HELD_CAST_UNBUDGETED`, heurarm `HELD_CAST_UNBUDGETED`,
default ON). The FINITE-budget half (a budget-staged wave instead of a gate) is DEFERRED: it changes shipped
play and is the USER's call.

## The game

Hinata2 regression case `hinata_regression_d5_s3003`: seed 3030, game index 27, `--depth 5 --budget-ms 20
--ignore-play-profile`. The committed tree and the tip with `MTG_CRACKLE_COUNT_EXACT=0` win T7. The tip wins
T8, and is still T8 at `--depth 8 --budget-ms 0`. A one-lever bisect names `MTG_CRACKLE_COUNT_EXACT` alone.

| arm | d5 b20 | d8 b0 | d7 b100 |
|---|---|---|---|
| tip (defaults) | 8 | 8 | 9 |
| `MTG_CRACKLE_COUNT_EXACT=0` (committed behaviour) | 7 | 7 | 9 |
| tip + `MTG_NO_HINATA_HOLD_CRACKLE=1` | 7 | 7 | 9 |
| tip + `MTG_HINATA_HOLD_NEEDS_HINATA=1` (lever B) | 7 | 7 | -- |
| tip + `MTG_BP_BASE_CANON=0` | 8 | -- | -- |

`--depth 7 --budget-ms 100` loses for EVERY arm (the `=0` arm there already differs on T1: Forbidden Orchard +
Preordain), so that recovery stage says nothing about this lever. `MTG_BP_BASE_CANON=0` does not restore
T7, so this is NOT the breakpoint empty-arm gap (bp-empty-arm-deferred-site.md).

## The first differing decision, and why

Opening hand: Preordain, Reality Spasm, Ornithopter of Paradise, Izzet Boilerworks, Forbidden Orchard,
Cascade Bluffs, Crackle with Power. At d5 b20 both arms play T1-T2 identically (Bluffs; Sol Ring, Ornithopter,
Boilerworks). At d8 b0 the two arms diverge at the same T3 decision.

**T3 main 1.** The `=0` arm plays Cascade Bluffs and casts Crackle with Power X=1 ({3}{R}{R}, 5 to the face).
The tip plays Forbidden Orchard and casts Preordain, holding the Crackle. The libraries diverge from here.

* `=0` arm: T6 Soulfire Eruption reveals a Soulfire Eruption for the face (9), T7 casts that staged
  Soulfire (9 more): 5 + 9 + 9 = 23. Win T7.
* Tip: Soulfire on T7 (2 to the face), then T8 Ponder/Preordain chain, Hinata, Reality Spasm x2, Crackle X=17.

Mechanism: with `MTG_CRACKLE_COUNT_EXACT` on, the Reality Spasm in hand is no longer counted as a second
Crackle, so `HinataLoneCrackleHeld` (DecisionProviders.cpp) is true for the lone Crackle. Under
`MTG_HINATA_HOLD_LETHAL_BURN` the provider's `HoldCastUnlessLethal` flags it, and BOTH subset walks drop
every subset that casts it without projecting lethal this turn (TurnSolver.cpp, Solve's consider and
EnumeratePlans' consider, label `hold-unless-lethal`). The 5-damage chip is not lethal, so it is dropped
at every budget. The `=0` arm reached T7 only because the misclassification switched the hold off.

## Legal, not a phantom

`MTG_USER_ORDER_TRACE=1` on the `=0` arm: every cast `paid=1` -- Sol Ring, Ornithopter (T2), Crackle (T3,
from 7 mana: Bluffs x2, Boilerworks, Sol Ring, Ornithopter), Soulfire (T6, 10 mana), the staged Soulfire
(T7, `staged=1`, 11 mana). No `paid=0` anywhere. Hinata is never on the battlefield, so no discount is
involved. The T7 line is real.

It is the hold's documented class 1 (reality-spasm-crackle-classification.md, "Every slower game, by
mechanism"): no Hinata, so the combo the hold protects cannot happen, and the hold blocks the chip clock.
The search reached it with clairvoyance: the chip is worth the turn only because the T6 Soulfire flips a
9-drop for the face.

## Why it is a completeness defect, and the fix for the unbudgeted half

The hold is a provider prune, and a provider may restrict options (NO GREEDY rule). But it was applied
as a GATE consulted at every budget, so the line was unfindable at `--budget-ms 0`. CLAUDE.md: "a budget
may re-price work, it may not make a line unfindable -- completeness at large budget is the invariant", and
TOPMAIN is the worked example of exactly this shape.

`MTG_HELD_CAST_UNBUDGETED` (default ON): at an UNBUDGETED SEARCH -- play budget 0 and lookahead depth
> 0, latched by `AIEngine::TakeTurn` (`UnbudgetedSearchScope`, EngineFlags.h) -- EnumeratePlans stops
flagging held casts, so the held line is OFFERED to the search and scored like any plan. Nothing is
picked. Untouched:

* Solve's consider (the d0 runner and rollouts past the horizon): the hold is a horizon prior, and the
  permitted greedy keeps it there.
* Every budgeted run: the latch is 0, so the lever is dead code by construction. Every regression,
  shipped and generation cell with a budget is byte-identical. The d0 regression cases run at
  `--budget-ms 0` but depth 0, so the latch stays down for them too.
* The offline label ladder (`LabelSearchScopeActive`): its answers may not move underneath it.

Unit test: `test/unit/test_hinata_crackle_count.cpp`, "HELD_CAST_UNBUDGETED" (unbudgeted + lever on: the
search's plan list casts the lone non-lethal Crackle while Solve still holds it; budgeted, or lever `=0`:
held). Full unit suite 1016 / 1016.

### gi 27 after the fix

| arm | d5 b20 | shipped (value_play d5 b30) | d7 b100 | d8 b0 |
|---|---|---|---|---|
| tip before | 8 | 8 | 9 | 8 |
| committed behaviour (`MTG_CRACKLE_COUNT_EXACT=0`) | 7 | 7 | 9 | 7 |
| fix (defaults) | 8 | 8 | 9 | **7** |
| fix, `MTG_HELD_CAST_UNBUDGETED=0` | -- | -- | -- | 8 |

At d8 b0 the fix replays the committed tree's line move for move (T3 Bluffs + Crackle X=1, T6 Soulfire,
T7 the staged Soulfire). The game now recovers at stage 2 of the adoption bar.

### Movers (one pooled `--batch`, both arms, lever on vs `=0`, seeds 9810001+, 976 games)

| cell | games | play digests moved | win turns moved | units on / off |
|---|---|---|---|---|
| Hinata2 d8 b0 (unbudgeted: the lever acts) | 12 | 3 (gi 4, 9, 11) | 0 | 1.004 |
| hinata2hg d8 b0 (unbudgeted) | 6 | 0 | 0 | 1.006 |
| Hinata2 d5 b20, shipped, d0 b0; hinata2hg d3 b10 | 40 + 20 + 300 + 30 | 0 | 0 | 1.000 |
| burn, stompy: d2 b0 and d3 b10 | 10 + 30 each | 0 | 0 | 1.000 |

Unbudgeted SHALLOW Hinata searches are very slow on their own: gi 27 at d3 b0 ran more than 13 minutes on
the tip binary (before this lever existed), and at d1 b0 more than 5 minutes on the fix binary; both were
stopped. d8 b0 finishes in 1-2.5 minutes, so the acting cells use it. That shallow-unbudgeted cost is not
investigated here.

## Deferred: what a FINITE budget should do (the USER's call)

At the shipped budget the hold is still a gate, so gi 27 stays T8 at d5 b20 and at the shipped d5 b30.
The sanctioned shape (CLAUDE.md, "waves are the sanctioned way to get a lossy lever's win legitimately")
would admit held subsets in a LATER wave -- e.g. a group-wave-style tranche that requires a held cast,
run after wave 0 while the node's budget remains -- so more budget reaches more held lines instead of
a cliff at unlimited. That changes shipped play wherever the budget reaches the wave, so it needs a
held-out per-deck A/B (Hinata2, hinata2hg) and the USER's sign-off. Related USER-owned levers:

* `MTG_HINATA_HOLD_NEEDS_HINATA` (lever B, default OFF, measured worse on held-out) also restores T7 here.
* `HoldSacLandBurn` (Shard Volley) and the Irencrag waste gate have the same every-budget gate shape and
  were not touched.
