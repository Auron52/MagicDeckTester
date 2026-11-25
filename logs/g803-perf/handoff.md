# g803 perf handoff (Maze's End 2HG s38200803 gi803, d5 b20)

Worktree: `/workspaces/MagicDeckTester/.claude/worktrees/g803-perf`, branch `wip/g803-perf` (from
origin/phase-1-2-deck-analyzer d88b4bfe0). Scratch: `/workspaces/MagicDeckTester/build/scratch/g803-perf/`.
Commits: `4a982a5f5` (WIP with TMPDIAG) and the commit after it (TMPDIAG removed, FanOverrunStop dropped, dead-tail
fan skip added). The tip is the candidate fix; it is UNBUILT.

## Rules (verbatim from the parent's brief and the coordinator)
- "an exact (byte-identical play) cut or a missing budget charge are the preferred shapes."
- "If it changes play (e.g. charging previously uncharged work), it is a play change: do NOT accept GT; report the
  measured impact on a small paired sample and stop -- the parent decides."
- "Commit on wip/g803-perf and push that wip branch (allowed). Write a report to logs/g803-perf/report.md (force-add:
  logs/ is gitignored) with mechanism, fix, proof, CPU before/after."
- "RUN ALL ENGINE WORK THROUGH THE SMALL-JOB LANE: bash scripts/box_lane.sh --slots 2 --cap-gb 16 -- <cmd> (at most 2
  slots; a big gate holds the box lock -- do NOT take build/scratch/claude_box.lock)."
- Coordinator (mid-task): "From now on keep at most 2 lane slots in use at once across all your jobs."
- Never push phase-1-2-deck-analyzer, never use scripts/push_queue.py, no `timeout`, build only via ./build.sh
  (`flock /workspaces/MagicDeckTester/build/scratch/claude_build.lock ... ./build.sh profile -- --target mtg`),
  wait ~200 s after editing source before building (9p clock lag), never `pkill -f` from the Bash tool.

## MECHANISM (measured)
- Reproduces on origin d88b4bfe0 (Profile build saved as `bin_before/mtg`): T1-T4 decisions identical to the
  maze-rank3 binary (census units 21098/24407/12211/11508); the T5 decision ran > 3700 s CPU (killed). Game total is
  69,801 units, so T5 charges ~577 units.
- perf: 95% of CPU in TakeTurn > FullSearchLineHybrid > FullSearchLine (dev-seq) > FullSearchLine > root FSLineWin >
  FSLineTail > BoundaryFan::Cross > SimulateEndAndStartNextTurn > ResolvePassiveOpponentLandTurn >
  ResolveBurgeoningTriggers (74%; MazesEndProvider::LandEntryPinCandidates 30%). Hot site = FSLineTail's SINGLE-MAIN
  boundary fan (the `BoundaryFan fan(state, /*with_echo=*/false)` block; addr2line).
- TMPDIAG on that fan (diag3 run): `site=2 T5 d0 nobs=9 widths=2,7,9,9,11,11,12,11,12` (~2e8 crossings), 39 us per
  crossing, `cutoff=4` while the node is at T5, `edge` = every crossing, `ovr=0 pex=0 used=42 lim=18000` (no ceiling
  ever trips). 3.1M crossings sampled: every child FSLineWin (T6, or a T5 boundary win) is refused at its entry by
  `turn > cutoff`. The root already holds a T5 win (FsChildCut = best-1 = 4) and keeps scanning later m1 plans; each
  later plan's depth-0 tail is DEAD but still walks the whole crossing-land odometer (2HG: 2 opponent land plays x 4
  Burgeonings = 8 put ordinals, menus 7-12 wide), uncharged.

## FIXES on the tip (both exact by argument; identity NOT yet measured)
1. `src/ai/TurnSolver.cpp` DeadTailFanSkip (`MTG_DEAD_TAIL_FAN_SKIP`, default ON): FSLineTail with
   `state.turn_number > cutoff` does not walk its boundary fans (4 sites; the single-main site returns no-win). The
   refused children charged and read nothing, so units/budget reads are unchanged; only a no-win tail's boundary pins
   differ (unadoptable). Caveat: skipped crossings also skip their priced work (workctr::t_priced: TapForCost calls),
   so a decision whose priced ceiling (MTG_DECISION_PRICED_X) tripped inside such dead fans could move -- check.
2. `src/core/SpellEffects.h` ResolveBurgeoningTriggers: idle-trigger fold (`MTG_BURGEONING_IDLE_FOLD`, default ON),
   exactness argument in the comment. Measured on g803 T1-T4: units identical, fan crossings 81->63, 40->29, 128->46.
   Alone it did NOT fix T5.
3. `test/unit/test_mazes_end_i3.cpp`: unit test for the fold.
(FanOverrunStop was tried and DROPPED: no ceiling trips in g803, and an Overrun() poll per crossing moves the
budgetbind read counters, which cache guards read.)

## Remaining, in order
1. Build the tip: `cd <worktree> && bash scripts/box_lane.sh --slots 2 --cap-gb 6 -- flock
   /workspaces/MagicDeckTester/build/scratch/claude_build.lock ./build.sh profile -- --target mtg`; copy
   build/Profile/mtg to `build/scratch/g803-perf/bin_after/mtg`.
2. g803 job (`build/scratch/g803-perf/g803_origin.json`, `--threads 1`, MTG_PGO=0) on bin_after: expect digest
   9e65ecd029edeb1a, T5, 69,801 units, wall ~seconds. Also with MTG_DEAD_TAIL_FAN_SKIP=0 MTG_BURGEONING_IDLE_FOLD=0
   only if a control is needed (it is the 55-min origin game; skip it).
3. Identity: `build/scratch/g803-perf/ident.json` (Maze std d5 b20 seeds 1001/2002/3003 = 466 games, 2HG 230 games,
   Blood Sun 2x30 -- cut Blood Sun to ~10 games/job, they take 60-200 s each) on `bin_before/mtg` AND `bin_after/mtg`,
   `--threads 2` each, ONE at a time (2-slot cap); compare per-job `digest=` and `units=` lines. Also one run of
   bin_after with each lever =0 separately if anything moves, to attribute.
4. Release build with mtg-test (`./build.sh` full, 2 slots, ~1.5 h of lane time) and `build/Release/mtg-test
   "[maze]"` plus `-tc="Maze's End I3*"`; or ask the parent to run units.
5. Report `logs/g803-perf/report.md` (git add -f), commit, `git push origin wip/g803-perf`.
6. Parent decisions to surface: none if identity holds. Follow-ups worth naming: (a) the whole dead FSLineTail (its
   m2 loop applies, each charged 1 unit) could return at entry -- sound but it moves units, so a play change; (b) the
   crossing is unpriced (no workctr kind), so the priced ceiling cannot see a live wide fan either; (c) the remaining
   duplicate orders of commuting puts (Burgeoning puts of A then B vs B then A) need the provider's
   LandEntryOrderClass premise, not an engine identity.
