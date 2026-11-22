# Blood Sun analysis -- handoff to the next agent (Claude -> Codex, 2026-11-22)

Self-contained checkpoint. The deck's ledger is `docs/design/analysis-Blood Sun.md` (every USER ruling, verbatim;
"Open USER decisions" at the end); the design + implementation notes are `docs/design/blood-sun-pipeline.md`
(§12 the step list, §14 per-step implementation notes). Read this file first, then the ledger's "Standing
decisions" and B12 / B12-bis, then only the §14 notes for the step you take.

## Where things are
- Worktree `/workspaces/MagicDeckTester2/.claude/worktrees/bloodsun`, branch `bloodsun-analysis`, **LOCAL ONLY (never
  pushed)**, rebased onto `origin/phase-1-2-deck-analyzer` 4a7f65983 (linear). Our 76 analysis commits end at
  f2f77ed40; integration commits on top: 95d250743 (Worldfire folded onto origin's implementation + Blood Sun gates
  for origin's new land abilities), 87143e9cb (TapAheadUndo: a failed untap-ritual cast rolls back its tapped-ahead
  sources and float; it fixed a phantom-mana "verified" kill exposed by step 6d's nested re-arm). Backup branch
  `backup/bloodsun-analysis-pre-int-20261122` = 22b283d64 (pre-rebase; keep).
- Second worktree `.claude/worktrees/bloodsun-perf` (branch `bloodsun-perf`): the perf parts 1-6; all of it is already
  contained in `bloodsun-analysis`. Nothing to land from it.
- Scratch (build volume, regenerable): `/workspaces/MagicDeckTester2/build/scratch/bloodsun/` (`int1/`, `int2/`
  integration; `perf2/`, `perf6/` Grim A/Bs; `gi518/` the lost-game diagnoses; `tools/digcmp.sh` smoke compare).
- Logs given to the USER: `/workspaces/MagicDeckTester2/logs/bloodsun-grim-lost/` (game logs of the Grim prune's lost
  games, ship / prune / amendment arms; play them in `tools/replay/index.html`).

## Integration: DONE (2026-11-22)
INT-2 (`logs/bloodsun/int-2-brief.md`): 87143e9cb TapAheadUndo fixed the phantom-mana kill; mtg-test 3981/3981,
scenarios 670/670; pooled tier: the same 27 keys as INT-1 move vs origin GT, 94 games, 8 faster, 0 slower; GT accepted
014ea5d01 (`check_gt_logs.py` 1409 consistent; `check_references_safe.py` rc 0). Root cause + table: blood-sun-pipeline.md
"Implementation notes (integration 2)". FOLLOW-UP (sound fix, do it): `EtbUntapTapAheadIntoFloat` (Peregrine Drake /
Cloud of Faeries; EDF) leaks the same way on a failed payment -- wrap it in `TapAheadUndo`, then A/B the EDF keys that
move and accept.

## Next, in order
0. **ORIGIN MOVED after the accept (checked 2026-11-22 ~12:00 UTC): 21 new commits on
   `origin/phase-1-2-deck-analyzer` (Creature Burn: Magma Jet cast_scry search, Fireblast pair fold, their GT accepts;
   CLAUDE.md "do nothing more is always an option" hard rule). They touch `src/` and GT, so: `git rebase
   origin/phase-1-2-deck-analyzer` (a `git merge-tree` preview shows only TWO conflicts: `src/ai/HeuristicArm.h` --
   both sides appended lever slots / names, keep both, keep the enum and the name table in lockstep -- and
   `test/regression_gt.txt` -- take ORIGIN's side, never hand-merge numbers), verify with `git range-diff`, rebuild
   (`MTG_BUILD_JOBS=12 ./build.sh`), `mtg-test`, `test/scenarios.sh`, ONE pooled `test/regression.sh --both` at full
   threads, inspect moves (expected: the same 27 keys as integration 2, and nothing from Creature Burn / Midrange Burn;
   any other moved key needs a mechanism), every slower game recovers at d8 b0, then `--accept`,
   `test/check_gt_logs.py`, commit. Only then step 1. `check_references_safe.py` passed (rc 0) at ~12:00.
1. **Land it (after the GT commit):** `python3 scripts/check_references_safe.py`; `git fetch`; if
   origin moved, rebase (never merge), rebuild (`MTG_BUILD_JOBS=12 ./build.sh`), smoke, `test/check_gt_logs.py`; push
   `bloodsun-analysis` to `origin/phase-1-2-deck-analyzer` (shared branch: at most once per 4 h per session); CI watch
   in the background with `--interval 60`, report the Windows result when it lands.
2. **Viewer for the USER's Blood Sun references (the USER is waiting for this):** fast-forward the PRIMARY checkout
   `/workspaces/MagicDeckTester2` (`git pull --rebase`; autostash handles its uncommitted files; its untracked
   `decks/Blood Sun/Blood Sun.cod` is byte-identical to the committed one -- compare before the pull, then move it
   aside if git refuses). Start the viewer ONLY from the primary:
   `MDT_REFERENCE_BACKUP_DIR=/home/vscode/.claude/mdt-reference-backups ./play.sh`. Tell the USER: Time Warp always
   targets self (not asked); the Overgrown Tomb `land_entry` prompt is hidden by "let AI decide"; the discard order is
   pending (item 6). Commit + push every new reference at once (CLAUDE.md play-server rules).
3. **Revelation fetches in game logs (USER request):** `PerformTutorPackageToHand` (`src/core/SpellEffects.h`, grep)
   emits no `EmitReveal`, so the replay viewer cannot show Diabolic Revelation's cards (the single-card tutor emits one,
   `src/core/SpellEffects.cpp` "(searched)"). Add one reveal listing the package, disposition "to hand", under
   `RevealVisible()` (logging only: smoke must be 0 moved). Then re-run the logs in `logs/bloodsun-grim-lost/` at d8 b0
   (seeds 910000 + gi, `--game-index gi`, deck `decks/Blood Sun/Blood Sun.cod`, no profile; prune arm =
   `MTG_BLOODSUN_GRIM_PRUNE=1`) and replace them.
4. **Grim Tutor list rework (ledger B12-bis, the USER's full spec):** build rows 1-8, the red-source 3 arms, LAND_CUT,
   ONLY_MISSING, and the Time Warp castability fix (count a land still playable this turn) in
   `src/ai/DecisionProviders.cpp` (`BloodSunGrimTargetAdmitted`, `BloodSunGrimTimeWarpCastable`,
   `BloodSunProvider::TutorCandidates`; levers in `src/ai/HeuristicArm.h` next to the existing Grim ones; read
   `.claude/skills/coding-conventions.md` first). All DEFAULT OFF. Validate as B12-bis says; ONE pooled held-out A/B
   (the perf6 `ab2` scheme: the 333 no-mulligan games of gi 0-599, seed 910000 + gi, d5 b20, nowin = 9; script
   `build/scratch/bloodsun/perf6/mk_ab2.py`), arms: ship, amended+LATE (perf part 6b), the new list, and its
   red-source / row-8 / LAND_CUT / ONLY_MISSING / >=8-gate variants; d8 b0 recovery of every slower game. Present to the
   USER (adoption is theirs, except the red-source arm: adopt the best).
5. **Step 7b (two-deviation walker gap) gains a second case:** gi 518 (seed 910518). Under the Grim prune the USER's
   line (T4 Revelation X3 -> Lotus Field x2 + Time Warp, Exploration, Time Warp; extra turn Grim -> Revelation X6)
   wins T4 but the d8 b0 search plays T5 at ANY budget: it needs two non-front breakpoint picks in one main
   (Revelation's hand-entry slot and Exploration's new-drop slot), and `DevSeqSeedWorth` (`src/ai/TurnSolver.cpp`,
   grep) opens compound slots only at latent / canon-drop occurrences. `MTG_BP_DEV_SEQ_ALL=1` finds it (about 183x units
   from a T4 hand-off). Proposed scoped fix: a compound slot at an occurrence a deviation itself created that grants a
   land drop / mana source (Exploration, Fastbond), priced after every single-deviation slot. Evidence and choice
   strings: `build/scratch/bloodsun/gi518/` (runs `ta_user`, `ta4_dsall`). This is a completeness violation (USER rule:
   everything findable at unlimited budget).
6. **Discard order (USER-owned, draft pending):** Stage 4's analysis (`build/scratch/bloodsun/viewer/stage4.json`
   `discard_analysis`, RULE_FOUND) derived [City of Brass, Blooming Marsh, Burgeoning, Scorched Ruins, Revelation,
   Lotus Field, Ulamog, Lotus Vale]; A/B d0 -0.133 (t -13.6, n 4000), d3 -0.0075 (t -0.3, n 800). **The USER reviews
   drafts of USER-owned heuristics BEFORE measurement** (USER 2026-11-22: "some of those should be reviewed prior to
   measurement such as the cast order and discard heuristics"). The USER REJECTED an order and gave BUCKETS (ledger
   B14, verbatim + the parent's readings): build B14 as the provider's `CleanupDiscardCandidates` override (default
   OFF), A/B at play depth, present the numbers. A discard heuristic is ALWAYS buckets, never an order.
6b. **Cast order (USER-owned, NO draft exists yet):** draft Blood Sun's cast order (and main-1 / main-2 split if the
   provider has one; read how `HinataProvider` / other providers express theirs) from the design's go-off sequence and
   present it to the USER for review BEFORE measuring. Same for the mulligan policy draft (ledger B11).
7. **Clairvoyance bracket (Stage 5, design risk R5):** gi 7 and gi 431 of the plain prune's losses are clairvoyant
   (the search knew the post-shuffle draw). Re-run them with `MTG_SEARCH_BLIND_UNKNOWN` (and report the
   `MTG_NC_SEARCH` bracket at Stage 5).
8. Then the remaining pipeline: Stage 4-bis suite entry under the 3x-Hinata cost rule (`scripts/suite_gate.py --cost`;
   Blood Sun is not yet in `test/regression_cases.sh` -- required before any mulligan profile / value leaf); the
   design's open items (§13, ledger "Open USER decisions"; round-win stop shared, delete `MTG_BLOODSUN_PLAN_PRICE`,
   pay-refine deferral note, Burgeoning own-extra-turn modelling fix, `MTG_CROSSING_WIDTH_PROBE_ALL` A/B, steps 7b / 9 /
   9b / 10); Stage 5 (5c2, the play sweep -- driven by agents of the parent's own type, see CLAUDE.md -- 5h audit,
   discard buckets); Stage 6 (report, 6a) per `.claude/skills/analyze-deck.md`.

## Open USER decisions
NONE right now. USER rule: a question for the USER is only a decision you cannot proceed without; never an item that
still needs an agent's investigation, test or measurement. Items 4-6 above (the Grim list, the >= 8 gate measured
jointly with it, the discard draft, a scoped `MTG_BP_DEV_SEQ_ALL`) and the cast-order / mulligan drafts (ledger B11)
become USER decisions only once their measurement is done and the result is in hand -- then present the numbers with a
recommendation.

## Box rules learned this session
- The 9p repo mount dies under memory pressure, and another container shares the VM (~22 GB): keep `free -g`
  available above ~10 GB. A 24-job build of this tree peaked at 23 GB; use `MTG_BUILD_JOBS=12 ./build.sh`.
- `mtg --batch` per-job `flags` take only heurarm levers; env-only flags (e.g. `MTG_BP_NEST_REARM`) need separate
  processes. Depth overrides on a deck with a value_play profile need `"ignore_play_profile": true`.
- `kill -STOP` / `-CONT` pauses a batch without changing its results (useful to free the box briefly).
- Old-name deck folders (FiveColour, KittyEquipment, StompySurprise, slivers_vial: gitignored August artifacts) were
  moved to `logs/archive/old-deck-folders-2026-11-22/` in the primary checkout at the USER's request; delete only if
  the USER confirms.

## Codex continuation (2026-11-22, 15:55 UTC)
- Integration correctness: failed ETB untap tap-ahead now rolls back (`02986649f`); Revelation packages emit a
  single reveal for replay (`50233ecae`). The latter still needs the 15 `logs/bloodsun-grim-lost/` files regenerated.
- Go-off bridge order and Grim's remaining-drop Time Warp payment test are in `d6289881a` and `845676e02`.
  B12-bis Grim list and variants are default off (`8c53f0fd3`, `e735ba06e`).
- Full pooled smoke + regression was accepted at `f95d28509`: 632 jobs, 180001 games; all 27 known moved keys
  match the prior Blood Sun integration and the 13 additional changes are Creature Burn / Midrange Burn from origin.
  `check_gt_logs.py` found 1409 consistent, 0 stale/missing. Full unit and scenarios passed afterward.
- Step 7b scoped arms are default off. `MTG_BP_DEV_SEQ_DROP` (`b3e06acd4`) recovers gi518 seed 910518 at d8 b0,
  T5 -> T4. `MTG_BP_DEV_SEQ_DEFERRED` (`5ae0deb64`) tracks every later site-3 occurrence and recovers the
  Machinations -> Grim -> Revelation -> two Bolts fixture at d3 b0 and b100, T5 -> T4; its fixture uses the same
  208 first-pass + 51 second-pass units as `MTG_BP_DEV_SEQ_ALL`. Full unit passed, scenarios 675/675.
  gi518 with the all-occurrences mask uses 288541 units (arm off 218437), so held-out cost measurement matters.
- B14 USER cleanup keep buckets are implemented default off (`cffd8a1eb`), with 3 focused tests and full unit pass.
  The cast-order draft was sent for USER review before measurement; no cast-order code has been adopted.
- Held-out B12-bis pool is running with frozen binary at `build/scratch/bloodsun/b12bis/` (7326 games, d5/d3,
  11 arms). After it finishes, run `table.py` and d8 b0 recovery for every slower game. Separate frozen binary and
  2664-game manifest for B14 and both scoped arms are at `build/scratch/bloodsun/b14/`; run it afterward.
- The 15 `logs/bloodsun-grim-lost/` game logs were regenerated at their original d8 b0 seeds with Revelation's
  `REVEAL` event; 38 package reveals show 209 fetched cards in total. Old logs are backed up at
  `build/scratch/bloodsun/reveal-refresh/backup/`.
- Blind-unknown bracket (`build/scratch/bloodsun/blind/`, gi7 and gi431, d8 b0, Grim prune on/off): gi7 win turns
  are off/open 5, off/blind 6, on/open 6, on/blind 7. gi431 are off/open 3, off/blind unwon at max turn 8,
  on/open 4, on/blind 4. Thus the unblinded comparison is affected by knowledge of post-shuffle draws; the blind
  comparison does not uniformly erase the prune's penalty, but gi431 reverses it. Report both brackets, do not
  treat the open-draw result alone as a correctness failure.
- Go-off ordering bug fixed in `bf84c87f0`: with two candidate Revelation packages in a plan, the Fastbond
  bridge retains the cheaper package's joint payment instead of the last package's bill. Five focused go-off tests
  pass (161 assertions). The B12-bis and B14 binaries were frozen before this change; their A/B arms remain
  internally paired, but the final default-on go-off fix needs the shared suite smoke before landing.
- At 16:18 the push queue put Blood Sun at its head. Rebased 104 local commits over origin `9575556d5f`
  (Worldfire, free-turn flood, tutor-width deletion, Merfolk); resolved the card-definition and search includes,
  merged both heuristic-arm blocks, kept origin's complete Worldfire LTB resolver, and took origin GT at conflicts
  pending a fresh pooled regression. The rebase exposed removed `TutorSearchWidth` / `TutorAxisWidth` APIs;
  Blood Sun's per-cast tutor axis now searches every distinct candidate, matching origin's complete search.
  The first build failed on those APIs; per CLAUDE.md queue rule the session requeued at 16:24 while fixing them.
  The incremental Release build completed (throttled to 4 jobs during a host-memory dip, then 12 jobs); full unit
  passed 4050 cases / 3,661,766 assertions. Reference safety passed across 107 checkouts; origin GT restored as one
  snapshot and `check_gt_logs.py` passed 1409 consistent / 0 stale. The pooled regression started, but was stopped
  when Beasts landed 47 new commits at origin `c841cd59c4`; Blood Sun is queue HEAD again at 17:04 and must rebase
  onto that tip before a final gate. The B12-bis frozen batch PID 459036 is PAUSED; `kill -CONT 459036` when gates
  permit. Queue waiter exec session 96044 is active (it may have exited on becoming HEAD).

## Codex continuation (2026-11-22, 23:05 UTC)
- Rebased 107 Blood Sun commits onto origin `138f60c83` (the Beasts tip plus a Worldfire Windows test fix). The
  rebased tree built successfully; full unit passed 4124/4124 cases and 3,666,730 assertions, scenarios 675/675.
  `check_gt_logs.py` passed 1409 consistent, 0 stale/missing; reference safety passed across 107 checkouts.
- One pooled `regression.sh --both` completed 632 cases / 180005 games. Exactly the previously identified 27
  integration configurations moved: smoke 9, regression 18. Smoke has 1 slower searched game, 2 faster, and 21
  same-turn play changes; regression has 0 slower, 5 faster, and 66 same-turn play changes. Hinata smoke d3 s1001
  gi22 (T5 -> T7 at 10 ms) recovers T5 at 4x and 16x budget and at depth 8 unlimited budget. The strict viewer
  replay reports 363 ok, 576 repaired, 1 beaten, and zero drift/gaps across 940 references; both suboptimal
  references are beaten. Smoke and regression GT were accepted; `check_gt_logs.py` again passed 1409/0/0.
- Blood Sun remains HEAD of the push queue. Commit the accepted GT, check reference safety, then `push_queue.py land`.
  Update the PRIMARY checkout `/workspaces/MagicDeckTester2` preserving its eight local reference/viewer commits and
  dirty files; its untracked Blood Sun deck is content-identical to the committed deck after CR removal. Start
  `MDT_REFERENCE_BACKUP_DIR=/home/vscode/.claude/mdt-reference-backups ./play.sh --no-open` there and verify HTTP 8080.
- The B12-bis frozen process did not survive a previous turn interruption. Its d5 333-game arm results remain in
  `build/scratch/bloodsun/b12bis/`; d3 is partial. Resume by running missing manifest cells, after viewer setup.
