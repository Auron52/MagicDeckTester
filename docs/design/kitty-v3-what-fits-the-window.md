# Kitty v3: what fits the window, and why the keep table does not

**Status: DECISION RECORD, 2026-10-09.** The v3 keep (mulligan) table is **DEFERRED**; the **value
leaf** is the stage being generated instead. Every cost number below is measured, and two of them
refute hypotheses this repo was about to spend an overnight on.

## The question

The v3 keep generation was killed at 9.4 h with 13.5–58 h still to run (user: *"That's crazy to wait
this long just to hit R=10"*). The follow-up question was narrower and had a deadline attached: with
roughly two hours of setup time and one overnight, **which significant generation — mulligan profile
or value leaf — can actually be launched and complete?** 24 h+ was ruled out explicitly.

## The cost is a tail, and now it is exactly quantified

From the killed run's own slow-rollout log (`*.raw.json.slow.log`, streams every rollout ≥ 30 s and
cannot be disabled):

| hand size | slow rollouts | CPU time | mean | worst |
|---|---|---|---|---|
| size7 (keep cells) | 3,971 | **155.2 h** | 140.7 s | — |
| size6 (bottoming sub-cells) | 1,987 | **60.3 h** | 109.3 s | **935,944 ms = 15.6 min** |

**215.5 CPU-hours in 5,958 rollouts — 3.4% of the 176,099 banked records holding ~72% of a 9.4 h ×
32-core box.** Every slow hand holds Colossus Hammer plus Gauntlets or Deconstruction Hammer and no
Puresteel. Note that size-6 sub-table (bottoming) rollouts are a full 28% of that, which earlier
accounts of this blowup missed by looking only at size-7 cells.

## TWO MORE HYPOTHESES REFUTED BY MEASUREMENT (do not re-run these)

Both at N=2000 stride-sampled size-7 floor rollouts, d1/b3, one Release binary, paired against the
established baseline (2400.45 CPU ms/rollout, digest `d8f658484e946613`). N=2000 and not N=64 because
at N=64 `p99 == max`, so the sample cannot see the tail at all and the baseline reads 3x cheaper than
production. Script: `logs/kittyv2_swaps/bench_fit.sh`.

| | baseline | **A: `MTG_FOLD_SEARCH_ODO=1`** | **B: value leaf attached** |
|---|---|---|---|
| CPU ms/rollout | 2400.45 | 2294.88 (**0.956x**) | 2460.64 (**1.025x**) |
| p50 / p90 | 337.1 / 2440.2 | 336.5 / 2469.2 | 341.4 / 2481.7 |
| p99 / max | 45,167 / 245,198 | 42,430 / 237,483 | 47,739 / 260,225 |
| units/rollout | 8,346.17 | 8,307.01 | 8,452.55 |
| slowest 1% / 10% share | 49.17 / 82.43% | 48.49 / 81.49% | 49.75 / 82.59% |
| win-turn digest | `d8f658484e946613` | **`d8f658484e946613`** | `ff51033b1482aa03` |
| mean win turn | 5.42 | 5.42 | 5.42 |

**A — the canonical-prefix fold on the search's own subset walk. A NULL.** The hypothesis was that
kitty should benefit more than Snow (the only deck that opts in), because kitty has far more
interchangeable cards: 16 zero-cost shields in 4 equivalence classes, 4 Gauntlets, 4 Hammers, 4
Puresteel, 4 Sram, against the 35.2% of Snow base plans that differed only in which interchangeable
card they touched. A *prediction was registered before the run*: if the reasoning held, the fold would
move p99/max, because in the tail the cost sits in the walk the budget never bounds and so cannot be
re-spent going deeper. It moved p99 by 6% and max by 3%. **The prediction failed and the hypothesis is
dead.** What the arm does establish is that the fold is **play-identical on this deck** (same digest,
same mean win turn) while removing 0.5% of units — a sound collapse worth adopting on its own terms
under the collapse-unconditionally rule, but not a plan.

**B — attaching a value leaf. A NULL, and slightly negative.** The hypothesis was structural and
looked strong: the killed run executed OUT of the mandated stage order (profile → value leaf →
mulligan) — v3 ships no `.value.json` at all — and the value leaf replaces the search's horizon
rollout with an O(1) evaluator, while the beyond-horizon equip-route DFS that this blowup traces to
lives *inside* that horizon playout. `value_min_depth = 0` in v2's table, so the leaf really is
consulted at d1. It measures **1.025x — marginally worse**, with mean win turn unmoved at 5.42.

**Why B fails, which is the useful part.** At d1/b3 the budget is 3 virtual ms ≈ 2,700 units, and the
measured spend is ~8,350 units per rollout. The cost is therefore not at the horizon at all; it is in
the **in-window subset enumeration at the root**, which neither an O(1) leaf nor a redundancy fold
touches. That is the uncharged greedy subset walk of
`docs/design/slow-rollout-tail-and-the-uncharged-greedy-walk.md` — a diagnosis the user has
explicitly closed as a route (*"I don't really want to revisit the 'we aren't budgeting right!'
line"*). So the two cheap levers both miss, and the thing they miss is the thing we are not pursuing.

HONEST LIMIT on arm B: it attached **v2's** sidecar to **v3's** list, so it is a cost probe only —
v3's four changed cards are unscored by an eval_model trained on v2's rows. It answers *"does a value
leaf make these rollouts cheap"* (no), not *"is this the model to ship"*.

## Six hypotheses are now ruled out

1. The cleanup-discard bucket fix — matched-window A/B: ~12% **faster**, not the cause.
2. The 128-link beyond-horizon equip-route DFS — 1.62x more units for the *same* CPU and less wall.
3. The list — a fixed 64-game play-digest battery gives v2-list/v2-engine 5 s, v2-list/today 137 s
   (27x), v3-list/today 45 s. 100% engine; the four card changes **helped**.
4. `MTG_DECISION_WORK_X` — ruled out by standing user policy, twice.
5. `MTG_FOLD_SEARCH_ODO` — 0.956x (above).
6. The value leaf as a generation-cost lever — 1.025x (above).

The cause remains the equip-chain go-off capability, which **did not exist** at v2's generation engine
(`namespace equiproute` and `EquipRouteEnabled`: 0 occurrences at `8d165350`). It is a new capability
that demonstrably improves play — it took kittyv2's reference shortfall from 3/14 to 1/14 — not a
regression to revert.

## Why the value leaf fits the window and the keep table cannot

This is a question about **bounds, not averages**, and it is the whole decision.

**The value-leaf generator is bounded by construction.** From v2's own run (`driver.log`): phase C
carries a *"wall-clock backstop: predictive cut at 120 min, hard cap at 210 min"* per cell, a per-game
unit ceiling (`abandon_units` 40,000,000, `abandon_k` 25x the median of the first 25), and a **phase
C.5 risk gate** that proves the abandoned games cannot have changed a verdict — it returned **CLEAN**
at 0–1.62% abandonment. Phase A's rows are *durable at every instant* and a documented `finish`
subcommand accepts whatever is banked and runs B..E on it, so the run cannot overrun the window
without a clean exit.

**The keep generator has no such bound.** One cell-side ran 15.6 minutes unimpeded; nothing in the
generator can stop it, `kMinProfileR = 10` is a hard floor *"deliberately NOT env-overridable"*, and
the only sub-floor output is a poolable raw chunk rather than a shippable profile.

**And the precedent is the right shape.** v2's full leaf generation ran 2026-10-04 16:48 → 10-05
08:02, **~15 h wall**, frozen at `bf67c6fa` — which is literally the commit *"price an equip-route
link at 8 units"*, i.e. **already a post-equip-route engine**, not the cheap pre-route one. Phase
breakdown: A 3 h 42 m (closed with `finish`), C 8 h 52 m, E 1 h 05 m, F 1 h 29 m. v3's list measures
**3x cheaper than v2's** on today's engine, and PGO+LTO adds 1.32x. So an overnight is the right size
for it.

## What the leaf is worth on its own, now that it is not a cost lever

Arm B removed the enabler argument, so the justification has to stand alone. It does:

* **v2's leaf was adopted as a clean win on both axes** (`e327854b`): play −0.00775 avg win turn,
  paired t −2.17, 7/1/0 seeds, and cost **0.76x**. A play-and-cost win in *play*, which is where it
  was always measured.
* **Phase F is the mulligan-settings measurement v3 never got.** On v2 it picked `d2 b3` (rho 0.9911,
  0.375x the play cost) and recorded `expected_buckets`. Worth noting for anyone expecting a big win
  here: v2's Phase F table shows `d1 b3` at rho 0.9924 / 0.378x against `d2 b3` at 0.9911 / 0.375x —
  i.e. **the depth the killed run used was already near-optimal**, and re-deriving the settings is a
  correctness step, not a cost lever. The v3 keep bench independently confirms **K = 15**, matching
  v2's discovery.
* It is the **mandated next stage**, and the stage order exists because a keep table is fitted to the
  deck's play: a table generated before the leaf is fitted to leaf-less play.

## The one lever that would bound phase A, and whose decision it is

`MTG_VALUE_LABEL_BUDGET_MS` (default 1,000,000 virtual ms = 900M units) is the per-position label
ceiling in phase A, and the engine default *"is not a bound at all"* — on Snow, roughly two hours per
position. `scripts/valueleaf.sh` keeps a measurement as an explicit **proposal**, not a setting:

| ceiling | positions kept | hardest games | worst game |
|---|---|---|---|
| 1e6 (default) | 100% | > 1.5 h each | **"NOT FINISHING unbounded in practice"** |
| 30,000 | 90.7% (5 of 54 cut) | 310–1166 s | 19 min |
| 10,000 | — | 207–296 s (2.7x cheaper) | — |

It is reversible and additive — *"a row is only written when the search completed, so the ceiling
decides WHICH positions are labelled and never what a label says… a later run at a HIGHER ceiling is
purely additive"* — but it **changes the training set**, which makes it an estimand change and the
deck owner's call, never an agent's (USER, 2026-09-20: *"I don't want you fiddling with the general
settings here. Optimizations are good, but this is not an optimization."*). **It is NOT set for the
v3 run.** The sanctioned answer to a window running out inside phase A is `finish`.

## OUTCOME, 2026-10-10: the leaf fits and is ADOPTED; it does NOT help generation

The value leaf was generated (phases A-F) and **adopted** at
`decks/KittyEquipment/v3-adelbert-gauntlets/KittyEquipment.value.json`. Phase E, 8 seeds x 1000 games
paired: play **-0.00025, t -0.10, 4/4/0** -- a textbook null -- at **0.64x** play cost. Not worse on
any axis, which is the skill's adoption bar. Phase C.5 returned **CLEAN** (50 games abandoned of
20,499; V arm 0-0.06%, H arm 0.12-2.00%, worst H4, against v2's worst of 1.62%). Crossover
`1->1 2->1 3->1 4->2 5->3 6->6 7->6 8->6`, trust UNSET.

**NOT YET VALIDATED AT SCALE.** Phase E read NEUTRAL, and an 8-seed neutral can hide a one-sided cost
(knights: neutral at 8 seeds, then +0.00020 / t +2.74 / 0-of-16-better on 16 fresh seeds x 2500). The
fresh-seed check uses disjoint seeds **630000-669999** (16 x 2500, spaced >= `games` so jobs cannot
replay games -- tighter spacing once manufactured a -14.4 sigma). Adoption is reversible by renaming
to `.value.DISABLED.json`; a GT rebaseline is not, so **GT waits on that check**.

**HYPOTHESIS 7, REFUTED -- the leaf is not a generation lever.** Paired, same binary, N=2000, d1/b3
(`logs/kittyv2_swaps/bench_leaf_now.sh`):

| | no leaf | v3's own leaf |
|---|---|---|
| CPU ms/rollout | 2408.43 | **2465.03 (1.024x, worse)** |
| p50 / p90 | 343.1 / 2545.9 | 341.8 / 2511.3 |
| p99 / max | 45,070 / 250,104 | 47,520 / 254,295 |
| units/rollout | 8346.15 | 8452.34 |

This lands within 0.001x of arm B's earlier probe using **v2's** sidecar (1.025x), so that probe's
known defect never mattered -- the conclusion was right the first time.

## THE ACTUAL FIND: the greedy subset walk OBSERVED NO BOUND (fixed, `a28b566e`)

`gamework::CheckDeadline()` has exactly ONE caller, inside `Add()`, which runs once per simulated
turn-step. The greedy subset walk consults **no budget** -- *"The budget was not exceeded. It was never
consulted"* -- so the per-game abandon flag was being SET and then ignored by the one region burning
the time. Neither the unit ceiling nor either wall stage could reach it.

**Proven live, not argued.** Phase C stalled at 99% complete on a single game (`V8_s10010` gi=370)
that ran **4.07 h against a 3.5 h hard cap** and kept going, holding 31 of 32 cores idle for over an
hour. V8's mean is 14.5 s/game, so it reached ~970x its own cell. Worse, the cell-condemnation
fallback that would otherwise have moved on **deliberately stands down whenever a wall cap is armed**
(*"It exists ONLY because a unit ceiling does not bound time; that premise is false here"*) -- so
arming the cap removed the only mechanism that could still have rescued the run. The batch monitor
logged it 23 times: *"bounded by the WALL-CLOCK backstop, so the cell is NOT condemned."*

Fixed with a **strided** poll (`kWalkAbandonStride = 1024`): a per-visit test was measured in
2026-10-01's wsweep3 at 8-14% core-ms on this very deck for ZERO play change, so the common visit
stays an increment and a compare, exactly as `kClockStride` already does for the clock.

**Verified inert by measurement rather than by the argument for it.** The harness's own freeze check
reported *"PLAY UNCHANGED (smoke digests fold to e797ccef2499) -- keeping every game"*, preserving all
20,499 banked games across the src change; and the keep bench gives units/rollout **8346.15 against
8346.17** pre-fix, which -- units being deterministic -- is byte-identical work.
mtg-test 2,645,541/2,645,541.

**WHY IT CHANGES THE PIVOT.** This is a TERMINATION defect, not a pricing one, so it sits outside the
"we aren't budgeting right" line the user closed. And with the walk now polling, a per-rollout wall cap
can finally bound the keep generator's tail -- which is the single structural difference that let the
leaf generation fit a window while the keep generation could not. **The open design question is the
user's**, because it changes the table's content rather than its schedule: abandoning a keep rollout
leaves that cell-side unlabelled, so the cell never freezes and R never advances. Phase C solves the
same problem by backfilling; the keep generator has no equivalent.

## Where the keep table goes from here

Deferred, with the 176,099-record journal preserved in four copies
(`logs/kittyv2_swaps/killed_run_20261009T092451Z/`) and the originals left in place so a
byte-identical fix still resumes. `MTG_FOLD_SEARCH_ODO=1` is digest-identical on this deck, so
adopting it would **not** invalidate the journal — it simply is not worth an overnight on its own.

The honest remaining options, none of them tonight's business:

1. **Price the in-window equip-route enumeration.** The per-link price (`MTG_EQUIP_ROUTE_NODE_UNITS`)
   is the shape the completeness invariant blesses — `ChargeDecisionUnits` returns immediately when
   `budget == nullptr`, so it is provably inert at unlimited budget and monotone in budget below it.
   This is adjacent to the closed charging line and needs the user's ruling on whether it counts as
   re-pricing (allowed) or as the budgeting line (closed).
2. **Pool sub-floor chunks** across several windows up to R ≥ 10. No artifact until the merge, but
   every chunk is durable.
3. **Accept that v3 ships without a keep table** — it has none now, play is unaffected, and
   `suite_gate.py --cost` on v3 is still the missing measurement that would say whether the deck is
   even inside the 3x rule on today's engine.

Related: `docs/design/keepgen-degenerate-cells-are-the-no-puresteel-hands.md` (the tail's shape and
the refuted list attribution), `docs/design/slow-rollout-tail-and-the-uncharged-greedy-walk.md` (the
diagnosis the user closed), `docs/design/rollouts-only-when-needed-and-estimated-ahead.md` (the user's
general rule and `MTG_LAZY_LEAF_BUDGETED`), `docs/design/mullgen-settings-derived-by-valueleaf.md`
(Phase F), `.claude/skills/value-leaf.md`.
