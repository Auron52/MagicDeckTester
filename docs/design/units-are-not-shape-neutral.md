# Work units are not shape-neutral, and other deferred items from the Snow leaf-cost work (2026-10-15)

Status: **DEFERRED.** Measured, not acted on. The open questions in §5 belong to the user; nothing here
changes play.

Context: Snow's value-leaf run (2026-10-14/15) found the staged model leaf cheaper in work units than
it really was. The user ruled that pipeline decisions must not rest on wall-clock time, because the box
is shared (2026-10-15: *"there may be contention, so please do not make decisions based on
wall-clock"*), so phase E.5 now prices arms in units (`vlq_ab_report`). This doc records why units alone
are not enough when the arms are different search SHAPES, and what was measured along the way.

## 1. Units under-price hybrid shapes

A work unit is one simulated rollout turn-step. A shape that leans on tree expansion (a model leaf, a
hybrid ladder) does more work per unit than live rollout play, because node expansion is largely
uncharged. This is the same defect `the-budget-is-denominated-in-the-wrong-unit.md` found on Kitty from
the plan side.

Callgrind on Snow (instructions retired, Ir; contention-proof). Every arm plays the same games, chosen
so the play is identical in all of them:

| arm | Ir per unit, s600000 x 5 games | s601000 x 10 games | vs live |
|---|---|---|---|
| live (no sidecar) | 97.5k | 106.1k | 1.00x |
| nlfit (no leaf, single FIT pass; SHIPPED) | 111.0k | 114.5k | 1.08-1.14x |
| staged (model leaf + escalation) | 120.8k | 133.6k | 1.24-1.26x |
| d6t6 (depth 6, trust depth 6) | 130.1k | 219k | 1.33-2.06x |

Real cost ratio = units ratio x Ir-per-unit ratio:

* staged: 1.089x units x 1.24x = ~1.35x, which matches phase E's wall core-seconds (1.34x). E.5's
  units-only verdict would have read it as 1.09x.
* nlfit: 0.895x units x 1.08-1.14x = ~0.97-1.02x. Cost-neutral, not the 10% saving units suggest.
* d6t6: 1.216x units x 1.33-2.06x = ~1.6-2.5x. The factor moves with the game sample, which is
  why a calibration needs more than one seed.

Where the uncharged work goes (staged vs live): leaf evaluation itself (`Score` +
`ExtractMidGameFeatures`, 120 trees of depth 4) is only about a fifth of the excess. The rest is
tree-node work: `BuildSimKey` / `BpEnumBuildKey` hashing, `EnumeratePlans` and `ApplyPlanDirect`,
`Plan` and `GameState` copies. A cheap leaf moves work out of rollouts, where units track Ir closely,
into node expansion, where they do not.

**Rule for anyone reading a phase E table:** a units ratio between two SHAPES is not a cost ratio.
Between two runs of the SAME shape it is fine.

## 2. Proposal: an Ir calibration step in E.5 (not built)

E.5 could keep its large-sample units ratio and multiply it by a per-shape Ir/unit factor measured
with callgrind on a few games:

1. Pick 5-10 games whose play is identical across every arm (same per-game digest).
2. Run each arm under `valgrind --tool=callgrind` (only `Ir` is needed; ~50x slowdown, so a handful
   of games costs minutes of one core).
3. Report `units ratio x Ir/unit ratio` beside the units verdict and pick on the product.

It is contention-proof (instruction counts do not depend on the box) and deterministic. `perf stat`
would be cheaper, but this WSL kernel exposes no hardware counters. The alternative, pricing tree work
in units, changes what a budget buys in play, so it is a play change and a user decision
(`the-budget-is-denominated-in-the-wrong-unit.md` §6 poses the same choice from the plan side).

## 3. Snow at depth 6 with trust 6 (measured, rejected on cost)

Phase C's per-game records (mt=8, which reproduce phase D's V5 numbers exactly) show depth 6 is
trustable by the shipped rule: V6 vs H5 gap 0.0000, upper95 +0.0015 <= tol 0.002, with 2 of 1,560 games
differing. Caveat: there is no H6 cell, so the reference is H5. The rule leaves it unset because a trust
depth greater than the play depth (5) is unset by construction.

Measured as a play shape (8 phase-E seeds, units):

| arm | vs live, turns | t | seeds better | units |
|---|---|---|---|---|
| d6t6 (td 6, trust 6) | -0.0124 | -7.15 | 8/0 | 1.216x |
| d6 (td 6, no trust) | -0.0013 | n.s. | | 1.016x |

Against the shipped nlfit, d6t6 is -0.0089 turns at 1.36x units, so ~1.6x real cost or more (§1). Mechanism, per
decision: d6t6 verified 54%, escalated 40%, committed at d6 30%, 1.9 hybrid decisions per game. Plain d6
verified 38% and escalated 62% (2.7 per game). Live: 4.7 per game.

USER 2026-10-15: *"I'm not a huge fan of anything that increases Snow's cost for now."* nlfit shipped
instead. Revisit d6t6 if Snow's cost is ever re-opened. It is the largest single quality gain measured on
this deck.

## 4. Leaf-cost census findings (Snow, Pirates)

* The first lead was that phase E measured the staged leaf in a shape nobody ships (no trust depth, so
  every unproven decision pays the full heuristic ladder). It is REFUTED: phase E did measure the
  one-depth capped shape (`pd4/pd5/pd6` = staged + enabled target depth + cap). Against the default it
  measured +0.0045..+0.0055 turns (0 of 4 better) at 0.92-0.94x units, which is about 1.01x live and
  worse. nlfit dominates it.
* `trust5` plays identically to staged.
* Snow's leaf V-H gaps and crossover table are typical of the fleet. The leaf is not unusually weak.
  What makes it cost is §1.
* Pirates for contrast: its leaf runs at 0.24x the units of no sidecar. A leaf is a large saving on a
  deck whose rollouts dominate, and roughly neutral on one whose node expansion does.

The census instrumentation (`HybDecisionLog.h`, per-decision `[hgm]` rows, `scripts/hyb_census.py`)
lives on the local `leafcost` branch.

## 5. Open items for the user

* **Build the E.5 Ir calibration (§2)?** Recommended. Without it, E.5 can adopt a shape that is more
  expensive than the one it replaces while reading cheaper.
* **An unbudgeted backstop in the suite.** The exact unit cuts (`snow-exact-unit-reductions.md`) now
  run at every budget. Several remaining levers still only fire unbudgeted (below), so a few cheap
  decks at `--budget-ms 0` would keep that path under ground truth. Candidates: WhiteKnights,
  Soldiers, Fluctuator, Goblins, Angels, Knights; Snow H1 and Melira for the heavy end. Size not set.
* **The unbudgeted-only latch family.** These are still scoped to `UnbudgetedWorkScopeActive()` (label
  search or `g_unbudgeted_play`). The user ruled out unbudgeted-only changes on 2026-10-14. Each needs
  its own truncation review before it can follow the exact cuts to every budget:
  * `MTG_BP_WAVE_NSKIP` (breakpoint wave stillborn-slot skip). Ungated, it once moved 5 budgeted GT keys
    at equal averages on Fluctuator, Kitty and Goblins. That is the budget re-spending saved work, the
    shape the ruling now accepts, but it needs a held-out A/B.
  * `MTG_WINLESS_DEVELOP` (end-of-turn state closure on a turn the provider proves winless; Snow and
    EldraziDisplacerFlicker), default ON.
  * `MTG_BP_NSKIP_GLOBAL` and `MTG_FSW_EOT_DEDUP`. Default OFF, so these only matter if adopted.
