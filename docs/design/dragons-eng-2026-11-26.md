# Dragons mana-oracle engine follow-ups (2026-11-26, branch `wip/dragons-eng`)

Checkpoint + design note for the three engine items the Dragons ORACLE-GAP work (`dragons-oracle-gap-haven-2026-11-26.md`,
commit 35cd445d6) left open. Base: origin `wip/land-1127` (phase-1-2-deck-analyzer 34bd5960c + 35cd445d6 +
MTG_LINE_KEEP_CRIT_FILTER).

## 1. APPLY-DROP x262: the dig's {1} spent the floating {R} a held Bolt needed (MTG_DIG_FLOAT_HAND_DEMAND)

Repro s30096713 gi96713 T3 m1 `{land=Gruul Turf}`, board Haven of the Spirit Dragon + Mountain + Mind Stone, Bolt in hand.

Mechanism: Gruul Turf's bounce floats the bounced land's {R}. The plan's trailing sacrifice-draw dig (the dig loop in
both apply worlds: Mind Stone's "{1}, {T}, Sacrifice: draw") is the line's LAST payment, so the line hold
(`g_line_unpaid_cost`) is empty there, and the line-float rung (`LineFloatTapFirstPlan`, MTG_LINE_FLOAT_TAP_FIRST) saw
no later demand: the drain spent the {R} on the {1} while Haven's {C} stayed up. The dig's draw continuation then
re-solves and picks the Bolt already in hand, which has no red. The Bolt is not declared in the plan, so nothing the
line-keep / crit rungs read could see it at the dig.

Fix: `DigContDemandScope` (ManaPayment.h), bound around the sacrifice-draw payment in TurnSolver's dig loop and in
`AIEngine::PerformDigFull` (lockstep). While a colour floats, it publishes per colour the most pips any one nonland
card in HAND carries; `LineFloatTapFirstPlan` takes that as later demand beside the line hold's, so a spare source
(a land / rock whose payment colours no later pip wants: Haven's {C}) pays the generic and the owed colour keeps
floating. Same held attempt, same fall-through. A payment choice in both worlds at every budget (the mana-payment
exemption), never a pick among searched options. Only the visible hand is read. Folded into `PayContextDigest`.
Cycling digs are not covered (the executor shares `PerformDigFull` with the searched cycle seeds, whose rollout twin is
`cycle_chain_step`); a cast that opens a breakpoint is `g_line_opens_unseen`'s business and is untouched.

## 2. The menu offered a Haven rebuy only the Haven could pay (MTG_GYRETURN_TAP_DEBIT)

Mechanism: Haven's rebuy is "{2}, {T}, Sacrifice". Both apply worlds hold the Haven while paying its {2}
(`SelfTapSourceHoldBit`), and its {T} is part of the cost, so a rebuy whose {2} needs the Haven's own mana never
activates: the plan applies as the same plan without it (PHANTOM +4,422 once the dragons-ogap fix reported the drop).
The plan enumerators' flat pool counted the Haven's yield toward the plan's mana.

Fix: the "{cost}, {T}" self-funding debit (`PermAbilityTapDebitOf`, MTG_TAP_ABILITY_DEBIT, both scans: Solve and
EnumeratePlans) also covers a `GraveyardReturnAbility` candidate (`TapDebitCand`): the source's exact yield leaves the
plan's pool when the plan selects its rebuy. Haven and Haunted Fengraf are mana sources (debit 1); Cid makes no mana
(debit 0, unchanged). The inert-bit cache keeps running beside a gy-return debit (it rides on a costed action, never an
inert bit). A sound collapse (the dropped plans are ones the engine can never execute): default ON.

## 3. The 10 newly visible continuation rebuy drops

Same mechanism as 2, inside a breakpoint continuation (continuations enumerate through EnumeratePlansWithLand, so the
debit covers them with no further change). s30137584 T6 `{land=Mountain + Lathliss x2 (+ equip Greaves)}`: two Urza's
Incubators make each Lathliss {R}{R}; Gruul Turf's {R} + three Mountains are the only red, so both Lathliss take all
four, the Turf's {G} floats, and the legend rule's graveyard copy is offered back by a continuation rebuy whose {2} only
the float {G} and the Haven itself could pay. s30128278 T5 the same with one Lathliss beside the one in play. Single-game
oracle check (MTG_MANA_ORACLE=2, both levers per job): levers off reproduces all 12 APPLY-DROP lines of the three repro
games (s30096713 T3/T4, s30128278 T5, s30137584 T6); levers on, none. Units 2656 -> 2299, 4061 -> 3924, 928 -> 859.

## Gate (Dragons gi 96000-195999, 100,000 games, MTG_MANA_ORACLE=2; base = both levers off, same binary)

| | base | new |
|---|---|---|
| APPLY-DROP | 1,136 | **804** |
| of which the dig-float shape (`pay1[<src>:C keep-floating {R}] ; pay2[(float only)]`) | 301 | 1 |
| of which T3 m1 `{land=Gruul Turf}` dropped Bolt | 262 | 0 |
| of which a Haven rebuy (`act Haven ... unpaid`, item 3) | 10 | 0 |
| APPLY-DROP decisions (seed, turn) | 609 | 288 (323 gone, 2 new) |
| PHANTOM (quiet) | 103,802 | 99,560 |
| ORACLE-GAP | 0 | 0 |
| DROP-COVERED | 3,970 | 3,760 |

Base reproduces the dragons-ogap gate's fix column exactly (APPLY-DROP 1,136, PHANTOM 103,802), so the levers-off arm
is byte-identical to the pre-change engine on this slice. The 2 new decisions (s30174699 T4, s30122890 T4) are
two-Mind-Stone dig chains `{land=Gruul Turf}` beside Sol Ring, where a dropped creature (Dragonspeaker Shaman, Atsushi)
would need the oracle's order of two digs; the 1 remaining dig-float line (s30155709 T3) is a two-dig chain too
(ROLLBACK depth2). Not chased: 3 lines against 323 decisions gone.

## Status

- d88f2e1d8: code for 1 and 2 + `test/unit/test_dragons_eng.cpp` (2 cases, 36 assertions, pass). Full unit suite
  4,210 / 4,213 under both the default and the levers-off env; the 3 failures (Windswept Heath / Boars card checks,
  bottom-pref bucket) fail identically on the dragons-ogap base binary, so they are environmental, not this change.
- Next: unit tests on/off, Dragons gate base (both levers off) vs new, held-out play A/B (`logs/dragons-eng/mk_ab.py`).
