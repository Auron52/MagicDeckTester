# Bruna mulligan generation: where the time goes (2026-10-11)

Status: **diagnosis complete; mechanism built (opt-in, default off); the setting is the USER's (open).** The fast-recipe keep-table
generation (`mullgen.sh run decks/Bruna fast`, src `5c5cd74c`, d1 b3, K=21) was started 2026-10-10 on a
12-thread i5-1235U laptop chip and cancelled by the user after 16 h (journal kept at
`logs/bruna_mullgen/journal_keep_5c5cd74c/`, 303k cells, floor R=2 at 45%). Its rate, 10.9 rollouts/s
cumulative, projected ~2.5-3 weeks on that box (~2.8 days on the 24-core primary).

## Method

Workload = the hand scorer, which plays generation's rollout (same rollout profile, value sidecar, d1,
3 ms budget, `MTG_DECISION_WORK_X=1000`):

```
MTG_SCORE_COMPS=1 MTG_SCORE_HANDS=48 MTG_SCORE_R=4 MTG_EQUIV_DEPTH=1 MTG_SCORE_BUDGET_MS=3 \
MTG_SCORE_HAND_SEED=424242 MTG_DECISION_WORK_X=1000 taskset -c 0 build/Profile/mtg-analyze decks/Bruna/Bruna.cod
```

(`MTG_SCORE_COMPS=1` is what selects the scorer; without it the binary runs the full analyzer and its
1000-game d5 card-score pass.) 384 rollouts, single thread, 83.8-90.5 s CPU = **0.22-0.24 core-s per
rollout** on one core. Instruments: `MTG_TURN_CENSUS`, `MTG_M2_YIELD_STATS`, `MTG_ROLLOUT_STATS`,
`MTG_BRANCH_SHAPE` + `MTG_REENUM_CENSUS`, and `perf` (DWARF stacks; note they TRUNCATE at 32 KB on this
recursion, so only innermost-frame attributions are trustworthy).

## The cost tree

1. **Machine, ~3x.** Under all-core load the laptop chip (2 P-cores + 8 E-cores, 15 W) delivers ~1.1
   core-s per rollout against 0.23 single-threaded. The primary measured 0.356 core-s at full load.
2. **The escalation, ~89% of engine work.** At d1 Bruna's value leaf is not trusted: the sidecar carries
   no `value_trust_depth`, so `escalate_below` defaults to `lookahead + 1` (AIEngine.cpp) and nearly
   every decision re-runs its line search on the HEURISTIC leaf (`FSLINE HORIZON LEAF: value=8870
   forced-heuristic=7637` over 96 rollouts) and keeps it (`take_heuristic_at_hdepth[1] = 1`). That is
   correct for this deck -- its leaf is ~0.5 turn weaker than a playout at d1 (`value_leaf_lp` 5.48 vs
   `heuristic_lp` 5.00).
3. **The heuristic leaf is a 1-ply LOOKAHEAD playout** (`search_leaf_depth`, engine default 1), not a
   greedy one: every leaf turn runs `SolveWithLookahead(1)`, whose candidates each get a searched second
   main (one ply) scored by greedy playouts. That nesting is the multiplier: per 384 rollouts, 568k
   branch-site searched m2 solves, 617k playouts, 2.5M greedy turn solves (1.5M after the solve memo).
4. **Inside it (innermost-frame shares of all CPU):** greedy turn-solve subset walk 27.6%, mana payment
   17.9% (7.9M solves), plan enumeration 16.7% (487k, enum memo 0.9% hit), plan apply 16.2%, combat
   4.9%, search bookkeeping 4.7%, state keys 4.4%, tutor ranking 3.1%, state copies 2.2%.

## Measured levers (384 paired rollouts, mean win turn vs base; lower = better)

| arm | CPU | speed | delta | hand-sides worse / better |
|---|---|---|---|---|
| base | 83.8 s | 1x | - | - |
| `MTG_ROLLOUT_HORIZON=2` (value-leaf tail after 2 turns) | 70.0 s | 1.2x | +0.159 | 43 / 4 |
| `MTG_ROLLOUT_HORIZON=1` | 35.5 s | 2.4x | +0.185 | 47 / 3 |
| `MTG_FD_LEAF_DEPTH=0` (greedy horizon leaf) | 9.4 s | 8.9x | +0.208 | 49 / 0 |
| `MTG_VALUE_MIN_DEPTH=0` (pure value leaf, no escalation) | 3.0 s | 28x | +0.370 | 67 / 0 |
| greedy m2 INSIDE the leaf only (lever DELETED, user ruling) | 74.2 s | 1.13x | -0.003 | 0 / 1 |

Every cheaper LEAF is lossy on Bruna. The 1-ply leaf is real search value, not waste. A leaf's searched m2
scores ~1.0 candidate on average, and that candidate's playout IS the first-main candidate's score, so
making it greedy moves the playout rather than removing it. The leaf's cost unit is (first-main candidates
per leaf turn) x (one greedy playout).

## Ruled out

* **Budget discipline.** At d1 there is one pass and it always completes; 73% of the metered work is the
  unbudgeted greedy walk inside leaf playouts (now bucketed as `u_meter_*` in the census).
* **Re-work** 0.8%. **Wide menus:** heavy decisions have 2-15 plans; the thousands of scored candidates
  per decision are the nested leaf searches.
* **Caching:** walks are 1.35x repeats at most (488k distinct of 660k) -- a perfect memo is worth <= ~7%.
* **Per-node code** has had several passes (Selesnya bulk-cost, mana backtracker, width collapses); the
  profile is flat and memory-latency bound (e.g. `CollectActions`, 7,500 lines, 4.7% self).

## User rulings (2026-10-11)

* **No greedy, anywhere it is not already.** *"greedy logic in search is a major no-go for me in
  general."* That rules out every leaf-truncation lever above that adds greedy turns:
  `search_leaf_depth 0`, first-turn-only lookahead, and a greedy m2 inside the leaf. Their code was deleted.
* **A Bruna PLAY setting, not a generation-only one.** *"I want to work on a Bruna play setting and NO
  GREEDY."* The `value_play.mull_gen_leaf_*` keys were deleted.
* **Target:** generation on the 12-core laptop box in about a week (it projects to 2.5-3 weeks).

What remains in scope: sound collapses of branching, per-node speed, and replacing work with search/value
(e.g. a value model trusted at d1). The next step is attributing cost to SITUATIONS (which boards and
hands make a decision expensive) via the census `board` column.

## Update 2026-10-11 (afternoon): throttling, ranking fidelity, and the mechanism

**The laptop throttles.** Six back-to-back 12-thread runs of identical work took 81, 113, 167, 163,
168, 179 s: sustained throughput is HALF of burst. That -- not generator overhead -- is why the gen fell
from 21/s to ~11/s within its first hour. (Uniform-over-compositions hands also cost 1.43x natural
openers; the generator itself adds nothing measurable.) A Windows power mode / cooling change is the
only lever on this factor.

**Cheaper leaves preserve the hand RANKING**, which is what a keep table is built from. 48 held-out hands
(seed 777) x R=16, paired, d1 b3:

| leaf | CPU (paired, 192 rollouts) | mean win-turn delta | Spearman vs base (draw / play) |
|---|---|---|---|
| base (1-ply leaf every turn) | 109.0 s | - | - |
| first turn 1-ply, rest greedy | 73.1 s (1.49x) | +0.050 +- 0.007 | 0.994 / 0.997 |
| ... + greedy m2 inside the leaf | 51.7 s (2.11x) | +0.054 +- 0.007 | 0.994 / 0.997 |

(Per-hand noise se ~0.22; hand means spread sd 0.77-0.89.) Melira already ships `search_leaf_depth 0` +
`search_leaf_first_turn_depth 1` as PLAY settings for the same reason (its leaf owned 93% of a d3 game).

The leaf-truncation mechanism measured above (`search_leaf_m2_greedy`, `value_play.mull_gen_leaf_*`,
`GenLabelProfile`) was DELETED under the rulings above; the table is kept only as a measurement.

## Which situations cost the most (2026-10-11, census `board` column)

10,322 root decisions (6 natural-opener seeds + 96 uniform-composition hands, 24 hands x R=4, d1 b3,
single-threaded, `logs/bruna_perf/sit/`, analyzer `attr.py`):

* **A tutor in hand is the cost.** Decisions holding Glittering Wish or Open the Armory are 47% of
  decisions and **78% of wall**; decisions without one run at 0.42x the average. Both tutors castable:
  4.6% of decisions, 19% of wall (4.2x). Even an uncastable tutor costs 1.1-1.5x, because the 1-ply
  leaf looks into the turn where it is castable.
* **It is branching, not per-node cost.** Wall per scored candidate is flat (~220 us) in every group;
  candidates per decision (turns 1-4) are 497 with no tutor, 1,206 with Armory, 1,504 with Wish, 2,941
  with both.
* Turns 2-4 hold 79% of wall; once Bruna is on the battlefield a decision costs 0.02x; Auras in hand
  make decisions CHEAPER. Half the wall is in the top 7.4% of decisions.
* Example (`MTG_PLAN_DUMP=4 MTG_PLAN_DUMP_SEED=15755400385037043839`, hand file `sit/dump/h10.txt`):
  31 plans, 20 distinct states, of which 14 are tutor branches (Wish x4 targets, Armory x3, each alone
  or beside Somberwald Sage). Every one pays a full 1-ply leaf, and the fan-out repeats in each leaf turn.

**Target width (probe only; `MTG_TUTOR_WIDTH=1` is a front pick and NOT a lever).** 96 natural hands x
R=4, paired, 12 concurrent processes (`logs/bruna_perf/tutw/`):

| arm | CPU | speed | win-turn delta (se) | worse / better |
|---|---|---|---|---|
| base (the USER's narrowed role rules) | 765 s | 1x | - | - |
| width 1 | 460 s | 1.66x | +0.087 (0.012) | 52 / 2 |
| full width (`MTG_WISH_FULL_WIDTH=1 MTG_ARMORY_FULL_WIDTH=1`) | 1,367 s | 0.56x | +0.026 (0.010) | 22 / 8 |

The width is real value, and the role narrowing already helps both speed AND quality.

**Which roles the search takes** (800 games, d1 b3, `MTG_TUTOR_CHOSEN_RANK=1`, real resolutions,
`logs/bruna_perf/roles/`): Wish set sizes 1:124 2:108 3:48 4:89 5:69 6:22. Offered -> chosen: Bruna
261 -> 225 (86%), Indrik Umbra 74%, Almost Perfect 45%, Troyan 13%, Vexing Shusher 12%, Linvala 5%,
Unflinching Courage 3%. **When Bruna is in the set it is taken 225/261 times, yet 123 of those 261
sets carry 4-6 roles.** Armory: Colossification 74%, Arcanum Wings 57%, Lightning Greaves 21%, Wild
Growth 14%. Any amended rule is the USER's call and needs a held-out A/B.

**Outcome (USER-directed cuts, 2026-10-11):** the role cuts that are play-neutral are worth **1.05x** (rule
and measurements: `docs/design/glittering-wish-heuristic.md`, "TUTOR FIT"). The cuts that bought more
(1.09x-1.24x) lost turns. Target-set narrowing is bounded by the width-1 probe (1.66x), and most of the
tutor's cost is the tutor being castable at all, so it cannot close the 2.5-3x gap alone.
