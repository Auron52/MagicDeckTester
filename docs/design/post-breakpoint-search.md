# Mid-turn breakpoints are search-free zones (2026-07-28)

**Status: FIXED and ADOPTED.** The fix is `Plan::bp_choice` (below). Defaults `MTG_BP_SEARCH=2`,
**Follow-up (2026-07-28, second pass): a rollout mana-payment bug this work uncovered is fixed and is
a standalone win on held-out seeds (see ROOT CAUSE); the nested `bp_at` axis is now defect-free but
stays default-off.**

`MTG_BP_SITES=23`, `MTG_BP_MAXBASE=16`, fan-out at every ply. Ground truth rebaselined and
re-verified in all three modes: **smoke 21/21, regression 35/35, overnight 84/84 ALL PASS**
(makespan 12 s / 58 s / 3m48s). `MTG_BP_SEARCH=0` restores the pre-fix engine exactly.

Was an ENGINE-wide gap affecting Hinata, burn, Dragonstorm and TH — found on TH, but Hinata was an
order of magnitude worse by volume (see Scope). The north star (user, 2026-07-28) is **every decision
searched**, with heuristics allowed only to prune *cost*, never to decide quality.

## The finding

When a spell that creates NEW castables mid-main-phase resolves — a dig (Treasure Hunt), a staged
exile (Light Up the Stage, Apex of Power), or a plain cantrip (Ponder) — `ApplyPlanDirect` opens a
*breakpoint* and re-decides the rest of the turn. Everything after that point was decided **greedily,
with no search node at all**:

1. **The land drop** is picked by `play_drawn_flood_keep_land` → `SimulateLandPlay`
   (`TurnSolver.cpp`), a static ranker: *first multi-colour land in hand order, else first land*.
   It is blind to per-tap yield.
2. **The casts** come from `TurnSolver::Solve`, which its own header calls the "greedy path (d0
   decision + every rollout leaf)". It never calls `EnumeratePlansWithLand`, so it enumerates no land
   variants and performs no lookahead.

So the whole post-dig continuation of the turn was a d0 decision embedded inside an otherwise-searched
game. **That is why no depth and no budget could reach an alternative post-dig line** — there was no
search node there for the budget to be spent on. Not a heuristic out-voting the search; a heuristic
standing in for a search that was never invoked.

### Latent bug found on the way (FIXED)

`apply_plan_actions(plan.actions, ...)` applies only `plan.actions`. A plan's **searched land**
(`Plan::land_decided` / `land_to_play`, plus `fetch_target` / `land_face`) was silently discarded at
the breakpoint. Fixed (now `bp_play_searched_land`): the re-solve's land is played before its casts,
so their mana is available, and recorded into the breakpoint sink for commit-the-line replay. It was
**inert** while greedy `Solve` never set `land_decided` — but without it, making the breakpoint a
search node would have silently thrown the answer away.

## The fix: `Plan::bp_choice` — the breakpoint continuation is a searched plan variant

The engine already has an idiom for "a sub-decision the search owns": carry it **on the Plan** and
resolve it at apply time (`fetch_target` for fetchlands, `land_face` for MDFCs, `searched_order` for
cast order). The breakpoint continuation now uses the same idiom.

* `Plan::bp_choice` (default `-1` = the old greedy continuation, byte-identical).
* `k >= 0` means: *at the first breakpoint of this apply, play candidate `k` of
  `EnumeratePlansWithLand` — its land drop AND its casts — instead of the static ranker + greedy
  `Solve`.*
* `AppendBreakpointVariants` emits `W` such variants for every plan that opens a breakpoint, so the
  **outer rollout scores each continuation** and the search decides.

Why this shape rather than a nested search at the breakpoint:

* **Consistent by construction.** The variant is part of the plan, so the line the rollout *scored*
  is byte-for-byte the line the game *realises*. A nested search that ran only on the committed path
  would make prediction and realisation disagree (fd-diverge).
* **Free scoring.** No new search machinery, no budget/depth threading into `ApplyPlanDirect`.
* **Conservative.** Variants are appended after the sort and share their base plan's `value`, and the
  equal-win-turn tiebreak is `value` — so a variant can only win on a **strictly better rolled-out
  win turn**. Ties always go to the existing behaviour.
* **Bounded and revertible.** `W` is the knob; `W=0` restores the old engine exactly.

Guards: `g_bp_enum_depth` stops a breakpoint's own enumeration from fanning out again (only the first
breakpoint of an apply is searched — deeper ones are the leaf evaluator's own horizon), and
`g_fsline_nest` / `enforce_budget` identify the committed decision node.

### Removing the static pick alone was NOT the fix

`MTG_BP_DROP_SEARCHED=1` (diagnosis only) omits the static drop and defers to the re-solve. Measured:
**no land is played at all** and the line gets worse. The static ranker was not a restriction layered
over a working search — it was scaffolding holding up a gap. Both had to be replaced together, which
is what `bp_choice` does.

## Scope: an ENGINE gap, not a TH one (MEASURED)

Five `ApplyPlanDirect` sites ran the same greedy `TurnSolver::Solve` + `apply_plan_actions` pattern.
`MTG_BP_SITES` is a bitmask over exactly these (bit = site index):

| bit | site | breakpoint opened by | deck |
|---|---|---|---|
| 0 | `stages_cards` / Expressive Iteration | Light Up the Stage | burn, Hinata |
| 1 | `DrawUntilNonland` | Treasure Hunt | TH |
| 2 | `impulse_exile` | Apex of Power | Dragonstorm |
| 3 | `deferred_cantrip_resolve` | Ponder / Preordain | Hinata |
| 4 | dig-through-lands | sac / cycle dig | TH |

`MTG_BP_PROBE=1` counts them, split into `greedy=` vs `searched=`. Per 100 games at d5, against
the contention-free node telemetry (`MTG_ROLLOUT_STATS=1`), BEFORE the fix (all greedy):

| deck | interior_nodes | turn_steps | greedy re-solves | on committed line | greedy / interior |
|---|---|---|---|---|---|
| **Hinata** | 844,966 | 232,216 | **1,153,741** | 1,044,647 | **1.37** |
| burn | 133,283 | 4,118 | 37,531 | 36,719 | 0.28 |
| TH | 259,980 | 38,254 | 64,130 | 53,061 | 0.25 |
| Dragonstorm | 186,029 | 36,500 | 9,848 | 1,236 | 0.05 |
| slivers / Knights / Anti-Lifegain / Auras | 93k-205k | - | **0** | 0 | - |

`TurnSolver::Solve` is *legitimately* the rollout-leaf evaluator, so a greedy call at the horizon is
by design; the "on committed line" column is the subset that decides an actual chosen line.

### Reading the probe AFTER the fix: `greedy=0` is the wrong target

With `bp_choice`, a plan applied with `bp_choice = -1` still resolves its breakpoint greedily -- but
that application *is the search evaluating the greedy option*, which now sits beside W enumerated
alternatives as one candidate among W+1. So the probe's `greedy=` count stays high by construction
and is NOT a measure of unsearched decisions. Measured at the adopted defaults (40 games, d5):

| deck | site | searched share |
|---|---|---|
| TH | `DrawUntilNonland` | 16.7% |
| Hinata | stages / EI | 14.9% |
| burn | stages / EI | 9.0% |
| Dragonstorm | `impulse_exile` | 1.1% |
| Hinata | plain cantrip | 0% (pruned by default) |

The right questions are instead "does the search ever prefer a non-greedy continuation?" (yes -- that
is where every quality gain below comes from) and "is any continuation *unreachable*?" Two remain:

1. **The plain-cantrip site**, pruned for cost (see the flag below).
2. **Nested breakpoints.** Only the FIRST breakpoint of an apply is searched; a second one in the
   same turn (a cantrip chain, a second Treasure Hunt) still resolves greedily. Making `bp_choice` a
   vector would fix it, but the variant count would grow combinatorially, and the width sweep below
   already shows the budget -- not the width -- is the binding constraint. Deliberately not built.

## Measurements (2026-07-28)

Node counts are `interior_nodes` from `MTG_ROLLOUT_STATS` — deterministic and contention-free, so
they are the perf metric (not wall clock). Quality is the loss-penalised avg win turn (lower better).
Baseline = `MTG_BP_SEARCH=0` (the old engine, byte-identical: smoke 21/21 PASS).

### Per-site attribution, W=1, fan-out at every ply

| site | deck / case | quality | node cost | verdict |
|---|---|---|---|---|
| 1 `DrawUntilNonland` | TH d5 75g | 4.1333 → **4.1067** | +40% | **the win** |
| 1 `DrawUntilNonland` | TH d3 150g | 4.2200 → **4.1733** | +11% | **the win** |
| 2 `impulse_exile` | Dragonstorm d5 75g | 4.6267 → **4.6000** | **−1.4%** | **free win** |
| 0 stages / EI | burn d5 250g | 4.3640 → 4.3640 | +6% | neutral |
| 4 dig | TH d5 / d3 | unchanged | +0.7% / +4.6% | neutral |
| 3 plain cantrip | Hinata d3 150g | 5.9400 → **5.9667 (worse)** | **+26%** | **pruned** |
| 3 plain cantrip | Hinata d5 75g | 6.0667 → 6.0533 (better) | **+31%** | noise, both ways |

Site 3 (Ponder / Preordain) is the only class that costs a lot and buys nothing: it flips sign
between d3 and d5 at ~30% more nodes. That is **budget dilution**, not a modelling bug — Hinata is
the branchiest deck, and multiplying its candidate set at a fixed per-decision node budget makes
every rollout shallower. It is pruned by default (`MTG_BP_SITES` bit 3 clear) and is the one place a
heuristic prune was needed; `MTG_BP_SITES=31` restores full free rein for the benchmark A/B.

### Width, with site 3 pruned (`MTG_BP_SITES=23`), d5

| deck | W=0 | W=1 | W=2 | W=4 | nodes @W=4 |
|---|---|---|---|---|---|
| TH | 4.1333 | 4.1067 | 4.0933 | **4.0667** | +71% |
| Hinata | 6.0667 | 6.0667 | **6.0400** | **6.0400** | +19% |
| Dragonstorm | 4.6267 | **4.6000** | 4.6000 | 4.6000 | +1.2% |
| burn | 4.4533 | 4.4533 | 4.4533 | 4.4533 | +25% |

Every deck is better or equal at every width — with site 3 pruned, wider is monotonically safe.

### Where the win comes from: rollouts, not the root

Restricting the fan-out to the committed decision only (root-only, no rollout fan-out) is nearly
free but recovers **none** of the TH or Dragonstorm gain (TH stays 4.1333, DS stays 4.6267). The
greedy continuation's real damage is that it makes the *leaf evaluator* mis-score lines, so the root
ranks candidates on bad estimates. The fan-out has to run inside the rollouts to fix that.
`MTG_NO_BP_SEARCH_ROLLOUT` restores root-only for the A/B.

### Suite validation at the adopted defaults (W=2, sites=23, fan-out at every ply)

Ground truth is the `MTG_BP_SEARCH=0` engine, so every FAIL below is a measured delta, and
`MTG_BP_SEARCH=0` re-verifies ALL PASS (smoke 21/21) — the hatch is exact.

**Regression suite (2 seeds, 35 cases): every changed case improved; ZERO cases regressed.**

| case | W=0 | adopted | delta |
|---|---|---|---|
| `th_regression_d3_s2002` | 4.3000 | 4.2500 | **−0.050** |
| `th_regression_d3_s3003` | 4.1220 | 4.0380 | **−0.084** |
| `th_regression_d5_s2002` | 4.2500 | 4.2000 | **−0.050** |
| `th_regression_d5_s3003` | 4.1300 | 4.0400 | **−0.090** |
| `dragonstorm_regression_d3_s2002` | 4.4367 | 4.3967 | **−0.040** |
| `dragonstorm_regression_d3_s3003` | 4.5633 | 4.5500 | **−0.013** |
| `dragonstorm_regression_d5_s2002` | 4.4200 | 4.3720 | **−0.048** |
| `dragonstorm_regression_d5_s3003` | 4.5440 | 4.5320 | **−0.012** |
| `hinata_regression_d5_s3003` | 5.8600 | 5.8500 | **−0.010** |
| `burn_regression_d3/d5_s2002` | 4.3520 | 4.3500 | −0.002 |
| hinata d3 ×2, d5 s2002 | — | — | 0 (play changed, same score) |
| slivers / Knights / Anti-Lifegain, all d0 | — | — | byte-identical |

Per-game audit: **136 faster, 5 slower** (regression), **16 faster, 1 slower** (smoke). d0 is
byte-identical everywhere — depth 0 *is* the greedy policy, so the mechanism cannot reach it.

**All 6 slower games recover fully at depth 9 with unbounded budget**, each matching its W=0 result
exactly — so none is a search defect; they are gate-budget churn at the suite's 10/20 ms budgets.

| slower game | at gate budget | at depth 9 / unbounded (W=0 → adopted) |
|---|---|---|
| `th_smoke_d5_s1001` gi68 | 6→7 | 6 → 6 |
| `th_regression_d3_s2002` gi257 | 6→7 | 6 → 6 |
| `th_regression_d3_s3003` gi45 | 7→8 | 7 → 7 |
| `th_regression_d5_s2002` gi204 | 5→6 | 5 → 5 |
| `dragonstorm_regression_d3_s2002` gi171 | 6→7 | 6 → 6 |
| `hinata_regression_d3_s3003` gi163 | 6→7 | 6 → 6 |

`MTG_FD_ORACLE` divergence counts are unchanged (TH 0→0, Dragonstorm 1→1): prediction and
realisation stay in lockstep, as the plan-carried design intends.

### Overnight suite (4 held-out seeds, 84 cases) — the deep validation

Run twice on the same tree (`MTG_BP_SEARCH=0` vs the adopted defaults) so the delta is attributable
to this mechanism alone. **28 cases better, 1 worse, 55 byte-identical.** Suite mean
4.82064 → **4.80934** (−0.0113). Every `d0` case is byte-identical — depth 0 is the greedy policy and
the mechanism cannot reach it, which is the expected control.

| deck | cases changed | range |
|---|---|---|
| TH d3/d5 | 8 of 8 | **−0.074 … −0.097** |
| Dragonstorm d3/d5 | 8 of 8 | **−0.010 … −0.040** |
| Hinata d3/d5 | 6 of 8 | −0.0025 … −0.010 |
| burn d3/d5 | 7 of 8 | −0.001 (6) / **+0.001 (1: `burn_overnight_d3_s4004`)** |
| slivers / Knights / Anti-Lifegain, all d0 | 0 | byte-identical |

The single worse case is one game shifting a turn on a deck with no measurable gain either way.

NOTE for whoever reads the raw run: the committed overnight fingerprints in `regression_gt.txt`
predate this session's EARLIER work (Hook 22 + draw-safe prepay), which smoke/regression were
rebaselined for and overnight was not. Comparing an overnight run straight to GT therefore shows d0
deltas that have nothing to do with `bp_choice`. The A/B above avoids that entirely.

### Performance: the cost is ENUMERATION, not nodes

`interior_nodes` (the contention-free metric) badly UNDERSTATES this change: a `bp_choice` variant
pays a full `EnumeratePlansWithLand` at apply time, and an enumeration is not a search node.
Dragonstorm at the overnight budget measured **+5% nodes but 45x wall clock**. Average-case cost is
mild (60 games, budget 80: TH +19% wall, Hinata +0.2%, Dragonstorm ~0%, burn ~0%) — the cost is a
TAIL of wide, high-budget nodes, and suite makespan is set by the tail.

Two performance-only fixes were added (neither changes a decision; every base plan is still searched):

1. **Memo on `EnumerateBreakpointPlans`.** The W variants of one base plan are emitted consecutively
   and all reach the same breakpoint state, so one enumeration serves all of them. Keyed on
   `BuildBreakpointKey`. **A first attempt keyed on `BuildSimKey` alone silently changed Hinata's
   play digest** — `BuildSimKey` is a TURN-BOUNDARY key, and `GameState` documents that
   `floating_mana` / `spells_cast_this_turn` are deliberately never folded into it because they are
   0 at every boundary. Mid-turn those are exactly what distinguishes two breakpoint states (Hinata's
   ritual float; Dragonstorm's storm count). Folding them makes the key exact.
2. **Fan-out cap (`MTG_BP_MAXBASE`, default 16).** `plans` is sorted best-first, so fanning out only
   the top 16 breakpoint-opening plans keeps the widening where the search actually is and bounds the
   blowup on very wide nodes. Measured at 4 it is both slower AND worse; 16 is the knee.

Measured, all at identical quality (smoke/regression avgs byte-identical across all three columns):

| case | W=0 | adopted, no memo/cap | adopted (memo + cap 16) |
|---|---|---|---|
| regression suite makespan | 31 s | 307 s (9.9x) | **59 s (1.9x)** |
| smoke suite makespan | 13 s | 14 s | 13 s |
| Dragonstorm 300 g, budget 80 | 63.9 s | 2859.7 s (**44.8x**) | **97.8 s (1.53x)** |

That one case breaks down as: memo alone 2859.7 s -> 337.2 s (8.5x), then the cap 337.2 s -> 97.8 s
(3.4x). Both are needed; neither changes the answer (avg = 4.4200 in all three adopted columns).

#### Per-deck cost at the adopted defaults (150 games, seed 4004, 8 threads)

Quality column is `avg` W=0 -> adopted. Wall ratios under ~1.1x on sub-second runs are scheduling
noise, not a measurement.

| deck | budget 20: nodes / wall | budget 80: nodes / wall | quality |
|---|---|---|---|
| slivers | **0% / 1.00x** | **0% / 1.00x** | byte-identical |
| Knights | **0% / 1.00x** | **0% / 1.00x** | byte-identical |
| Anti-Lifegain | **0% / 1.00x** | **0% / 1.00x** | byte-identical |
| burn | +14.6% / 0.95x | +13.6% / 1.12x | unchanged |
| Dragonstorm | +1.3% / 0.95x | +1.0% / 1.00x | **better** |
| Hinata | +8.0% / 1.04x | +9.8% / 1.01x | **better** |
| TH | +48.6% / 1.17x | +44.4% / 1.36x | **better** (-0.087) |

The three decks with no breakpoint-opening cards pay **exactly nothing** — the fan-out never fires,
which is the intended shape. TH is the only deck with a real standing cost, and it is also the
biggest quality winner. Dragonstorm is free at this seed: its 44.8x blowup above was seed 5005 at 300
games, i.e. a TAIL, not a per-game cost — which is why suite makespan (set by the tail) moved so much
more than any average.

The overnight suite was measured at 2m36s (W=0) vs 21m28s BEFORE the memo and cap; it has not been
re-measured since, so expect roughly the regression suite's ~2x rather than 8x.

### Per-game analysis of the changed games (2026-07-28)

**Every slower game recovers at depth 9 with unbounded budget** — 7 of 7 across all three modes, each
matching its `MTG_BP_SEARCH=0` result exactly. So none is a search defect; all are gate-budget churn.

| slower game | at gate budget | at depth 9 / unbounded |
|---|---|---|
| `th_smoke_d5_s1001` gi68 | 6->7 | 6 -> 6 |
| `th_regression_d3_s2002` gi257 | 6->7 | 6 -> 6 |
| `th_regression_d3_s3003` gi45 | 7->8 | 7 -> 7 |
| `th_regression_d5_s2002` gi204 | 5->6 | 5 -> 5 |
| `dragonstorm_regression_d3_s2002` gi171 | 6->7 | 6 -> 6 |
| `hinata_regression_d3_s3003` gi163 | 6->7 | 6 -> 6 |
| `burn_overnight_d3_s4004` gi445 | 5->6 | 5 -> 5 |

**Play-level diff of a regression** (`MTG_FD_TRACE`, burn gi445). The adopted engine's T1 committed
line already carries `win=6` where W=0's carries `win=5` — i.e. its T1 search never reached the
turn-5 line within the 80 ms budget. Burn's breakpoint card is Light Up the Stage, and burn gains
nothing from the fan-out, so this is exactly budget dilution: extra candidates squeezed the winning
line out at a fixed budget. Not a modelling error, and it is 1 game in 1000.

**Play-level diff of an improvement** (TH gi169, 4 -> 3) — the mechanism working as designed:

```
W=0       [replay-bp] turn=2 recs=1: Steam Vents        -> win 4
adopted   [replay-bp] turn=2 recs=1: Sandstone Needle   -> win 3
          T3 becomes  pre:spells[Land's Edge, Treasure Hunt]{land=Cascade Bluffs}
```

The searched breakpoint takes **Sandstone Needle** (a 2-mana depletion land) over the static ranker's
**Steam Vents** ("first multi-colour land in hand order"), and that extra mana lets turn 3 cast
Land's Edge AND Treasure Hunt instead of one of them. Same shape as the tracked reference case
`claude_s2_gi1` (Saprazzan Skerry `{U}{U}` over Thundering Falls) — the post-dig drop is precisely
the decision the static ranker owned, and the search now makes it per game instead of by rule.

### Reference benchmark (`scripts/ref_bench.py`) — the shortfalls this was chasing

| deck | clairvoyant LP, W=0 | adopted | note |
|---|---|---|---|
| TH (n=5) | 4.600 | **4.200** | |
| Dragonstorm (n=39) | 4.513 | **4.462** | |
| Hinata (n=5) | 5.800 | 5.800 | unchanged |
| burn (n=16) | 4.375 | 4.375 | unchanged |

Individual tracked shortfalls:

* `treasure_hunt/claude_s2_gi1` — **CLOSED**. Engine 5 → **4**, matching the human. This is the case
  the whole investigation started from: reaching turn 4 needs a different post-dig drop on turn 2
  (Saprazzan Skerry `{U}{U}` over Thundering Falls), which is exactly the choice the static ranker
  owned. The search now finds it with no static rule at all — and the rejected Hook 23 is not needed.
* `treasure_hunt/claude_s5_gi4` — engine 5 → **4** (now 4 turns better than the human).
* `Dragonstorm/claude_s24_gi23` — **CLOSED**. Engine 7 → **6**, matching the human.
* `Dragonstorm/claude_s1_gi0` — engine 6 → **5** (human 4; halved, not closed).

The Dragonstorm pair were classified as class-B ("the Apex-of-Power staged-exile chain is never
generated as a candidate — segments 2/3 use cards that don't exist at enumeration time"). Cards that
don't exist at enumeration time **is** the breakpoint condition, so the hypothesis that class A and
class B shared one root cause is CONFIRMED.

## Configuration

| env | default | meaning |
|---|---|---|
| `MTG_BP_SEARCH=<W>` | 4 | continuation variants per breakpoint-opening plan; **0 = the old engine, byte-identical** |
| `MTG_BP_SITES=<mask>` | 23 (`0b10111`) | which breakpoint classes fan out; 31 = all (free rein), see the bit table |
| `MTG_NO_BP_SEARCH_ROLLOUT` | unset | fan out only at the committed decision, not in rollouts |
| `MTG_BP_PROBE=1` | off | count greedy re-solves per site (the purge is done when these reach 0) |
| `MTG_BP_DROP_SEARCHED` | off | diagnosis: omit the static post-dig drop entirely |
| `MTG_FORCE_LAND="<turn>:<land>"` | off | force a land drop, including the post-dig one |
| `MTG_BP_DEPTH=<L>` | 1 | how many breakpoints deep the fan-out reaches (`Plan::bp_at`); emits `L*W` per base plan, not `W^L`. 2 = nested, measured NOT adopted (below) |
| `MTG_BP_TRACE=1` | off | diagnosis: print `[bp-apply]` / `[bp-exec]` breakpoint sequences + a `[bp-pay]` line per cast (cost, float, untapped sources) from BOTH sides. Use with `MTG_FD_TRACE=1` |
| `MTG_LEGACY_CCO_PAY=1` | off | A/B hatch: restore the pre-fix rollout payment (coloured pips off a `colored_creature_only` land for a non-creature spell) |

Policy this satisfies (user, 2026-07-28):

* A heuristic **may** decide when to play the land, as a **performance** shortcut.
* Where the heuristic is **inactive, search must have free rein** — no static pick standing in.
* Running with the heuristic disabled at unbounded budget is the **benchmark** that validates it.

`MTG_BP_SITES=31 MTG_BP_SEARCH=<large>` at unbounded budget is that benchmark.

## Tracked regression cases (acceptance criteria)

Baseline = the pre-session engine (git `HEAD` of `test/gt_logs`, all of this session's hooks off).
Hook 22 (`HoldDeferredDropForFurtherDig`) + draw-safe batch prepay were adopted earlier the same day:
across all 8 TH suite cases (3825 games) they were **10 faster, 2 slower**.

### A. Live regressions vs the pre-session engine

| case | gi | pre-session | current | cause | recovers under search? |
|---|---|---|---|---|---|
| `th_smoke_d0_s1001` | 246 | T5 | T6 | draw-safe declines the whole-turn prepay; the greedy pilot can no longer fit Land's Edge on T5 | **yes** — T4 at depth 9 / unbounded |
| `th_smoke_d0_s1001` | 393 | T6 | T7 | same; loses the T6 Treasure Hunt | **yes** — T4 at depth 9 / unbounded |

Both are d0-only (no lookahead), where the breakpoint search does not run at all — d0 *is* the greedy
policy. All 27 d0 slowdowns seen during this work vanish at depth 9 with unbounded budget.

### B. The line the search could not reach — the fix's real target — **CLOSED**

| ref | human | engine before | engine after |
|---|---|---|---|
| `references/treasure_hunt/claude_s2_gi1` | **4** | 5 | **4** |

### C. Validation set — games a static drop rule provably got wrong

These regressed **at depth 9 with unbounded budget** under the removed Hook 23 (a yield-ranked static
drop rule). With a real search node each must be **≤ baseline**, since the search can choose either
drop.

| case | gi | baseline | under a static yield rule | adopted (searched) |
|---|---|---|---|---|
| `th_regression_d3_s2002` | 157 | T4 | T5 | **T4** |
| `th_regression_d3_s2002` | 320 | T4 | T5 | **T4** |
| `th_regression_d5_s2002` | 158 | T4 | T5 | **T4** |

All three hold: none appears in the per-game slower list, so the searched breakpoint keeps the line
a static yield rule lost. That is the point of the whole exercise — the search can pick *either*
drop per game, where any static rule must pick one for all of them.

## How to re-measure

```bash
# quality + contention-free node cost for one config
MTG_BP_SEARCH=4 MTG_ROLLOUT_STATS=1 build/Release/mtg <deck> --profile <prof> \
    --games 75 --seed 1001 --budget-ms 20 --threads 4

# the whole suite as the A/B (ground truth is the W=0 engine, so FAILs are the deltas)
MTG_BP_SEARCH=0 bash test/regression.sh --smoke      # must be ALL PASS
bash test/regression.sh                              # current defaults

# per-game diff vs the pre-session engine
git show HEAD:test/gt_logs/<case>.wins ; test/logs/<mode>/wins/<case>.wins

# the benchmark: no prune, unbounded budget
MTG_BP_SITES=31 MTG_BP_SEARCH=16 build/Release/mtg <deck> --games 1 --seed <s> \
    --game-index <gi> --depth 9 --budget-ms 0 --ignore-play-profile
```

## History

`docs/design/clairvoyant-reference-shortfalls.md` §A4/A5 has the full derivation, including the
removed Hook 23 (`PostDrawDropLandName`, a yield-ranked drop rule) — **rejected**: it bought the
turn-4 line but lost games no budget could recover, because it made the static pick smarter instead
of making the decision searched. `bp_choice` is the version that makes the decision searched.

## Nested breakpoints: BUILT, MEASURED, NOT ADOPTED (2026-07-28)

> **Read the CORRECTION and ROOT CAUSE sections below before this one.** The first measurement was
> confounded by a rollout mana-payment bug; the counter-mismatch suspicion recorded here is refuted.
> The `bp_at` work is no longer stashed -- it is in the tree, default-off (`MTG_BP_DEPTH=1`).


Only the FIRST breakpoint of an apply is searched; a second one in the same turn (a cantrip chain,
an Apex cast off another Apex's exile, a second Treasure Hunt) still resolves greedily. That is real
unsearched logic, and on Dragonstorm the nested ones **outnumber** the searched ones (183 vs 145 per
40 games at d5 — `MTG_BP_PROBE` now reports `nested-unsearchable=`).

It was implemented as a second AXIS -- `Plan::bp_at` (which breakpoint the choice applies to), so the
emitted count is `depth*W`, not `W^depth`. Every individual breakpoint's continuation becomes
reachable; a line needing two simultaneous non-greedy choices does not.

**Result: adopted-looking on the TRAIN seeds, reversed on the HELD-OUT seeds.**

| suite | seeds | outcome |
|---|---|---|
| smoke + regression | 1001 / 2002 / 3003 (train) | every changed case **better or equal**, and the regression suite got FASTER (58s -> 31s: most depth-2 variants dedup away, and earlier wins end rollouts sooner) |
| overnight | 4004-7007 (held out) | **9 cases worse, 2 better**, net **+0.00035** avg; 14 slower games vs 4 faster |

This is a textbook train/held-out split, and precisely what the suite's disjoint seeds exist to catch.
Had it been judged on smoke+regression alone it would have been adopted. **Not adopted.**

It also did NOT close `Dragonstorm/claude_s1_gi0` (still engine 5 vs human 4), so nesting is not that
reference's blocker -- that hypothesis is refuted and the case needs its own investigation.

The work is preserved in a git stash (`WIP nested breakpoint search (bp_at axis)`), NOT on a branch.
Anyone resuming should know its default-off revert was **not yet byte-identical** when it was
stashed: `bp_seen` must count only breakpoints of an ENABLED class (Hinata prunes the cantrip class,
so its first enabled breakpoint is often not the apply's first), and the executor's own breakpoint
counter must match that same rule -- it currently counts every breakpoint regardless of class.

### CORRECTION: the held-out result was measured on a BROKEN implementation

The 14 slower games were then re-run at **depth 9 with unbounded budget** (the bar: no regression may
survive full search). **12 of 14 recover exactly. TWO SURVIVE**, both Dragonstorm, both 2 turns worse:

| game | at gate budget | depth 9 / unbounded |
|---|---|---|
| `dragonstorm_overnight_d3_s4004` gi372 | 7->8 | depth1 = **7** -> depth2 = 8 |
| `dragonstorm_overnight_d5_s5005` gi4 | 5->7 | depth1 = **5** -> depth2 = **7** |

A regression that survives unlimited budget and max depth is a DEFECT, not budget dilution. So the
"nested search is net negative on held-out seeds" conclusion above was **confounded**.

The suspect recorded here at the time was "ApplyPlanDirect and the executor count breakpoints
differently". **That hypothesis is REFUTED** -- see below. Both counters were in fact consistent, and
on the committed line the executor does not use its counter at all (it replays the recorded
`breakpoint_actions` script, not `resolve_draw_breakpoint`). Anyone resuming should not spend time on
the counters.

## ROOT CAUSE (2026-07-28, second pass): the rollout could pay coloured pips off a creature-only land

`MTG_BP_TRACE` (added with this work) prints the breakpoint sequence from BOTH sides -- `[bp-apply]`
from ApplyPlanDirect (armed only for the fd-trace committed-line replay) and `[bp-exec]` from the
executor -- plus a `[bp-pay]` line per cast giving cost, floating mana and untapped sources. On
`dragonstorm_overnight_d3_s4004` gi372 it showed the two sides agreeing on the breakpoint indices
(`idx=0` greedy, `idx=1` searched, `bp_at=1`) and agreeing on the recorded script, then diverging
**inside a single cast**, from an IDENTICAL state:

```
[bp-pay] apply cast=Irencrag Feat  cost=0/R3 creature=0 float=R2 untapped=[Unclaimed Territory]   -> paid
[bp-pay] exec  cast=Irencrag Feat  cost=0/R3 creature=0 float=R2 untapped=[Unclaimed Territory]   -> FAILED
```

Unclaimed Territory is `colored_creature_only`: its coloured mana may pay only for a creature spell,
so for a sorcery it yields `{C}` alone and cannot cover a red pip. The executor is right to refuse.
`TurnSolver::TapForCostDirectOnce`'s **scarcity-first** source selection (default ON) called
`EffectiveProduces` where its documented mirror `AIEngine::TapForCost` calls `ProducesForPayment` --
so the ROLLOUT paid `{R}{R}{R}` off two floating red plus Unclaimed Territory. The legacy
(`MTG_TAP_LEGACY`) path and both AIEngine paths always had it right; only the scarcity path drifted,
against its own header contract ("MUST stay byte-for-byte identical to AIEngine::TapForCost").

Consequence: the search **scores and commits lines the real game cannot play**. The realised turn
stops mid-script (gi372 cast 8 of its 10 spells, landing 1 creature where the line predicted 6) and
no budget or depth can recover it, because there is no search error to find. Nested breakpoint search
did not cause this -- it *exposed* it, by reaching deeper into a turn where mana is tightest.

Fixed: both call sites now use `ProducesForPayment(state, active, def, for_creature)`, via
`PayProduces` so `MTG_LEGACY_CCO_PAY=1` restores the pre-fix behaviour for the A/B (smoke 21/21 PASS
with the hatch on = the hatch is exact). Affects every deck with such a land: Dragonstorm (Unclaimed
Territory), Knights, slivers_vial, Goblins, Minotaur.

### The payment fix, measured ALONE (`MTG_BP_DEPTH=1`, the shipped nesting)

Ground truth is the pre-fix engine, so every FAIL is the measured delta. **Zero regressions in any
mode; every changed case improved.**

| mode | cases changed | per-game | range |
|---|---|---|---|
| smoke | 2 (Dragonstorm d3/d5) | 2 faster, **0 slower** | −0.020 … −0.040 |
| regression | 4 (Dragonstorm d3/d5 ×2 seeds) | 8 faster, **0 slower** | −0.004 … −0.020 |
| **overnight (held out)** | **8 of 8** Dragonstorm d3/d5 | **36 faster, 0 slower** | **−0.010 … −0.033** |

`d0` is byte-identical everywhere. `dragonstorm_smoke_d3/d5_s1001` gi70 goes 7 -> 4.

### Re-measured: nested breakpoints on the FIXED engine -- defect-free, still not adopted

With the payment bug gone, the two surviving regressions are gone too, and one becomes a gain:

| game | before the payment fix | after |
|---|---|---|
| `dragonstorm_overnight_d3_s4004` gi372 | depth1 7 -> depth2 **8** | depth1 7 -> depth2 **6** |
| `dragonstorm_overnight_d5_s5005` gi4 | depth1 5 -> depth2 **7** | depth1 5 -> depth2 **5** |

gi372 now *realises* the turn-6 win the nested search predicted -- prediction and realisation back in
lockstep. Re-running the full A/B on the fixed engine (`MTG_BP_DEPTH=1` vs `2`, both arms with the
fix, same tree):

| suite | seeds | outcome |
|---|---|---|
| smoke | 1001 (train) | 3 better, 0 worse, 15 byte-identical; mean −0.00254 |
| regression | 2002 / 3003 (train) | 7 better, **0 worse**, 25 byte-identical; mean −0.00103; and FASTER (59s -> 33s) |
| **overnight** | 4004-7007 (**held out**) | **3 better, 8 worse**, 62 byte-identical; mean **+0.00020** |

**All 12 held-out slowdowns RECOVER at depth 9 with unbounded budget** (12/12, each arm matching the
other exactly) -- so they are gate-budget churn, not defects. That is the difference from the first
attempt: the mechanism is now sound, and what remains is purely the cost of widening the candidate
set at a fixed per-decision budget. Hinata (the branchiest deck) is the clearest loser, as it was for
the plain-cantrip site.

**Verdict: NOT ADOPTED, default `MTG_BP_DEPTH=1`** -- but for an honest reason now (budget dilution
on held-out seeds), not a defect. The train/held-out split reproduces even on the fixed engine, which
is a second demonstration of why the suite's disjoint seeds exist. `MTG_BP_DEPTH=2` turns it on and
is legitimate at unbounded budget, where it is a strict gain.

It still does NOT close `Dragonstorm/claude_s1_gi0` (engine 5 vs human 4, both arms, and unchanged by
the payment fix) -- that reference needs its own investigation.
