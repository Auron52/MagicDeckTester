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

## In flight at handoff (check before acting)
Integration 2 (brief `logs/bloodsun/int-2-brief.md`; handoff, if written, `logs/bloodsun/int-2-handoff.md`): after
87143e9cb, ONE pooled `test/regression.sh --both` was running (`build/scratch/bloodsun/int2/tier.sh`, pid in
`int2/tier.pid`, `int2/tier.log`). Remaining INT-2 steps: inspect every moved key against origin GT (`int2/moves.md`,
the `int1/moves.md` shape: INT-1 traced all 27 moved keys / 88 games to step 6d `MTG_BP_NEST_REARM`, commit
39ca4245b); re-run every slower game at `--depth 8 --budget-ms 0` (must recover; the 3 Hinata seed-2019 gi17 games
already do on 87143e9cb, `int2/fix_rec*`); then `bash test/regression.sh --both --accept` (note naming 39ca4245b /
b5253ba63 / 87143e9cb / 95d250743), `python3 test/check_gt_logs.py`, `python3 scripts/check_references_safe.py`,
commit "GT: accept smoke + regression for Blood Sun integration ...". If git log shows that GT commit, INT-2 is done.

## Next, in order
1. **Land it (the parent's steps, after the GT commit):** `python3 scripts/check_references_safe.py`; `git fetch`; if
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
   measurement such as the cast order and discard heuristics"). The discard draft was shown to the USER on 2026-11-22
   (the derived order + refinements: Revelation only with a duplicate in hand; a redundant Blood Sun early); take the
   USER's review, THEN build it as a provider lever (default OFF), A/B at play depth, present the numbers.
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
