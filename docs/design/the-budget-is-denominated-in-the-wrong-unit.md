# The search budget is denominated in turn-steps, but the cost is per PLAN (2026-10-07)

Status: **OPEN, diagnosed, not fixed.** No ruling needed to start — the primary fix is
behaviour-identical engineering. This supersedes the "827,433 GameState deep copies" framing that
earlier notes (and several of my own messages) used as the headline; see §3, that framing is wrong.

## 1. The measurement

Two KittyEquipment v2 games under the instrumented tree (`-DMTG_PROFILE=ON`, `--threads 1`, the
shipped d5/b20 policy). `perf` does not work on this kernel (`Bad address`, WSL2 6.18.33.2), so
these are the repo's deterministic counters (`src/ai/Profiler.h`).

| | gi=371 | gi=369 |
|---|---|---|
| wall | 126,269 ms | 1,074,996 ms |
| search nodes (budget units) | 112,681 | 601,250 |
| `EnumeratePlans` calls | 177,791 | 1,131,337 |
| **plans enumerated** | **15,677,338** | **131,390,242** |
| `ApplyPlanDirect` calls | 178,456 | 928,458 |
| GameState deep copies | 90,215 | 827,433 |

## 2. The cost is PER PLAN, and that is not a guess

Three candidate cost models, tested against two games whose totals differ **8.5x**:

| model | gi=369 | gi=371 | spread |
|---|---|---|---|
| **µs per PLAN** | **8.18** | **8.05** | **1.02x** |
| µs per `EnumeratePlans` call | 950.20 | 710.21 | 1.34x |
| µs per NODE | 1787.94 | 1120.59 | 1.60x |

Cost-per-plan is stable to **2%** across an 8.5x change in total work, while per-call moves 34% and
per-node 60%. So the wall is plan VOLUME at a near-fixed ~8 µs each, and a "node" is not the unit of
work on this deck.

## 3. Why the deep-copy framing was wrong

For the 827,433 copies in gi=369 to explain 1,075 s, one `GameState` copy would have to cost
**1.3 ms**. That is implausible. At a realistic 50–200 µs per copy they are **3.8%–15.4%** of the
wall. The copy rate IS anomalous (1.38/node against stompy's 0.13, ~0.89 copies per apply) and worth
fixing eventually, but it is not the cost centre and must not be reported as one. Note also
`copies/plan` is only 0.0063 — the copies are per-apply, not per-plan, so they do not scale with the
term that actually dominates.

## 4. The consequence: the budget cannot see the work it authorises

`SearchBudget` charges one unit per simulated turn-step. Plans enumerated per unit charged:

| | plans per unit |
|---|---|
| stompy | 1.14 |
| kitty, typical game | 3.04 |
| kitty gi=371 | 139 |
| kitty gi=369 | **219** |

On stompy a unit is an honest proxy for work. On kitty's tail it under-counts by up to **190x**, so a
20 ms budget can authorise ~3.9 million plan enumerations and gi=369 burns 1,012 s inside a SINGLE
decision while believing it spent 18,000 units. Three things follow, all of them observed:

1. **The budget does not bound the tail.** It bounds a quantity only loosely correlated with time.
2. **Units are a lying instrument on this deck.** This is exactly what mis-priced the kitty value
   leaf as a 2.27x cost regression on 2026-10-04.
3. **`MTG_EQUIP_ROUTE_NODE_UNITS` is a local hand-patch for this global mis-denomination.** Its own
   comment says a route link "costs 2-4 ms of wall against ~0.17 ms for an ordinary unit of this
   deck, i.e. ~12-24 units" — that IS a per-site correction factor. It explains why the lever is so
   powerful and why its calibration came out counterintuitive (8 -> 2 is cheaper AND better).

## 5. The plans are DISTINCT — the branching explanation is closed

116 plans per call is combinatorics, not redundancy: Puresteel Paladin makes Equipment free to
equip, so a main-phase plan is a sequence of casts plus equips, and 20+ artifacts across several
hosts multiply the orderings. Three independent closures:

* `MTG_CAND_DEDUP` is **byte-identical** on kitty — there are no duplicate candidates to remove.
* Every duplicate found is a breakpoint variant, and those sit **upstream of a fixed-width index**
  (`candidates scored = W × BpSearchDepth() × bases`; the list's LENGTH appears nowhere), so pruning
  one only swaps slot occupants. See `fixed-width-index-prune-cannot-save`.
* The two 100%-duplicate arms are already collapsed by default-ON flags, and the residual miss split
  is `nomemo=0 multi=26947` — every survivor is the case where declining genuinely deletes a line.

So this is not a collapse-wasted-search problem any more. Per CLAUDE.md's own termination rule
("once all of those are gone the expectation would be that whatever remains is not branching
related"), the residual is per-node, which is where we are.

## 6. The two fix shapes

**(a) Make a plan cost less than 8 µs — LOSSLESS, no ruling needed, do this first.** ~8 µs is
roughly 20,000 cycles to build and score one plan, which points at per-plan heap traffic (a
`vector<Action>` constructed and destroyed per plan). Buffer reuse, an arena, or a small-vector
optimisation would be behaviour-identical, so it needs no A/B for quality — only a digest check. A
2x here is worth more than every budget lever measured on 2026-10-06/07 combined, because it applies
to all 131M plans rather than reallocating which ones get run. **Unverified premise:** that the 8 µs
is allocation rather than genuine scoring work. Check that before committing to the project — the
counters prove the cost is per-plan, they do not prove what the plan spends it on.

**(b) Denominate the budget in plans rather than turn-steps.** This makes nothing faster, but it
makes the budget actually BOUND the work, makes the tail predictable, and retires the per-site price
hand-patch in §4.3. Under the completeness rule in CLAUDE.md this is the GOOD lever shape: it
re-allocates a finite budget and is inert at an unlimited root by construction. Arguably it is the
principled version of what price 2 does crudely. It WILL move play at a finite budget, so it needs
the full adoption bar (recovery at `--depth 8 --budget-ms 0`, per-deck net <= 0).

Not a fix shape: enumerating fewer plans. That is a lossy narrowing, it needs a quality A/B, and §5
says the candidates are distinct states rather than waste.

## 7. Method notes worth keeping

* **An in-flight tail sum reads LOW on this deck.** The monster games land last in a batch, so
  summing `SLOW-GAME` lines mid-run understates the tail badly. Compare on a MATCHED set of games.
  This produced a bogus "10x tail reduction" reading for `MTG_TT_NOWIN_CACHE` before the two biggest
  games landed; the matched-set answer was 1.0335x, i.e. marginally worse.
* **Units are not stable run-to-run at multi-thread** (~0.5%) even with an identical digest (three
  runs at digest `98c00c1ec6d7a8a9`: 57,754,428 / 57,462,570 / 57,525,013). Sub-1% unit deltas are
  not real. Prefer `ms` ratios measured inside ONE pooled batch where both arms share contention.
* **The census counters are process-global atomics**, so a multi-threaded run cannot attribute work
  per site. Run `--threads 1` or filter `contended == 0`.
