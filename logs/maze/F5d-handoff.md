# F5d handoff (F5-4 -> next agent)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/maze-f5`, branch `wip/maze-f5`. Scratch:
`/workspaces/MagicDeckTester/build/scratch/maze/f5/` (tools/: one.sh, anab.py, mkab.py, mkabp.py, mkd8.py, cold.sh,
d8.sh, gm.sh). Read `logs/maze/F5c-handoff.md` first for the history (heap leak fix 867111d63, joint pay 321c48193,
ab3/ab4/ab5 numbers, the pooled-A/B arm-order wall bias).

## Commits
- `wip/maze-f5`: ... 867111d63, 321c48193, 7e1bdef18 (lever WIP + F5c handoff), **7641435f7** = strip
  `MTG_OWED_JOINT_REFINE` (no USER adoption; default ON was shipped behaviour, so play is unchanged). Lever patch
  kept at `logs/maze/owed-joint-refine-lever.patch` (force-added). This handoff file is committed on top.
- `wip/maze-f5p` (worktree `.claude/worktrees/maze-f5p`, branched at 7641435f7, pushed): **627fccdd1** WIP
  provably-unpayable plan prune -- **UNCOMPILED, never built, no A/B yet**. Contents:
  - `TurnSolver::DropProvablyUnpayablePlans` / `PlanProvablyUnpayableForTest` (TurnSolver.h decl next to
    OwedActsJointPrepay; body right after OwedActsJointPrepay in TurnSolver.cpp, anonymous-namespace helpers
    UnpayCollect / UnpayBoardPays / UnpayBuildBase / PlanUnpayableImpl). Optimistic board: the plan's land drop
    EnterLand(PreDecidedUntapped) (Baldur's Gate counts it), activation {T} sources set aside, other-Gate taps NOT
    set aside (over-credit); every other line-made land (PlayLand, Talon Channel put, TapTutorPut / land search,
    tutor_land_to_battlefield) credited as wild = 2 + our lands + our scaled lands, Treasure = 1, rock_mana exact;
    each step's credit capped at bill MV - own MV (own output never pays own cost). Continuation-opening steps (tutor /
    draw params, Gate entering under a draw_on_other_subtype_enter watcher) => plan dropped only if EVERY such step
    alone is unpayable; non-bp plans judged on the whole bill; a land drop that could draw => kept. Payer =
    TapForCostDirect on PayScratch with LineUnpaidCostScope(ManaCost{}) + PlanSourceReserveScope({}). Any unpriced
    kind (X, convoke, ritual_float, SacForMana, BankManaTap, unknown mana-paying kind) keeps the plan.
  - heurarm slot `MAZE_UNPAYABLE_PRUNE` / `MTG_MAZE_UNPAYABLE_PRUNE` (default ON) after OWED_ACTS_JOINT_PAY, name
    table updated.
  - DecisionProviders.cpp: lever via MAZE_P5_LEVER; deck property `MazeNoNetManaPositiveCards` (no ManaDork,
    mana_rock, ritual_floating_mana, sac_outlet_add_mana_*, reduces_* / hinata reducer among our nonland cards in
    hand/library/graveyard/battlefield non-token; memoised per thread on card numbers); `PlanListFilterLive` =
    prune-live || new `MazeHeapFilterLive` (the old Heap body; declared in DecisionProviders.h); `PlanListAdmit`
    runs the prune FIRST, then the Heap rule only if heap-live, skipping already-dropped plans.
  - Tests appended to `test/unit/test_mazes_end_f5.cpp` (4 TEST_CASEs "Maze's End F5 prune: ..."): lone Scrying /
    Exploration with no green dropped, lever off keeps all; Dark Ritual in library => not live; Gond Gate fetch line
    {Maze act, Exploration} KEPT, Maze act alone short by one Gate DROPPED; Baldur's Gate counts the Dimir drop (kept
    with drop, dropped without); Amulet + tapped-entering Azorius drop kept.
  - Expect compile fixes (names guessed: CardHasSubtype, rock_mana.Total(), RevealLogPause in TurnSolver.cpp,
    CardTemplate::ManaDork / d->tmpl, Player has no exile zone -- removed).

## RUNNING jobs (all queued on build/heavy.lock; waiters armed by F5-4 die with it -- re-arm on PID / marker)
1. `tools/cold.sh` PID 1234667 -> `build/scratch/maze/f5/cold/{base,fix,nr,base2}.{out,err}`, marker `cold/DONE`
   (four separate batches, binary mtg.f5g; compare ms/game + cpu/game per shape; base vs base2 = drift).
2. `tools/d8.sh` PID 1234836 -> `d8/{new,old}.{out,err}`, marker `d8/DONE` (d8 b0 abandon 50M of the 8 slower games;
   recovery rule in F5c handoff).
3. Gate verify: `tools/gm.sh mtg.f5g gm/r2` (bash child of F5-4's session) -> `gm/r2/run.{out,err}`, marker
   `gm/r2/DONE`. Pass = `grep -c GATE-MISS gm/r2/run.err` == 0 (r1, pre-321c48193, had 314).
4. build9: Release build of maze-f5 at 7641435f7 -> `build9.log`, binary copied to `mtg.f5h`, marker `build9.DONE`.
   If any process vanished without its marker, relaunch the same command.
5. Windows CI for the 7e1bdef18 push: run 44465784138 (`gh run watch 44465784138 --interval 60 --exit-status`,
   log `build/scratch/maze/f5/ci_7e1bdef18.txt`). The 7641435f7 push starts a new run: `gh run list --branch
   wip/maze-f5 --limit 3`.

## Prune A/B manifest (ready)
`build/scratch/maze/f5/abp/manifest.json` (2060 jobs, arms innermost prune vs base=`MTG_MAZE_UNPAYABLE_PRUNE`
false; decks point at maze-f5p): d5 b20 seed 201001 x200, d3 b10 202001 x150, hg (2 heads, life 30) d5 b20 204001
x80, d0 203001 x600. Generator `tools/mkabp.py`; analyse with `tools/anab.py run.out base`. Wall from a pooled
A/B is arm-order biased (F5c) -- use separate per-arm batches for wall if it matters.

## Remaining, in order
1. Read cold + d8 + gm/r2. Joint: deck aggregate net -21 (d5 +2, d3 -1, hg 0, d0 -22) passes the per-deck bar iff
   every slower game recovers at d8 b0; heap fix aggregate 0 passes iff d3 g47/g110 recover. Root-cause any
   non-recovery ("draws diverge" is not an explanation).
2. Smoke (`flock /workspaces/MagicDeckTester/build/heavy.lock env MTG_PGO=0 bash test/regression.sh --smoke` in
   maze-f5, after build9), diff per deck; then regression; accept `--deck=` scoped to moved keys
   (mazesend, mazesend2hg expected); `python3 test/check_gt_logs.py`; verify_deck mismatch gate for Maze's End.
3. Update the ledger F5 section in `docs/design/analysis-Maze's End.md` (ends ~line 4273): heap-leak root cause,
   joint numbers, order-bias finding, cold wall, d8, both USER rulings, refine lever stripped (patch path). Push
   `wip/maze-f5` only.
4. Prune: in maze-f5p build (`flock ... env MTG_BUILD_JOBS=12 ./build.sh`), fix compile, run units
   (Maze's End tests incl. P5 Heap tests -- the prune may now drop plans in older test fixtures), run abp A/B, d8 b0
   of every slower game pooled into one batch, rebase onto maze-f5's final head, separate commit. Optional pricing
   first with `MTG_AFFORD_AUDIT=1`. Interaction to report: P7's cast-dominance fold -- if it removes a subset {A}
   because {A,B} dominates and the prune then drops an unpayable {A,B}, {A} is lost; the prune must run before (or
   the fold must ignore) dominance by an unpayable superset.
5. Final report: commits, per-deck A/B + d8, cold wall, GT, CI, prune results, USER decisions.

## USER rulings (VERBATIM)
- Unpayable plans: "Unpayable plans I think we could try. I assume all of the plans die at some point anyway, so the
  only question is whether we can stop them early when they are causing issues. For simple decks with no rituals or
  equivalent (cards that add more mana then they cost) like this one it makes sense to drop plans that cannot pay a
  subset." (Scope by deck property; F6: judge IN SEQUENCE -- lands the line itself puts onto the battlefield untapped
  and Baldur's Gate's extra count are supply.)
- Guild Summit B1: "For Guild Summit I would prioritize the cards over the treasure here." (no reservation; nothing to
  implement)
- Heapstrict: "Honestly the rule is even fine outside of goldfish, so let's turn it on. We don't have interaction of
  our own to cast in response even if the opponent does something."
- MTG_OWED_JOINT_REFINE: no USER ruling yet; stripped (MTG_PAY_REFINE is a USER adoption; skipping it in the joint is
  the USER's call). Open USER questions: that lever; the pooled-A/B arm-order wall bias affects every A/B in the repo.

## Box rules (USER 2026-11-18)
ONE heavy process at a time; flock /workspaces/MagicDeckTester/build/heavy.lock; MTG_BUILD_JOBS=12; --threads $(nproc);
MTG_RSS_CAP_GB=20; default cache budget; waiters on PID/DONE, never pgrep -f; no timeouts; push wip/* only; rebase never
merge; don't touch references/ or the viewer; handoff at ~250K.
