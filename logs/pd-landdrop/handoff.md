# pd-landdrop status (paused waiting on the box lane, 2026-11-21 00:20 UTC)

Branch `wip/pd-hold-landdrop` (worktree agent-aa57dd6400917806d), based on origin/phase-1-2-deck-analyzer 1f3c23fab.
WIP commit faf8d8d8a (pushed): TEMP diagnostic `MTG_PD_HOLD_TRACE_TURN`, TEMP selector `MTG_PD_HOLD_FIX` (0 = current,
1 = no later-hand hold inside a known line (g_line_unpaid_cost holds this bill) that opens nothing unseen,
2 = 1 + that line's own remainder is the demand when the plan traits are stale), and the new unit test
"later-hand Earthquake hold pays no pain for a line that does not cast it (s6006 gi47 T7)".

## Mechanism (from the overnight-accept traces, to be confirmed by the chain)
gi47 = seed 6053, game_index 47, d5 b20 (profile), PD. Old line wins T8 at 1 life: T4 Forge, T5 Forge, T6 Tomb+Vito,
T7 Vito lifelink {3}{B}{B} paid City+Citadel+3 painless {C} (14 -> 10), T8 Tomb + Earthquake X=2 + lifelink.
With MTG_PD_LATER_HAND_HOLD on, the T7 lifelink payment (Earthquake drawn T7, in hand) is re-paid by the harmful-mode
line repair (src/core/DamageEvents.h ~1430-1476, try_hold) to keep a Forge for the Earthquake's {R}
(ManaPayment.cpp line_ok lambda, hand_holds from PreventDamageProvider::PaymentHoldUp): Ancient Tomb for generic,
6 damage instead of 4. The line casts no Earthquake that turn; the 2 life makes the T8 kill lethal, so every
land-drop line loses its win (also at d8 b0) and the search stops playing lands on T4/T5.

## Queued (box lane, behind another agent's chain3 regression tier)
`bash scripts/box_lane.sh --slots 4 -- bash logs/pd-landdrop/chain_diag.sh` (lane waiter pid 4022212; detached guard
`logs/pd-landdrop/guard.sh` re-queues it if that waiter dies first). Output under
/workspaces/MagicDeckTester/build/scratch/claude_pd_landdrop/: chain_diag.status (one line per step),
chain_diag.DONE. Steps: build; unit test per variant (ut_t7_f{0,1,2}.log); T7 hold trace (diag_t7.err `[pd-hold]`);
T4 solve trace on/off (diag_t4*.err); gi47 case per variant (diag/case_f{1,2}/*.wins); mana-clean repro seeds
30050876/30052809/30054696/30064758/30086899 at MTG_MANA_ORACLE=2 per variant (mc_f*.err, APPLY-DROP count in
status); gi47 d8 b0 per variant (diag/d8_f*/).

## Remaining, in order
1. Read the chain: confirm the T7 6-vs-4 payment and the `[pd-hold]` context (lu bound? opens_unseen?).
2. Pick the variant that restores gi47 (case + d8 b0) and keeps the mana-clean seeds at 0 APPLY-DROP; make it the
   real code (no TEMP selector, no diag print), with a comment; finalise the unit test.
3. Held-out pooled A/B fix vs current on PD (+ PD 2HG), one --batch under the box lock; every slower game at d8 b0.
4. `THREADS=4 bash test/regression.sh --both` via the lane; audit; accept smoke+regression GT in this worktree if
   net <= 0 per deck; `python3 test/check_gt_logs.py`. Never push the shared branch.
