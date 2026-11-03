# EDF search-cost explosion: anatomy, three collapses, and where a budget check belongs (2026-11-03)

Repro: EldraziDisplacerFlicker, seed 5826, game_index 821, depth 5, budget_ms 20 (18,000 units),
`ignore_play_profile`, `exhaustive_profile none`, `exhaustive_bottom false` -- the shipped shape of
`build/scratch/budget/leak/e821_x50.json` with `decision_priced_x` removed. Base commit ba8bf8a0e
(budget-rebalance). Single thread, `MTG_RSS_CAP_GB=8`, turn census on.

## 1. Where the time goes

**Units do not see it.** The worst decision (t3, root 2 = FullSearchLineHybrid) charged 513 units --
`u_fs_pre` 294 + `u_fs_bp_wave` 219, no rollout steps -- and took 205.8 s: 183 enumerations, 8.29M
odometer visits, 588k plans emitted (72.6k after dedup), 66.7M mana payments (`TapForCostShared`),
10.9M sim keys. One unit is one plan APPLY; the cost is inside the apply.

**Call tree** (perf, `--call-graph dwarf`, Release):

```
FSLineWin (one search node; polls budget->Overrun() at ENTRY only)
 +- ApplyPlanDirect(bp variant / base plan reaching a draw)          [1 unit]
 |   +- breakpoint handler -> BpEnumEntryFor (consumer "eligible": the plan's bp_choice)
 |       +- BpDeriveContinuationList -> EnumeratePlansWithLand -> EnumeratePlans
 |           +- odometer walk: eval_and_push per subset
 |           |   +- SubsetPayableWithFilters (real payment walk)
 |           |   +- exec_feas rescue: SubsetPayableSequential
 |           |       +- EldraziFlickerProvider::ArrangeCastOrder -> EdfRangeAurasBehindUntappers
 |           |           +- OrderPaysSequentially / OrderPrepaysWholeTurn walks (up to 31 orders)
 |           |               +- TapForCostShared -> manaoracle::RefinePayment (PayEnum DFS)
 |           +- dedup: range-hosts tier rh_moved -> the same arrangement again, per colliding plan
 +- SimulateToEnd (horizon tail rollout; polls Overrun() once per simulated TURN)
     +- RolloutPreCombatSolve -> SolveWithLookahead (1-ply) -> ApplyPlanDirect(candidate) -> (same as above)
```

At the first slow decision (t1, 70 s window) 97% of samples were under `SimulateToEnd ->
SolveWithLookahead -> ApplyPlanDirect -> BpEnumEntryFor -> BpDeriveContinuationList`; after the
alias (section 3) the whole game splits 45% search-tree applies (FSLineWin, no rollout) / 26% rollout
applies / 3% unattributed, of 75% total in derivations.

**The board.** Six turns ahead of the root (turn 7), the blink loop (Eldrazi Displacer + Cloud of
Faeries + Training Grounds) has gone off: 47 floating {C}, a 14-card hand -- Trace of Abundance x2,
Overgrowth x2, Peregrine Drake x2, Living Wish x2, Wild Growth, Fertile Ground, Cloud of Faeries,
Aether Hub, Conservatory, Brushland -- and Mariposa Military Base / Cloud cycling as draw sites, so
nearly every applied plan opens a breakpoint.

## 2. Branching anatomy of one continuation derivation

Heaviest enumeration (MTG_BRANCH_SHAPE + MTG_BRANCH_HEAVY): 12 option groups + 1 independent ->
odometer 93,312 positions = 3^6 (six land Auras, each `{not cast, host A, host B}`; the provider
proposes two hosts) x 2^6 (two Living Wish, two Drakes, Cloud, the Displacer X=3 activation) x 2.
62,751 subset visits, 31,376 pass the rules, 13,556 plans emitted, 827 kept by the signature dedup.
A derived continuation list on that board holds 10,208 plans.

What multiplies, and how much of it is duplicate:

| axis | multiplicity | duplicate? |
|---|---|---|
| live Living Wish TUTOR PIN of the applying plan | 8 (the sideboard) | **all 8 derive the identical list** (same sim key; list hash equal; only `g_scripted_tutor_choice` differs in the key) -- fixed, section 3a |
| land-Aura host per Aura | 2 per Aura, 2^6 assignments | the signature drops the host (EDF host fold); the range-hosts tier keeps <= 2 per class. Not identity: payability depends on the host. Not touched |
| physical copy (2x Trace / Overgrowth / Drake / Wish) | (1+s)^2 vs C(s+2,2) per pair | copy twins; the walks were already copy-memoised (MTG_PAY_PROBE_CANON) except SubsetPayableWithFilters -- fixed, 3b; the twin skip of the whole eval is section 5 |
| EDF range projection | up to 2^min(k,6)-1 = 31 candidate cast orders per subset | prefix rule + order memo already; the dedup's rh_moved re-asked them on another vector, bypassing the memo -- fixed, 3c |

**Payments per plan.** ~184 TapForCostShared calls per emitted plan (8 per odometer visit) at the
worst decision: every passed subset is paid once by SubsetPayableWithFilters (one payment per cast,
x3 for Trace's hybrid pip, x2 when the reserved-host first attempt fails), and the many the flat pool
rejects (coloured pips with only {C} floating) go to the sequential rescue, whose EDF range projection
walks the given order and up to 31 rearranged orders -- each walk a full per-cast payment -- and each
outermost payment runs the pay-refine oracle (`RefinePayment`, 22% of the post-alias game).

## 3. The collapses (all identity / exact, DEFAULT ON, `=0` restores the old path)

**3a. `MTG_BP_TUTOR_PIN_ALIAS`** (commit 0456a28fe). A tutor-axis variant of a plan reaches the same
breakpoint with its pick still pinned; `BuildBreakpointKey` folds the live pin, so each pin value
derived the same list. Sound by a READ WITNESS (`core/SpellEffects.h`, `tutorpin`): every read that
can observe the pin value standing at the derivation's ScriptedTutor depth is counted -- the two
consuming resolutions (PerformTutor, the single-target put), every `BuildBreakpointKey` built at that
depth (a memo keyed on it could serve an entry that read the pin; payment-side keys from the mana
oracle are exempt via `MidTurnStateKeyOfPayment`) and a re-installed captured pin. A read inside a
nested apply sees that apply's own ScriptedTutor value. A derivation with zero reads is filed under the
key built with the pin cleared, flagged `tutor_pin_unread`; a lookup under any pin tries its exact key,
then the alias. Verify `MTG_BP_TUTOR_PIN_ALIAS_VERIFY=1`: 74 alias hits re-derived, 0 mismatches. Whole
game: 7,183 derivations, 7,073 unread; 101,869 alias hits.

**3b. `MTG_SPWF_CANON_MEMO`** (commit ccbf143d4). SubsetPayableWithFilters joins the copy-equivalence
pay memo (scopes with a copy pair only). 0 verify mismatches over 299k hits.

**3c. `MTG_RH_MOVED_VIA_CANDS`** (commit ccbf143d4). The dedup's arrangement probe runs over the
enumeration's `cands` (exact-equal mapping) so its order walks are served from the enumeration's pay
memo. 0 verify mismatches (MTG_PAY_PROBE_MEMO_VERIFY, MTG_RH_MOVED_MEMO_VERIFY).

**Measured** (whole game, single thread; wall is on a shared 32-core box, so priced work is the
comparable number -- `kPriceNs` x the work counters):

| | base | + 3a | + 3b/3c |
|---|---|---|---|
| game wall | 1,802 s | 385 s | 354 s |
| game priced | 2,595 s | 667 s | 529 s |
| pay calls | 724M | 195M | 158M |
| sim keys | 114M | 26.6M | 19.1M |
| worst decision wall (513 units) | 205.8 s | 30.5 s | 25.2 s |
| digest / units / win | 6dcbeeee3de462ad / 3,290,793 / t7 | identical | identical |

EDF regression jobs (`reg_manifest.json` edf_*, 1,365 games, 2 threads): all six digests and unit
totals identical in every arm. Budget 0 (d5, unlimited, capped at 1,500 s): every decision both arms
completed has identical units and work.

## 4. Where a budget check belongs (for the priced per-decision ceiling)

Nothing below one apply polls `SearchBudget::Overrun()`: FSLineWin tests it at node entry, and
SimulateToEndImpl once per simulated turn, but a single apply can contain a breakpoint derivation of
seconds (here 5-10 s each, minutes per decision), and a derivation charges no unit. So a tripped
priced ceiling cannot interrupt the work it is meant to bound.

The minimal sound place is the derivation's own odometer walk, at the per-subset hook where the
per-game backstop already polls (`eval_and_push`, `bp_abandon_watch`): stop the derivation once
`decisionwork::PricedExceeded()`, report it INTERRUPTED (so the partial list is never cached -- the
`MTG_BP_DERIVE_METER` mode-2 machinery), and let the next Overrun poll roll the pass back. Built as
**`MTG_BP_DERIVE_PRICED_STOP` (DEFAULT OFF, a proposal)**. It is inert at an unlimited budget (the
ceiling is never armed there), so it re-prices work and cannot make a line unfindable at a large budget.
Caveat, same as meter mode 2: an interrupted (EMPTY) continuation can reach play only through the
anytime rescue, where the executor re-derives the breakpoint and may apply the canon `front()`.
Measurement: section 6.

## 5. Left

* One derivation per DISTINCT board remains (~5 s on the 14-card hand), and the "eligible" consumer
  derives the whole 10,208-plan list to read ONE rank (its bp_choice). A lazily derived rank is not
  byte-identical by construction (parked since 2026-10-17, edf-slow-rollout-tail section 4b).
* Copy-twin skip in `eval_and_push`: a subset whose canonical copy sequence (sel order) matches an
  earlier EMITTED twin produces a plan the dedup always drops. Not built: its exactness needs every
  check in eval_and_push to be identity-free (two read hand_index for Equipment pieces) plus a verify
  of (signature, wins, value) per twin.
* Per-node cost: `RefinePayment` 22% and the full `BuildBreakpointKey` it builds per refined payment
  (`MidTurnStateKeyOfPayment`) for its memo key.

## 6. MTG_BP_DERIVE_PRICED_STOP measurement

Same repro with `decision_priced_x: 50` (the 22.5 s priced ceiling), on top of 3a-3c, single thread:

| | stop off | stop on |
|---|---|---|
| worst decision priced / wall | 47.3 s / 15.7 s | 25.1 s / 7.8 s |
| decisions over the 22.5 s priced limit | 6 | 11 (all <= 25.1 s) |
| game priced (refused visits counted at the odometer price) | 508 s | 610 s |
| game wall | 180 s | 210 s |
| units / win / digest | 3,291,912 / t7 / 6dcbeeee3de462ad | 3,932,026 / t7 / 6dcbeeee3de462ad |

So the check does what the ceiling needs -- every decision now ends within ~1.1x of its priced limit --
but on this game it costs work overall (+20% priced, +22% units): an interrupted derivation is not
cached and is derived again by the next pass, and rolled-back passes are re-spent. 19.7M subset visits
were refused (cheap; their odometer price inflates the 610 s). One game, same win turn: the quality
question (and whether a cached "interrupted at this price" marker should stop the re-derivation) is
the budget work's A/B to run, not settled here.

## 7. Wall clock

Quiet box (load ~1-3), final binary: the repro game in **178 s** (units 3,290,793, t7, digest
6dcbeeee3de462ad); the worst decision 12.4 s. The base binary on the same quiet box: **767 s** (same
units, digest, win) -- 4.3x. The 1,802 s figure above was on a loaded box (load ~30); the priced ratio
(2,595 s -> 529 s, 4.9x) is the load-independent comparison.

The second tail game (`build/scratch/budget/ab_x3.json`, EDF seed 1014228 gi214, d5 b20, keep table on),
final binary, 2 threads: X=0 44.9 s / 170,364 units / t4; X=20 47.2 s / 188,269 units / t4 (the
budget-rebalance binary: 136 s and 3,147 s, 206k units at X=20). Caution: the priced ceiling trips on
the work counters these collapses reduce, so at X > 0 the trip points -- and therefore play and units
-- differ from that binary's; X=0 is the like-for-like comparison. EDF regression jobs back to
back on the quiet box, 4 threads: base 72.8 s / final 61.3 s batch wall (d5 jobs 64.0 + 49.7 s vs
61.6 + 50.2 s core time) -- neutral, as expected: the suite's games rarely reach the go-off boards.
