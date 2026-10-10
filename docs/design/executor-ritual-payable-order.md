# Executor: a ritual-funded opaque cast can be ordered before its rituals (DEFERRED)

Status: DEFERRED -- found 2026-10-10 while root-causing the one SLOWER smoke game of the discard round.
Two executor defects, both pre-existing, neither in the discard policy. Not fixed: both change depth-0
play (and the plan executor's mirror, ApplyPlanDirect) for every ritual deck, so each needs its own A/B.

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
than Rite + Pyretic + Apex (affordable: 6 + 2 + 1 + 1 = 10); why it did not retry Apex is not yet
established.

Fix sketch: gate the uncommitted branch on `cast_paid` like the committed one, and establish what the
re-solve should do after an unpaid cast (continue the plan's remaining casts vs re-solve).

## Measurement owed

Each fix alone, then both: the Dragonstorm, BreachingDragonstorm and every other ritual deck's suite
cells (d0 and searched -- ApplyPlanDirect mirrors the executor), plus a paired fresh-seed A/B. How often
defect 1 fires suite-wide is unknown (`MTG_PAYABLE_ORDER_STATS=1` counts domain entries).
