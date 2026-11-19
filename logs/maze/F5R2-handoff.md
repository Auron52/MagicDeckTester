# F5R-2 handoff (pay-refine speed-up, Maze's End)

Worktree `.claude/worktrees/maze-f5r`, branch `wip/maze-f5r` (pushed). Scratch `build/scratch/maze/f5r/`
(binaries mtg.base = 7a59f73dc, mtg.bound1 = ef45d809d, mtg.layer2 = 0e0b59aa6; tools/ has every script used).

## USER ruling (VERBATIM)
"For 4 it seems like we need to have something like the refine, but make it cheaper? I'm not entirely sure, but we
are looking for better results overall."
(MTG_PAY_REFINE is a USER adoption: a play change of it goes to the USER with its A/B.)

## Commits
- ef45d809d release bound (fed extensions parked, Hopeless prune) + unit test ("never releases fewer than eager").
- 0e0b59aa6 release layers + layer halt (exact vs uncapped eager wherever it finishes) + Face::gains +
  MTG_PAY_REFINE_BOUND_VERIFY_UNCAPPED + trace rej= counters.

## Measurements (430-game wall manifest = d5 b20 191001 x200, d3 b10 192001 x150, 2HG d5 b20 194001 x80)
- Base caps 890 refines; 334 of them find nothing, 330 of those solvable uncapped (diag census capd/).
- bound1: verify 708,515 refines, 227 mismatches, all eager-capped-nothing vs bound-found; 182 still capped
  (121 nothing found, all in the first pass on 8-10 Gate boards; base's uncapped answer there is release 3-4).
- layers+halt vs UNCAPPED eager: 708,100 refines, 0 mismatches outside a capped refine; 64 capped, 60 of them
  missing an uncapped release ({11}{U} x40, {9}{G}{G} x10, {9}{G} 3, {10}{U} 3, {8}{U}{G} 2, {8}{U} 2).
  Their candidates PAY but the choice turns them down in ReachesByTapping (rej= trace, ~550-680 per refine).
- Identity: smoke + regression keys of layer2 == bound1 exactly; bound1 vs base moved 1/231 smoke
  (mazesend_smoke_d5_s1001) and 1/368 regression (mazesend_regression_d0_s2002) digests, same aggregates.
- Held-out A/B (seeds 231001 d5 x600, 232001 d3 x300, 233001 d0 x1000, 234001 2HG x200; separate per-arm batches):
  bound1 and layer2 both 2,099/2,100 digests == base, one d0 game a turn faster (net -1), no slower game -> d8 b0
  set empty. layer2 digests == bound1 on all 2,100.
- Repros (single thread): gi40 refine 5,815 (base) -> 3,132 (bound1) -> 140 ms (layers); wall 7.4 -> 4.8 -> 1.5 s.
  gi13 refine 1,938 -> 736 -> 420 ms; wall 13.4 -> 11.5 -> 10.9 s.
- Wall batches: clean 09:1x run base vs bound1 makespan 100.7 vs 101.4 s. The val/ wall run (base 392 s, bound1
  260, layer2 223) is CONTAMINATED: a p5d agent ran two 32-thread batches at the same time. Clean rerun wall2/
  (load logged, idle box): refine CPU base 127.2 s -> bound1 99.1 s -> layers 69.6 s (-45%); user CPU 1,512 ->
  1,502 -> 1,450 s (-4.1%); makespan 101.3 / 101.4 / 100.5 s (tail-bound); ms/game layers vs base d5 0.959x,
  2HG 0.969x, d3 0.922x.

## The 10 GT failures (both arms, every binary; base = 7a59f73dc on wip/maze-f5)
Not caused by this work: unaccepted GT moves from F5's engine commits after the last accept e682d7942
(d03420baa, 2285091fd, 867111d63, 321c48193 -- owed-activations joint pay; generic code, moves other decks).
Lists with exp vs got are in build/scratch/maze/f5r/{smoke,reg}/base.log.

## Remaining / proposals
1. The 60 capped refines: a sound pre-check for ReachesByTapping (or ranking candidates by it) is the next lever;
   otherwise the BUDGET-CHARGED cap: charge refine probes as decision units and stop on budget exhaustion
   (inert at budget 0, monotone below) instead of the constant 4,096 (ManaOracle.cpp PayEnum::cap), which today
   is read at every budget including unlimited.
2. Adoption of bound1/layers onto the shared branch is a USER decision (play change: 1 game/2,100 held-out,
   better; 2 tier digests). GT for those two keys must be accepted with it.
