# Merfolk gate -- handoff #3 (written by agent 2), 2026-11-21 22:51 UTC

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/agent-a1651726d5b941608`, branch `wip/merfolk-gate` (pushed).
Doc: `docs/design/merfolk-gate-2026-11-20.md` (4.6 = tier audit, 5 = ENUM-GAP status). Scratch:
`build/scratch/claude_merfolk_gate/` (= $S below). The parent's note: commits up to b8f076da0 are in integration batch
wip/batch-1122a -- NEVER rewrite/rebase them; add commits on top.

## Done
- ddc28771c GT accept (smoke + regression) for the levers, binary t2/mtg.final = b8f076da0's tree. Only mazesend /
  merfolkturns rows moved; searched slower 0 both tiers; d0 smoke mazesend gi592 T5->T7 recovers at d8 b0 (T4 both arms;
  $S/d8b0_t2/); every deck net <= 0; units 3,805/3,805; check_gt_logs 1,358 consistent. (The batch-1122a integrator
  re-accepts GT under its combined binary.)
- fdbe68d1d doc 4.6 + "refine cut (B) is a narrowing, not an identity".
- 1d34d618c WIP ENUM-GAP code, all three flags default OFF (temporary selectors, heurarm slots SCALED_GROWTH_BOUND,
  SCALED_GROWER_DECIDER, MERFOLK_GROWER_ORDER):
  * MTG_SCALED_GROWTH_BOUND: +1 per City-matching creature cand per untapped scaled land in EnumeratePlans' scalar bound
    (`land_growth_bound`, beside `growth_bound`), AND the same credit in `SpwfProvablySupplyShort` (TurnSolver.cpp ~10713)
    -- that supply pre-gate was the second cut (trace: `[dbgmulti] reject=1 mana_ok=0 why=ef-no-interact`; the filter
    rescue SubsetPayableWithFilters never ran because the supply bound read City at its current count).
  * `ScaledLandGrowerOrder` (ManaPayment.cpp, before ApplyDeckCastArrangement): when the order holds a creature that grows
    an untapped City, ALL creature casts go cheapest-first among CastOrderLess ties (Seahunter is Human Mercenary, NOT a
    Merfolk -- a grower-only reorder does nothing on the repro).
  * Option (a) MTG_SCALED_GROWER_DECIDER: PayableCastOrderCore's last fallback (after UnlockPayableOrder), site 0 only:
    take the grower order when the given order pays in neither OrderPaysSequentially nor OrderPrepaysWholeTurn and
    OrderPaysSequentially pays the reorder.
  * Option (b) MTG_MERFOLK_GROWER_ORDER: MerfolkTurnsProvider::ArrangeCastOrder applies ScaledLandGrowerOrder always.
  * Unit test "Three Tree City: Dockhand before Seahunter is on the menu and applies (s30000762 T4)" (test_merfolk_turns.cpp
    end): passes for (1,1,0) and (1,0,1); bound alone puts it on the menu but the apply drops (MESSAGE applied=false).
    Full unit suite NOT yet run on this tree.

## Measurements so far (binary $S/eg/mtg.v2 = 1d34d618c's tree)
Gate slice (`$S/slice_eg/`, oracle level 2, Merfolk Turns gi 0-399 on gatetree deck files, 3 arms), DISTINCT game-turns:
| class | off | a | b |
|---|---|---|---|
| ENUM-GAP | 27 | 17 | 17 |
| APPLY-DROP | 1 | 3 (new: s30000034 T5, s30000075 T5) | 6 (5 new) |
| ORACLE-GAP | 9 | 8 | 20 (11 new) |
| APPLY-DROP-ORDER | 1 | 0 | 0 |
(Raw line counts: ENUM-GAP 118/68/68, APPLY-DROP 2/158/308, ORACLE-GAP 44/36/464.) Slice CPU: a ~= off (g0 job cpu 103074 vs
103672). So (a) is ahead of (b) on gate cleanliness, but (a) adds 2 APPLY-DROPs on newly admitted lines:
s30000034 T5 {Island, Prophets, Dockhand} dropped Prophets; s30000075 T5 {Island, Pilgrimage, Seahunter, Douser, Greaves}
dropped Seahunter. Oracle: "ROLLBACK depth1: keep the engine's first mana decision, re-make the rest (decision #3 failed)
and every action pays" -> the engine's SECOND payment is the bad one (suspect: a cheap Merfolk's pip/generic, or Mihail's
may-pay {1} trigger, taken off City or an Island the City feed needed). Not yet traced.

## RUNNING (both DETACHED, PPID 1 -- they survive; nothing watches them now)
1. APPLY-DROP repro, lane job PID 479297 (`box_lane.sh --slots 2 -- bash logs/merfolk-gate/eg_rep1.sh`): arm a, seeds
   30000034 and 30000075, oracle level 2 on T5, MTG_EXEC_TAP_TRACE=1, game logs. Output `$S/eg/rep1/{seed}_1.{out,err}`,
   `$S/eg/rep1/log_{seed}_1/`; marker `$S/eg/rep1/DONE`. Lane queue was ~3 h deep at 22:49.
2. Held-out A/B, `logs/merfolk-gate/eg_ab.sh` PID 479894 (its flock child 479903 waits on claude_box.lock, held at 22:49 by
   bdo3/gt.sh). Arms off/a/b, Merfolk Turns 2000 + Goblins 2000, seeds 34000000+off, game_index off, full width,
   MTG_RSS_CAP_GB=40. Output `$S/ab_eg/{ab.out,ab.err,logs/}`, marker `$S/ab_eg/DONE` (rc=). Compare:
   `python3 logs/merfolk-gate/eg_compare.py $S/ab_eg/logs $S/ab_eg/ab.out a` (and `b`); the mtg PID appears once the lock
   is granted (`ps -ef | grep ab_eg/manifest`). If the A/B results are needed after a code change, re-run it on the new
   binary (eg_ab.sh copies nothing; it runs $S/eg/mtg.v2).

## Remaining, in order
1. Read rep1: find the engine's bad second payment in s30000034 / s30000075 T5 under (a) and fix it (likely in the payer /
   MTG_SCALED_LAND_ROUTE's "rest of the line" check not crediting City growth, or Mihail's trigger payment). Re-run the
   unit test + the gate slice (logs/merfolk-gate/eg_chain1.sh rebuilds, unit-tests and slices in one 4-slot lane job;
   wait ~200 s after edits and delete the edited .o first).
2. Read the A/B (per deck net <= 0 for the chosen option; every slower game at --depth 8 --budget-ms 0; CPU = cpu/units
   ratios from eg_compare.py, plus perf-stat instructions if close: $S/cpu/{mk2.py,job.sh}).
3. Ship the CHEAPEST option that corrects the issue and meets the bar (current evidence favours (a): same ENUM-GAP cut,
   fewer new issues, ~0 CPU, and it changes only orders that do not pay; (b) re-orders every City board). Delete the other
   option's code + heurarm slot, flip the winner and the bound ON (keep `=0` restoring), full unit suite, smoke +
   regression tiers (`MTG_BIN=<pinned copy> MTG_PGO=0 bash test/regression.sh --both`, under the box lock or per the
   parent), audit, `--both --accept`, `python3 test/check_gt_logs.py`, commit, push wip/merfolk-gate. Update doc section 5.
4. Never push phase-1-2-deck-analyzer, never join the push queue (the parent lands it).

## USER rulings (VERBATIM)
- 2026-11-21, on the Merfolk Turns ENUM-GAP cast order: "I'm fine with trying either option as needed. Whatever does
  better can win out. We are looking for the cheapest that corrects the issue."
- Parent's instruction with it: build BOTH options behind a temporary selector, measure each on a gate slice (ENUM-GAP
  count) and a held-out pooled A/B (Merfolk Turns, plus any deck the generic option moves), every slower game at d8 b0,
  CPU per option. Ship the CHEAPEST option that corrects the issue and meets the bar (every deck net <= 0, slower games
  recover); delete the other. "Big runs go under claude_box.lock, never a full tier in the lane."
- Parent, on lane use (2026-11-21): "Do not hold slots across an agent think step again: queue each later phase as its
  own box_lane.sh job, and use fewer slots (1-2) where the measurement allows it."
- From the first brief: "A default-ON play change needs: unit test(s), a held-out A/B on the decks that move (scoped and
  sized to the effect; every slower game re-run at --depth 8 --budget-ms 0), and accepted smoke + regression GT in YOUR
  worktree ... Net <= 0 per deck (+1 is not neutral)." -- "Do NOT push the shared branch (phase-1-2-deck-analyzer); the
  parent session lands it. You may push your own wip/* branch." -- "Never decide a USER-owned policy question yourself."
  -- USER rule: "3x is not a CPU licence: cut any fix CPU cost".
- Parent 2026-11-21 ~10:30: when the lock holder is in a long tail and MemAvailable >= 22 GB, the tiers may run without
  flock at MTG_RSS_CAP_GB=16 (this is how ddc28771c's run was made).

## Notes
- Option (b) is a deck cast-order rule; if (b) were ever preferred, it is the USER's cast-order policy (state it to them).
- Lane latency today was 2-4 h per job; batch steps into one lane script (eg_chain1.sh pattern).
