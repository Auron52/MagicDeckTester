# rock-in-plan handoff (2026-11-19 ~15:30 UTC, agent at ~273K context)

Worktree: `/workspaces/MagicDeckTester/.claude/worktrees/agent-ade0a1bc23ae96aa7`, branch `wip/rock-in-plan`
(based on origin/phase-1-2-deck-analyzer 93634781f; upstream unset deliberately -- push ONLY `wip/rock-in-plan`).
Scratch: `/workspaces/MagicDeckTester/build/scratch/claude_rock_plan/`. Helper scripts (gitignored) in
`logs/rock-in-plan/` of the worktree.

## Done (commits)

* `ce7d362a4` WIP (UNBUILT, UNMEASURED): `MTG_LINE_KEEP_ROCK`, default OFF.
  - `src/ai/EngineFlags.h`: `LineKeepRockOn()` (heurarm slot `LINE_KEEP_ROCK`, appended last in
    `src/ai/HeuristicArm.h` enum + names table).
  - `src/ai/TurnSolver.cpp` ~70095: `LineCastIsRock` (held cast, `mana_rock`, non-creature, not `enters_tapped`, not a
    pure converter `any_color_filter && filter_no_free_colorless`); `LineKeepBound(cands, sel)` / `LineKeepBound(acts)`
    now return true for KEEP_ALL (as before) OR for KEEP_ROCK when the line has >= 2 held casts and one is a rock;
    `LineNoncreatureMv` also enabled by KEEP_ROCK.
  - `src/ai/ManaPayment.cpp` ~10650-10920: `LineKeepRock` / `LineKeepRockFor()` (the rock = `PayingSpellCard()` when
    lever on and it is an untapped-entering rock), `PutLineKeepRock()`, `LineBillPayable(..., rock)`; in
    `LineReducerKeepHold` every rest judgement (witness board after `PaymentTapped`, a2, the non-keep_all branch,
    the joint check, hold-out boards, oracle-option boards) gets the rock on the board; the `PaymentManaCovers` joint
    pre-test is skipped when a rock is credited (LineBillPayable does its own on board+rock). Counters
    `[line-keep-rock] rock_trials= rock_fired=` under `MTG_LINE_REDUCER_KEEP_STATS=1`.
  - `test/unit/test_line_keep_rock.cpp` (added to CMakeLists after test_line_keep_all.cpp): Dragons s30094937 board
    (Mountain#32, Mind Stone#29, Gruul Turf#13; line {Dragonspeaker Shaman, Mind Stone}) on/off; KEEP_ALL-alone
    cannot fire it; inert control + no-rock line not bound. UNVERIFIED: the off arm assumes the greedy strands; if it
    does not, rework the test from the TAPDBG traces.
  - `docs/design/rock-in-plan-2026-11-19.md`: skeleton only.

## Mechanism (hypothesis, strongly supported by code reading; NOT yet confirmed by trace)

* `TurnSolver::BatchPrepayMainCasts` (TurnSolver.cpp ~67490) DECLINES the whole-turn joint prepay on any line with a
  producer cast (`a.ritual_float > 0 || a.rock_mana.Total() > 0` -> `PP_PRODUCER`, unless `MTG_PREPAY_PRODUCER=1`,
  default off, closed OFF 2026-09 per docs/design/search-recoverability-audit.md 4b). So every multi-cast line that casts
  Mind Stone / Sol Ring / a Signet is paid by the per-cast greedy with no joint allocation.
* The only joint-keeping net for per-cast payments is the line-keep rung `LineReducerKeepHold` (ManaPayment.cpp),
  gated in `TapForCostSharedCore` (~11190) on `g_line_reducer_credit > 0 ? LineReducerKeepOn() : g_line_keep_bound`.
  Rock lines have no reducer credit and `MTG_LINE_KEEP_ALL` is OFF (USER decision 2026-11-19, Alchemist +3/36000), so
  the rung never runs: Stuffy s30003847 T4 {GC W, Mind Stone 2, Loran's Escape W} on Plains + Garrison (RW) + Mind
  Stone: the greedy pays Mind Stone with Garrison's {R}{W} and Loran's Escape's {W} meets two Mind Stones.
* Second half (the Dragons residual named in docs/design/line-keep-all-2026-11-18.md "Residual"): even when the rung
  runs, it judges the rest on the board BEFORE the rock resolves, so a rest that needs the new rock's mana (Dragons
  s30094937 {Dragonspeaker Shaman 1RR + Mind Stone}: 5 mana with the new Mind Stone for a 5-mana line) reads
  "joint unpayable" and never fires. `RampLaterPayable` (ManaPayment.cpp ~10189) already has the right rule (the
  paid-for rock on the rest board); the rung lacked it.
* ORACLE-GAP s30000027 T4 ("Mind Stone#33 tapped by oracle only; float engine - oracle {C}") NOT analysed: likely
  oracle-side (no candidate order/payment leaving the new rock untapped). Needs the oracle=3 trace.

## Running job

* Chain: `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock
  <worktree>/logs/rock-in-plan/chain_build_repro.sh v1 manifest_repro.json` -- flock PID 3219269 (bash wrapper
  3219268), WAITING ON THE BOX LOCK at handoff time (not started). Monitor task b5zp8w19j and waiter bhuim0ybj in the
  old agent will die with it; re-arm on `v1.done`.
* What it does: `./build.sh` (builds ce7d362a4), `build/Release/mtg-test -tc="Line keep*"` -> `v1.units_lk.txt`, then
  `run_repro.sh v1 manifest_repro.json`: (a) `MTG_MANA_ORACLE=2 mtg --batch manifest_repro.json` (381 jobs: every
  flagged Stuffy game (18) + Dragons game (109) x arms off / lka (MTG_LINE_KEEP_ALL) / rock (MTG_LINE_KEEP_ROCK)) ->
  `v1.batch.out/.err`; (b) 12 single repros (Stuffy 3847 T4, 27 T4, 4614 T5; Dragons 94937 T4, 93961 T4, 93693 T4)
  x {MTG_LINE_KEEP_ROCK=0,1} with `MTG_TAPDBG=1 MTG_PREPAY_PROBE=1 MTG_LINE_REDUCER_KEEP_STATS=1 MTG_MANA_ORACLE=3`
  -> `v1.<arm>.<Deck>.s<gi>.out/.err`.
* Status file `v1.status`, DONE marker `v1.done` (content `DONE`, or `FAIL` if the build failed; see `v1.build.log`).
* Counting flags: `grep "^\[mana-oracle\] \(APPLY-DROP\|APPLY-DROP-ORDER\|ORACLE-GAP\) seed=" v1.batch.err` and split by
  `job=<arm>__<deck>__<gi>`.

## Measurements so far

* Baseline gate flags (claude-queue-20261119g2, stuffy): APPLY-DROP 45, APPLY-DROP-ORDER 14, ORACLE-GAP 3 over 18
  games (`stuffy_flags_base.txt`). Dragons (claude-queue-20261119b): APPLY-DROP 218 (199 name Mind Stone/Sol Ring),
  ORACLE-GAP 152, 109 games (`dragons_flags_base.txt`).
* Rock decks in the suite (`rock_decks.json`, `logs/rock-in-plan/rock_decks.py`): angels bruna dragons extraturns
  fivecolour fungusv3 galadriel giants hinata kittyv3 merfolkturns niv sea stompy stuffy whiteknights (+ snow's
  Astrolabe, excluded as a converter).

## Remaining, in order

1. When `v1.done`: check the build and `v1.units_lk.txt`; read the TAPDBG / PREPAY_PROBE traces to CONFIRM the
   mechanism (prepay decline PP_PRODUCER -> greedy strand); fix the unit test if the off arm does not strand.
2. Count flags per arm in `v1.batch.err` (off vs lka vs rock). If rock clears the Stuffy/Dragons rock drops, proceed;
   analyse the ORACLE-GAP s27 from its oracle=3 trace (oracle-side fix if so, no GT).
3. Full units (`build/Release/mtg-test`).
4. Held-out A/B: `manifest_ab.json` ALREADY GENERATED (`mk_ab.py manifest_ab.json 660000`: 1960 jobs, 24,500 pairs:
   stuffy/dragons 5000, sea 500, other rock decks 1000, controls burn/goblins 500), one pooled batch under the lock:
   `MTG_RSS_CAP_GB=40 MTG_PGO=0 build/Release/mtg --batch <S>/manifest_ab.json --threads $(nproc) --game-log-dir
   <S>/logs_ab > ab.out 2> ab.err`; table via `python3 logs/rock-in-plan/compare.py <S>/logs_ab <S>/ab.out`.
5. Every slower game at d8 b0: `mk_d8.py <compare output> manifest_d8.json 660000`, one batch. Root-cause any slower
   game that does not recover.
6. If the bar holds (every deck net <= 0, every slower recovers): flip `MTG_LINE_KEEP_ROCK` default ON (EngineFlags.h +
   HeuristicArm comment), rebuild (wait ~200 s after edits), units, smoke + regression from the worktree
   (`MTG_PGO=0 MTG_RSS_CAP_GB=40 bash test/regression.sh --smoke` / regression), REPORT per-deck config changes to the
   parent; DO NOT accept GT. Re-run step 2's batch with the default-ON binary for the before/after flag counts.
7. Finish `docs/design/rock-in-plan-2026-11-19.md`; commit; push `wip/rock-in-plan` only.
8. If the bar fails: root-cause, report as a USER decision with numbers + recommendation (note KEEP_ALL is OFF by
   USER ruling; this lever is the rock-line scope of the same rung + the rock-aware rest).

## Original brief (verbatim)

Fix an engine mana-payment defect family found by the mana-clean gate in the MagicDeckTester repo (C++ MTG simulator). Read CLAUDE.md first (binding rules), then .claude/skills/mtg-rules.md as needed. Base your work on origin/phase-1-2-deck-analyzer (93634781f or later); branch wip/rock-in-plan.

THE DEFECT FAMILY: a mana rock cast EARLIER IN THE SAME PLAN is not used, or is used wrongly, to pay for later actions. The engine then DROPS a later cast that a legal payment pays (APPLY-DROP), or the oracle cannot reproduce the engine's own outcome (ORACLE-GAP).
- Gate report: logs/mana_clean/claude-queue-20261119g2/report.md (in the primary checkout /workspaces/MagicDeckTester). Stuffy Doll has 45 APPLY-DROP, 14 APPLY-DROP-ORDER and 3 ORACLE-GAP, nearly all on plans that cast Mind Stone mid-plan.
- Top repros:
  - s30003847 gi3847 T4 m1 `{Guilty Conscience + Mind Stone + Loran's Escape}`: the engine drops Loran's Escape; the payment is pay1[Boros Garrison:R Mind Stone:C], pay2[float only], pay3[Plains:W]. Note Boros Garrison (a bounce land producing {R}{W}) and floating mana.
  - Several T5 `{Loran's Escape x2 + Mind Stone}` variants.
  - ORACLE-GAP s30000027 gi27 T4 m1 `{Inspiring Vantage + Guilty Conscience + Mind Stone + Mogg Maniac}`: "Mind Stone#33 tapped by oracle only; float engine - oracle {C}".
  - Each flag's repro command line is in the report: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=<T> build/Release/mtg '<deck>' --profile '<profile>' --games 1 --seed <S> --game-index <G>`.
- The handoff queue also lists a Dragons "Mind Stone / Sol Ring rock APPLY-DROP" residual of the same shape. Check whether it shares the mechanism; see docs/design/handoff-mana-chain-to-claude-2026-11-17.md (grep "Dragons" and "Mind Stone") and earlier gate reports under logs/mana_clean/.
- Precedent: a similar earlier defect was "the plan-menu mana gate counts sac-for-mana fodder only for an outlet already on the battlefield" (docs/design/goblins-sac-mana-oracle-2026-11-18.md, and MTG_SAC_FODDER_MENU). Look at how ManaPayment / the plan's payment sequencing treat a permanent that entered earlier in the same plan, and floating mana between casts.

STEPS:
1. Reproduce 2-3 flags and root-cause with the engine's payment trace. Name the exact code and mechanism.
2. Fix it in the engine (no greedy pick inside the search window; heuristics only in a DecisionProvider and only to restrict/order). A sound fix that only makes legal payments findable is a correctness fix. If it changes play, it ships default ON only with the full bar:
   - a held-out A/B on the decks that move (scripts or the batch harness; the adoption bar: every slower game recovers at --depth 8 --budget-ms 0, and every deck's aggregate net <= 0);
   - units, plus a new unit test;
   - smoke + regression from your worktree, reporting the per-deck changes for the parent to accept.
   Do NOT accept GT yourself.
3. Show the repro flags disappear: re-run the gate's flagged seeds with MTG_MANA_ORACLE=2 or 3 and count.
4. A design note in docs/design/.

MACHINE RULES (strict):
- Box lock: every build and engine run goes under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock <cmd>`, with MTG_RSS_CAP_GB=40. The lock is shared and not FIFO.
- Inside a hold, use the whole machine: pooled `mtg --batch` with --threads $(nproc), or parallel processes whose per-process MTG_RSS_CAP_GB sum to <= 36. Batch your single-game repros into ONE hold, run in parallel. Never hold the lock with a single-threaded or open-ended job.
- ./build.sh only; wait ~200 s after edits before building.
- Scratch in /workspaces/MagicDeckTester/build/scratch/claude_rock_plan/.
- No `timeout`. Waiters key on a PID or a DONE marker, never `pgrep -f`, never polling turns.
- Rebase, never merge. Push only wip/rock-in-plan; NEVER push phase-1-2-deck-analyzer. Never touch references/.
- Flags: EnvOn("MTG_X", true) per .claude/skills/coding-conventions.md.

CONTEXT: read narrowly (grep/sed ranges, tail logs; never whole large files). At ~250K tokens, finish the step, commit, write logs/rock-in-plan/handoff.md (done with hashes, remaining in order, this brief verbatim, running jobs with PIDs/markers, measurements) and stop. Never exceed 400K.

FINAL REPORT: mechanism, fix commits, flag counts before/after on the repros, A/B table per deck (faster/slower/net, CPU), units/smoke/regression results with which configs changed, and any USER decision (with numbers and a recommendation).
