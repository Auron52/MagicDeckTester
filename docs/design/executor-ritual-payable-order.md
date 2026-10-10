# Executor: a ritual-funded opaque cast can be ordered before its rituals

Status: IMPLEMENTED BEHIND THREE LEVERS (2026-10-10), all default ON, each `=0` restoring the previous
behaviour exactly; the pooled A/B (below) is still owed. Found 2026-10-10 while root-causing the one SLOWER
smoke game of the discard round. Two executor defects, both pre-existing, neither in the discard policy.

## The fix (2026-10-10)

| lever (heurarm slot) | what it changes | where |
|---|---|---|
| `MTG_DRAGONSTORM_ORDER_OPAQUE` | `DragonstormProvider::OrderOpaqueCastsByRank()` returns true, so an opaque set (Apex) is cast in CastOrderRank order -- rituals 15, Ruby Medallion 16, Irencrag 18, payoff 20 -- in the executor AND ApplyPlanDirect (one shared `OpaqueCastOrderActive`). It also turns on `ApplyCastOrderRangeLadder` for the deck (inert: no Dragonstorm card has a range or a fallback ladder). | `src/ai/DecisionProviders.cpp` (`DragonstormProvider::OrderOpaqueCastsByRank`) |
| `MTG_EXEC_FAILED_CAST_NO_RESOLVE` | Defect 2: the executor's uncommitted draw-engine branch, its site-6 partition truncation, and the nested continuation loop no longer arm a breakpoint for an UNPAID cast. | `src/ai/AIEngine.cpp` (`ExecFailedCastNoResolve`, the three main cast loops + `resolve_draw_breakpoint`'s continuation loop) |
| `MTG_PAYABLE_ORDER_RITUALS` | Defect 1: `PayableCastOrderCore` admits an order holding a producing cast the walk credits (ritual float, same-turn rock, spendable Treasure mint) -- APPLY SITE ONLY (site 0: executor + ApplyPlanDirect). The enumeration's rescue retry (site 1) keeps the maker / reveal domain, so subset admission is unchanged. | `src/ai/ManaPayment.cpp` (`PayableOrderRitualsOn`, `PoCreditedProducer`) |

Target game (gi 533, seed 1534, d0): all three ON -> T5; all OFF -> T8 (= HEAD). Alone:
ORDER_OPAQUE -> T5, PAYABLE_ORDER_RITUALS -> T5, FAILED_CAST_NO_RESOLVE -> T8 (see below).
Unit cover: `test/unit/test_dragonstorm_ritual_order.cpp`.

Small sanity read (50 Dragonstorm d0 games, seeds 600001.., one job per arm, NOT a verdict): turn sum
all-off 282.0, all-on 260.0; ORDER_OPAQUE alone 278.0, FAILED_CAST_NO_RESOLVE alone 282.0 (different
digest, same sum), PAYABLE_ORDER_RITUALS alone 260.0. The all-off arm's digest (`de8c074a7dfb425f`)
equals the c77cffb5 binary's on the same 50 games, and the 39 Dragonstorm references replay with the same
verdicts on both binaries (6 ok / 33 repaired / 0 drift).

**Why the re-solve after the failed Apex cast Scourge, not Rite + Pyretic + Apex.** Not a payability
error: both were affordable (8 on hand; Rite + Pyretic + Apex = 13 = 8 + 5). The d0 greedy Solve scored
{Scourge of Valkas} 400 against {Apex, Rite, Pyretic} 300 (a diagnostic run removing each hand card in
turn). The turn-start plan won with the two extra rituals' value in it; once Desperate and Seething were
sunk, Apex's line scored below a 4/4 Dragon. That is the d0 greedy's EvalCard valuation (it cannot see
what Apex's exile finds), not a clear bug, so it is left alone -- and it is why FAILED_CAST_NO_RESOLVE
alone does not rescue gi533: without the mid-plan re-solve, Rite and Pyretic are cast (8 floating +
Bazaar 2 = 10) and the d0 second pass then makes the same choice, Scourge.

**History the A/B must weigh for FAILED_CAST_NO_RESOLVE.** Step 31 (`dfc11ec41`, 2026-10-01) measured
this exact gate on the uncommitted path at +1351 turns on dragonstorm d0 over the overnight tier (+4
searched) and scoped its fix to the committed line for that reason -- on an engine that still cast Apex
sets in hand order. Also: at depth > 0 TakeTurn requests no second pass, so in an uncommitted searched
turn the lever leaves NO re-solve after a failed draw-engine cast (the remaining plan casts are made and
the phase ends).

BreachingDragonstorm has no exposure: its list holds no ritual, rock or Treasure maker (cascade / free-cast
spells and lands only), so no cast's position changes what the rest can pay. Not opted in.

## The game

`dragonstorm_smoke_d0_s1001` gi 533 (seed 1534, depth 0). Ground truth wins turn 5; with the USER's
Dragonstorm cleanup-discard design (MTG_DRAGONSTORM_BUCKET_DISCARD, default ON) it wins turn 8.

- **The discard is correct.** Turn 4 cleanup, hand Lathliss, Scourge, Desperate Ritual, Seething Song,
  Apex of Power (opening hand), Rite of Flame, Pyretic Ritual, Apex (drawn turn 4); board Sandstone
  Needle, Mercadian Bazaar. The policy keeps every Apex (no Dragonstorm in hand), judges mana NOT very
  limited (Needle 2 + Bazaar 1 + two land drops 2 + ritual net 5 = 10 >= 9 -- correct: turn 5 really
  makes exactly 10) and sheds Lathliss. The generic ranking shed the opening-hand Apex instead.
- **What lost the turn.** Both arms choose the same turn-5 plan (four rituals, then Apex). Apex is
  ordering-opaque, and with `MTG_ORDER_OPAQUE` off (Dragonstorm does not opt into
  `OrderOpaqueCastsByRank`) the opaque set is cast in RAW plan order, which follows hand order
  (`src/ai/AIEngine.cpp` ~6158-6185). The solver's affordability check is order-blind (totals). With
  the policy on, the surviving opening-hand Apex sits third in hand: it is cast after only Desperate +
  Seething (6 floating + Bazaar 2 = 8 < 10) and fails to pay. With the policy off the surviving Apex was
  the turn-4 draw, last in hand, so the order happened to pay exactly 10. The old turn-5 kill was
  hand-order luck.
- Confirmed counterfactuals: the policy-on arm with `MTG_ORDER_OPAQUE=1` sorts the casts Rite,
  Desperate, Pyretic, Seething, Apex and wins turn 5 (the policy-off arm too).
  Repro scripts: `logs/discard_impl/rc/dragonstorm/run1.sh` (batch), `run4.sh` (`MTG_EXEC_TAP_TRACE`),
  `run5.sh` (ordering counterfactuals).

## Defect 1: the payable-order fallback excludes ritual casts

`MTG_PAYABLE_ORDER` (USER-adopted 2026-09-27, default ON) already states the right rule: keep the given
order unless it projects UNPAYABLE, then take the nearest payable permutation of the same casts ("it
really is just for the 'fail to pay' case"). But `PayableCastOrderCore` (`src/ai/ManaPayment.cpp`
~3089) returns unless the order holds an ETB-Treasure maker or a reveal-or-pay cast, on the stated
premise that "a pure reorder of non-producing, reveal-free casts cannot change the aggregate verdict".
A mana RITUAL is a producing cast, so the premise does not cover it, and the projection walk already
credits ritual float (`FirstUnpayablePos`, ~2588 "A PRODUCER (ritual float ...) is NOT a decline").

Fix sketch: admit an order holding a mana-producing ritual cast into the domain (the walk is
unchanged). Bound: exhaustive for <= 7 casts, single moves beyond (as today). Lockstep with
ApplyPlanDirect's opaque branch. Byte-identical for any order that already pays.

## Defect 2: a FAILED draw-engine cast still runs the post-cast re-solve

In the executor's cast loop (`src/ai/AIEngine.cpp` ~6212-6235) the uncommitted
`is_draw_engine || put_armed` branch calls `resolve_draw_breakpoint(0)` without checking `cast_paid`
(the committed branch does check it). When Apex fails to pay, the turn is re-solved as though a
draw-engine breakpoint had resolved. In gi 533 the re-solve then cast Scourge + Rite + Pyretic rather
than Rite + Pyretic + Apex (affordable: 6 + 2 + 1 + 1 = 10). ESTABLISHED 2026-10-10 (see "The fix"
above): the re-solve itself cast only Scourge (d0 greedy value 400 vs 300 for the Apex line); Rite and
Pyretic were the base plan's remaining casts, made by the main loop after it returned.

Fix sketch: gate the uncommitted branch on `cast_paid` like the committed one, and establish what the
re-solve should do after an unpaid cast (continue the plan's remaining casts vs re-solve).

## Measurement owed

Each fix alone, then both: the Dragonstorm, BreachingDragonstorm and every other ritual deck's suite
cells (d0 and searched -- ApplyPlanDirect mirrors the executor), plus a paired fresh-seed A/B. How often
defect 1 fires suite-wide is unknown (`MTG_PAYABLE_ORDER_STATS=1` counts domain entries).
