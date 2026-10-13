# Hinata's hold-a-lone-Crackle: from a gate to a budget-staged late wave (2026-10-13)

Lever: `MTG_HELD_CRACKLE_LATE_WAVE` (heurarm `HELD_CRACKLE_LATE_WAVE`), **default ON**; `=0` restores
the gate at every budget, byte-identical to dd4859c3. It replaces a first cut (`MTG_HELD_CAST_UNBUDGETED`,
admitted the held line only at `--budget-ms 0`), which was a cliff: at b5000 the line was still
unfindable.

## The game

Hinata2 regression case `hinata_regression_d5_s3003`: seed 3030, game index 27, `--depth 5 --budget-ms 20
--ignore-play-profile`. The committed tree and the tip with `MTG_CRACKLE_COUNT_EXACT=0` win T7. The tip wins
T8, and is still T8 at `--depth 8 --budget-ms 0`. A one-lever bisect names `MTG_CRACKLE_COUNT_EXACT` alone.

| arm (dd4859c3 binary) | d5 b20 | d8 b0 | d7 b100 |
|---|---|---|---|
| tip (defaults) | 8 | 8 | 9 |
| `MTG_CRACKLE_COUNT_EXACT=0` (committed behaviour) | 7 | 7 | 9 |
| tip + `MTG_NO_HINATA_HOLD_CRACKLE=1` | 7 | 7 | 9 |
| tip + `MTG_HINATA_HOLD_NEEDS_HINATA=1` (lever B) | 7 | 7 | -- |
| tip + `MTG_BP_BASE_CANON=0` | 8 | -- | -- |

`--depth 7 --budget-ms 100` loses for EVERY arm (the `=0` arm there already differs on T1: Forbidden
Orchard + Preordain), so that recovery stage says nothing about this lever. `MTG_BP_BASE_CANON=0` does not
restore T7, so this is NOT the breakpoint empty-arm gap (bp-empty-arm-deferred-site.md).

## The first differing decision, and why

Opening hand: Preordain, Reality Spasm, Ornithopter of Paradise, Izzet Boilerworks, Forbidden Orchard,
Cascade Bluffs, Crackle with Power. At d5 b20 both arms play T1-T2 identically (Bluffs; Sol Ring,
Ornithopter, Boilerworks). At d8 b0 the two arms diverge at the same T3 decision.

**T3 main 1.** The `=0` arm plays Cascade Bluffs and casts Crackle with Power X=1 ({3}{R}{R}, 5 to the
face). The tip plays Forbidden Orchard and casts Preordain, holding the Crackle. The libraries diverge.

* `=0` arm: T6 Soulfire Eruption flips a Soulfire Eruption for the face (9); T7 casts that staged Soulfire
  (9 more): 5 + 9 + 9 = 23. Win T7.
* Tip: Soulfire on T7 (2 to the face), then T8 Ponder/Preordain chain, Hinata, Reality Spasm x2, Crackle X=17.

Mechanism: with `MTG_CRACKLE_COUNT_EXACT` on, the Reality Spasm in hand no longer counts as a second
Crackle, so `HinataLoneCrackleHeld` (DecisionProviders.cpp) is true for the lone Crackle. Under
`MTG_HINATA_HOLD_LETHAL_BURN` the provider's `HoldCastUnlessLethal` flags it, and both subset walks drop
every subset that casts it without projecting lethal this turn (Solve's consider and EnumeratePlans'
consider, label `hold-unless-lethal`). The 5-damage chip is not lethal, so it was dropped at EVERY budget.
The `=0` arm reached T7 only because the misclassification switched the hold off.

## Legal, not a phantom

`MTG_USER_ORDER_TRACE=1` on the `=0` arm: every cast `paid=1` -- Sol Ring, Ornithopter (T2), Crackle (T3,
from 7 mana: Bluffs x2, Boilerworks, Sol Ring, Ornithopter), Soulfire (T6, 10 mana), the staged Soulfire
(T7, `staged=1`, 11 mana). No `paid=0`. Hinata is never on the battlefield, so no discount is involved.

It is the hold's documented class 1 (reality-spasm-crackle-classification.md): no Hinata, so the combo the
hold protects cannot happen, and the hold blocks the chip clock. The search found it with clairvoyance:
the chip is worth the turn only because the T6 Soulfire flips a 9-drop for the face.

## The defect and the fix

The hold is a provider prune, which the NO-GREEDY rule allows -- but it was applied as a GATE consulted
at every budget, the shape CLAUDE.md's completeness rule rules out ("findable at unlimited or LARGE
budgets; adding budget fills in more lines proportionally"). Its sanctioned remedy is a budget-staged
admission order, and the engine already has the machinery: the group waves (group-waves.md), whose
tranches partition a capped plan space and are walked after a node's wave 0 while its budget allows.

**The held tranche.** `groupwave::State::held_mode` (scoped by `groupwave::HeldWaveScope`, pinned to the
host's own enumeration level so a continuation list derived inside it stays normal) makes EnumeratePlans
emit EXACTLY the subsets its hold drop removed: a held cast, not lethal by the walk's projection. Wave 0 is
unchanged. Wave 0 + the held tranche partition the unheld plan space, so nothing is scored twice; Plan-B
and the per-land idle plan are skipped in held mode for the same reason the group tranches skip them; the
enumeration memo is bypassed.

**The hosts** (each after wave 0, its bp waves and its group tranches; strictly-better win turn only;
budget checked AND charged per plan through the same `ConsumeAt` sites as the group tranches; an
exhausted budget is counted as a truncation; unlimited => always reached):

* `SolveWithLookahead`'s per-pass candidate loop -- the per-turn decision (m1 and m2), every rollout turn
  that re-enters it at depth > 0, and the searched second main (`SearchedSecondMainMemoized`). Built once
  per node, rescored per pass, exactly like the group tranches.
* `FSLineWin` (m1 of the full-search line) -- shares the group tranches' scorer, now hoisted.
* `FSLineTail`'s m2 loop -- scored like the m2 continuation waves (that host has no group waves).

One held tranche per group range wave 0 and the group tranches covered (R = -1 is wave 0's groups), so a
held line that also needs a cap-dropped group is reachable too. Beam-cut nodes skip it (counted), as they
skip the group tranches.

**Untouched:** Solve's consider (the d0 runner, rollouts past the horizon) keeps the hold -- the permitted
greedy scope. The kill-scans (ApplySecondMainInSearch / the FSLineTail m2 fixpoint) probe projected-lethal
plans only, which the hold never drops. The offline label ladder (`EnumerateEarliestWins`,
`LabelSearchScopeActive`) keeps the hold, so value-leaf labels do not move -- see "Open".

Diagnostic: `MTG_HELD_WAVE_PROBE=1` prints `[held-wave] nodes/enumerated/scored/improved/budget-stopped`.

Unit tests: `test/unit/test_hinata_crackle_count.cpp`, the three `HELD_CRACKLE_LATE_WAVE` cases (the held
Crackle is absent from wave 0 and present in the held tranche, which holds only held, non-lethal plans; a
lethal Crackle stays in wave 0; no wave without a held card, with the lever `=0`, or on another deck).

## Results

### gi 27, monotone recovery (one pooled batch; `--seed 3030 --game-index 27`)

| setting | lever on | lever `=0` |
|---|---|---|
| d5 b20 (the regression case) | **7** | 8 |
| d5 b30 | **7** | 8 |
| shipped (value_play d5 b30) | **7** | 8 |
| d5 b100 | **7** | 8 |
| d5 b1000 | **7** | 8 |
| d8 b0 | **7** | 8 |

The wave is reached at every budget in this game, including the regression case's b20, and the win
turn does not move with budget. At d5 b20 the line is T3 Preordain, T5 Crackle X=2 (main 2), T6
Soulfire, T7 Ponder + a second Crackle X=2; `MTG_USER_ORDER_TRACE`: every cast `paid=1`. It is a
different T7 line from the committed tree's (which chipped X=1 on T3), and equally legal. The arm is
also cheaper here because the game ends a turn sooner: d5 b1000 used 1.86M units against 5.12M.
Probe over the six lever-on games: 272,737 held-phase node visits, 333,304 plans scored, 4,848
improvements, 4 budget stops.

### Movers (one pooled batch, both arms paired by gi, fresh seeds 9950001+, unwon = 9, 2980 games)

| cell | n | net | faster | slower | digests moved | units on / off |
|---|---|---|---|---|---|---|
| Hinata2 d0 | 200 | 0 | 0 | 0 | 0 | -- |
| Hinata2 d3 b10 | 150 | 0 | 0 | 0 | 1 | 1.022 |
| Hinata2 d5 b20 | 100 | -1 | 1 | 0 | 2 | 1.008 |
| Hinata2 shipped | 100 | 0 | 1 | 1 | 2 | 1.041 |
| hinata2hg d0 | 200 | 0 | 0 | 0 | 0 | -- |
| hinata2hg d3 b10 | 100 | 0 | 0 | 0 | 1 | 1.014 |
| hinata2hg d5 b20 | 60 | 0 | 0 | 0 | 0 | 1.007 |
| hinata2hg shipped | 60 | 0 | 0 | 0 | 0 | 1.028 |
| burn, stompy: d0 / d3 b10 | 200 / 60 each | 0 | 0 | 0 | 0 | 1.000 |

d0 is byte-identical by construction (the d0 runner is Solve, which keeps the hold), and every other deck
is byte-identical too (their providers hold nothing). The work-unit share is +1-4% on Hinata.

**The one slower game, Hinata2 shipped gi 46** (`--seed 9956047 --game-index 46`): `=0` T8; lever on,
unwon. It recovers at stage 1 (`--depth 8 --budget-ms 100`: 8, same digest as `=0`) and at stage 2
(d8 b0: 8). It is budget-induced: the held phase spends 2.5x the units on this game at the shipped budget
(193,708 against 78,541), and the re-priced search commits a different line.

### Identity

With `MTG_HELD_CRACKLE_LATE_WAVE=0` the new binary replays seven of the regression tier's own jobs
(hinata d0 s2002, d3 s2002, d5 s3003; hinata2hg d3 / d5 s2002; stompy d3 s2002; burn d3 s3003; 2250
games), which were run on the dd4859c3-source binary (`test/logs/regression/batch.log`). Every case
digest AND every work-unit total is identical.

## Open (the USER's calls)

* **The label ladder** keeps the hold. Admitting the held tranche there would make Hinata's value-leaf
  labels "earliest win with the chip allowed" instead of "with the hold"; it changes the next value-leaf
  regeneration, not play.
* `MTG_HINATA_HOLD_NEEDS_HINATA` (lever B, default OFF, measured worse on held-out) also restores T7 here.
* `HoldSacLandBurn` (Shard Volley) and the Irencrag waste gate have the same every-budget gate shape and were
  not touched.
* `EnumerateMainPlansExhaustive` (a provider DFS's enumerator) does not request the held tranche; no deck
  that uses it holds a card today.
