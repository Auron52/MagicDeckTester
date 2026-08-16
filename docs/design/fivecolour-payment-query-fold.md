# Making the FiveColour mulligan run feasible: fold the payment QUERIES

2026-08-16. Self-contained; read this and `fivecolour-mulligan-and-slow-atom.md` §5 and you can start.
**Status: STEP 0 DONE (2026-08-16) — and it picked branch (a). The payment fold ALREADY EXISTS;
the lever is search BREADTH. The §"design" below is therefore NOT the work — see §Step 0 RESULT.**

## The whole problem in one ratio

At K=27 (fetch cycle merged, already by-construction in `DiscoverEquivalence`) the size-7 phase is
1,977,898 cells. At R=10 — the floor for a shippable runtime profile — that is 39.6M rollouts.

| rollout rate | wall clock (23 cores) |
|---|---|
| 110/s/core (the skill's guide) | **~4.3 h** |
| 4.8/s/core (FiveColour, measured) | **~100 h** |

Neither K nor R needs to change. **Close the rate gap and the run lands in hours.**

## What the gap is made of (measured at HEAD, 12 games, seed 1001, `--threads 1`)

| deck | payment entries/game | nodes/game | nodes/entry |
|---|---|---|---|
| FiveColour | **5,567** | 224,625 | 40.3 |
| Goblins | **6** | 12 | 1.9 |

~930x more payment questions; only 21x more expensive per question. And the cache is NOT leaking —
`miss=66,804` equals `top-level entries=66,804` exactly, at a 91.7% hit rate. So these are 5,567
**genuinely distinct** questions per game, which no cache can absorb.

This also retro-explains the parked flow-guided tap order (`flow-guided-tap-order.md`): it cut
backtracker nodes 13.6x and moved runtime ~1% on BOTH workloads. It made each answer cheaper while
the engine went on asking a thousand times more often. **Do not re-attempt cost-per-answer work here.**

## STEP 0 — ATTRIBUTE THE 5,567 BEFORE DESIGNING ANYTHING

Do not skip this. Twice on 2026-08-16 a confident causal story about this deck was written up and then
refuted by a five-minute measurement (see `hypothesis-before-measurement-mirrorwing` and the CORRECTION
headers in the two sibling docs). The design below is contingent on what step 0 finds.

`BatchPrepayMainCasts` (TurnSolver.cpp ~8879) ALREADY folds a whole plan's casts into ONE combined
cost and a single `TapForCostBacktrack` (~9002/9019), and it is called once per applied plan
(~11002). So there are two very different worlds:

* **(a) The calls are per-PLAN prepay.** Then the fold already exists and 5,567 just means "5,567
  plans applied per game". The lever is then search/enumeration BREADTH, not payment at all, and
  everything below is the wrong tree.
* **(b) The calls are per-SUBSET / per-cast.** Then the fold is missing at the enumerator and the
  design below applies.

Cheapest attribution: add a thread_local counter per call site (the `TapForCostShared` public entry in
ManaPayment.cpp ~651, the two `BatchPrepayMainCasts` backtracks, and the per-cast fallback), print
under the existing `MTG_TAP_STATS` dumper, and run the 12-game FiveColour command above. One build,
one run. Note `MTG_SEL_MANA_GATE` is **default OFF**, so the enumerator's selection-exact gate is NOT
the source.

## The design, IF step 0 says (b)

Answer one stronger question per enumeration instead of thousands of per-subset ones.

Today each candidate subset asks "can this board pay THIS cost, with this tap-state and floating?".
The board is invariant across the whole enumeration (a payment only TAPS; nothing enters, leaves, or
changes colour — the same invariant the mana-cache key already relies on). So the enumeration could
compute ONCE a compact description of what the board can pay, and answer each subset from it.

The right shape is the flow oracle's, generalised from a yes/no on one cost to a *frontier*: the
max-flow in `TapFlowInfeasible` (SpellEffects.cpp) already models sources → colours → demand exactly
and has **zero bail clauses** on all 12 suite decks (verified 2026-08-16), which is what makes a
precomputed answer trustworthy. Candidate frontier: per colour-set, the max total mana extractable —
i.e. fold on the REALISED COLOUR SET rather than the source permutation, which is the direction both
independent measurement paths pointed at (slow-rollout ranking and the depth-matrix perf profile).

Soundness bar, non-negotiable: the frontier must be an EXACT decision or a conservative OVER-estimate
that then falls through to the real solve. An under-estimate silently drops legal casts — the exact
failure mode the domain-source under-count caused before (`60b56ae1`, the FiveColour claude-play
sweep's 14/18 games).

## Acceptance

* **Byte-identical** is the bar if the frontier is exact — it only skips questions whose answer it
  reproduces. Verify with smoke + regression digests, no rebaseline.
* If it is not byte-identical it is a heuristic and must clear BOTH seed sets summed, per
  `heuristic-optimization.md`. (The board-lethal short-circuit cleared at −0.1563; the flow-order tap
  order failed the holdout after looking good on smoke. Only the sum decides.)
* The number that matters is **rollouts/s/core on the gen workload**, not play wall-clock. Re-measure
  the 12-game entries/game table above; it is deterministic and immune to load, unlike wall time
  (see `thread-wall-timings-and-misdirected-logs`).


## STEP 0 RESULT — branch (a). Do NOT build the payment fold.

Instrumented all four top-level payment call sites plus `BatchPrepayMainCasts` invocations/declines
(`MTG_TAP_STATS`, `=== PAYMENT SITES:` line). 12 games, seed 1001, `--threads 1`:

| | FiveColour | Goblins | ratio |
|---|---|---|---|
| **BatchPrepayMainCasts invocations / game** | **407,187** | **317** | **1,284x** |
| payment entries (solves) / game | 5,567 | 6 | 928x |
| prepay-held / prepay-plain / per-cast | 320,326 / 316,002 / 170,290 | 0 / 180 / 227 | |

The three site counters sum to **806,618**, exactly the mana cache's total calls (739,814 hits +
66,804 misses) — so the attribution is complete, and every payment question is accounted for.

**The verdict.** `BatchPrepayMainCasts` — which already folds a whole plan's casts into ONE combined
cost and a single backtrack — is called **407,187 times per game** on FiveColour against Goblins' 317.
The payment queries are a downstream SYMPTOM of plan applications, and they scale with them (1,284x
plans → 928x payments). Building an enumerator-level payment fold would have optimised the symptom.

**The atom, restated for the third and final time:** FiveColour's search applies ~1,284x more PLANS
per game. That is enumeration breadth × rollout count, not mana payment.

This is corroborated by the independent `perf` profile (`engine-cost-profile-2026-08-16.md`), whose
top FiveColour-gen costs are ALL per-plan-application: `EnumeratePlansWithLand` 25.0%,
`SimulateEndAndStartNextTurn` 24.8%, `GameState` copy 12.5%, allocator 13.4%. Three independent
measurement paths now agree.

### Where to go next (unmeasured — size before building, cf. `profile-before-optimizing`)

1. **Fewer plans per node.** FiveColour uses the GENERIC group cap; Mirrorwing overrides `EnumGroupCap`
   to 8 for exactly this reason. A cap is a QUALITY prune though — Goblins measured a gentle top-6 cap
   at +0.0025 rollout win-turn and top-4 at +0.085, so it must clear both seed sets.
2. **Cheaper plan application.** `GameState` copy + allocator churn are ~26% combined and are
   byte-identical to attack (no play risk, no rebaseline). Lower ceiling, zero downside.
3. **Note the decline rate: 92.2% of the 4.9M prepay invocations DECLINE** (87% before ever reaching a
   backtrack). Worth understanding why before anything else — a fold that declines 9 times in 10 may be
   cheap to fix and would cut the per-cast fallback traffic (170,290 calls) too.

Deliberately NOT recommending one yet. Every previous confident story about this deck was wrong.

## The actual design (USER, 2026-08-16): "figure out what we can generate, then use that to evaluate different plans"

Compute ONCE per board what mana it can generate, then evaluate every candidate plan against that
structure. The point is NOT to make payment cheaper -- it is to **reject a plan before it costs a
`GameState` copy and a turn simulation**.

That distinction is the whole value, and it is why my earlier framing was too small:

| target | measured share of runtime | ceiling |
|---|---|---|
| payment SOLVING (backtracker) | ~1-2% (flow-order cut nodes 13.9M -> 1.46M for ~1%) | ~2% |
| plan APPLICATION (copy 12.5% + turn sim 24.8% + allocator 13.4%) | ~50% | the 23x lives here |

FiveColour applies **407,187 plans/game** (Goblins: 317). Every one pays a copy + simulate. A
colour-aware frontier that answers "is this cost multiset generatable from this board?" cheaply lets
the enumerator drop unaffordable plans at zero application cost.

**The structure already exists.** `TapFlowInfeasible` (SpellEffects.cpp) builds exactly this: a
max-flow over sources -> colours -> demand, with **zero bail clauses on all 12 suite decks** (verified
2026-08-16) and exact modelling of filters, storage, domain and scaled sources. Today it is solved
per (board, cost) and thrown away. Built once per board and kept, each plan becomes an incremental
feasibility query (augmenting paths against the retained flow) instead of a fresh solve.

### Prior art -- read before building

A per-card scalar version of this ALREADY SHIPPED and saved nothing: `OptimisticTurnMana` /
`MTG_EMIT_PRUNE` (default OFF) dropped 37% of emissions for **zero work saved, ~0.4% CPU = noise**
(`d8d7da10`), because it is redundant with the enumerator's own per-subset gate. Two differences make
this proposal not that:
* it is COLOUR-AWARE (a max-flow, not a scalar total) -- the scalar bound is far too loose on a
  five-colour board, which is exactly why it never fired usefully;
* it filters whole PLANS before application, not single cards before emission -- so what it saves is
  the copy+simulate, not an emission.

### Size it before building (the standing rule on this deck)

The prize is (fraction of the 407,187 plans/game that a colour-exact frontier would reject) x (cost of
a plan application). If most applied plans are affordable, this saves nothing and the lever is a
breadth cap instead. **Measure first**: count applied plans, and how many are rejected on mana at
each stage (the 92.2% BatchPrepay decline rate is a hint, not an answer -- 87% of declines happen
before any backtrack, so their cause is not yet known).

Soundness bar unchanged: the frontier must be EXACT or a conservative OVER-estimate falling through to
the real solve. An under-estimate silently drops legal casts (`60b56ae1`, 14/18 games).

## SIZING RESULT (2026-08-16) — the frontier is DEAD, and so is every VALUE cache across plans

The sizing run the section above demanded was run. It refutes the frontier, by the frontier's own
stated precondition ("if most applied plans are affordable, this saves nothing"). **Most are.**

**Kill 1 — there is no mana duplication to cache.** `MTG_ENUM_STATS`, 12 games seed 1001:

| mana-side key | combos | collapse |
|---|---|---|
| exact (mana+storm+cards+colour) | 173,616 -> 173,526 | **1.00x** |
| demand-bucketed | 173,526 | 1.00x |
| mana only (loosest possible — storm AND cards-spent identity discarded) | 159,428 | 1.09x |

A cache needs repeats. The existing symmetry prunes have already taken them: even the loosest key
that throws away information the engine actually needs finds 9%. **Do not build a mana cache.**

**Kill 2 — the 92.2% prepay decline rate was never a failure signal.** `MTG_PREPAY_PROBE`, same run,
4,886,249 calls:

| outcome | share |
|---|---|
| declined: <2 casts (single-cast turn) | **89.0%** |
| PREPAID (folded, succeeded) | 7.8% |
| declined: producer (ritual/rock) | 1.8% |
| **declined: combined UNPAYABLE** | **1.1%** |
| declined: float non-empty | 0.2% |

89% of declines are turns with 0 or 1 mana-paying cast, where folding one cost into one cost is a
no-op — the decline costs nothing and there is nothing to fix. Only **1.1%** are unaffordable, so a
frontier rejecting plans pre-application would skip ~1% of the 407,187 applications/game. Against
plan application's ~50% of runtime that is ~0.5%, i.e. noise — the same verdict, for the same
reason, as the scalar `MTG_EMIT_PRUNE` prior art.

**Kill 3 — `MTG_SOLVE_MEMO` is worth ZERO at HEAD.** Re-A/B'd (12 games, paired, alternating,
comparing minima): baseline **18.42s** vs memo **18.46s**, identical `interior_nodes=1480179` and
identical avg. The memo is live and hitting (42% hit rate) — it just costs what it saves. The
`single-consideration.md` figure of −21% was measured at d3/budget-200ms; the shipped FiveColour
play config is d6/budget-20ms `value_play`, and the collapse does not survive the config change.
Independently, it memoizes the GREEDY second-main solve, a path the engine is actively removing.

### What the three kills have in common

The plans genuinely differ. What repeats across them is **not the answer — it is the allocation**.
Every plan loop applies its plan to a copy of the SAME parent state, copy-CONSTRUCTED inside the
loop, so each of FiveColour's 1.48M plan applications frees the previous plan's buffers and mallocs
new ones of nearly identical size. That is precisely why `GameState`'s copy ctor (12.5%) and the
allocator (13.4%) top the profile. The fix is capacity reuse, not memoisation: hoist the scratch
board out of the loop and copy-ASSIGN (`s_state_reuse` / `LoadPlanState`, TurnSolver.cpp;
`MTG_NO_STATE_REUSE=1` restores per-plan construction for the A/B). Byte-identical by construction.

**Measured: −4%** (FiveColour, 4 games, seed 1001, `--threads 1`, 10 alternating runs per arm).
The box was contended (load 22, another agent building), so wall clock alone is untrustworthy —
but the distributions are CLEANLY SEPARATED, every `on` sample faster than every `off` sample:

```
off:  5.347 5.353 5.355 5.355 5.366 5.377 5.435 5.457 5.467 5.551     (min 5.347)
on:   5.111 5.142 5.160 5.179 5.180 5.205 5.207 5.211 5.218 5.247     (min 5.111)
```

Identity held on every run (`avg=5.2500`, `interior_nodes=1480179` on all arms). Hoisted at the two
interior-node loops (FSLineWin's plan loop, FSLineTail's second-main loop) plus the two FSLineWin
ladder passes; the ladder pair added nothing measurable (−3.9% vs −4.4%, same experiment noise),
which is expected — they are far colder than the two interior-node loops.

Method note: hardware counters are `<not supported>` in this container, so `perf stat -e
instructions` is unavailable and there is no contention-proof counter for an allocation change
(node counts are identical by construction). Many SHORT alternating runs comparing minima is the
workable substitute — and the full separation above is stronger evidence than any single pair.

### Kill 4 — `EnumGroupCap` is NOT the FiveColour lever (2026-08-16)

The "fewer plans per node" candidate above was swept with `MTG_SOLVE_GROUP_CAP` (150 games, seed
1001). Both metrics here are DETERMINISTIC — avg win turn for quality, `interior_nodes` (= plans
applied) for cost — so this measurement is immune to the machine contention that made wall clock
unusable that evening:

| cap | avg win turn | interior_nodes | cost cut |
|---|---|---|---|
| 12 (base) | 5.1133 | 10,738,054 | — |
| 10 | 5.1133 | 10,738,054 | 0.00% |
| 8 | 5.1133 | 10,732,887 | 0.05% |
| 6 | 5.1133 | 10,469,369 | 2.5% |
| 4 | **5.1867 (WORSE)** | 9,178,364 | 14.5% |

The cap does not bind: identical node counts at 10 and 12, and it only starts removing real work at
the point where it also starts costing quality. **FiveColour's breadth is not many GROUPS.** The
branch stats say the same thing from the other side — the `groups=9-12` bucket has 102 calls out of
2.09M, while the dominant bucket is `groups=0-4 board=7-10` with **1,147,215 calls at an average
odometer of just 10.1**.

So the 23x is not wide nodes, it is MANY nodes: 2,090,441 enumeration calls per 12 games with a
modest odometer each. That is the search visiting more states, which is what a five-colour deck with
many permanents and many activatable sources genuinely produces — not an inefficiency with a switch.
Anyone returning to this should treat "make FiveColour's search cheaper per node" as largely
exhausted (four independent kills above) and attack either the NUMBER of nodes (depth/beam policy,
which is a quality trade needing both seed sets) or accept the rate and size K/R around it.

### Still un-taken (not caches)

* **Land-fan sharing.** `EnumeratePlansWithLand` re-runs the whole spell odometer once per land
  candidate (~4.3x on 5c). NOT cacheable by equality — `land_sig` already dedups interchangeable
  lands, so the surviving candidates genuinely produce different enumerations. The route is
  structural (the land-after-draws timing doctrine), which deletes the fan rather than caching it.
* **Odometer rejection rate.** 29.6M odometer positions produce 2.8M emitted plans (10.6x). The
  mana gate prunes SELECTIONS, not DIGITS (TurnSolver.cpp ~3420), so uncastable cards still become
  odometer digits. The enumerator's own `two-stage gating potential` line prices the fix at only
  **1.24x fewer visits**, and it applies to just the 79,489 of 2,090,441 calls that have a mana
  side — so this is a few percent at best, not the 23x.
