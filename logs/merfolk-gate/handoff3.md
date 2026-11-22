# Merfolk gate -- handoff #4 (written by agent 3), 2026-11-22 05:53 UTC

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/agent-a1651726d5b941608`, branch `wip/merfolk-gate`.
Doc: `docs/design/merfolk-gate-2026-11-20.md` (section 5 = ENUM-GAP status; NOT yet updated with the results below).
Scratch `$S` = `build/scratch/claude_merfolk_gate/` (worktree-relative; it is on the build volume).
Commits up to b8f076da0 landed (batch 1122a = f9a59b8a7 on phase-1-2-deck-analyzer). Never rewrite them; add commits on top.
This branch is NOT rebased onto f9a59b8a7 (the parent's integrator re-accepts GT under its combined binary).

## Done (commits on wip/merfolk-gate)
- 3dedc59dc: fix for option (a)'s two new APPLY-DROPs (s30000034 T5 Wanderwine Prophets, s30000075 T5 Seahunter).
  ROOT CAUSE: Emperor Mihail II's "may pay {1}" is paid in FireOnCastTriggers while the cast Merfolk is still on the stack
  and before its token exists. ScaledLandRouteHold (MTG_SCALED_LAND_ROUTE) priced Three Tree City at the board's count, so
  it saw no hold-out that kept the rest payable, and the greedy paid {1} with City's plain {C}, stranding the line.
  FIX (under MTG_SCALED_GROWTH_BOUND): SpellEffects.h `PendingScaledEntrants` (set at the Mihail payment: the cast creature
  + the tokens) and, at a creature spell's own payment, that creature; `ScaledManaCreatureCount` adds them ONLY while
  `ScaledEntrantCreditArmed()` is set, which the route sets around its rest-of-line solves (this payment itself is made at
  the real count). Unit test "Mihail's {1} keeps Three Tree City for Wanderwine Prophets (s30000034 T5, ...)" -- passes
  on and off (off reproduces the defect). Repro (v3): APPLY-DROP 144 -> 0 (s34) and 12 -> 0 (s75).
- 3218cc593: per-selection re-pricing of the scalar land-growth bound (`land_growth_refuses`, EnumeratePlansCore). Measured
  byte-identical (all 8,000 games of the v3 vs v4 A/B) and units 1.0000 / 0.9999: it saves nothing measurable. Kept (exact,
  harmless); a successor may delete it as dead complexity -- that is a src change and needs its own build + tiers.
- 16e134265: SHIP TREE for option (a). Deleted option (b) (MerfolkTurnsProvider::ArrangeCastOrder override,
  MerfolkGrowerOrderOn, heurarm slot MERFOLK_GROWER_ORDER + its name). MTG_SCALED_GROWTH_BOUND and MTG_SCALED_GROWER_DECIDER
  now DEFAULT ON (=0 restores). Slot/flag comments rewritten. Unit test GrowerArms is now (bound, decider); arm (-1,-1)
  checks the defaults. NOT yet built/tested -- that is chain4 (running).

## Measurements
Binaries: $S/eg/mtg.v2 (1d34d618c tree), mtg.v3 (3dedc59dc), mtg.v4 (3218cc593); v5 = ship tree (chain4 makes it).

Gate slice (Merfolk Turns gi 0-399, gate deck files in $S/gatetree, MTG_MANA_ORACLE=2), distinct game-turns:
| class | off (v2) | a (v2) | b (v2) | a (v3) | b (v3) |
|---|---|---|---|---|---|
| ENUM-GAP | 27 | 17 | 17 | **15** | 16 |
| APPLY-DROP | 1 | 3 | 6 | **1** (s30000006 T4, same as off) | 4 |
| ORACLE-GAP | 9 | 8 | 20 | 8 | 20 |
VERDICT: option (a) wins (generic; no new issue; the remaining APPLY-DROP is present with the lever off). Outputs
$S/slice_eg/ (v2, all arms) and $S/slice_eg3/ (v3, a and b).

Held-out A/Bs (eg_compare.py; net = loss-penalized turns, negative = better):
- $S/ab_eg (v2, seeds 34000000+, searched only, 2000/deck): a: Merfolk 0, Goblins -1 (1 faster); b: same nets. No slower.
- $S/ab_eg3 (v3, FRESH seeds 35000000+, 2000/deck, off vs a):
  searched: Merfolk net 0 (0 slower), Goblins net 0 (0 slower); units ratio 1.0071 / 1.0089, cpu 1.038 / 1.019 (shared box).
  d0: Merfolk net -5 (5 faster, 0 slower), Goblins 0; cpu 1.0105 / 0.9919.
  No slower game anywhere -> no d8 b0 recoveries owed.
- $S/ab_eg3b (v3, bound-only arm B): units B/off 1.0072 (Merfolk) / 1.0097 (Goblins); a/B 1.0000 / 0.9991, so the
  decider costs ~0 and the whole +0.7-1.0% units is the bound (the selections it admits + the supply-credit rescue).
- $S/ab_eg4 (v4): identical games to ab_eg3 on every arm; units unchanged.
CPU note for the report: the +0.7-0.9% units is the cost of the correction itself (the per-selection cut removed nothing).

## RUNNING
- chain4 = lane job, real PID 670315 (`box_lane.sh --slots 2 --cap-gb 3 -- bash logs/merfolk-gate/eg_chain4.sh`), queued
  at ~05:46 UTC. It sleeps 200 s, builds the ship tree (-j2, log $S/eg/build6.log), copies $S/eg/mtg.v5 and
  $S/eg/mtg-test.v5, runs the FULL unit suite to $S/eg/ut5_full.out. Status lines in $S/eg/chain4.status
  ("build rc=", "units rc="); DONE marker $S/eg/CHAIN4.DONE; lane log $S/eg/chain4.lane.log. Verify with `kill -0 670315`;
  if dead without the marker, relaunch detached (box_lane.sh now refuses --slots > 2).
- Nothing else of mine is running.

## Remaining, in order
1. chain4: build rc=0 and units rc=0 (expect 3,806 or so cases, all pass). Fix any failure (a test may pin the old default-OFF).
2. Tiers on v5 under the box lock: `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock env MTG_BIN=$S/eg/mtg.v5
   MTG_PGO=0 MTG_RSS_CAP_GB=40 bash test/regression.sh --both` (or pin via test/snapshot_bin.sh / harness_install_bin per
   .claude/skills/regression-testing.md; never run a binary from 9p). Read the audit: every changed game analysed, searched
   slower games re-run at --depth 8 --budget-ms 0 (only CLEAR clairvoyance, MTG_NC_SEARCH=1, may stay unrecovered), every
   deck net <= 0 (searched AND d0). Then `bash test/regression.sh --both --accept`, `python3 test/check_gt_logs.py`, commit GT.
3. Update doc section 5 (root cause + fix, slice table, A/B tables above, option (a) shipped, (b) deleted) and commit.
4. Push wip/merfolk-gate only. Never push phase-1-2-deck-analyzer and never join the push queue (the parent lands it).
5. Report to the parent: APPLY-DROP root cause/fix, slice table, A/B per deck (searched + d0, slower games none, CPU
   ratios), "option (a) shipped", hashes. Option (b) lost, so no USER cast-order decision is owed.

## USER rulings (VERBATIM)
- 2026-11-21, on the Merfolk Turns ENUM-GAP cast order: "I'm fine with trying either option as needed. Whatever does
  better can win out. We are looking for the cheapest that corrects the issue."
- Parent's instruction with it: build BOTH options behind a temporary selector, measure each on a gate slice (ENUM-GAP
  count) and a held-out pooled A/B (Merfolk Turns, plus any deck the generic option moves), every slower game at d8 b0,
  CPU per option. Ship the CHEAPEST option that corrects the issue and meets the bar (every deck net <= 0, slower games
  recover); delete the other. "Big runs go under claude_box.lock, never a full tier in the lane."
- Parent, on lane use (2026-11-21): "Do not hold slots across an agent think step again: queue each later phase as its
  own box_lane.sh job, and use fewer slots (1-2) where the measurement allows it."
- Parent 2026-11-22: resubmit at --slots 2 (the lane admits only its head; a 3/4-slot head starves). box_lane.sh now
  refuses --slots > 2 (4a7f65983 on the shared branch).
- From the first brief: "A default-ON play change needs: unit test(s), a held-out A/B on the decks that move (scoped and
  sized to the effect; every slower game re-run at --depth 8 --budget-ms 0), and accepted smoke + regression GT in YOUR
  worktree ... Net <= 0 per deck (+1 is not neutral)." -- "Do NOT push the shared branch (phase-1-2-deck-analyzer); the
  parent session lands it. You may push your own wip/* branch." -- "Never decide a USER-owned policy question yourself."
  -- USER rule: "3x is not a CPU licence: cut any fix CPU cost".
- Parent 2026-11-21 ~10:30: when the lock holder is in a long tail and MemAvailable >= 22 GB, the tiers may run without
  flock at MTG_RSS_CAP_GB=16.
- If option (b) had won it would be a deck cast-order policy for the USER; it lost, so nothing is owed.

## Scripts (tracked, logs/merfolk-gate/)
eg_chain2.sh (v3 build + repros + slice), eg_chain3.sh (v4 build), eg_chain4.sh (v5 build + full units),
eg_manifest3.py + eg_ab3.sh `<arm> <binary> [outdir]` (off vs arm, searched + d0, seeds 35000000+, box lock),
eg_compare.py `<logs> <ab.out> <arm> [base]`.
