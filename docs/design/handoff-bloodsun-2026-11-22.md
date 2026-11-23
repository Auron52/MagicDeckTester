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

## Codex continuation (2026-11-22, 23:50 UTC)
- Blood Sun landed on the shared branch at `249db2aa7` via the push queue. The primary checkout was updated to that
  commit with its dirty user files preserved. The primary viewer is serving `http://localhost:8080/` from PID 673169
  (`tools/play/server.js`, parent 1), with Blood Sun and its profile available. It writes references into the primary
  checkout's persistent `references/`; check, commit, and push any new reference promptly. Its log is
  `logs/play/bloodsun-viewer-2026-11-22.log`. CI Windows run 45081937077 was still building at 23:50 UTC.
- B12-bis d5 held-out 333-game/arm results are complete. The new Grim list improves loss-penalized win turn by about
  0.087 (SE 0.033) and uses 68% of ship search units; the row-8 Fastbond variant improves by 0.093 (SE 0.033) at
  67% units. Go-off OFF has identical win turns in all 333 games and 1% more units than ship. These are frozen pre-rebase
  binaries and must still pass d3 and unlimited-budget recovery before any adoption decision.
- B12-bis d3 resumed as a detached pooled batch (supervisor PID 676112, binary PID 676116 at 23:50 UTC), logs and
  marker in `build/scratch/bloodsun/b12bis/` (`ab.log`, `ab.err`, `glog/`, `d3.done`). Its ship baseline was 333 games,
  5.5706 average. B14's 8-arm d5/d3 pooled batch is chained after it (watcher PID 676277; marker `b14/b14.done`).
  A same-run Blood Sun vs Hinata suite cost gate is chained after B14 (watcher PID 677135; `suite-cost.done`). All
  three use `build/scratch/claude_box.lock`; only one big process runs at a time.
- `test/regression_cases.sh` is dirty with **provisional** Blood Sun smoke/regression/overnight rows needed for the cost
  gate. `bash -n` and `suite_gate.py --require 'decks/Blood Sun'` passed. If the 3x-Hinata gate fails, remove these rows;
  if it passes, size them to the suite budget, run Blood Sun's tier cells, accept ground truth, and commit. The cost
  watcher uses the worktree's existing Release binary, so avoid replacing it before that measurement.
- The B12-bis d3 run may take hours. After completion, compare paired turns and recover **every** slower game at d8 b0
  before recommending a Grim arm to the user. The USER owns the Grim-list adoption except the red-source variant.

## Codex continuation (2026-11-23, 00:40 UTC)
- Primary viewer is running from the persistent checkout on port 8080. The prior Blood Sun rejection's queued-line
  fix is in its static page, and multi-pick dialogs now retain both modal and card-grid scroll offsets after a pick
  (`29bbb390e` in this worktree, `59fcbf048` in primary). Refresh the browser for the static update. Seeds 1–6 were
  saved as primary checkout commits; new reference files must still be backed up as they appear.
- Seed 5 reference `references/Blood_Sun/claude_s5_gi4.json`: a widened viewer plan baked Grim Tutor → City of Brass
  while Blood Sun was already out and City was in hand. The human tutor dialog copied that baked target as its AI
  badge. `AskHumanTutorPick` now computes a shipped candidate list under `ExecutorMenuScope` and uses it when the
  widened baked target is outside that list. Burgeoning's human dialog similarly badges the first shipped-eligible
  land (or none); this closes the reported pre-Sun Lotus recommendation while leaving all human choices available.
  These engine edits are dirty and need build, replay, tests, then commit and viewer binary refresh.
- B12-bis d5/d3 333-game paired runs are complete. V2 improves average loss-penalized win turn by 0.087 / 0.090
  at d5/d3 and uses ~68% of ship units, but has five unbounded slower games (gi7/290/411/431/518) at d8 b0;
  do **not** adopt the hard V2 list. A default-off `MTG_BLOODSUN_GRIM_V2_RESCUE` arm adds the old amended targets as
  fallbacks; its five-game d8 manifest is in `build/scratch/bloodsun/b12bis/rescue_d8/`. Run after the source rebuild.
- The seed 1 T4 Emrakul/Revelation package observation is encoded as default-off
  `MTG_BLOODSUN_EMRAKUL_BRIDGE`: it only fronts Fastbond + Lotus + Time Warp for Revelation X3 after a deterministic
  payment probe proves Emrakul payable on the extra turn. A focused unit test is dirty in
  `test/unit/test_bloodsun_perf.cpp`. Build and test, then measure before adopting.
- B14 pooled batch began at 00:29 UTC with frozen binary, marker `build/scratch/bloodsun/b14/b14.done`;
  the Blood Sun vs Hinata cost gate watcher follows it (`suite-cost.done`). The worktree Release build is being
  refreshed for the viewer and the provisional suite-cost rows remain dirty in `test/regression_cases.sh`.

## Codex continuation (2026-11-23, 01:10 UTC)
- The primary viewer still serves HTTP 200 on port 8080. Revelation's card picker now changes selection/count/confirm
  in place, so a click or Follow AI keeps the same `.tutgrid` DOM and scroll offset. This is committed in both the
  Blood Sun worktree (`bbe17651b`) and primary checkout (`f6b0b699e`); the static page was copied to primary.
  A focused jsdom check preserved a nonzero grid scroll offset through both actions, and
  `node test/viewer_linebuild_check.js` passed 946 references / 0 failures.
- User references seeds 1–10 are committed in both checkouts; seed 11 appeared while checking and was committed
  separately (`a197b3248` worktree, `5ebf70192` primary). The remote `references-autosave` branch already contains
  seed 11. `viewer_protocol_check.py --only Blood_Sun --threads 2` on the rebuilt engine passed the first 10:
  9 exact and 1 repaired (seed 6 predates a bounce subdecision), 0 drift/gaps, 0 stateless fallback in 331 steps.
  Recheck seed 11 and run viewer validation once its current process finishes.
- The rebuilt worktree `build/Release/mtg` was copied to primary at 01:04 UTC for NEW viewer sessions. The updated
  Burgeoning badge is Rakdos Carnarium in seed 2 and Graven Cairns in seed 6, not pre-Sun Lotus Field. Seed 5 Grim
  Tutor badge is now Scorched Ruins instead of the invalid City of Brass; under `MTG_BLOODSUN_GRIM_PRUNE=1` it is
  Diabolic Revelation, matching the user's chosen T3 line. The default-on tutor/Burgeoning fixes are still dirty
  pending focused unit validation and commit.
- B12-bis 5-game unbounded rescue check (`build/scratch/bloodsun/b12bis/rescue_d8/`) restored all five V2 slow games
  to ship turn or better: gi7/411/431/518 tie ship, gi290 wins one turn earlier. A fresh 4-job, 1332-game paired
  d5/d3 rescue-vs-ship batch is queued behind the cost gate under `build/scratch/bloodsun/b12bis/rescue_ab/`.
  Do not adopt pending full paired results and recovery of every slower game.
- B14 bucket discard had 22 faster and 30 slower paired games at both d3/d5, including up to nine turns regret;
  do not adopt despite its better mean. The drop arm had 4 faster and 1 slower (gi449, +2); it needs unbounded
  recovery. The deferred arm was turn-identical. B14's full log is `build/scratch/bloodsun/b14/batch.log`.
- The original suite cost gate raced the Release build and failed flag-registry validation. A clean retry is running
  under the big-process lock at `build/scratch/bloodsun/suite-cost-retry.log`; a two-job `mtg-test` build is running
  separately in the small lane (`build/scratch/bloodsun/mtg-test-build.log`). After that build, run the focused
  Blood Sun performance/unit cases, then commit the remaining engine code and update primary binary if rebuilt.

## Codex continuation (2026-11-23, 01:25 UTC)
- Seeds 11 and 12 are now saved in both checkouts and replay exactly through the viewer protocol: seed 11 wins T4
  (40 steps), seed 12 wins T4 (44 steps). Seed 12 is a concrete held-tutor bridge: the first T4 Revelation X2 takes
  two Lotus lands, held Time Warp starts the extra turn, and held Grim Tutor retrieves the second Revelation then.
  Two Warps were already in hand; the library had no Warp left. A good go-off test must allow this while requiring
  a second accessible Warp and a Revelation still in the library. Dimir Machinations can replace held Grim if Grim
  remains in the library and the extra-turn board can pay both tutors.
- A default-off `MTG_BLOODSUN_TUTOR_BRIDGE` ordering arm now recognizes that two-Lotus package with two open land
  drops, the Warp/retrieval prerequisites, and a next-turn mana floor; it also ranks Lotus classes first in
  Revelation package generation. A focused unit case covers positive and missing Warp/Revelation/drop negatives.
  Source is dirty and a Release `mtg` rebuild is in progress. The prepared seed 11/12 paired manifest is
  `build/scratch/bloodsun/seed12_bridge/manifest.json`; run it after build, then broader paired measurement.
- The formal suite cost gate FAILED: worst Blood Sun searched case 48,552.833 ms/game vs Hinata 8,395.837,
  ratio 5.78x over the 3x cap. Provisional Blood Sun entries in `test/regression_cases.sh` and the resulting
  `test/suite_cost.json` edits were reverted. Search-cost reduction is the immediate priority before suite entry.
- B12-bis rescue 333-game paired result: d5 ship mean 5.4625, rescue 5.4114, ship 3.2426e7 units,
  rescue 2.4881e7 units; d3 ship mean 5.4685, rescue 5.4054, ship 3.1169e7 units, rescue 2.3908e7 units.
  At d5, 33 games improved, 22 slowed, 278 tied (losses scored as 9); at d3, 34 improved, 21 slowed, 278 tied.
  The 21 d3 slower games are a subset of the 22 d5 slower games. This is not enough for adoption yet; the exact
  unbounded recovery manifest is `build/scratch/bloodsun/b12bis/rescue_d8_slower_corrected/manifest.json` (44 jobs).
- The full `mtg-test` build was stopped after 61/353 objects so the new arm could be built into `mtg` for paired
  play first. Resume `mtg-test` when the arm stabilizes. The primary viewer still serves the previously validated
  binary; copy a newly validated binary only after the new build passes.
- `node test/viewer_validate_check.js Blood_Sun` on all 12 references passed: 130 accepted lines, 22 choice lines,
  zero regressions, 128 mana audits yes. The seed 11/12 protocol replays were exact (40/44 steps respectively).
- The new `mtg` built successfully. An initial seed 11/12 d5/b20 and d8/b0 paired run used game index **0** for both,
  NOT the saved references' indices 10/11. Those index-0 games had T4 wins in every arm with identical play digests;
  seed 12 index 0 tutored Lotus Vale and cast Time Warp on T3. Do not use that result to judge the user reference.
  The corrected exact-reference paired run is queued at `build/scratch/bloodsun/reference_bridge/` (seed 11/index 10,
  seed 12/index 11) with traces. The synthetic unit case covers the reference's first Revelation X2 state. Broader
  333-game d5 paired run is active under `build/scratch/bloodsun/tutor_bridge_ab/`.
- `MTG_BLOODSUN_PLAN_PRICE=1` failed a 3-game 2HG cost screen: identical T4 outcomes and digest, but CPU rose
  86.7s -> 199.7s and units 2.258m -> 3.218m (the sample's three games). Keep it off.
- The 333-game d5 go-off arm A/B completed: 333/333 same win turn, ship mean 5.4625 vs arm 5.4625;
  units 32.426m vs 32.736m (+0.95%). No performance or win-turn case to enable it from this sample.
  The corrected reference indices 10/11 also yield the same autonomous T4 line under ship/arm at d5 and d8.
  Batch `game_index` labels the game trace; seed fixes the shuffle, so the index-0 and corrected-index opening hands
  are identical and match the saved reference. Autonomous play differs from the user's choices on T3, fetching Lotus
  with Grim and casting Time Warp then; the user's reference reaches Revelation X2 on T4. The synthetic decision-state
  unit must still be built and run. Keep the arm default OFF pending a useful reachable-state demonstration.
- IMPORTANT replay correction: a one-game batch chunk with global `game_index=i` must set `seed=base_seed+i`;
  setting `game_index=i` alone changes only the logged index/opponent schedule and repeatedly shuffles the base seed.
  The first 44-job d8 rescue recovery used the wrong seeds and is INVALID. The corrected distinct-shuffle run is
  `build/scratch/bloodsun/b12bis/rescue_d8_slower_corrected/`, launched after fixing the manifest. Do not use the
  original `rescue_d8_slower/run.log` verdicts. A resumed `mtg-test` Release build is active in the small lane.
- Corrected unbounded rescue recovery completed: all 22 distinct d5-slower games tie the shipped win turn at d8 b0;
  none is truly slower there. The earlier five-game hard-V2 rescue run was valid because it used one 600-game job with
  `skip_games`, so its local `seed+i` rule worked. A disjoint held-out paired d5/d3 rescue-vs-ship batch using
  base seed 920000 (same sampled indices, 1332 games) is active at `build/scratch/bloodsun/b12bis/rescue_heldout/`.
  Do not adopt until that result and its own slower-game recovery are reviewed.
- The first tutor-bridge arm ranked only a *completed Revelation target package*. The viewer's main-phase offer is
  Revelation X2 with no target yet; the targets are selected in a later `dragon` decision. This is why the first arm
  had no practical front-of-main-plan effect. A revised default-off arm now also recognizes the unbound Revelation X2
  offer, with two Lotus cards in the library as an added prerequisite; the focused test covers that unbound action.
  The Release `mtg` rebuild is active (`build/scratch/bloodsun/mtg-tutor-early-build.log`). After build, replay the
  saved seed12 prefix to the T4 Revelation decision with flag off/on at viewer hint depth and check whether the AI
  choice moves. A fresh paired 333-game manifest is ready at `build/scratch/bloodsun/tutor_bridge_early_ab/`.
- The revised `mtg` build passed. Replaying the exact saved T4 frame at depth5 with flag off/on returned the same
  displayed 45 main-phase plans and no `ai_choice` field: this protocol's plan list is stable enumeration order,
  while `PlanMoveOrderFront` influences autonomous search beneath it. Therefore the viewer frame alone cannot prove
  the autonomous ordering benefit. A focused unit now tests both the unbound Revelation X2 rank and Lotus class
  order; the full `mtg-test` build is active (`build/scratch/bloodsun/mtg-test-final-build.log`). The revised
  333-game paired A/B is queued under the big-process lock behind the Grim held-out batch.
- The focused test object and doctest main were linked into
  `build/scratch/bloodsun/mtg-bloodsun-focused-test` against the Release core library. Running `--test-case='Blood Sun*'`
  passed 26 cases / 695 assertions; the held-tutor case alone passed 34 assertions. The general 348-object `mtg-test`
  build was stopped after the focused slice passed, to free the small lane for the paired batches.
- Held-out seed block 920000 is far heavier than training: its d5 shipped 333-game job finished at mean 5.5045,
  182.982m units and 14.748m CPU-ms (vs training ship 32.426m units / 2.949m CPU-ms). The rescue/d3 jobs were still
  running when this was written. `run.err` records exact slow-game replay seeds; gi208 shipped used 7.050m units.
  A 30-game 2HG rescue-vs-ship cost probe matching the gate's worst case is queued after the revised go-off A/B at
  `build/scratch/bloodsun/b12bis/rescue_2hg_cost/`.
- The handoff's seed 910518 / gi518 two-deviation completeness case has a targeted d8 b0 pair queued after those
  batches, with `MTG_BLOODSUN_GRIM_PRUNE=1` in both arms and `MTG_BP_DEV_SEQ_DROP=1` only in the second. Correct
  one-game chunk seed/index are used; manifest `build/scratch/bloodsun/gi518_drop_correctness/manifest.json`.
- Replaying all 12 Blood Sun user references against the latest rebuilt worktree binary passed: 11 exact, one
  repaired for seed 6's older bounce prompt, zero play drift / board divergence / enum gap / contract failure,
  zero stateless fallbacks in 415 steps. Log `build/scratch/bloodsun/viewer-protocol-12-current.log`.
- The held-out batch's heartbeat at 02:12 UTC shows gi208 (`--seed 920208 --game-index 208`) still RUNNING in d5
  rescue at 0.25h, whereas shipped gi208 finished at 0.15h / 7.050m units. This rescue tail may erase aggregate
  speed gains. Preserve that exact pair for tutor-choice diagnosis after the batch; do not infer cost from the
  partial count of slow games.
- Held-out d5 333-game pair completed: ship mean 5.5045, 182.982m units, 14.748m CPU-ms; rescue mean 5.4565,
  175.913m units, 15.244m CPU-ms. Rescue has 32 faster, 16 slower, 285 tied games. It improves mean win turn by
  0.048 but saves only 3.9% units and costs 3.4% MORE CPU on this block. gi208 is T5 ship -> T7 rescue; gi433 and
  gi528 are T8 ship -> no win by T8 rescue. This candidate alone is not a robust suite-cost fix. The d3 jobs remain.
- The 16 held-out d5 slower games are queued for corrected d8 b0 recovery as two 600-game jobs with 584 skipped
  indices each (`build/scratch/bloodsun/b12bis/rescue_heldout_d8/`), so seed+local index matches each original
  shuffle. A depth-5 ship/rescue trace pair for the tail gi208 (`--seed 920208 --game-index 208`) is queued at
  `build/scratch/bloodsun/b12bis/gi208_diagnosis/` for tutor-choice diagnosis. Both wait under the big-process lock.

## Codex continuation (2026-11-23, 02:46 UTC)
- The held-out 333-game/arm Grim rescue pair is complete at both depths. d5: ship 5.5045 vs rescue 5.4565 average
  win turn, 32 faster / 16 slower / 285 ties, but CPU 14.748m -> 15.244m ms (+3.4%) and units 182.982m ->
  175.913m. d3: ship 5.5135 vs rescue 5.4625, 33 faster / 17 slower / 283 ties, CPU 16.071m -> 15.437m ms
  (-3.9%) and units 173.451m -> 161.012m. Thus quality gain is repeatable, but the speed gain is not.
- The d3-only slower index 140 was added to the d8 b0 recovery manifest, now 17 distinct held-out slower games:
  `build/scratch/bloodsun/b12bis/rescue_heldout_d8/manifest.json`. The prior queued lock waiters for recovery,
  2HG cost and gi518 were stopped; manifests remain. This allowed the go-off A/B to use 20 free cores while the
  held-out batch had only one active worker. Go-off A/B PID 766192, log
  `build/scratch/bloodsun/tutor_bridge_early_ab/run.log`; it is still active. The gi208 trace pair was moved to
  `scripts/box_lane.sh --slots 2 --cap-gb 4` (PID 765707, exec session 19924) and is still active.
- `python3 scripts/check_references_safe.py` passed: 107 checkouts/worktrees scanned, every uncommitted reference
  protected on origin or the autosave branch. Primary viewer remained HTTP 200.
- The revised held-tutor go-off A/B completed on 333 train games at d5: all 333 paired win turns tied at 5.4625;
  search units rose 32.426m -> 32.736m (+0.95%), CPU fell 3.451m -> 3.377m ms (-2.2%, noise against no quality
  movement). `MTG_BLOODSUN_TUTOR_BRIDGE`, its helper, and its synthetic test were removed from dirty source. The seed-12
  human reference remains valid and exact, but autonomous play already finds a different T4 line. A pre-list witness or
  staged plan generation is more promising than a later move-order tier for speed.
- The 30-game two-headed ship/rescue cost probe is active as PID 767381,
  `build/scratch/bloodsun/b12bis/rescue_2hg_cost/run.log`; the 17-case d8 b0 recovery is active as PID 768450,
  `build/scratch/bloodsun/b12bis/rescue_heldout_d8/run.log`. Both used idle cores left by prior long tails. The gi208
  trace ship arm completed T5 using T3 Grim -> Time Warp, T4 Dimir -> Blood Sun, then a T5 Revelation chain; its rescue
  arm is still running in the small lane.
- The 2HG cost probe completed: ship mean 5.2, CPU 1.931m ms / 30 = 64.4 s/game, 20.161m units; rescue mean 5.1,
  CPU 1.348m ms / 30 = 44.9 s/game, 17.050m units. Four games improved, two slowed (gi24 T6->T7, gi26 T5->T6),
  24 tied. The rescue is 30% cheaper in this sample but still far above the ~25.2 s/game suite gate. A paired d8 b0
  recovery for gi24/26 is running in the small lane at `build/scratch/bloodsun/b12bis/rescue_2hg_d8/`; gi24 already
  ties T6. The earlier formal gate's shipped 48.6 s/game differed from this isolated probe's 64.4, so use the formal
  gate for admission and this probe only as a paired relative comparison.
- The seed 910518/index518 d8 b0 completeness pair completed: `MTG_BLOODSUN_GRIM_PRUNE=1` alone wins T5, adding
  `MTG_BP_DEV_SEQ_DROP=1` finds the T4 line (218k vs 289k units). This is an actual searched continuation gap in the
  optional Grim-pruned arm; it has not been enabled by default. Prior B14 paired data for DROP had four faster / one
  slower budgeted game and its slower case recovered at d8 b0.
- A 333-game d5 A/B for the held-Emrakul seed-1 bridge is running from
  `build/scratch/bloodsun/emrakul_bridge_ab/manifest.json` (PID 768685). A Release rebuild with the failed held-tutor
  bridge removed is also running via `MTG_BUILD_JOBS=8 bash build.sh release -- --target mtg`; log
  `build/scratch/bloodsun/mtg-after-bridge-revert-build.log`.
- The gi208 depth-5 trace pair completed: shipped T5, 7.050m units / 0.481m CPU-ms; Grim rescue T7, 14.129m units /
  1.088m CPU-ms. The divergence is at T3: shipped Grim -> Time Warp, then held Dimir -> Blood Sun at T4; rescue
  Grim -> Blood Sun, then a slow Revelation/Fastbond line. At d8 b0 the rescue gi208 already recovered T5. This is a
  budget-ordering loss and the main held-out CPU tail.
- A new default-off `MTG_BLOODSUN_GRIM_V2_MACH_WARP` ordering arm fronts Time Warp in the V2+rescue Grim list when
  Blood Sun and Warp are both offered, Warp is not held, held Dimir Machinations can transmute now, and a red land is
  on board or in hand. This aims to recover gi208's T3 Grim -> Warp line without changing target coverage. A focused
  unit case is in dirty `test/unit/test_bloodsun_perf.cpp`; the first rebuild was stopped to avoid recompiling the
  giant source twice, and the current rebuild log is `build/scratch/bloodsun/mtg-mach-warp-build.log`. Exact gi208
  replay manifest: `build/scratch/bloodsun/b12bis/mach_warp_gi208/manifest.json`; run its candidate arm after build.
- The held-out d8 b0 recovery completed: all 17 distinct d3/d5-slower rescue games match shipped win turn, 17/17.
  Rescue 9.831m units vs ship 17.162m over those cases; log
  `build/scratch/bloodsun/b12bis/rescue_heldout_d8/run.log`. This clears the missing-line check for this block, while
  budgeted slowdown remains. The Emrakul seed-1 bridge A/B also completed: 333/333 equal win turns at 5.4625, units
  32.426m -> 32.414m (noise); remove that default-off temporary arm and its unit case at final cleanup. The revised
  held-tutor arm was already removed after its no-gain A/B.
- `MTG_BUILD_JOBS=8 bash build.sh release -- --target mtg` passed with the new Machinations-aware Grim arm. Exact
  gi208 candidate-only trace is running in the two-slot lane (exec session 27552), while a 333-game/arm d5 paired
  training batch is running at `build/scratch/bloodsun/b12bis/mach_warp_d5/` (exec session 95405). The 2HG gi26
  d8 b0 recovery remains active in the other two-slot lane; gi24 tied T6.
- The 2HG gi26 d8 b0 pair completed T5/T5 (rescue 13.184m units, ship 15.921m); both 2HG budgeted slower cases
  recovered (gi24 T6/T6, gi26 T5/T5). The broad Machinations-aware order's d5 train A/B completed: rescue baseline
  5.4114, candidate 5.4204; 0 faster, 2 slower (gi176 T5->T6, gi528 T5->T7), 331 ties, units 24.881m ->
  25.170m and CPU 2.539m -> 2.654m ms. Do not adopt this broad arm.
- The gi176/gi528 d5 trace pair (`build/scratch/bloodsun/b12bis/mach_warp_slow_trace/`) shows gi176 candidate
  T3 Grim -> Revelation instead of Sun; gi528 candidate T5 Grim -> Warp then T6 Dimir -> Sun, delaying its chain
  from T5 to T7. Neither opening hand held Revelation, unlike held-out gi208. A plausible narrowing is to require
  Revelation in hand at the tutor state, but confirm gi208's current exact candidate replay before recompiling.
- A d3 333-game/arm Machinations-order A/B is active at `build/scratch/bloodsun/b12bis/mach_warp_d3/`; its baseline
  has finished at 5.4054, candidate still running. The 2HG slower-case sweep freed its small-lane slots.

## Codex continuation (2026-11-23, 03:35 UTC)
- The Machinations-aware Warp-front arm failed its exact target: held-out seed 920208 / gi208 remained T7 with the same
  digest `57b44d69958848f1` and 14.129m units as rescue-only. Its depth-3 333-game train pair also matched the
  depth-5 failure: baseline 5.4054 vs arm 5.4144 average win turn, 0 faster / 2 slower (gi176 and gi528) / 331 tied,
  units 23.908m -> 24.198m. The `MTG_BLOODSUN_GRIM_V2_MACH_WARP` arm and synthetic unit were removed.
- The held-Emrakul seed-1 ordering arm had no win-turn movement in 333 paired games and was removed with its unit.
  The revised held-tutor package-order arm had already been removed for the same reason. All 12 human references
  remain preserved and protocol-valid; the autonomous engine's different lines should be judged by actual win turn.
- Dirty source now contains only the default-off `MTG_BLOODSUN_GRIM_V2_RESCUE` arm. It produced a repeatable quality
  gain in train and held-out depth-3/5 blocks, and all 17 held-out budgeted-slower games recovered at depth-8/budget-0.
  It is not an established speed fix: held-out depth-5 CPU rose 3.4%, while depth-3 CPU fell 3.9%. The 30-game 2HG
  cost probe was ~30% cheaper under rescue, but still above the formal suite's 3x-Hinata ceiling. Do not enable or
  enter the suite without cost work and user review of the Grim target list.
- Performance diagnosis remains plan materialization: slow nested decisions build millions of plans and score only
  a tiny fraction after the existing same-round win stop. The go-off rank runs after construction, so adjusting it
  alone cannot remove most of this cost. The next technical experiment should stage nested plan generation or use
  a verified same-round witness before constructing the full list. Preserve unlimited-budget coverage; do not use a
  hard candidate cap.
