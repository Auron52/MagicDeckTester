# Whole-turn prepay: holding what the rest of the hand needs

Self-contained record of the 2026-10-17 human-play fix (`MTG_HUMAN_PREPAY_DEMAND`, default ON) and
the one open question it leaves for autonomous play.

## The defect (human play)

Found twice by the Colossus Hammer claude-play sweep:

* **seed 2100016 T3.** Board: Mountain #26, Furycalm Snarl #11 (R/W — the only `{W}` source),
  Swiftblade Vindicator; hand Colossus Hammer, Loxodon Warhammer, Magnetic Theft, Mountain,
  Sigarda's Aid `{W}`, Speaker of the Heavens `{W}`. The human commits
  `land=Mountain; cast: Colossus Hammer, Magnetic Theft (Colossus Hammer) -> Swiftblade Vindicator`
  (`{1}` + `{R}`). Result: Mountain #26 and the **Snarl** tapped, the new Mountain #27 untapped; the
  next line in the same main offers no white card.
* **seed 2100017 T6.** Plains #36/#37/#38, Mountain #26 (the only `{R}`); the line
  `cast: Speaker of the Heavens, Colossus Hammer` paid the Hammer's `{1}` with the Mountain, two Plains
  idle, two Magnetic Thefts (`{R}`) in hand.

**Mechanism.** A line with two or more hand casts is paid up front by
`TurnSolver::BatchPrepayMainCasts` (`src/ai/TurnSolver.cpp`), reached from `ApplyPlanDirect` — the
human commit path is `AIEngine::TakeTurn -> TurnSolver::ApplyPlan -> ApplyPlanDirect`. Its joint solve
is `TapForCostBacktrack` over the COMBINED bill (`src/core/SpellEffects.cpp`,
`TapForCostBacktrackWorker`): a DFS that keeps its FIRST solution with candidates in battlefield order
(Mountain #26 idx 0, Snarl idx 1, Mountain #27 idx 3), and nothing in it reads the hand. The casts then
drain the pre-loaded pool, which is why `MTG_TAPDBG` showed `[paydbg] cost={1} taps:(none)` and no
`[tapdbg]` lines. `MTG_NO_BATCH_PAY=1` makes the line pay with the two Mountains, confirming the site.
A one-cast line never reaches the joint solve (the `eligible < 2` fast decline), so it is paid by the
scarcity payer in `ManaPayment.cpp`, whose `MTG_HUMAN_TAP_DEMAND` tiebreak spends the source the hand
needs least — the only reason "Hammer alone" paid correctly.

## The fix

`TurnSolver::HumanPrepayDemandHolds` builds a reserve: one untapped source per coloured pip the REST of
the hand owes (hand minus the cards this line takes out of it), colours scarcest first (fewest untapped
producers), each from its narrowest producer (fewest colours; tie lower card number). The prepay's hold
ladder tries each existing rung with the reserve on top, releasing the reserve one pick at a time
before the rung itself gives way, and tries the reserve alone before the unrestricted solve. Lossless:
a hold only removes options from a solve that is retried without it.

Scope: `hand_demand` is passed only by `ApplyPlanDirect` (whole-turn prepay and the hoisted-minter
prefix prepay), as `HumanPlayActive()`. The executor (`AIEngine::TakeTurn`'s autonomous apply), every
rollout (`HumanPlaySuppress`) and every payability probe pass false, so autonomous play is
byte-identical. Pinned by `test/unit/test_human_prepay_demand.cpp` and
`test/scenarios/colossus_human_prepay_keeps_{snarl,mountain}.json`.

## Verification (2026-10-17)

* Both claude-play repros: the follow-up decision now offers Speaker / Sigarda's Aid (seed 2100016)
  and Magnetic Theft (seed 2100017); both scenarios FAIL under `MTG_HUMAN_PREPAY_DEMAND=0`.
* Smoke: 124/124 byte-identical to the accepted ground truth (autonomous play unchanged).
* Reference replay (`viewer_protocol_check.py --strict`, 600 refs): 196 ok / 404 repaired / 0 drift
  of any kind, the same totals as the lever-off arm. One reference changed only its repaired index:
  EldraziDisplacerFlicker `claude_s6_gi5` T4 (1152 -> 1144, same line, same T4 win). Cause: with the
  lever off, the unrestricted joint solve for `{Living Wish, Training Grounds}` taps Brushland first,
  whose Fertile Ground mana is `wild`, so the prepay declines (`PP_WILD`) to the order-dependent
  per-cast payer and the menu keeps both orderings. With the reserve, the solve holds Brushland and
  pays the line wild-free from Kitchen + Overgrowth, so both orderings reach the same end state and the
  menu's end-state dedup folds 8 of them. The real replay is unchanged because that reference's
  recorded `--tap-pref` governs its own payment.

## Open question: does the same blindness cost autonomous play?

The autonomous engine runs the same demand-blind joint solve. Its plan is the whole phase's cast set
and the rollout runs the same prepay, so the search prices what a later main can afford — but payment
is not searched (the standing exemption), so a line "main 1 {A, B}, then main 2 / a breakpoint
continuation {C}" can be undervalued when a different tap set would have afforded C. In a goldfish the
search can usually fold C into main 1 instead, which is why this is plausible but unproven. Not
changed. The way to find out: pass `hand_demand` from the autonomous apply worlds behind a default-OFF
arm and A/B it on held-out seeds (both worlds must move together — the executor's `AIEngine::TakeTurn`
prepay and `ApplyPlanDirect` — or lockstep breaks). That is a play change and needs the usual bar and
the user's call.
