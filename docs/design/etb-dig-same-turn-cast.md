# ETB-dig -> same-turn cast is inexpressible at searched depths (DEFERRED)

**Status: FIXED 2026-09-26** (`MTG_BP_ETB_DIG`, default ON) after the user rejected the deferral:
*"my understanding is that a breakpoint should open which would allow us to cast the new spell ... If
that doesn't happen, I agree it is a bug."* The fix is not a new arming site: `etb_dig_count` was
removed from `TurnSolver::ParamKeyedDrawClass`, so the general site-10 put-in-hand arming (outcome-
keyed, rollout + executor in lockstep) now fires after the dig, and site 10's `PlanOpensBreakpoint`
route list names `etb_dig_count` so the continuation is fanned (canon audit: 0 unchallengeable).
gi11 now wins T4 on the Crewmate -> dig Buccaneer -> cast Buccaneer line. Still open: a VIAL-put
digger (outside any cast window) -- the `MTG_BP_HAND_ENTRY` hole. The text below is the original
diagnosis, kept for the record.

## The defect

A creature whose ETB digs a card into hand (`etb_dig_count`: Staunch Crewmate, Acclaimed Contender)
can have that card cast **the same main phase** with the mana left over. The searched engine cannot
represent that line at any budget:

* The rollout (`ApplyPlanDirect`, cast branch, the `PerformEtbDig` call) deliberately does **not** arm a
  breakpoint after the dig (the `MTG_ACQ_DIG` note at that site: re-arming it was measured 2026-08-19
  and turned 6/8 held-out searched keys red, attributed to the then-greedy continuation misplaying).
* The general put-in-hand rule (`MTG_BP_PUT_IN_HAND`, default ON) would arm site 10 on "the hand gained
  a card", but it stands down for any cast `TurnSolver::ParamKeyedDrawClass` claims, and that list
  includes `etb_dig_count > 0`. So the class is **claimed but never armed** in searched play.
* `MTG_ACQ_DIG` (adopted) covers only the depth-0 executor's second pass.

## Evidence

Pirates, seed 777011, game-index 11, prefix `1,0,-1,-1,0,-1,-1` then `--choices-then-auto`: the search
wins T5 at `--budget-ms 200` **and** 20000 (budget-invariant, so a representation gap and not
starvation). Claude's line (T3 Crewmate -> dig Daring Buccaneer -> cast it with the leftover {R};
prefix `1,0,-1,-1,0,-1,-1,9,1`) wins T4.

## What a fix looks like

Arm the deferred re-solve after an ETB dig that put a card in hand, in BOTH worlds at the same point
(rollout: the cast branch's dig, the Vial-put `apply_vial` dig; executor: `note_draw_engine` /
`is_draw_engine` classification plus the Vial deploy path), so the continuation is a SEARCHED node
(bp_choice / waves), not the greedy re-solve the 2026-08-19 measurement ran against. Then measure on
Knights and Pirates at play settings (smoke + regression, held-out confirm) before adoption; expect
Knights GT to move.

Open edge already recorded in `EngineFlags.h`: a Vial-deployed digger has no second pass at all.
