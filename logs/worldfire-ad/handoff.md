# Worldfire APPLY-DROP handoff (2026-11-25, ~22:00 UTC)

Worktree `.claude/worktrees/worldfire-ad`, branch `wip/worldfire-ad` (off origin `phase-1-2-deck-analyzer` e430cc585,
src tree 401750ac). Push ONLY `wip/worldfire-ad`, never the shared branch. Never touch `references/`.

## Done
- `fdca1387e` WIP: the three levers below, unit tests, design doc `docs/design/worldfire-apply-drop-2026-11-25.md`,
  and the scripts `logs/worldfire-ad/{mk_gate.py,mk_ab.py,run_gate.sh}`. **UNBUILT**: the first build
  (`build/scratch/worldfire-ad/build1.log`) failed only because ManaPayment.cpp compiled while I was still editing it
  (the errors were `LineKeepRitualOn` undeclared and `ManaPool::Add(ManaPool)`, both already fixed in the committed
  source). No unit test or measurement has run on the new code yet.

## Clusters (gate claude-queue-20261125a, Worldfire gi 0-22999; 6,938 APPLY-DROP, 5,000 whole lines kept)
Raw lines: `build/scratch/worldfire-ad/flags.txt` (all 6,938; 1,938 are counter-only "seed=0" lines) and `whole.txt`
(the 5,000 whole lines). Source: `build/wt/gate-1125/scratch/mana_clean/claude-queue-20261125a/on.*.err`.
- Dropped cast (all 6,938): Ponder 2,353, Brainstorm 885, Personal Tutor 707, Mystical Tutor 545, Preordain 283,
  Seething Song 58, and the rest small. All are one-blue-pip spells.
- 4,473 of the 5,000 have Mercadian Bazaar on the board.
- By line shape (5,000 whole lines): 2 held casts + ritual + Bazaar 1,367; 3 casts + ritual + Bazaar 1,201;
  1 cast + 1 paid suspend + Bazaar 695; 1 cast, no suspend + Bazaar 354; 2 casts no ritual + Bazaar 250;
  2 casts + suspend + ritual + Bazaar 215; 3 casts + ritual, no Bazaar 165; and smaller groups.

## Root cause (reproduced with build/scratch/worldfire-ad/bin_base/mtg = origin HEAD Release, MTG_MANA_ORACLE=3)
This is an engine payment defect, not a modelling gap and not a USER policy question. The USER's Dragonstorm ruling
of 2026-11-18 ("it should spend the counters if they are required for the line") covers spending the Bazaar here.
A red payment earlier in the line is paid with the blue-capable sources (Steam Vents, Saprazzan Skerry). The Bazaar's
banked {R} is held, because storage taps last (rank 62). The cantrip then has no {U}. The crit line-keep rung
(MTG_LINE_KEEP_CRIT) is meant to catch this, but it misses three cases, and `docs/design/line-keep-crit-2026-11-24.md`
lists two of them as known gaps:
1. Suspend lines are unbound. `LineKeepBound` counts held CASTS only, so {suspend Keldon Halberdier + Ponder} never
   binds. Repro: s30000447 gi447 T3 (Bazaar + Steam Vents). The engine paid {R} with Vents.
2. The gate only finds a SINGLE last source. Repro: s30000065 gi65 T4 {Seething Song + Brainstorm} on Vents + Bazaar
   + Skerry. The engine paid Song with Vents {R} + Skerry {U}{U}. Neither source alone is the last {U}.
3. The ritual's float is not counted in the rest. For {Song, Song, Personal Tutor}, the joint test fails because the
   rest is judged without the first Song's {R}x5 (s30000125 gi125 T4, from the report).

## The fix (in fdca1387e). Each lever defaults ON, has a heurarm slot, and `=0` is byte-identical by construction.
- `MTG_LINE_KEEP_SUSPEND` (TurnSolver.cpp `LineSuspendPaid`): a suspend with mana value > 0 counts as a held payment
  in LineKeepBoundOver, and its cost counts in LineNoncreatureMv.
- `MTG_LINE_KEEP_CRIT_MULTI` (ManaPayment.cpp `LineKeepCritGate`): a multi-source superset test. For each owed colour,
  the largest c-capable sources are taken until they cover the bill's non-c pips. The gate passes if what remains is
  less than rest_c + cost_c.
- `MTG_LINE_KEEP_RITUAL` (ManaPayment.cpp `LineKeepRockFor(state)` / `PutLineKeepRock`): the float of the ritual
  being paid for (RitualFloatAmount at no splice, in its colour) is put on every board the rest is judged on. The
  joint amount pre-test subtracts it.
- Unit tests: `test/unit/test_worldfire_line_keep.cpp` (registered in CMakeLists.txt). It covers the three repro
  boards with each lever on and off, plus the no-strand controls. The expected values are my predictions and are
  unverified. Check them first: for example, whether the greedy really picks Vents over the Bazaar for {R} under the
  generic provider, and whether a 1-counter Bazaar is live.

## Remaining, in order
1. Wait for the build (queued on claude_build.lock, flock pid 3561693, log `build/scratch/worldfire-ad/build2.log`,
   DONE marker = a line starting `DONE`). Then run the unit tests (`build/Release/mtg_tests` or the repo's unit
   runner) through box_lane, with --slots <= 2. Fix the tests or the code as needed.
2. Single-game repros with the new binary: gi447 T3, gi65 T4 and gi125 T4, each with MTG_MANA_ORACLE=3 and
   MTG_MANA_ORACLE_TURN set. The commands are in the gate report. APPLY-DROP should go to 0 on those lines.
3. Re-run the gate on the same seeds. `python3 logs/worldfire-ad/mk_gate.py
   build/wt/gate-1125/scratch/mana_clean/claude-queue-20261125a/manifest.on.0.json build/scratch/worldfire-ad/gate.json
   --loo --loo-n 6000` gives 629 jobs and 64,054 games: arms off/on over all 23,000, plus leave-one-out arms over
   6,000 each. Run `logs/worldfire-ad/run_gate.sh build/Release/mtg build/scratch/worldfire-ad/gate.json
   build/scratch/worldfire-ad/gate_out` under `flock build/scratch/claude_box.lock`. It is about 40-50 min at 32
   cores on Release. Count APPLY-DROP per arm from the `job=<arm>__...` field in flags.err. Recluster what remains.
4. Play A/B (play moves): `python3 logs/worldfire-ad/mk_ab.py <out.json> --base 4870000`. That is all 63 suite
   decks at 400 per arm, plus Worldfire standard and 2HG at 3,000 per arm: 61,600 games, arms all-off vs all-on. Seed
   base 2710000 is ALREADY USED (maze-summit); I checked 4870000 and it looks unused. MULTI and RITUAL can reach any
   deck with multi-cast or ritual lines, so the whole suite is in scope. Compare CPU from the per-job ms (the USER:
   "3x is not a CPU licence"; for the crit adoption the USER asked for "doesn't have much CPU cost"). Expand the sample
   on decks that move.
5. Check every slower game at `--depth 8 --budget-ms 0`. If one does not recover, re-run it with MTG_NC_SEARCH=1.
6. Write `logs/worldfire-ad/report.md` (`git add -f`), update the design doc's Measurements section, then commit and
   push `wip/worldfire-ad`.

## Running jobs
- My build: flock pid 3561693 (cwd worldfire-ad), log `build/scratch/worldfire-ad/build2.log`, DONE line marker.
  No other job of mine is running.

## USER rulings in the brief (verbatim from CLAUDE.md / brief)
- The engine stays heuristic-free in the search window. Provider-side ordering or restriction is allowed only as
  CLAUDE.md describes. A sound fix defaults ON. A lossy or heuristic change defaults OFF until measured.
- Dragonstorm, 2026-11-18: *"For Dragonstorm it should spend the counters if they are required for the line."*
- MTG_LINE_KEEP_CRIT adoption, 2026-11-25: *"If it is slightly better, more correct in your understanding and doesn't
  have much CPU cost let's do it."*
- Box rules: small jobs go through `scripts/box_lane.sh --slots N` with N <= 2. Batches over 4 threads run under
  `flock build/scratch/claude_box.lock` with MTG_RSS_CAP_GB=40 and MTG_PGO=0. Builds run under `flock
  build/scratch/claude_build.lock ./build.sh`. No timeout. No pgrep -f / pkill -f.
