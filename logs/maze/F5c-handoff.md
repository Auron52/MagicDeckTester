# F5c handoff (F5-3 -> next agent)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/maze-f5`, branch `wip/maze-f5`. Scratch:
`/workspaces/MagicDeckTester/build/scratch/maze/f5/` (tools/: one.sh, anab.py, mkd8.py, ab3.sh, cold.sh, d8.sh, gm.sh).
Commits on top of e682d7942: d03420baa, 2285091fd, 2857568d9 (F5b), then 867111d63 (heap leak fix), 321c48193
(joint cost/soundness), and a WIP commit adding the MTG_OWED_JOINT_REFINE experiment lever (see below).

## USER rulings received by F5-3 (VERBATIM, via the coordinator, 2026-11-18)
1. Guild Summit rule B1 (should the rest-of-line count reserve a planned Heap Gate Treasure?): "For Guild Summit I
   would prioritize the cards over the treasure here." => NO reservation; Summit's taps go to cards ahead of a Heap
   Gate Treasure. (Current behaviour already does not reserve; SELFTAP stays the plan's own choice. Nothing to
   implement.)
2. Planner over-affording casts (the 277 prune candidate): "Unpayable plans I think we could try. I assume all of the
   plans die at some point anyway, so the only question is whether we can stop them early when they are causing
   issues. For simple decks with no rituals or equivalent (cards that add more mana then they cost) like this one it
   makes sense to drop plans that cannot pay a subset." => Build it as a provider-side/plan-list prune scoped by a
   deck property (no net-mana-positive cards: no rituals or equivalents), dropping a plan as soon as a subset of it is
   unpayable. AFTER the heapstrict leak fix and the joint-pay landing, as a SEPARATE commit, with its own held-out
   A/B and d8 b0 recovery check.
Earlier ruling (heapstrict): "Honestly the rule is even fine outside of goldfish, so let's turn it on. We don't have
interaction of our own to cast in response even if the opponent does something."

## Progress (F5-3)
- chain3 (F5b) read: f5tests/units green; audit a2 GATE-MISS=386 (count gate unsound); ab2 joint A/B: d5 +2 (1/3),
  d3 -2, hg 0, d0 -22; wall +17% d5 / +36% d3 / +11% hg, units flat.
- `867111d63` HEAPSTRICT LEAK FIXED. Root cause, three parts: (1) MazesEndProvider::PlanListFilterLive read only the
  battlefield, so a Treasure plan that PLAYS Heap Gate from hand was never filtered (the gi14 repro); (2)
  EnumerateM2PlansBody never applied the filter; (3) the "never empties" keep returned all-Treasure lists whole.
  Fix: ApplyProviderPlanListFilter in both hosts, Live reads hand+library, the filter may empty a list. Tests in
  test_mazes_end_p5.cpp (+ seam TurnSolver::EnumerateM2HostPlansForTest); I4 test pins strict off. gi14 repro: no
  heap-leak lines, still wins T5.
- `321c48193` joint pay: duplicate trial solve removed (gi40 d3 16.1 s -> 7.2 s, base 1.4 s); count gate skips
  other-tap lines (all 314 misses were Heap Gate Treasure lines).
- ab3 (binary mtg.f5f, scratch build/scratch/maze/f5/ab3): joint fix vs base: d5 +2 (1/3), d3 -1 (1/0), hg 0,
  d0 -22 (22/3) => deck aggregate -21. Wall d5 +12%, d3 +26%, hg +3%; median per-game +3.5-4%, rest is a tail of
  games where the joint whole-line bill's PAY-REFINE costs ~10 ms each (gi40: 822 joint fires).
  Heap fix (ab3 base vs ab2 base, i.e. new vs old binary, joint off): d5 -1, d3 +1 (1/2: d3 g47 6->7, g110 4->5),
  hg 0, d0 0 => aggregate 0; units slightly lower.
- WIP commit: heurarm slot OWED_JOINT_REFINE / env MTG_OWED_JOINT_REFINE (default ON; =0 the joint core solves
  without the pay-refine). A COST EXPERIMENT only; MTG_PAY_REFINE is a USER adoption, so turning it off in the joint
  is the USER's call. Binary with it: build/scratch/maze/f5/mtg.f5g. If not adopted, DELETE the slot (no lever
  left behind).
- ab4 (3 arms, mtg.f5g): fix d5 +2 (1/3), d3 -1, hg 0, d0 -22; nr d5 +2, d3 -1, hg 0, d0 -18 (20/4).
- **WALL MEASUREMENT IS ORDER-BIASED IN A POOLED A/B**: the SECOND job of a same-seed pair runs much faster (some
  process-wide cache, not identified; same_seed_inflight is only a memory limiter). ab5 = d3 with base FIRST:
  base 2,303 ms/game, fix 2,091 (fix -9%), versus ab4 fix-first: fix 3,210, base 2,374. Single cold runs:
  d3 s192014 gi13 fix 42.0 s vs base 37.0 s (+13%, refine 4.97 s vs 0.36 s); d3 s192041 gi40 fix 7.2 s vs 1.4 s.
  So the earlier "+35-43%" / "+12-26%" were inflated by arm order. Cold per-arm batches are RUNNING (below).
- Joint cost source (perf, dwarf): the remaining overhead is the PAY-REFINE of the whole-line joint bill inside
  LineJointPrepayCore (manaoracle::RefinePayment -> PayEnum::Dfs/Verify), ~10 ms per refine on big Gate boards;
  the probe and gates themselves are small.

## RUNNING jobs (both queue on build/heavy.lock; started by F5-3)
1. `tools/cold.sh` PID 1234667: four SEPARATE pooled batches (base, fix, nr, base2) of the ab4 d5/d3/hg jobs, no d0,
   binary mtg.f5g. Output `build/scratch/maze/f5/cold/{base,fix,nr,base2}.{out,err}`, DONE marker
   `cold/DONE`. Compare ms/game and cpu/game per shape per arm (base vs base2 = box drift). This is the clean wall.
2. `tools/d8.sh` PID 1234836: d8 b0 (abandon 50M) recovery. `d8/new.json` on mtg.f5g = the joint's 6 slower
   games (d5 g164 g173 g193, d0 g297 g306 g473; fix and base arms) + heap-fix slower games d3 g47, g110 (joint off);
   then `d8/old.json` on mtg.f5d (pre-heap-fix binary, joint off) for d3 g47/g110. Outputs d8/{new,old}.{out,err},
   DONE marker `d8/DONE`. A slower game recovers if its new arm at d8 b0 wins no later than the reference arm at d8
   b0 (heap: new vs old; joint: fix vs base). "Draws diverge" is not an explanation.

## Remaining, in order
1. Read cold/ and d8/. Joint: deck aggregate on ab3/ab4 is net -21 (d5 +2, d3 -1, hg 0, d0 -22) -> passes the
   per-DECK bar if every slower game recovers. Heap fix: aggregate 0 (d5 -1, d3 +1, hg 0, d0 0) -> passes if d3 g47
   and g110 recover. Root-cause any non-recovery.
2. Decide the refine experiment: report fix vs nr numbers to the USER (do not settle); remove the lever if not taken.
3. Optional verify: rerun `tools/gm.sh` (MTG_OWED_ACTS_JOINT_GATE_VERIFY) on the new binary; GATE-MISS must be 0.
4. Smoke all decks (`flock heavy.lock env MTG_PGO=0 bash test/regression.sh --smoke` in the worktree), diff per deck;
   then regression; accept with `--deck=` for moved keys only; `python3 test/check_gt_logs.py`. verify_deck mismatch
   gate for Maze's End.
5. Update the ledger F5 section in `docs/design/analysis-Maze's End.md` (heap leak root cause, joint numbers, the
   order-bias finding, both USER rulings). Push `wip/maze-f5` only (`--force-with-lease`); never the shared branch.
6. THEN the USER's unpayable-plan prune (ruling 2 above), a SEPARATE commit with its own held-out A/B + d8 b0.
   F6's note (coordinator): "unpayable" must be judged on the line IN SEQUENCE, counting lands the line itself puts
   onto the battlefield untapped as supply. Scope by a deck property: no net-mana-positive cards (no rituals or
   equivalents). Drop a plan as soon as a subset of it is unpayable. Provider-side / plan-list prune (the
   ApplyProviderPlanListFilter route now reaches every enumeration). Price it with MTG_AFFORD_AUDIT=1 first.

## Open USER questions
- MTG_OWED_JOINT_REFINE: skip the pay-refine inside the owed-activations joint solve? (USER adoption at stake.)
- The order bias in pooled arms-innermost wall comparisons affects every A/B in the repo, not just F5.

## Box rules (USER 2026-11-18)
ONE heavy process at a time; flock /workspaces/MagicDeckTester/build/heavy.lock around builds, batches, tiers;
MTG_BUILD_JOBS=12; --threads $(nproc); MTG_RSS_CAP_GB=20; default cache budget; waiters on PID/DONE; no timeouts;
subagents push wip/* only; handoff at ~250K.
