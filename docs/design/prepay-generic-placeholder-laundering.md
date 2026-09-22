# Prepay's generic placeholder launders colourless mana into coloured pips (DEFERRED engine bug)

Self-contained. Found 2026-09-22 while attributing Hinata's movers under `MTG_BP_NEW_ONLY`
(`bp-new-only-continuations.md`). **Not fixed.** It is present in BOTH arms of every A/B run to
date, it changes shipped play when fixed (a rebaseline), and it was set aside so the breakpoint
work could finish. The user has been told; the fix is theirs to schedule.

## The defect

`TurnSolver::BatchPrepayMainCasts` (TurnSolver.cpp) pre-pays a plan's whole main-phase bill at
once. It loads the float as the plan's **pinned coloured pips plus a placeholder** for the generic
part: `wild = combined.generic`, funded colourless-first (Sol Ring's `{C}{C}`, an Everflowing
Chalice, any colourless source) precisely so coloured sources stay free for the coloured pips. That
placeholder is documented at the prepay site as "not an undecided tap, it is a bucket the plan's
own casts drain generic-first" (SpellEffects.h, the `wild` note near the float contract).

The contract holds for the casts INSIDE the batch. It breaks for a cast made **outside** it in the
same phase: a breakpoint continuation's cast. That cast pays through `SpendFloatingTowardCost`,
whose step 2 spends *wild* float on **coloured** pips (wild is, by the float's own semantics, any
colour). So colourless mana that the prepay parked as a generic placeholder pays a coloured pip it
could never have paid at the tap.

## The proven instance

Hinata, seed 1129, turn 3, `MTG_BP_NEW_ONLY=0` (the shipped arm), budget 0:

* batch `{Ponder, Ornithopter}` -> float `{u1, w1, *2}` (the `*2` is Sol Ring's colourless);
* Ponder takes the `u`;
* the continuation casts **Expressive Iteration `{U}{R}`** -- paid entirely from the two wild
  units, i.e. from Sol Ring. The lands that could have paid `{U}` and `{R}` (Forbidden Orchard,
  Mystic Monastery) were the ones the prepay had held aside.

Evidence: `MTG_EXEC_TAP_TRACE=1` on that game (`logs/snowdiag/hin_off_1129_b0_exectap3.out`, the
executor's per-cast tap trace), and a unit probe that asked the payer for `{U}{R}` after `{U}` on
Orchard + Monastery + Sol Ring -- the payer correctly REFUSES it, which is what shows the float path
is the launderer (probe reverted, not committed). `ManaPool::CanPay` is colour-correct, so the
new-only arm -- where the base plan `{Ponder, EI}` is unenumerable on that board and an `{EI}`
continuation is an old card -- cannot reach the laundered line at all. **Part of Hinata's measured
"loss" under `MTG_BP_NEW_ONLY` is therefore the shipped arm winning on illegal mana.**

## Why it was deferred

* Both A/B arms carry it, so no measurement of the breakpoint work is biased by it in one direction
  only -- except where one arm can reach a laundered line the other cannot, which is exactly the
  Hinata case above (the shipped arm is flattered).
* Fixing it changes play for every deck that prepays with a colourless source in the pool and then
  casts from a breakpoint continuation. That is a ground-truth rebaseline, which the user schedules.

## The fix, when scheduled

Two shapes, either sound:

1. **A generic-only bucket.** Give the prepay's placeholder its own pool slot that
   `SpendFloatingTowardCost` may spend on GENERIC pips only. Smallest change; the wild float keeps
   its meaning for every other producer (a ritual's "any one colour" float is genuinely wild).
2. **Keep true colours.** Instead of collapsing the generic part to a placeholder, record which
   sources funded it and float their real colours (a colourless source floats `{C}`). More faithful,
   more code, and it changes the prepay's own colour reservation.

Either way: build, flag-off identity is NOT expected (this is a correctness fix), run smoke +
regression, classify the moved keys with the per-game audit, and accept. Re-measure Hinata's
`MTG_BP_NEW_ONLY` delta afterwards -- the +0.005 residual on that deck may vanish with the launderer.

## Where the rule is written

`SpellEffects.h`, the mana-float contract notes around `SpendFloatingTowardCost` and the prepay
pool's `wild` paragraph (search `BatchPrepayMainCasts' pool`), and `TurnSolver::BatchPrepayMainCasts`
itself. An earlier laundering of the same family (a filter land's `{U/R},{T}` mode, "the gi164 prepay
laundering channel") was fixed at the tap; this one is at the float.
