# Dragons engine follow-ups: report

(Written by the parent from agent a1e5abfbafc210125's final message, 2026-11-26.)

All three items from `logs/dragons-ogap/report.md` are fixed on `wip/dragons-eng` (head 323e3ce46). Both new levers default ON. Dragons gate APPLY-DROP 1,136 -> 804; play A/B has no slower game.

Commits: d88f2e1d8 (levers + tests), aa6118e74 (design note), 615d4ccd0 (gate), e506476c3 (A/B), 323e3ce46 (Cid skip + CPU table). Design note `docs/design/dragons-eng-2026-11-26.md`; tables `logs/dragons-eng/{gate_report,ab_table}.txt`; raw output `build/scratch/dragons-eng/{gate_base,gate_new,ab_out,cg}`.

## Item 1: Bolt dropped after the Mind Stone dig (`MTG_DIG_FLOAT_HAND_DEMAND`)
- **Mechanism:** the dig is the plan's last payment, so the line hold is empty. The line-float rung (`LineFloatTapFirstPlan`) saw no later demand, and the drain spent Gruul Turf's floating {R} on the dig's {1}. Haven's {C} stayed up; the continuation's Bolt (in hand, undeclared) had no red.
- **Fix:** `DigContDemandScope` (ManaPayment.h) bound around the sacrifice-draw payment in TurnSolver's dig loop and `AIEngine::PerformDigFull`. While a colour floats it gives the rung, per colour, the most pips any one nonland card in hand needs, so a spare source (Haven {C}, Sol Ring) pays the generic and the colour keeps floating. Reads only the visible hand; a payment choice at every budget; falls through to the plain payment if the held attempt fails; folded into `PayContextDigest`.
- **Not covered:** cycling digs (shared with the searched cycle seeds and their rollout twin `cycle_chain_step`).

## Item 2: a rebuy only the Haven could pay (`MTG_GYRETURN_TAP_DEBIT`, sound collapse)
- **Mechanism:** the enumerators' flat pool counted the Haven's own mana toward its "{2}, {T}, Sacrifice" rebuy; both apply worlds hold the Haven, so the plan applied as itself minus the rebuy (a dead branch).
- **Fix:** the self-funding debit (`PermAbilityTapDebitOf`, in the Solve and EnumeratePlans scans) now covers `GraveyardReturnAbility` candidates via `TapDebitCand`. Haven and Haunted Fengraf lose their yield from the pool when the plan includes their rebuy. Cid, Freeflier Pilot (no mana) is skipped. A creature that gains a mana ability elsewhere isn't debited, which keeps the old menu and never loses anything. The inert-bit cache stays on.
- **Not covered:** the filter-rescue path (`SubsetPayableWithFilters`) doesn't hold the source; neither Dragons nor Landfall has a filter land.

## Item 3: the 10 continuation rebuy drops
Item 2's mechanism inside a breakpoint continuation (`EnumeratePlansWithLand`), covered by the same debit with no extra code. s30137584 T6: two Incubators make each Lathliss {R}{R}, all four red sources go, and the legend-ruled copy is offered back by a rebuy only the floating {G} and the Haven could pay. s30128278 T5 is the same with one Lathliss in play. Single-game oracle check on the three repros: levers off, 12 APPLY-DROP lines; on, none.

## Gate (Dragons gi 96000-195999, 100,000 games, oracle level 2; base = same binary, levers off)

| class | base | new |
|---|---|---|
| APPLY-DROP | 1,136 | 804 |
| dig-float shape (item 1) | 301 | 1 |
| T3 m1 `{land=Gruul Turf}` dropped Bolt | 262 | 0 |
| Haven rebuy drops (item 3) | 10 | 0 |
| APPLY-DROP decisions (seed, turn) | 609 | 288 (323 gone, 2 new) |
| PHANTOM | 103,802 | 99,560 |
| ORACLE-GAP | 0 | 0 |
| DROP-COVERED | 3,970 | 3,760 |

The base reproduces the dragons-ogap fix column exactly. The 2 new decisions (s30174699 T4, s30122890 T4) and the 1 remaining dig-float line (s30155709 T3) are two-dig chains (two Mind Stones beside Sol Ring); not chased.

## Play A/B
Held out, base seed 5260000, seed = base + gi and game_index = gi; play settings; one pooled batch of 3,048 jobs (76,200 games), heartbeat 32/32 at 10 min. Each lever paired off vs on, the other at its default.

| lever | pairs | net | slower | faster | units | CPU (byte-identical chunks) |
|---|---|---|---|---|---|---|
| GYRETURN_TAP_DEBIT (Dragons, Landfall, Kitty; std + 2HG) | 10,400 | -1 | 0 | 1 | -0.40% | Dragons -0.31% std / -0.42% 2HG |
| DIG_FLOAT_HAND_DEMAND (14 sacrifice-draw decks; std + 2HG) | 27,700 | 0 | 0 | 0 | -0.01% | noise |

- Gy lever by deck: Dragons std/2HG net 0 (units -0.38%), Landfall std net -1 (units -0.69%), Kitty byte-identical.
- Dig lever: 7 of 1,108 chunks change (Dragons 4, Stuffy Doll 3), no win turn moves; in play it is close to inert.
- No slower games, so no d8 b0 replays.
- Instruction counts (callgrind `RunGame` inclusive Ir, one 25-game chunk per arm): Dragons dig lever +0.0007%; Dragons gy lever -0.0104%; Kitty -0.0016% after the Cid skip (noise; the skip is kept because it is free and exact).

## Unit tests
`test/unit/test_dragons_eng.cpp`: 2 cases, 36 assertions, each lever on and off. Agent's full suite 4,210/4,213 under default and levers-off; the 3 failures (Windswept Heath / Blood Sun, Boars produces, `test_bottom_pref_order`) fail identically on its base binary (worktree environment; the parent's land-1127 build passed 4,211/4,211).

## Open
- Windows CI not checked (gh unauthenticated).
- Both levers change play: smoke + regression GT at landing.
- No USER decisions: the gy lever is a sound collapse (default ON by rule); the dig lever is a payment choice that measured neutral.

Agent ended at 464K tokens (over the 400K limit; the parent missed the 250K check).
