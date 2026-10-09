# The keep-gen cost tail: what is established, and the two explanations that did NOT survive

**Status: PARTIALLY DIAGNOSED 2026-10-09, cause NOT found, nothing fixed.** No engine change was
made: a generation was in flight and its freeze keys on `HEAD:src`. This doc was rewritten after its
first version made two claims the evidence contradicts; both retractions are kept below on purpose,
because each is a reusable methodological trap.

## Established

Mid-run state of the v3 exhaustive keep generation (`recipe fast`, `R=30`, floor `R=2`, `K=15`,
`depth=1`, `budget_ms=3`, `max_turns=8`), ~8 h in, ~32 cores:

| | |
|---|---|
| rollouts completed | 369,152 |
| rollouts logged SLOW (>30 s) | 5,410 = **1.45%** |
| core-hours inside those | **197.6** of ~259 ⇒ **~75% of the machine** |
| worst single rollout | **3,405,242 ms = 56.8 minutes** |

**The tail is the FLOOR, not the refinement.** Cost by rollout index:

| r | records | core-h | share of tail |
|---|---|---|---|
| 0 | 2,418 | 90.4 | 45.8% |
| 1 | 2,415 | 89.7 | 45.4% |
| ≥2 | 577 | 17.5 | 8.8% |

The floor rolls r=0 and r=1 for every cell-side, so **91% of the tail is the two unavoidable
rollouts every cell gets**, not escalation on ambiguous cells.

**The cost lives beyond the horizon, where NO BUDGET APPLIES.** This is the structural point and the
code states it outright (`DecisionProviders.cpp` ~23275): the beyond-horizon site
(`TurnSolver::InRolloutPlayout`) and the depth-0 runner are

> the places that still carry a fixed cap are the places that have **NO decision budget to charge**
> and are owed no completeness

so they are bounded by `MTG_EQUIP_ROUTE_ROLLOUT_NODES` links alone. At `depth=1` essentially the whole
trial game is played there, so the generator's `budget_ms: 3` **cannot bound its dominant cost**. A
link cap is a count of links, not a bound on work — which is how one rollout reaches 56 minutes.
This is the same complaint as `docs/design/the-budget-is-denominated-in-the-wrong-unit.md`.

**A fixed 64-game battery got ~9x slower.** The rollout-config play digest is a fixed battery at the
same `d1/b3`: **v2 5 s, v3 45-46 s** (consistent across three v3 runs). This is the cleanest signal
that per-rollout cost rose, and it is free of any cell-space or probe-carry effect. It is not a pure
*engine* measurement, because the deck also differs — but 9x from four card changes is implausible.

## RETRACTED #1 — the list did not do this, and the slow-cell characterisation cannot explain it

The first version characterised the tail as **Sram + Remote Farm, no Puresteel, no body, no
auto-equip**, and offered it as the explanation for v3 costing ~2x v2. The characterisation is
correct; the explanation is **wrong and was a non-sequitur**, as the user pointed out: *"that is
unchanged from V2... We made very few changes from the V2 list."*

Sram ×4, Puresteel ×4, Remote Farm ×4 and all sixteen 0-cost shields are **identical in both lists**.
A property shared by both lists cannot explain a difference between them. **150 commits touched
`src/` between v2's generation engine (`8d165350`, Oct 3) and v3's (`c62c2957`, Oct 8)**; the list
delta is four cards (Adelbert 0→1, Cid 1→0, Gauntlets 3→4, Skateboard 2→1).

What *is* a real and striking finding is that the tail's composition **inverted**:

| present in the slow tail | v2 | v3 |
|---|---|---|
| **Puresteel Paladin** | **56.0%** | **0.1%** |
| Sram, Senior Edificer | 58.3% | 77.5% |
| Colossus Hammer | 12.4% | 44.4% |

In v2 the expensive cells *were* the Puresteel cells (free equip under metalcraft explodes the route
search — the known hole). In v3 they are the cheapest. Something between the two engines made
go-off hands terminate early, and left everything else paying full price.

## RETRACTED #2 — the "14.5x worse tail than v2" was confounded by PROBE-CARRY

v2's logged run reports:

```
[keepgen] PROBE-CARRY: reused 186476 cell-sides (r=0/cell) ... -> rolling only r>=1
```

**v2's run never rolled r=0** — and r=0 alone is 45.8% of v3's tail cost. So v2's 13.7 core-hour tail
is missing the single most expensive rollout of every cell. Comparing it to v3's 197.6 raw is invalid;
the honest like-for-like is **~7.8x**, and v3 is not finished. *Lesson: before comparing two
generations, diff their carry/resume lines — a reused artifact silently removes a cost class.*

## REFUTED — the beyond-horizon 128-link DFS is not the cost driver

The prime suspect was `9b117f4c` (authored 2026-10-04): the beyond-horizon equip-route DFS default
went certificate-only → **128 links**. Its justification — *"a rollout that FINDS the kill ends
there, which is why a small cap is CHEAPER than no DFS at all"* — was measured on the **14 reference
games, all of which the deck wins**, while the generator enumerates mostly unwinnable hands. The
predicted failure was that the DFS would pay full price and earn no early exit there.

**Measured, and it does not hold.** Paired bench (`MTG_KEEP_BENCH=32`, identical stride-sampled
size-7 floor cells, 4 pinned cores, on a copy of the deck dir; `units` is deterministic so the live
generation's contention cannot corrupt it):

| | `NODES=128` (default) | `NODES=0` (certificate only) |
|---|---|---|
| units / rollout | 6,808.3 | 4,209.6 |
| **CPU ms / rollout** | **1012.7** | **1011.1** |
| wall (contended) | 16.4 s | 21.2 s |
| win-turn digest | `7e5a565b914cfb53` | `75782fe436b52443` |
| mean win turn | 5.47 | 5.50 |

It explores **1.62x more units for the same CPU and less wall** — exactly its adoption claim. So it is
not the regression, and `=0` would not recover the cost.

It is worth recording that the knob is **not play-neutral**: the digests differ and the mean win turn
moves. So it does change which keep labels come out, and any future change to it owes a keep-label
comparison, not just a cost one.

## What is still open

The ~9x on the fixed play-digest battery is **unexplained**. The candidate set is the 150 `src/`
commits between the two generation engines, which include a dense equip-route series: the go-off cut
fix (`e56ec720`), two completeness fixes (`31ef68f4`, `91611ad8` — *"unlimited must be a superset of
finite"*), the per-link price `8 → 2` (`c8797289`), and breakpoint condemnation (`bb623a2a`). Several
of those deliberately **widen** what is findable, which the completeness invariant requires and which
necessarily costs work — so the regression may be the correct price of a soundness fix rather than a
defect. That has to be measured, not assumed.

**The instrument for it already exists and is the right one:** `MTG_KEEP_BENCH=N` (`50c26b99`) runs N
stride-sampled size-7 floor rollouts byte-identically to the gen's own floor and reports units, CPU
and a `(cell, side, win-turn)` digest, ignoring the play digest so an engine change benches on the
same buckets. Bisecting the 150 commits with it, on a copy of the deck dir, is a bounded job.

## Does it reach normal runs?

**Every other deck: no, provably.** `DecisionProvider::EquipRouteEnabled()` returns `false` by
default (`DecisionProvider.h:768`) and **exactly one** provider overrides it — the `Equipment`
provider, which the source says *"serves KittyEquipment alone"*. The call sites are
`rp.EquipRouteEnabled() ? EquipRouteApply(...) : ...`, and the gate is deliberately the provider
rather than a card check *"so the scan cost is zero on every other deck"*.

**KittyEquipment itself: unmeasured, and the number one would reach for is stale twice over.**
`test/suite_cost.json` lists `kittyv2` at 1649.2 ms/game — 4th of the suite behind snow (4178.8),
soldiers (3077.8) and selesnya (1843.6), comfortably inside the 3.0x gate. But that row was authored
**2026-10-02**, before the entire equip-route series, and it was measured on **v2's list** while the
suite row now points at v3. It cannot answer the question. Running `suite_gate.py --cost` on v3 is
the missing measurement, and this is the reason to stop deferring it.

Related: `docs/design/play-digest-gate-misses-rollout-only-heuristics.md` (the other defect this run
surfaced), `docs/design/the-budget-is-denominated-in-the-wrong-unit.md`,
`docs/design/selesnya-keepgen-bulk-cost.md` (where `MTG_KEEP_BENCH` came from),
`src/analyzer/ExhaustiveKeep.cpp`.
