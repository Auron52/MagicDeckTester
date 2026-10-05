# Why the regression suite got slow: cost breakdown and proposals (2026-10-05)

This is a standalone report. It answers the user's complaint that the regression suite has become
slow. It separates CPU contention from real growth in work, names where the time goes, and ranks
fixes. **Nothing here is adopted.** The search and GT items are user decisions.

## TL;DR

- **The overnight tier is about 39 minutes on a clean box at `--threads 20`.** That is 12.4 core-h
  of game time, or 37.3 min of batch, plus 1.9 min of serial keep-table building in a fresh
  worktree. The recorded **12m52s** (2026-09-26) was measured on a **32-thread** box with a
  **309-job** matrix. Today's ">60 min" run also took a ~2.4x contention hit (another container's
  builds).
- **The growth, attributed** (minutes of wall at 20 threads):

  | cause | minutes |
  |---|---|
  | six decks added since 09-26 | **+15.5** |
  | 32 -> 20 threads | **+7.4** |
  | keep-table rebuild per fresh worktree | +1.9 |
  | Hinata no-greedy purge `d1832b52` | +1.5 |
  | Snow `MTG_ACT_LINE_HOLD` | +0.6 |
  | scheduling / tail | ~0 |

  Selesnya and Soldiers alone are **31% of the overnight tier** (3.8 core-h).
- **Scheduling is not the problem.** The per-game LPT pool packs every tier to within one game of
  the ideal (Sigma core-ms / threads). The worst single game in the tier is ~44 s.
- **Two of the biggest problems are outside the game batch:**
  - The **reference-replay gate takes 5m40s of the ~10 min regression tier**. **One reference**
    (`EldraziDisplacerFlicker/claude_s8_gi7.json`, 360 s) is the entire critical path; the other 441
    refs finish in parallel long before it.
  - The **keep-table cache is keyed by the sidecar's absolute path**. Every new worktree therefore
    rebuilds ~26 tables **serially, before any game starts**: 114-119 s, which is **47% of today's
    4m13s smoke**.

## Method

| source | what it gives |
|---|---|
| `/home/vscode/wt/dwalin/test/logs/{smoke,regression}/batch.{log,err}` (2026-10-05 09:16-09:25, `--threads 20`, box otherwise idle) | full smoke and regression tiers, per-job ms + `units` |
| **New: a 10%-sampled overnight tier** (every one of the 385 jobs; searched jobs at `ceil(games/10)`, first games of each seed; d0 jobs in full) run alone on the box, `--threads 20`, `MTG_SLOW_GAME_MS=1` so every game's ms+units is logged | per-deck overnight cost, scaled x10; per-game durations for the makespan simulation |
| **New: the same sample for soldiers/selesnya/snow/fivecolour with `MTG_BOTTOM_EVAL_DEPTH=0`** | share of units spent on bottoming trial games (fivecolour = control: keep table, so it must not move; it did not, units identical 1.00x) |
| **New: timed `viewer_protocol_check.py --strict --threads 20`, plus a per-reference timing wrapper** | reference-gate wall and critical path |
| `test/logs/overnight/batch.log` in the main tree (2026-09-11, 268 jobs, no units) | historical per-deck overnight cost |
| `docs/design/hinata-perf-regression-2026-10.md` (hinata-perf worktree) | Hinata's cost growth, bisected to `d1832b52` |
| `docs/design/snow-cost-2026-10-01.md` | Snow +27.4% from `MTG_ACT_LINE_HOLD` |

**Contention control.** `units` are deterministic, so they are the work metric. ms/unit is the
contention detector. At 20 threads the regression run's batch wall (284 s) equals Sigma game-ms / 20
(282 s), so those ms are clean. Today's soldiers-analysis overnight read 2.36x the ms of a clean
rerun at identical units (hinata-perf report). It was **not** used for magnitudes, and its batch
log was lost in the `/tmp` wipe.

**Makespan simulation.** A list-scheduling replica of `BatchRunner`'s `stable_sort` (weight, then
resolved depth, then resolved budget, then profile path, then job index; per-game items) is fed the
measured per-game ms. On the real regression run it predicts 287 s against 284 s measured.

## 1. Where the time goes

### Overnight (385 jobs; clean full-tier estimate = sample x10; 20 threads)

| deck | core-h | share | units | searched games | ms/game | 09-11 core-h |
|---|---:|---:|---:|---:|---:|---:|
| **selesnya** (new 10-01) | **2.40** | **19.3%** | 583M | 6000 | 1441 | -- |
| **soldiers** (new 10-04) | **1.39** | **11.2%** | 340M | 6000 | 834 | -- |
| snow | 0.99 | 7.9% | 139M | 1000 | **3548** | -- |
| hinata (+2hg) | 0.98 | 7.8% | 159M | 3200 | 1097 | 0.53 |
| th | 0.74 | 6.0% | 181M | 8000 | 335 | 0.85 |
| creature_giving | 0.52 | 4.2% | 100M | 6000 | 315 | 0.64 |
| kitty | 0.52 | 4.2% | 97M | 6000 | 311 | 0.73 |
| pirates (new 09-26) | 0.48 | 3.9% | 93M | 6600 | 262 | -- |
| giants (new 09-26) | 0.44 | 3.5% | 120M | 6000 | 262 | -- |
| burn | 0.43 | 3.5% | 114M | 8000 | 193 | 0.57 |
| auras | 0.41 | 3.3% | 104M | 8000 | 186 | 0.57 |
| fivecolour | 0.41 | 3.3% | 62M | 3200 | 460 | **3.41** |
| kittyv2 (new 10-02) | 0.29 | 2.3% | 42M | 1500 | 699 | -- |
| 16 other decks | 1.46 | 11.7% | | | | |
| **total** | **12.44** | | 2.2G | | | 11.20 |

- **Total work is only ~11% above 09-11.** FiveColour fell from 3.4 to 0.4 core-h and paid for most
  of the new decks. The added decks are now 41% of the tier.
- **Critical path.** In the simulation the overnight finishes at 37.3 min against an ideal of 37.3
  min at W=20; the last item to land is a d0 game. At W=24 and W=32 it is 31.1 and 23.3 min,
  assuming equal per-thread speed. That assumption is optimistic on this box, an i9-12900K: 8 P-cores
  with SMT plus 8 E-cores, so its 24 "cores" are not 24 equal cores. The longest games are Snow d5
  (44 s, 27 s, 21 s), Selesnya d5 (24 s, 20 s) and Hinata d3 (17 s). None comes close to setting the
  makespan.
- Measured wall of the sample itself: 355 s = **114 s keep-table build** + 241 s batch (ideal 232 s).

### Regression (160 jobs, measured, 20 threads): batch 4m44s + reference gate ~5m10s ≈ 10 min

| deck | core-s | share | units | vs 09-23 log | vs `suite_cost.json` |
|---|---:|---:|---:|---:|---:|
| hinata | 928 | 16.4% | 39.9M | **2.32x** | 2.54x |
| snow | 778 | 13.8% | 27.9M | **1.66x** | 1.87x |
| selesnya (new) | 511 | 9.1% | 31.9M | -- | 0.92x |
| soldiers (new) | 329 | 5.8% | 22.3M | -- | 0.38x |
| fivecolour | 324 | 5.7% | 13.6M | 1.03x | 1.07x |
| kitty | 257 | 4.6% | 13.2M | 1.07x | |
| creature_giving | 232 | 4.1% | 11.3M | 0.95x | |
| fungusb (new, transition) | 197 | 3.5% | 8.3M | -- | |
| kittyv2 (new, transition) | 138 | 2.4% | 5.3M | -- | |
| others (21 decks) | 1546 | 27% | | ~1.0x | |
| **total** | **5641** | | | 4229 at 09-23 | 5114 cached |

The tier's wall clock, measured from file mtimes:

- scenarios: 8 s;
- batch: 284 s (simulated ideal 282 s);
- **compare, reference gate and audit: 309 s.**

The skill's "~5 min" for this tier is the batch alone.

### Smoke (114 jobs, measured, 20 threads): 4m13s

- **Game work is 0.69 core-h, i.e. 125 s ideal.** Snow 21%, Hinata 12%, Selesnya 11%, Soldiers 7%.
- **The other ~119 s is keep-table building.** It was the first run in the fresh `dwalin` worktree;
  the regression run that followed in the same worktree built nothing. The simulated batch is 136 s,
  measured 253 s.

### The reference gate (regression tier only)

- `viewer_protocol_check.py --strict --threads 20`: **5m40s wall, 864 CPU-s, 253% CPU.** The pool
  is mostly idle.
- Per reference, 442 refs: summed 2,060 s, so the ideal at 20 threads is 103 s.
  **`references/EldraziDisplacerFlicker/claude_s8_gi7.json` takes 360 s on its own** (71 decisions).
  Each decision is a fresh stateless `--claude-play` replay from turn 1 with `MTG_PLAY_PLANS_CAP=0`
  (uncapped plan emission), so the cost is quadratic in decisions.
- The next slowest: KittyEquipment v2 refs (80, 64, 61, 61, 56 s) and EDF s7_gi6 (66 s).
  EldraziDisplacerFlicker is not a suite deck.

## 2. Growth since the recorded 12m52s, attributed

The 309-job matrix behind the 12m52s is today's 385 jobs minus 76:

| deck | jobs |
|---|---:|
| pirates | 16 |
| fungus | 12 |
| giants | 12 |
| kittyv2 | 12 |
| selesnya | 12 |
| soldiers | 12 |

**Cross-check.** Remove those decks' 5.15 core-h, the Hinata purge's 0.51 and Snow's
ACT_LINE_HOLD 0.21 from today's 12.44 core-h. That leaves **6.57 core-h**, which at 32 threads is
**12.3 min**, against **12m52s recorded**. The decomposition closes.

| cause | core-h | wall @20 thr | evidence |
|---|---:|---:|---|
| **New decks** | **+5.15** | **+15.5 min** | selesnya +2.40 (`a32f0653`, 10-01), soldiers +1.39 (`81b2b435`, 10-04), pirates +0.48 / giants +0.44 / fungus +0.15 (the 09-26/27 pooled run), kittyv2 +0.29 (`6f90f520`, 10-02) |
| **Core count 32 -> 20 threads** | 0 | **+7.4 min** | the 6.57 core-h base runs 12.3 min at 32 threads and 19.7 min at 20 |
| Keep-table rebuild in a fresh worktree | 0 | +1.9 min | serial in `ParseJob` (BatchRunner.cpp:818 -> `ProfileCache` -> `BuildKeepTable`): 114 s / 119 s measured twice |
| Hinata no-greedy purge `d1832b52` | +0.51 | +1.5 min | 2.07x units pre-purge -> tip on these 12 cells (hinata-perf report; rung 1 runs unbudgeted, guarded only by the 25x overrun) |
| Snow `MTG_ACT_LINE_HOLD` (09-30) | +0.21 | +0.6 min | +27.4% (`snow-cost-2026-10-01.md`) |
| Other decks' drift | ~0 | ~0 | most decks are 0.7-1.0x of 09-11 |
| Scheduling / tail / job granularity | 0 | **~0** | the pool is per-game; simulated makespan = ideal; longest game 44 s |
| **Clean total** | 12.44 | **≈ 39 min** | |
| **Contention (today)** | -- | x~1.6-2.4 on wall | soldiers-run ms were 2.36x a clean run at identical units; another container ran 4 parallel builds, then an OOM. 10-04's run: 50m14s at 24 threads |

**Why Selesnya and Soldiers cost so much.** Both were sized at **"Pirates' counts"**: overnight is
d3 b20 x1000 and d5 b40 x500 at 4 seeds, i.e. 6000 searched games each. When Selesnya was admitted,
Pirates was believed to be ~0.7-1.3 s/game. Neither deck ships a keep table, so every mulligan falls
through to lookahead bottoming.

**Measured bottoming share of overnight units** (`MTG_BOTTOM_EVAL_DEPTH=0` arm, 10% sample;
fivecolour control unchanged):

| deck | d3 | d5 |
|---|---:|---:|
| selesnya | **75%** | **79%** |
| soldiers | 57% | 53% |
| snow | 35% | 41% (matching the 59.6%-of-charged-units finding's direction) |

Soldiers' own sanity batch (`analysis-soldiers.md` 5b) shows d3 b10, d3 b20, d5 b20 and d5 b40 all
at avg 4.3733 (or 4.3717) on 600 games. Its overnight's doubled budgets buy no measured quality.

## 3. Should the suite_cost gate have caught this? Why it didn't

**It could not, by design.** `scripts/suite_gate.py` is a one-time **admission** gate on the **per-game**
cost of a deck's worst searched **regression-tier** cell: it must be within 3x the most expensive deck
that has both a value leaf and a keep table. Five gaps follow:

1. **It is invariant to game counts and budgets, deliberately.** The new decks' cost lives in the
   overnight rows' counts. Per game, Selesnya is 1.33x and Soldiers 1.21x fivecolour, both well
   inside 3x. Per tier, they are 5.9x and 3.4x fivecolour's overnight total. No tier-total budget
   exists. The tier "budgets" (15 min / 45 min / 8 h) are ceilings far above what the user
   tolerates, so 40-60 min is "in budget".
2. **It never looks at the overnight tier.** `TIER = "regression"`.
3. **It never re-measures.** `suite_cost.json` is written at admission. Engine changes to existing
   decks are invisible:
   - Hinata went from 0.58x to **1.47x** fivecolour (the purge).
   - Snow sits at **6.85x**, more than double the 3x line. It predates the gate and was never
     re-checked.
4. **The cached measurement is noisy in both directions.** Small filtered runs inflate ms per game
   2-4x. Soldiers passed "borderline" at 2.98x on 3,078 ms/game; it is 1,108 in a pooled run.
   kittyv2's cached 1,649 is 806 today.
5. **It measures ms, not units.** On a contended box the number is not a property of the deck.

## 4. Proposals, ranked

Savings are at `--threads 20` on a clean box, overnight unless noted. **[U]** means it needs a user
decision: search, GT or a sample change. **[L]** means lossless infrastructure with no GT change.

| # | proposal | saves | kind |
|---|---|---|---|
| 1 | **Keep the box uncontended, and make contention self-reporting.** Add expected-vs-achieved µs/unit, or "projected clean wall = Sigma units x reference µs/unit / threads", to the batch summary and the heartbeat, so a 2.4x-contended run says so in its own output. Do not run a tier while builds or another batch share the box. | today's 60+ min becomes ~39 (x1.6-2.4 removed) | [L] process + small runner change |
| 2 | **Fix the reference-gate critical path.** Options: run `viewer_protocol_check.py` in the background *during* the batch (its 864 CPU-s is ~4% of the regression batch); make the slow replays incremental rather than O(n²) restarts; or re-record or cap `EldraziDisplacerFlicker/claude_s8_gi7` (a non-suite deck). LPT-ordering refs alone cannot help: one ref is 360 s. | **regression tier ~10 -> ~5.5 min** (-4 to -5 min every regression run) | [L] harness (re-recording a ref is the user's call) |
| 3 | **Keep tables (mulligan profiles) for Selesnya, Soldiers and Snow.** These are the documented next pipeline stages: Selesnya has a value leaf; Soldiers needs value leaf -> mulligan, strictly serial; Snow's keep profile is reportedly being generated on the secondary machine. Bottoming trial games are 77% / 55% / 38% of their units. | ≈ **-3.0 core-h ≈ -9 min (-24%)**; regression batch ≈ -45 s; smoke ≈ -30 s | [U] pipeline (moves GT; adoption is a user review) |
| 4 | **Resize the Selesnya/Soldiers overnight rows.** Copying "Pirates' counts" at 2x budget made them 31% of the tier. Options: halve the games (d3 500, d5 250) or drop to the regression budgets (b10/b20). Soldiers' 5b shows identical averages across b10-b40. | halving ≈ **-1.9 core-h ≈ -5.7 min**; less after #3 | [U] GT re-accept of those keys; trades sample size |
| 5 | **Key the keep-table cache by CONTENT, not path.** Use, for example, the sidecar's git blob or a content hash instead of canonical path + mtime, so all worktrees share one table. Or build tables in parallel or lazily rather than serially in `ParseJob`. The cache holds **7 copies of Hinata2's 525 MB table**, one per bisect worktree. | **-1.9 min per tier run in any fresh worktree**: smoke 4m13s -> ~2m15s (-47%); also several GB of cache | [L] engine infra, byte-identical play |
| 6 | **Hinata purge overshoot.** Rung 1 of iterative deepening is unbudgeted (start gate only, 25x overrun guard). Prefer the *sound* pre-apply continuation dedup (the purge doc's top perf item) over any truncation, since the user rejects lossy truncation. The d5 cells bought no measurable quality for ~2x. | up to -0.5 core-h ≈ -1.5 min overnight, -25 s regression | [U] search change |
| 7 | **Retire transition duplicates once settled** (kitty v1 / kittyv2, fungus / fungusb). | ~-0.5 to -0.8 core-h ≈ -2 min overnight; ~-6% regression | [U] list adoption |
| 8 | **Gate the tier, not just the deck** (fixes §3). (a) Record `units` per GT key on `--accept`, and have `regression.sh` print a per-deck **cost diff vs GT units**, flagging any deck past ±15%. This would have shown Hinata's 2.07x in the purge commit's own suite run. (b) Add a tier makespan target computed from units, e.g. smoke ≤ 3 min, regression batch ≤ 5 min, overnight ≤ 20 min at 20 threads, and make admission price **all three tiers' rows** of the new deck against it. (c) Periodic `suite_gate.py --measure-all`, pooled, on units. | prevents recurrence | [L] harness |

**Not recommended:**

- **Job splitting.** Work items are already per game.
- **Largest-first reordering.** The depth/budget LPT proxy already packs every tier to within one
  game of the ideal.
- **Raising `--threads` past 20.** The BOX-LIMITS rule caps it, and the extra hardware threads are
  SMT siblings and E-cores.

**Achievable picture, clean box, 20 threads:**

| tier | today | after #1 #2 #5 (lossless) | after #3 #4 too (user decisions) |
|---|---|---|---|
| overnight | ~39 min | ~37 min | ~22-25 min |
| regression | ~10 min | ~5.5 min | |
| smoke | ~4 min | ~2 min | |

## Artifacts (scratch, not committed)

- Sample run: `logs/suiteperf/ov10/` and `logs/suiteperf/ov10_nobot/` in the `suiteperf` worktree.
- Reference-gate timing: `logs/suiteperf/vpc*.{out,time}`.
- The manifest generator mirrors `regression.sh`'s emission byte-for-byte; it was verified
  identical on the smoke manifest.
- The keep tables this run built were deleted afterwards (7.9 GB) to respect the cache limit.
