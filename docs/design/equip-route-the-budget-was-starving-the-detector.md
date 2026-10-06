# The equip route's per-link price was starving the detector that pays for itself (2026-10-06)

Status: MEASURED, candidate identified, NOT adopted. Runs in flight; §6 records what is still open.

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

| arm | price | avg win turn | wall (thread-s) | vs base | faster/slower | paired net |
|---|---|---|---|---|---|---|
| nu4 | 4 | **3.9083** | **6,272** | **0.856x** | 41 / 15 | **-24 game-turns** |
| base | 8 (shipped) | 3.9163 | 7,324 | 1.000x | — | — |
| nu12 | 12 | 3.9207 | 8,183 | 1.117x | 9 / 23 | +13 game-turns |
| nu32 | 32 | 3.9380 | 9,877 | 1.349x | 32 / 100 | +65 game-turns |

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

It is **not** a clean win in this repo's strict sense (15 games got worse), so it is subject to the
adoption bar: every slower game must recover at `--depth 8 --budget-ms 0`, and the deck aggregate
must be net <= 0 on a large held-out sample. The aggregate half passes at -24 game-turns on 3,000
games; the per-game half is running (§6).

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

**Two negative results worth keeping, so nobody re-runs them:**
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
| `MTG_EQUIP_ROUTE_TOPMAIN` | predicate in the GATE (`EquipRoutePiecesInPlace`) | **no** — consulted at every budget | lossy as a gate — **and it is DOMINATED, so the question is moot** |

**TOPMAIN is dominated by the safe lever, which settles it without needing the wave.** At b20, 3,000
games paired:

| arm | avg win turn | wall | faster/slower | paired net | lossy? |
|---|---|---|---|---|---|
| nu4 (price 4) | 3.9083 | **0.856x** | 41 / 15 | **-24** | no, provably |
| topmain | 3.9080 | 0.879x | 40 / 15 | -25 | **yes, by construction** |

They buy the *same* quality (-24 vs -25, and the **same 15 slower games**, gi=470/630/723/938/1112
…), because they do the same thing: free up budget that then finds the turn-3 kill. But the
non-lossy one is also the cheaper one. So TOPMAIN has "good results" exactly as the user
anticipated — and there is still no reason to take it, because the price lever already captures its
entire win without removing capability. **The wave/position-aware-price design is therefore NOT
needed for this lever**; it is kept on file in CLAUDE.md as the sanctioned pattern for the next
restriction that is *not* dominated.

The b200/b1000 arms still run (§6) to record whether TOPMAIN is lossy *in fact* and not merely in
shape, because that is a reusable fact about the redundancy argument in
`EquipRoutePiecesInPlace`'s comment — but it can no longer change the recommendation.

`TOPMAIN`'s in-code justification is a *redundancy* argument (§3). If that argument is true the
gate is a sound identity fold rather than a capability deletion — and the only way to tell is to
run it at the **top** of the budget range, because at the shipped b20 neither arm reaches the line
and the lossy lever looks free. If it does move games at a large budget, the sanctioned route is
not the gate but a **budget-staged admission order** (wave k = turn k fully explored, per the
user's preference that a binding budget finish a turn before reaching the next), which keeps every
line findable while still spending small budgets where the yield is.

Explicitly ruled out: a damage pre-filter on the gate. The route's own measurement says ~90% of
trials DO find a kill, so there is almost no hopeless-board detection to skip.

## 6. Open / in flight

Running as one serial chain (`logs/kittyv2_swaps/levers/run_overnight.sh`; serial because
`NodeUnits()` and `s_topmain` are `static const` env reads, so a price arm cannot be a job inside
one manifest — the one case the pooling rule's "waves are a loop" allows):

1. **Profile** of gi=371 (the worst per-unit game, 2.15 ms/unit) under `build/Profile` — where the
   per-unit cost goes. Any waste found here is a non-lossy win needing no ruling.
2. **Is TOPMAIN lossy?** `b200` (10x shipped), 1,000 games, paired, topmain off vs on.
3. **Price curve downward:** nu2, nu1 at b20, 3,000 games, paired.
4. **Recovery bar** at `--depth 8 --budget-ms 0` for every slower game of every candidate price,
   as one pooled manifest (`mkrecovery.py`).
5. **"Budget well spent":** nu4 at b24 and b30 — whether spending nu4's 14.4% wall saving on
   budget buys more than base got for the same wall.

Also open, and independent of all of the above: the viewer's node cap is mis-shaped (bounded in
LINKS, not work), which costs 13 minutes per decision and makes `regression.sh --regression`
unable to finish — `docs/design/equip-route-viewer-dfs-cap-is-mis-sized.md`. The constant's
re-size is the user's call because it trades menu completeness.
