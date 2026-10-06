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

| arm | price | avg win turn | wall (thread-s) | vs base | paired vs base |
|---|---|---|---|---|---|
| base | 8 (shipped) | 3.9163 | 7,324 | 1.000x | — |
| nu4 | 4 | **3.9083** | **6,272** | **0.856x** | 41 faster / 15 slower / 2,944 identical, net **-24 game-turns** |

So halving the price is **better and cheaper at the same budget** — the budget buys more detection
*and* less total work. The dominant paired transition is `4 -> 3` in **25 games**: the route is
finding the turn-3 kill in games where it previously did not.

**The mechanism is that the route is a detector whose success removes work.** A verified lethal
fires the go-off cut, which collapses the frame to one plan; more detection therefore means more
collapses and shorter games, and both of those cut the number of frames still to be searched. The
route pays for itself, so pricing it high starves the thing that was reducing the bill. This is the
"COLLAPSE WASTED SEARCH UNCONDITIONALLY" dynamic seen from the other end.

It is **not** a clean win in this repo's strict sense (15 games got worse), so it is subject to the
adoption bar: every slower game must recover at `--depth 8 --budget-ms 0`, and the deck aggregate
must be net <= 0 on a large held-out sample. The aggregate half passes at -24 game-turns on 3,000
games; the per-game half is running (§6).

## 5. The lever triage under the user's completeness rule

| lever | shape | findable at unlimited budget? | verdict |
|---|---|---|---|
| `MTG_EQUIP_ROUTE_NODE_UNITS` | price, read only to decrement a budget | **yes, provably** — `ChargeDecisionUnits` returns on `budget == nullptr` and the cut test requires `budget != nullptr`, so at an unlimited root the price is never read | legitimate; this is the candidate |
| `MTG_EQUIP_ROUTE_TOPMAIN` | predicate in the GATE (`EquipRoutePiecesInPlace`) | **no** — consulted at every budget | lossy as a gate; out unless §6 shows it answer-identical at a large budget |

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
