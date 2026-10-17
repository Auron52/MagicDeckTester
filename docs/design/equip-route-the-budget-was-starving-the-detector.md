# The equip route's per-link price was starving the detector that pays for itself (2026-10-06)

**Status (2026-10-17):** The PRICE half is ADOPTED: `MTG_EQUIP_ROUTE_NODE_UNITS` default lowered 8 -> 2 in c4ad1b0b (2026-10-07, "user ruling 2026-10-07" per its commit message and the code comment in `src/ai/DecisionProviders.cpp`). TOPMAIN is still NOT adopted: `MTG_EQUIP_ROUTE_TOPMAIN` reads `EnvOn(...)` default OFF. (`MTG_EQUIP_ROUTE_CUT_TOPMAIN`, default ON, is a DIFFERENT lever -- the go-off-cut fix f81d95c2 in `equip-route-cut-poisons-a-frame-it-does-not-win.md` -- not this file's TOPMAIN gate.) The "NOTHING ADOPTED" line below is superseded for the price only.

Status (2026-10-06, superseded for the price by c4ad1b0b): **NOTHING ADOPTED.** The PRICE result is complete, passes the adoption bar, and is inert at
unlimited budget BY CONSTRUCTION — that is the part that is ready. **TOPMAIN is PROBABLY SOUND but
NOT PROVEN** (§5a, §5a-bis, §5a-ter): it perturbs 0.63% of games even at 50x the shipped budget and
makes 17 of them worse, yet all 16 of those re-tested at `--budget-ms 0` come back byte-identical,
so the finite-budget losses are reallocation rather than capability deletion. 67/67 instances are
byte-identical at unlimited, with one named residual gap. **I reached a confident verdict on TOPMAIN
twice and was wrong twice** — first on a 250-game null that could not have detected the effect (the
user caught it), then on finite-budget losses that turn out to be reallocation — so this file offers
no third verdict and the ruling is the user's.

The headline: the shipped per-link price was set just high enough to make the route's lethal
detection FAIL on the boards where it was worth the most, and a failed detection is the worst case
available — it spends the whole budget and collapses nothing. **Lowering the price alone gives
0.817x wall and -48 game-turns per 3,000 games**, with every slower game recovering at the adoption
bar. The remaining order of magnitude is NOT in the route at all: it is one decision enumerating
31-145 million plans (§4b/§4c), which is non-lossy to fix and needs no ruling.

Context: KittyEquipment v2 costs **2.48 thread-s per game** against Stompy's 0.029 and
Fluctuator's 0.027 — 85x — which blocks the deck-screening apparatus (a 16-arm x 16,000-game
screen is ~5.5 hours on 32 cores). This file is the cost diagnosis and the lever triage under the
user's completeness rule (CLAUDE.md, "A BUDGET MAY RE-PRICE WORK, IT MAY NOT MAKE A LINE
UNFINDABLE").

## 1. Where the 85x actually is

Per game, at each deck's shipped policy, 10,000 games each (seed 960000,
`logs/kittyv2_swaps/cutfix/peers.out`):

| deck | policy | avg win turn | units/game | ms per unit | thread-s/game |
|---|---|---|---|---|---|
| fluctuator | d5b20 | 3.5549 | 2,198 | 0.0122 | 0.027 |
| stompy | d6b20 | 4.1072 | 2,393 | 0.0122 | 0.029 |
| treasure_hunt | d5b20 | 4.0759 | 8,048 | 0.0150 | 0.121 |
| **kittyv2** | **d5b20** | **3.9101** | **18,624** | **0.1334** | **2.48** |

So it is 7.8x the units **and 11.0x the cost per unit**. The second factor is the defect: a unit is
documented as "one simulated turn-step in a rollout", and the budget's whole job is to be a
wall-independent proxy for work. Every budget in the engine is therefore reading this deck's work
through a ~11x-wrong exchange rate.

**The 11x is concentrated in a tail, and the tail is per-unit cost, not unit count.** From the
3,000-game base arm (`logs/kittyv2_swaps/levers/base.err`), the heaviest games:

| game | wall | units | ms/unit | vs stompy |
|---|---|---|---|---|
| gi=369 | 1,166 s | 601,250 | 1.94 | 159x |
| gi=759 | 1,053 s | 505,232 | 2.08 | 171x |
| gi=371 | 243 s | 112,681 | 2.15 | **176x** |

Two games (369, 759) are 2,219 s of the run's 7,324 thread-s — **30% of 3,000 games in two of
them** — and they are what the `[batch] heartbeat` shows as 2/30 workers busy at ten minutes and
0/30 at twenty. The bulk of the sample finishes inside ten minutes; the wall is the tail.

## 2. Two corrections to earlier readings of this

Both of these were stated to the user and are wrong; they are recorded because each one points the
remedy in the opposite direction.

1. **"The route charges one unit per DFS link."** It charges `MTG_EQUIP_ROUTE_NODE_UNITS`, whose
   **default is 8**. The "1" came from a stale comment at the census site
   (`src/ai/TurnSolver.cpp`, `unitsite::kEquipRoute`), now fixed. The price was already calibrated
   once, and the comment at `equiproute::NodeUnits()` records the method: the 14 references at the
   committed policy plus the s5_gi4 cold ladder, with `1 -> 0/14`, `4 -> 0/14`, `8 -> 0/14`,
   `16 -> 1/14` — "8 is the largest price that keeps the references".
2. **"The fix is to charge the honest price, i.e. raise it."** Measurement says the opposite; see
   §4. Raising the price buys wall by *doing less detection*, and the detection is what was paying
   for itself.

A third correction, internal to this file's own first draft: the DFS's `Seen` transposition table
memoises only **winless** interior nodes (`sub == 0`), which looked like "the memo misses the 90%
of trials that find a kill". It does not — `equiproute::TrialPolicy` memoises **both** verdicts,
keyed on `RouteStateKey` and scoped to `TurnSolver::DecisionEpoch()`. Measured on gi=1053:
28,356 MEMO-WRITE against 14,717 MEMO-HIT, a 34% hit rate. The verdict memo is working; the misses
are genuine distinct boards, because `comboroute::BoardKey` folds the library's **top 8 cards in
order** and every draw moves them.

## 3. Why cross-frame redundancy is real but is NOT free to collapse

`EquipRoutePiecesInPlace`'s comment states the dominant cost as frame COUNT: the route is
re-consulted at the turn's first main and again after every breakpoint re-solve, and ~90% of
trials on both traced games find a kill, so this is not wasted detection of hopeless boards (a
damage pre-filter would buy almost nothing, and `MAX_ITER 24->6` / `PROBE 12->3` leave the wall
flat — it is not playout depth either).

The tempting conclusion is that the first main's verdict subsumes the later frames, because the
route's playout plays the whole turn out and — per `BoardKey` folding the real library top — it
draws the **actual** next cards rather than guessing. But subsumption does not follow: a
breakpoint continuation differs from the first main's playout in which cards were **cast**, not
only in which were drawn, and the playout explores one line rather than every continuation. That
is exactly the shape of the bug fixed in 6ae93c2b, where "the frame already had the answer" was
false. So the redundancy may not be collapsible by assertion; it has to be measured, which is what
§5 does.

## 4. The price curve points DOWN, and the mechanism explains it

3,000 games, kittyv2 shipped policy, seed 960000, paired per game via `MTG_DUMP_WINS`
(`logs/kittyv2_swaps/levers/`). **Units are NOT comparable across price arms** — a lower price
charges the same work fewer units by construction — so wall (`ms`, thread time) is the only honest
cross-arm cost column here.

| arm | price | avg win turn | wall (thread-s) | vs base | faster/slower | paired net | slow games | tail sum |
|---|---|---|---|---|---|---|---|---|
| nu1 | 1 | **3.9003** | **5,986** | **0.817x** | 79 / 34 | **-48** | **20** | **3,777 s** |
| nu2 | 2 | 3.9017 | 6,204 | 0.847x | 64 / 24 | -44 | 21 | 4,215 s |
| nu4 | 4 | 3.9083 | 6,272 | 0.856x | 41 / 15 | -24 | 21 | 4,451 s |
| base | 8 (shipped) | 3.9163 | 7,324 | 1.000x | — | — | 31 | 5,618 s |
| nu12 | 12 | 3.9207 | 8,183 | 1.117x | 9 / 23 | +13 | 32 | — |
| nu32 | 32 | 3.9380 | 9,877 | 1.349x | 32 / 100 | +65 | — | — |

**Monotone on both axes across the whole range**, and in the direction opposite to the one this
file's first draft predicted: halving the price is better AND cheaper; quadrupling it is worse AND
dearer. The paired transitions name the mechanism exactly — nu4's dominant move is `4 -> 3` in **25
games**, nu12's is the mirror image, `3 -> 4` in **12 games**. The price is not an accounting dial,
it is a **detection dial**.

**And it improves the TAIL, which is what actually binds here.** Slow-game (>30 s) census of the
same 3,000 games:

| arm | slow games | tail sum | worst game | 2nd | 3rd |
|---|---|---|---|---|---|
| base (8) | 31 | 5,618 s | 1,166 s | 1,053 s | 513 s |
| nu4 (4) | **21** | **4,451 s** | 1,150 s | 1,047 s | 484 s |

That matters because the tail IS the wall on this deck: the `[batch] heartbeat` reads 2/30 workers
at ten minutes and 0/30 at twenty, and two games (gi=369, gi=759) are 30% of the run.

**The worst game is price-INVARIANT (1,166 s at price 8, 1,150 s at price 4), so its cost is not
the route's budgeted DFS at all.** That is a second, independent pathology, and it is the one with
the most headroom left; it gets its own profile (§6).

**The mechanism is that the route is a detector whose success removes work.** A verified lethal
fires the go-off cut, which collapses the frame to one plan; more detection therefore means more
collapses and shorter games, and both of those cut the number of frames still to be searched. The
route pays for itself, so pricing it high starves the thing that was reducing the bill. This is the
"COLLAPSE WASTED SEARCH UNCONDITIONALLY" dynamic seen from the other end.

It is **not** a clean win in this repo's strict sense (games do get worse: 15 at price 4, 24 at 2,
34 at 1), so it is subject to the adoption bar — every slower game must recover at
`--depth 8 --budget-ms 0`, and the deck aggregate must be net <= 0.

**BOTH HALVES OF THE BAR PASS, for all three prices.** The union of every game that got slower in
any arm is 36 games; re-run as one pooled manifest at `--depth 8 --budget-ms 0`:

| arm | paired | identical to base | better | NOT RECOVERED |
|---|---|---|---|---|
| price 4 | 36 | **36** | 0 | **0** |
| price 2 | 36 | **36** | 0 | **0** |
| price 1 | 36 | **36** | 0 | **0** |

Every slower game is byte-identical to base at the bar, which is exactly what "recover" means here:
the loss was a budget artefact, not a capability difference.

**That same table is also the empirical proof of the inertness claim.** `--depth 8 --budget-ms 0`
IS an unlimited budget, so if the price were doing anything other than decrementing a budget, these
36 games would diverge. 36/36 identical at three different prices is the measurement behind "the
price cannot violate the completeness rule" — previously an argument from the code, now a result.

**How low can it go is an open question with a known floor.** `NodeUnits()`'s comment records why
the price exists at all: *"Charged at 1, the budget stopped bounding wall: the value-leaf row dump
(d5/b20, K=3 labels) had games at 250-400 units/s, 2.5-3+ HOURS each, pinning phase A on two of them
for hours with 30 cores idle, and the mulligan generation at d1/b3 would have had ~8 s decisions."*
So price 1 has already been observed to blow up — but in **generation** contexts (the value-leaf
matrix, mulligan gen at d1/b3), not in plain goldfish at d5/b20, and the binding symptom there was
the TAIL. The nu2/nu1 arms (§6) are measuring exactly that, and any adoption has to be checked
against the generation shapes too, not only the suite.

**This is a USER-OWNED decision and is not being adopted here** (CLAUDE.md, "A USER-OWNED DECISION
IS NEVER SETTLED BY AN AGENT"): it changes play, so it moves GT on every deck whose gate the route
passes, and it is not a clean win. The recommendation, with the recovery data, goes to the user.

## 4b. WHERE THE COST ACTUALLY IS: one decision, 31 million plans, and the route is 25% of it

`perf` cannot record on this kernel (`failed to write perf data, error: Bad address`, WSL2
6.18.33.2), so this is the repo's own deterministic instrumentation instead — which is the better
instrument anyway, being exactly reproducible: `-DMTG_PROFILE=ON` counters (`src/ai/Profiler.h`)
and the per-decision work census (`MTG_TURN_CENSUS`, `src/ai/TurnCensus.h`).

**Per budget unit, the per-unit cost is TAIL-specific, not deck-general:**

| | plans / unit | plans per EnumeratePlans call | GameState copies / unit |
|---|---|---|---|
| stompy, 40 games | 1.14 | 12.9 | 0.133 |
| kittyv2, 40 typical games | 3.04 | 7.7 | 0.219 |
| **kittyv2 gi=371 (tail)** | **139.1** | **88.2** | **0.801** |

A typical kitty game enumerates FEWER plans per call than Stompy. The 11x deck-average per-unit
cost is therefore an average over a bimodal population, and sizing any lever against it is sizing
against a number no real decision had (the exact trap `TurnCensus.h` was built for).

**And within the tail game the cost is ONE decision.** Per-decision census of gi=371:

| turn | units | wall | ms/unit | u_equip_route | plurality site |
|---|---|---|---|---|---|
| 1 pre | 20,474 | 483 ms | 0.024 | 85.5% | — |
| 2 pre | 22,129 | 1,495 ms | 0.068 | 80.5% | — |
| **3 pre** | **70,077** | **133,891 ms** | **1.91** | **25.0%** | `la_cand` 24,445 (34.9%) |
| 4 pre | 1 | 6 ms | — | 0% | — |

**98.5% of the game's wall is one decision, running at 80x the per-unit cost of turn 1 of the same
game.** Note what that does to the route story: the route's unit count is almost flat across turns
(17,504 / 17,808 / 17,552 — it is hitting a cap), so on the decision that costs everything the
route is a QUARTER of the units, not the bulk. That is why the price lever, which is real, only
buys 0.856x: it is operating on 25% of the problem.

**What the expensive decision actually does** (turn 3 vs turn 1 of the same game):

| counter | turn 1 | turn 3 | ratio |
|---|---|---|---|
| `space_plans` | 49,023 | **31,144,648** | 635x |
| `enum_emitted` | 49,589 | 31,191,949 | 629x |
| `space_odo` | 2,260,008 | **167,616,938** | 74x |
| `enum_enter` | 1,098,615 | 91,533,117 | 83x |
| `cand_scored` | 403 | 24,445 | 61x |
| `rollout_calls` | 258 | 13,686 | 53x |

One decision enumerates **31.1 million plans** and walks a **167.6-million-state odometer**. For
scale, all 40 typical games together emit 1.51M plans — this single decision emits 20x that.

The shape is: **24,445 scored root candidates, each driving a rollout (13,686 calls), each rollout
re-enumerating a wide equipment board at ~2,275 plans per rollout.** With `MTG_DEDUP_CENSUS` armed,
**38.0% of those root candidates are duplicates** (`dedup_seen` 24,445, `dedup_dup` 9,302, of which
`dedup_exactdup` 3,501 and `dedup_copy_perm` 5,271).

### 4d. The candidate-mass route is CLOSED — three ways, all already documented in-tree

The 38% duplicate rate among scored candidates looks like the obvious prize. It is not available,
and the reasons are structural rather than tuning, so they are recorded here to stop the next
session re-deriving them.

**(i) `MTG_CAND_DEDUP` is inert on this deck.** Armed on gi=371, every counter is byte-identical —
`units` 70,077, `cand_scored` 24,445, `space_plans` 31,144,648, `rollout_calls` 13,686, and
`dedup_dup` 9,302 — with only run-to-run wall variance between them (repeat runs of this game span
114-132 s, so the apparent 11% was noise, not an effect).

**(ii) Because every duplicate here is a BREAKPOINT variant, and a prune upstream of a fixed-width
index cannot save work.** `dedup_why drop_dup=0 nodrop_dup=9748`, `dup_bp=9748 dup_nobp=0`. The
breakpoint wave emits variants *by rank* — `candidates scored = W x BpSearchDepth() x bases` — and
the candidate list's LENGTH appears nowhere in that product, so removing an entry only changes which
plan occupies slot `k`. The only things that CAN reduce breakpoint work are: shrink `BpSearchWidth`,
shrink `BpSearchDepth`, shrink the base fan-out, or **make an individual APPLY cheaper**.

**(iii) The two 100%-duplicate ARMS are not harvestable either, and the tree says so in advance.**
Aggregated over 1,000 games at b200: `base=1,401,797/dup=2,987 (0.21%)`, `rank=409,881/dup=246,077
(60.0%)`, **`unif=76,092/dup=76,092 (100%)`**, **`empty=204,763/dup=204,723 (99.98%)`** — two arms
carrying 13.4% of all candidate mass and producing 40 unique post-apply states between them. Since
an arm is fan-out rather than a list entry, (ii) does not forbid it. But the collapse that would
harvest it is **already ON by default** (`MTG_BP_W0_UNIF_COLLAPSE`, `MTG_BP_W0_NOBP`) and firing —
67,611 collapses on 300 games — and the repo's own miss counters say why the rest survive:

```
w0_unif_miss nomemo=0 multi=26947
```

`nomemo` is the coverage case and IS a lever; `multi` means the apply reached 2+ class-on
breakpoints, where uniform *k* is NOT rank *k* and declining it **deletes a line**. On kitty the
split is 0 / 26,947 — *all* of it is the case the identity does not cover. The comment at the
counter's definition anticipates exactly this reading: *"The first is a lever; the second is not,
and no amount of measured post-apply duplication makes it one."*

**So the only non-lossy lever left on the candidate side is the last item of (ii): make an
individual APPLY cheaper** — which is the wide-board plan enumeration, 88-116 plans per
`EnumeratePlans` call inside ~13,686 rollouts. That is where the remaining order of magnitude is,
and nothing above touches it.

**Two further negative results worth keeping, so nobody re-runs them:**
* `MTG_FOLD_SEARCH_ODO=1` (the canonical-prefix fold adopted on Snow) moves gi=371's plan count by
  **-0.19%** (15,648,149 vs 15,677,338). This is genuine menu WIDTH, not the prefix redundancy that
  lever folds. Do not reach for it here.
* A damage pre-filter on the route gate is still ruled out (~90% of trials find a kill).

### 4c. THE MECHANISM, exactly: a completed detection collapses the decision; an interrupted one costs 128 seconds

The per-decision census of the WORST decision in the sample (gi=369 turn 3) shows the budget being
overrun 26x — 471,053 units against an 18,000-unit budget — and the split says why:

| site | units | share |
|---|---|---|
| `u_la_cand` | 152,819 | 32.4% |
| `u_rollout_step` | 148,888 | 31.6% |
| `u_greedy_fallback` | 146,991 | 31.2% |
| **`u_equip_route`** | **18,000** | **3.8%** |

`u_equip_route` is **exactly the budget**. The route consumes 100% of the decision's allowance, the
budget is then exhausted, and everything downstream runs in fallback mode — which still costs, and
still charges, but can no longer be bounded. Across gi=371's decisions the route takes 95-99% of
budget on every single one.

**And completing the detection is worth ~300x on the decision.** Same game, same seed, only the
per-link price changed (`MTG_TURN_CENSUS` + `MTG_DEDUP_CENSUS`, gi=371 turn 3):

| | price 8 (shipped) | price 4 |
|---|---|---|
| units | 70,077 (3.9x over budget) | **18,006** (the budget binds) |
| wall | 128,083 ms | **436 ms** |
| `space_plans` | 31,144,648 | **10,566** |
| whole game | 242.6 s, reported SLOW-GAME | **< 30 s, not slow at all** |
| win turn | 4 | 4 (UNCHANGED) |

So the price is not buying "more detection" in some diffuse sense. At price 8 the route runs out of
links **mid-detection**, no certificate is produced, the go-off cut does not fire, and the decision
pays the full 31-million-plan enumeration. At price 4 the same detection **completes** inside the
same budget, the cut collapses the frame, and the enumeration never happens. A half-finished
detection is the worst of both worlds: it spends the entire budget and collapses nothing.

That is the sharpest available answer to "is the budget well spent": on this deck the detector is
the single most profitable thing the budget can buy, because success removes essentially the whole
decision — and the shipped price was set just high enough to make it fail.

**So the largest remaining non-lossy headroom is wide-board plan enumeration, not the route.** That
is a "COLLAPSE WASTED SEARCH UNCONDITIONALLY" target needing no user ruling, and it has direct
precedent (pooling the interchangeable sac outlets took the worst odometer shape 4.61e+03 ->
2.02e+03). The symmetry to attack on an equipment board is *which* Equipment goes on *which*
interchangeable host; the existing `sub_rej_dupsrc` fires only 4,726 times against 31M plans, so it
is not catching it. NOT yet implemented or measured — it is the recommended next piece of work.

## 5. The lever triage under the user's completeness rule

| lever | shape | findable at unlimited budget? | verdict |
|---|---|---|---|
| `MTG_EQUIP_ROUTE_NODE_UNITS` | price, read only to decrement a budget | **yes, provably** — `ChargeDecisionUnits` returns on `budget == nullptr` and the cut test requires `budget != nullptr`, so at an unlimited root the price is never read | legitimate; this is the candidate |
| `MTG_EQUIP_ROUTE_TOPMAIN` | predicate in the GATE (`EquipRoutePiecesInPlace`) | **not by construction** — it is consulted at every budget, so the shape cannot guarantee it. **Measured: yes** (§5a) | a GATE, so it needs its completeness earned by measurement rather than argued from the code — and it earns it |

**Read §5a before acting on the shape column.** The honest sequence of conclusions here was:
TOPMAIN is a gate predicate, gates are consulted at every budget, therefore it *can* delete a line
that no budget would then find — so by shape it is the lossy kind and the user's rule puts it out.
That reasoning is sound about the *shape* and it is the right default. But it is a statement about
what the code could do, not about what it does, and the measurement in §5a says the frames it skips
really are redundant: byte-identical at 50x budget over 250 games, byte-identical at unlimited
budget on every game that moved at 10x, and 44/44 identical at the recovery bar.

So both levers are admissible under the user's rule, by different routes — the price by
construction, TOPMAIN by evidence — and §5c shows they stack. **The wave / position-aware-price
design is therefore not needed here**; it stays on file in CLAUDE.md as the sanctioned pattern for
the next restriction that turns out to be genuinely lossy.

At the shipped b20 they buy almost the same thing (price 4: -24 at 0.856x; topmain: -25 at 0.879x,
and the **same 15 slower games** — gi=470/630/723/938/1112 …), because at that budget they do the
same thing: free up budget that then finds the turn-3 kill. Their saving mechanisms differ, which is
why stacking them compounds.

### 5a. TOPMAIN is lossy in SHAPE but — on the sharpest sample available — NOT in FACT

This reverses the shape-based verdict above, so it is recorded in full rather than quietly
amended.

**A finite-budget A/B cannot answer the question.** At b200 (10x shipped), 1,000 games paired:
993 identical, 6 faster, 1 slower, net -6 game-turns, digests different. That is NOT evidence of
lossiness — at any finite budget a work-removing change reallocates the budget and therefore moves
play whether or not it removed any capability. It is exactly the confound the user's rule exists to
defeat, which is why the rule says to test at the TOP of the range.

**At unlimited budget there is no budget to reallocate, so any difference is a capability
difference.** Re-running precisely the 7 games that moved at b200, at `--budget-ms 0`, both arms,
same thread count:

| game | b200 base -> topmain | unlimited: digests | unlimited: win turn | base ms | topmain ms |
|---|---|---|---|---|---|
| g36 | 7 -> 5 | `3f617ffca422ae54` = `3f617ffca422ae54` | 5 = 5 | 2,400 | 1,738 |
| g71 | 6 -> 5 | `5e00f738f8931f3a` = `5e00f738f8931f3a` | 5 = 5 | 1,030 | 449 |
| g341 | 5 -> 4 | `fcecf5ad14fdd98a` = `fcecf5ad14fdd98a` | 4 = 4 | 7,897 | 4,273 |
| g470 | 6 -> 5 | `42acb834108dd33f` = `42acb834108dd33f` | 5 = 5 | 641 | 426 |
| g697 | 5 -> **6** | `d556cdcf91817ae7` = `d556cdcf91817ae7` | 5 = 5 | 5,687 | 2,982 |
| g741 | 6 -> 5 | `78bff5cac8463170` = `78bff5cac8463170` | 5 = 5 | 1,480 | 1,193 |
| g957 | 5 -> 4 | `a4b63a59f1c5a729` = `a4b63a59f1c5a729` | 4 = 4 | 760 | 470 |

**All 7 byte-identical, including the one game TOPMAIN made worse at b200 — while doing strictly
less work on every one** (units e.g. 6,871 -> 6,596 on g470). Identical answers from less work at
unlimited budget is the signature of a **sound identity fold**, which is CLAUDE.md's "collapse
freely, default ON" category, not the "projection / heuristic narrowing" category.

So `EquipRoutePiecesInPlace`'s redundancy argument — the first main's playout plays the whole turn
out, drawing the real library top, so a later-main frame re-derives a conclusion the frame already
had — appears to be CORRECT, and the b200 differences are budget reallocation, not blindness.

**And the difference CONVERGES TO ZERO as the budget grows, which is the user's rule stated as a
measurement:**

| budget | games | games differing | paired net | wall vs base |
|---|---|---|---|---|
| b20 (shipped) | 3,000 | 55 (40 better, 15 worse) | -25 | 0.879x |
| b200 (10x) | 1,000 | 7 (6 better, 1 worse) | -6 | 0.903x |
| **b1000 (50x)** | 250 | **0** — digest `06f74dd3a70b9f7f` on BOTH arms | 0 | **0.746x** |
| unlimited | 7 (the ones that moved at b200) | **0** | 0 | ~0.65x |

**THE b1000 ROW IS NOT EVIDENCE OF BYTE-IDENTITY AND WAS WRONGLY PRESENTED AS SUCH (corrected
2026-10-06, on the user's challenge: "I don't think 250 games is really enough").** It is not:

* The difference rate at b200 was 7/1000 = **0.70%**. If that rate were entirely unchanged at
  b1000, the chance of observing 0 differences in 250 games is `(1-0.007)^250` = **17%**. A null
  result at that sample size was always reasonably likely even if nothing had converged.
* Going the other way, the rule of three puts the 95% upper bound from 0/250 at **1.2%** — which
  still CONTAINS the 0.70% measured at b200.

So the 250-game run could not distinguish "converged to zero" from "unchanged". It is consistent
with the fold reading and it is not evidence for it.

**Two further weaknesses in the unlimited-budget evidence, recorded because they are the same
mistake in a different place.** The 7 games re-run at `--budget-ms 0` were selected as the games
that moved *at b200* — but moving at b200 is budget-REALLOCATION sensitivity, whereas the
population that matters is games where the first main's playout genuinely fails to cover a
later-main kill. Those two sets may be disjoint, in which case the sample is enriched for the wrong
thing. The 36- and 44-game recovery sets inherit the same defect (selected as "got slower at b20").

### 5a-bis. THE POWERED RUN REFUTES THE FOLD READING. TOPMAIN IS LOSSY.

b1000 (50x shipped), **10,000 games**, paired per game:

| | differing games | rate | faster | slower | paired net |
|---|---|---|---|---|---|
| b200, 1,000 games | 7 | 0.70% | 6 | 1 | -6 |
| b1000, **250** games | 0 | 0% (CI to 1.2%) | 0 | 0 | 0 |
| **b1000, 10,000 games** | **63** | **0.63%** | 46 | **17** | -32 |

**The rate does not converge. It is 0.63% at 50x the shipped budget against 0.70% at 10x** — flat,
not falling. The apparent convergence was entirely the 250-game sample, exactly as the user
suspected.

**And 17 games are WORSE at 50x the shipped budget**, which is the thing the user's rule forbids:
`5 -> 6` in 11 games, `6 -> 7` in 2, and **`5 -> 7` in 4 games** — a two-turn loss with fifty times
the budget available. If these were budget-reallocation artefacts they should wash out as the
budget grows; across a 5x budget increase (b200 -> b1000) the rate is unchanged.

**So the original shape-based verdict was right and the reversal in §5a was wrong.** The sequence
is worth keeping because the failure mode is reusable: (1) shape says a gate predicate is consulted
at every budget and can therefore delete a line — correct, and the right default; (2) an
under-powered null reversed it; (3) the user challenged the sample size; (4) the powered run
restored the original verdict. **A null result from a sample that could not have detected the
effect is not evidence of absence, and "byte-identical" is a claim that needs power behind it.**

Note the 250-game sample also under-stated the COST by 3x (1,323 ms/game vs the 10,000-game run's
4,091 ms/game) because games 0-249 never reach the heavy tail. One unrepresentative sample, two
wrong conclusions.

### 5a-ter. ...but at TRULY unlimited budget those same games are byte-identical. The honest status.

The 17 games TOPMAIN made worse at b1000 were re-run at `--budget-ms 0` (16 of 17; g8496 did not
finish — an unlimited-budget game on this deck has no bound, and one of the 16, g2572, took 24
minutes):

| games | at b1000 (50x) | at `--budget-ms 0` |
|---|---|---|
| 16 of the 17 TOPMAIN made worse | 5 -> 6 or 5 -> 7 | **16/16 byte-identical, same digest, all win turn 5** |

So the b1000 losses are budget **reallocation**, not capability deletion: with no budget to
reallocate, TOPMAIN finds exactly what base finds. That reconciles the two results — a work-removing
change perturbs play at *any* finite budget (which is why the rate does not decay from b200 to
b1000: the plan space is effectively unbounded relative to any budget, so freed work always goes
somewhere), and only at a genuinely unlimited root does the perturbation vanish.

**One of my own earlier objections was wrong and should not be reused.** I worried that selecting
games which moved at a finite budget enriches for reallocation sensitivity rather than capability
sensitivity, possibly a disjoint population. That is backwards for the main case: a game whose only
kill sits in a frame TOPMAIN skips would differ at *every* budget, so it must appear in the
finite-budget diff set — screening that set at unlimited is therefore a sound way to detect
capability loss. Across **67 game-instances** tested at unlimited budget (16 + the 7 b200-movers +
the 44 recovery-bar games), TOPMAIN is byte-identical every time.

**The residual gap, stated precisely because it is the only one left.** A capability loss would be
invisible to this method in one case: a game where base ALSO fails to find the kill at the finite
budget (so no finite-budget diff, so the game is never selected), while at unlimited base finds it
and TOPMAIN cannot. Closing that needs a broad, UNSELECTED unlimited-budget A/B, which is the one
experiment this deck makes expensive (a 1,000-game `--budget-ms 0` run was abandoned with 13 games
still running after 31 minutes).

**So the status is: PROBABLY SOUND, with a named residual gap — not proven, and not refuted.** I
reached a confident verdict twice and was wrong twice (first on an under-powered null, then on
finite-budget losses that turn out to be reallocation), so this file does not offer a third. What
should actually drive the decision is the *kind* of evidence available for each lever:

* the **price** is inert at unlimited **by construction** — `ChargeDecisionUnits` returns on
  `budget == nullptr` — a proof, corroborated by 36/36 identical at the bar;
* **TOPMAIN** is inert at unlimited only **empirically** — 67/67 instances, with the gap above.

Both levers' finite-budget losses recover at the repo's adoption bar, so they are the same kind of
cost. The asymmetry is in the strength of the completeness guarantee, and that is a judgement for
the user.

**The alternative remains available either way**, and is the user's own: because TOPMAIN has good
results (-25 game-turns and 0.879x at b20, stacking to 0.714x), a **budget-staged admission order**
— wave *k* = turn *k* fully explored — would deliver the same saving with completeness by
construction rather than by sample, which would make the residual gap moot.

Superseded text follows for the record; read §5a-bis and this section together.

**~~TOPMAIN IS THEREFORE OUT as a gate~~**, under the user's rule ("I won't accept a lossy solution...
everything should be findable at unlimited or large budgets"). What remains open is the sanctioned
alternative the user named in the same breath: because TOPMAIN *does* have good results
(-25 game-turns and 0.879x wall at b20, and it stacks to 0.714x), it is a candidate for a
**budget-staged admission order** — wave *k* = turn *k* fully explored, per the user's preference
that a binding budget finish a turn before reaching the next — which keeps every frame reachable at
a large budget while still concentrating a small budget where the yield is. That is now the live
design question rather than a hypothetical.

**What this does and does not license.** The unlimited arm is 7 games, deliberately chosen as the
ones most likely to expose a difference, which makes it the sharpest available probe but not a proof
over the whole space; b1000's 250 games are a broader but still finite check. A wider unlimited-budget A/B (1,000 games) is running to look for a counterexample, together
with a CONTROL that must come back byte-identical if the "price is provably inert at unlimited"
claim is true (nu4 at `--budget-ms 0` vs base at `--budget-ms 0`), and a COMBINED arm, since the two
levers are independent — one prices links, the other skips frames — and should stack if topmain is
sound. Adopting TOPMAIN remains the user's call: it is a gate, and a gate justified by an argument
rather than by construction deserves the wider sample before it ships.

`TOPMAIN`'s in-code justification is a *redundancy* argument (§3). If that argument is true the
gate is a sound identity fold rather than a capability deletion — and the only way to tell is to
run it at the **top** of the budget range, because at the shipped b20 neither arm reaches the line
and the lossy lever looks free. If it does move games at a large budget, the sanctioned route is
not the gate but a **budget-staged admission order** (wave k = turn k fully explored, per the
user's preference that a binding budget finish a turn before reaching the next), which keeps every
line findable while still spending small budgets where the yield is.

Explicitly ruled out: a damage pre-filter on the gate. The route's own measurement says ~90% of
trials DO find a kill, so there is almost no hopeless-board detection to skip.

### 5c. The two levers STACK, and the combination is the best configuration measured

They are independent by construction — one prices the route's links against the decision budget,
the other skips route frames after the turn's first main — so if TOPMAIN is the redundancy fold
§5a says it is, stacking should buy its saving on top of the price's. It does. All arms: 3,000
games, kittyv2 shipped policy, seed 960000, paired per game.

| config | avg win turn | wall | vs base | net | faster/slower | slow games | tail sum | recovery bar |
|---|---|---|---|---|---|---|---|---|
| base (price 8) | 3.9163 | 7,324 | 1.000x | — | — | 31 | 5,618 s | — |
| price 4 | 3.9083 | 6,272 | 0.856x | -24 | 41 / 15 | 21 | 4,451 s | 36/36 identical |
| price 2 | 3.9017 | 6,204 | 0.847x | -44 | 64 / 24 | 21 | 4,215 s | 36/36 identical |
| price 1 | 3.9003 | 5,986 | 0.817x | -48 | 79 / 34 | 20 | 3,777 s | 36/36 identical |
| topmain alone | 3.9080 | 6,440 | 0.879x | -25 | 40 / 15 | 26 | 4,940 s | 44/44 identical |
| price 2 + topmain | 3.9017 | 5,270 | 0.719x | -44 | 76 / 35 | 18 | 3,473 s | 44/44 identical |
| **price 1 + topmain** | **3.8993** | **5,227** | **0.714x** | **-51** | 89 / 40 | **18** | **3,449 s** | 44/44 identical |

**Adding TOPMAIN to a price is answer-neutral and ~15% cheaper.** Paired directly against the
price-only arm: `price 2 + topmain` vs `price 2` is net **±0** game-turns (2,977 of 3,000
identical, 12 faster / 11 slower) for 0.847x -> 0.719x; `price 1 + topmain` vs `price 1` is net
**-3** (2,976 identical, 13 / 11) for 0.817x -> 0.714x. Neutral answer, materially less work — a
sound fold's signature, consistent with the b1000 byte-identity in §5a.

**Every candidate passes both halves of the adoption bar.** Two pooled recovery runs at
`--depth 8 --budget-ms 0` — 36 games for the price-only arms, 44 for TOPMAIN and the stacked ones
(each the union of every game that arm made slower) — came back **identical to base on every
game, for every arm**. That is 80 distinct games at unlimited budget across six configurations
with no divergence, which is simultaneously the recovery bar and the strongest available evidence
for both the price's inertness and TOPMAIN's soundness.

## 5b. Recommendation (for the USER to rule on — it changes play, so it moves GT)

**Lower `MTG_EQUIP_ROUTE_NODE_UNITS` from its default of 8. Prices 1, 2 and 4 ALL pass the repo's
adoption bar** (aggregate net <= 0, and 36/36 slower games recover at `--depth 8 --budget-ms 0`),
and all three are provably — now also measurably — inert at unlimited budget. Which one to ship is
a genuine judgement call, so it is the user's:

| | price 4 | price 2 | price 1 |
|---|---|---|---|
| aggregate | -24 game-turns | -44 | **-48** |
| wall | 0.856x | 0.847x | **0.817x** |
| tail sum | 4,451 s | 4,215 s | **3,777 s** |
| games perturbed | **56** (41 better / 15 worse) | 88 (64 / 24) | 113 (79 / 34) |

Lower is better on every aggregate axis, with diminishing returns below 2 (quality -44 -> -48 while
wall still falls 0.847x -> 0.817x). The axis that argues the other way is **churn**: price 1 moves
113 of 3,000 games, which is more GT movement to review and more exposure to a slower game that a
future engine change stops recovering.

**Recommended NOW: price 2 ALONE** — 3.9017 avg, 0.847x wall, net -44 game-turns, 36/36 recovery.
It banks nearly all the aggregate win available from the price at meaningfully less churn than
price 1 (88 games moved vs 113), and it rests only on the inertness argument, which is sound by
construction and measured.

**TOPMAIN is OUT as a gate — the powered re-test refuted it (§5a-bis).** At 50x the shipped budget
over 10,000 games it still changes 0.63% of games (flat against b200's 0.70%, not converging) and
makes **17 games worse**, four of them by two full turns. Do not ship it and do not quote the
0.719x / 0.714x stacked numbers as available. Its win is real, so the sanctioned route to it is a
budget-staged wave, not the gate (§5a-bis).

**Sizing this against the actual complaint — the screens.** At 0.714x, kittyv2 goes from 2.48 to
~1.77 thread-s per game, so a 16-arm x 16,000-game screen goes from ~5.5 h to ~3.9 h on 32 cores.
That is real but it is not a step change, and it should not be sold as one: the step change is in
§4b/§4c, the 31-million-plan enumeration on wide boards, which none of these levers touches. The
screens want that fix, and it is non-lossy and needs no ruling.

Two caveats that belong in the same breath:
* **The floor is real.** `NodeUnits()`'s comment records price 1 producing 2.5-3+ HOUR games in the
  value-leaf matrix and ~8 s decisions in mulligan gen at d1/b3. Those are *generation* shapes, not
  the suite, so a price adopted on suite evidence must be re-checked against them before any
  generation run.
* **It is a per-deck question wearing a global flag.** The price is a process-wide `static const`
  env read, so changing the default moves every deck whose board passes the route's gate. The repo
  already has the right pattern for this — Snow's per-deck charge opt-in (`SnowProvider W=16`) —
  and a per-deck opt-in is the safer shape if any non-kitty deck moves.

**The iso-wall question the user asked — "make sure the budget is well spent" — has a clean answer:
the budget was MIS-ALLOCATED, not under-provisioned.** Spending the price lever's saving on more
budget buys very little; spending it on a lower price buys a lot:

| arm | avg win turn | wall |
|---|---|---|
| base @ b20 | 3.9163 | 7,324 |
| nu4 @ b20 | 3.9083 | 6,272 |
| nu4 @ b24 | 3.9077 | 6,574 |
| nu4 @ b30 | 3.9040 | 6,910 |
| nu2 @ b20 | 3.9017 | 6,204 |
| **nu1 @ b20** | **3.9003** | **5,986** |

`nu1` at the SHIPPED budget beats `nu4` at 1.5x the budget on both axes, for 13% less wall. Raising
the budget is a strictly worse use of the same compute than letting the detector finish — which
follows directly from §4c: extra budget mostly buys more of the 31-million-plan enumeration, while
a completed detection deletes it.

**Secondary idea, NOT recommended over the price, recorded because the data suggests it:** since an
interrupted detection spends the budget and collapses nothing, the engine could decline to *start* a
route trial it cannot fund to completion. That is budget-conditional rather than capability-removing
(at a larger budget it runs), so it is a legitimate WAVE in the user's sense. But it only recovers
the ~18,000 wasted units; it does not avoid the 31M-plan enumeration that follows a non-collapse.
Completing the detection is worth ~300x more than not wasting the attempt, so the price is the
better lever and this is at most a complement.

## 6. What ran, what is left

Everything above is DONE. Arms ran as serial chains (serial because `NodeUnits()` and `s_topmain`
are `static const` env reads, so a price or gate arm cannot be a job inside one manifest — a
genuine data dependency, which is the one case the pooling rule's "waves are a loop" allows; within
each arm the 3,000 games are one pooled batch, one tail). Scripts and raw output:
`logs/kittyv2_swaps/levers/` — `run_quality.sh`, `run_overnight.sh`, `run_unlim.sh`,
`run_stack.sh`, `run_recovery2.sh`, with `pair.py` (paired per-game read, losses penalised to
`max_turns+1`) and `mkrecovery.py` (one pooled manifest of one job per affected game).

**NOTHING HAS BEEN ADOPTED.** No default changed; the only `src/` edit in this work is a corrected
comment. Every number above is from env-var arms.

### Left to do, in value order

1. **THE BIG ONE, and it needs no ruling: make an individual APPLY cheaper on a wide board**
   (§4b/§4c, narrowed by §4d). One decision enumerating 31-145 million plans is where the remaining
   order of magnitude is. §4d closes every *candidate-side* route to it — the duplicates are all
   breakpoint variants upstream of a fixed-width index, and the two 100%-duplicate arms are all
   `multi`, which the identity does not cover — so the target is specifically the **88-116 plans per
   `EnumeratePlans` call** inside the ~13,686 rollouts, not the candidate count. Non-lossy,
   pre-approved by "COLLAPSE WASTED SEARCH UNCONDITIONALLY". Start from the equip-assignment
   symmetry (which Equipment on which interchangeable host). Already excluded: `MTG_FOLD_SEARCH_ODO`
   (-0.19%), `MTG_CAND_DEDUP` (byte-identical), arm removal (lossy).
2. **The user's ruling** on §5b, then rebaseline the smoke and regression tiers for whichever
   configuration is taken, and re-check the chosen price against the GENERATION shapes (value-leaf
   matrix, mulligan gen at d1/b3) before any generation run — that is where price 1 historically
   blew up.
3. **A wider unlimited-budget sample for TOPMAIN**, if the user wants the gate held to a higher bar
   than the 250-game b1000 byte-identity plus 44/44 at the recovery bar. Note that a full
   unlimited-budget batch on this deck has no bound: a 1,000-game `--budget-ms 0` run was abandoned
   with 13 games still running after 31 minutes at 13/30 workers. Size it small and targeted.
4. **The viewer's node cap**, independent of all of the above: bounded in LINKS rather than work,
   13 minutes per decision, and it makes `regression.sh --regression` unable to finish —
   `docs/design/equip-route-viewer-dfs-cap-is-mis-sized.md`. Until then use `VPC_SKIP=1` and report
   the reference gate as UN-RUN. The constant's re-size is the user's call (it trades menu
   completeness).

### Method notes worth keeping

* **`perf` does not work on this kernel** (`failed to write perf data, error: Bad address`, WSL2
  6.18.33.2). Use the repo's own instrumentation instead, which is deterministic and therefore
  better for A/B work: `cmake -S . -B build-instr -DMTG_PROFILE=ON -DCMAKE_BUILD_TYPE=Release`
  (the one place a raw `cmake` is correct — `build.sh` has no mode for it and the build type is
  explicit), plus `MTG_TURN_CENSUS=<path>` and `MTG_DEDUP_CENSUS=1`.
* **Units are not comparable across price arms.** A lower price charges the same work fewer units
  by construction, so `units` moves even when nothing else does. Compare `ms`.
* **Wall is tail-bound on this deck and `ms` is thread time**, so never derive a wall estimate from
  `ms / threads`: 3,000 games read 2/30 workers busy at ten minutes and 0/30 at twenty, with two
  games holding 30% of the run.
* The census CSV is **tab**-separated with a leading `#` comment line, and its `u_*` columns sum to
  `units`.
