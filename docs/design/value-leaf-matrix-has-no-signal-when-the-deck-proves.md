# The depth matrix can contain ZERO signal, and the crossover fit reads that as "always trust"

**Status:** diagnosed with measurements on Angels, 2026-09-18. Fix not implemented — it is a design
decision. Applies to `scripts/valueleaf_depth_matrix.py` (phase C) and whatever fits
`value_fallback_crossover` / `value_trust_depth` from it.

Found because the USER asked two questions in a row that turned out to be the same question:
*"Does this mean that our matrix was insufficient to find where trust fails?"* and *"trusting at
depth 3 is extremely unusual. This would be the first time I have seen it."*

## The claim

The depth matrix runs **UNBOUNDED** (its own header says so: `DEPTH MATRIX (UNBOUNDED, INCREMENTAL)`).
A deck that PROVES its wins inside the horizon therefore reaches the same answer with either
evaluator — the leaf changes only how much work is done, never the outcome. The H-vs-V comparison is
then **empty**, and the crossover fit, seeing no case where the heuristic was better, emits the most
permissive table it can: never fall back, trust early.

That is backwards. **No evidence that the heuristic is ever better is not evidence that the leaf is
always safe.**

## The measurement (Angels, `logs/vlq_angels/matrix.txt.cells.json`, per game)

| depth | games compared | identical | H better | V better |
|---|---|---|---|---|
| 1 | 1600 | **1600** | 0 | 0 |
| 2 | 1600 | **1600** | 0 | 0 |
| 3 | 1600 | **1600** | 0 | 0 |
| 4 | 1600 | **1600** | 0 | 0 |
| 5 | 1600 | **1600** | 0 | 0 |

Not "equal means" — **zero differing games at any depth**. The arms are genuinely separate runs, not
copied rows: their costs differ (depth 5, 257.5 ms H vs 256.5 ms V; V6 110.5, V7 66.6, V8 75.2 with no
H twin). Same play, different cost, at every rung.

## What the fit then produced, against the fleet

| deck | trust | `take_heuristic_at_hdepth` (first four) |
|---|---|---|
| **Angels (as generated)** | **cand 3** | **`[2, 3, 6, 6 …]`** |
| Anti-Lifegain | — | `[1, 1, 1, 2 …]` |
| Auras | 5 | `[1, 1, 1, 3 …]` |
| BreachingDragonstorm | 4 | `[1, 1, 2, 6 …]` |
| CritterLifegain | 5 | `[1, 1, 1, 2 …]` |
| Fluctuator | 4 | `[1, 1, 2, 3 …]` |
| Hinata2 | — | `[1, 1, 1, 1 …]` |
| KittyEquipment | — | `[1, 1, 1, 2 …]` |
| Melira Pod | — | `[1, 2, 3, 4 …]` |
| Minotaur | 5 | `[1, 1, 1, 2 …]` |
| StompySurprise | — | `[1, 1, 1, 2 …]` |

Every other deck falls back essentially immediately at shallow committed depths (`1, 1, 1`). Angels is
the only one with `take[1]=2`, `take[2]=3`, and — since the H ladder caps at **H5** — `take[3]=6` is the
`maxH+1` **never-fall-back sentinel**. Its candidate trust depth of **3 is the lowest in the repo**;
every adopted value elsewhere is 4, 5 or 6. The user flagged that as unprecedented before any of this
was measured, and it is: **it is an artifact of a signal-free matrix, not a property of the deck.**

## And the matrix's verdict is contradicted in budgeted play

The matrix says V3 ≡ H3 (5.4344 both, and identical on all 1600 games). In BUDGETED play at d3b10,
paired over 4,000 games, the model leaf is **worse than the plain rollout ladder**:

```
delta +0.0088 turns (se 0.0017)   5 better / 40 worse / 3955 tied   z = -5.22
```

So the matrix is not merely silent about where trust fails — it is **actively wrong about the exact
comparison the crossover fit is built on.**

## Why, mechanically

A leaf only decides an outcome when the search CANNOT prove its win and must compare estimates.
Unbounded, Angels proves essentially everything, so the leaf is never load-bearing. Under a budget it
fails to prove on **42% of ladder decisions** (148/352 at d5b20, `MTG_ROLLOUT_STATS`), and there the
estimate decides — which is where the leaf's error appears.

The matrix therefore excludes, by construction, the only regime in which a leaf can be wrong.

## Who else is exposed

Any deck whose matrix is saturated — quality flat across rungs, H ≡ V per game. The risk is highest
for a deck that ALSO has a high probe-failure rate under budget, because that is the deck whose leaf
is heavily consulted in play while being untested by the matrix. Both halves are cheap to check:
per-game H/V agreement is already on disk in `matrix.txt.cells.json`, and the probe-failure rate is
one 60-game `MTG_ROLLOUT_STATS=1` run.

**Angels was exactly that combination** — proves ~everything unbounded, fails 42% under budget — which
is what made its matrix maximally misleading.

## Suggested fixes (a design decision, not adopted)

1. **Report H/V discriminating-game COUNT per rung, and refuse to fit a permissive crossover from
   zero.** If no game distinguishes the arms, the honest output is `UNRESOLVED` plus conservative
   defaults (fall back early, trust late) — not the most permissive table in the fleet. This is the
   same `3*step/n` resolution-floor discipline `dead_rung` already applies and the phase E A/B never
   inherited (`value-leaf-ab-never-measures-the-benefit.md`).
2. **Measure at least one rung at the SHIPPED budget**, so the fit sees the regime it is fitting for.
3. **Sanity-bound the emitted values against the fleet.** A candidate trust depth of 3, or a
   `take_heuristic_at_hdepth` starting `[2, 3, …]` when every other deck starts `[1, 1, …]`, should
   be surfaced as an outlier for review rather than shipped silently.

## What it cost here, and what it is worth

On Angels the two bad keys cost **+0.0168 turns at d3b10 (70 worse games in 4,000)**. Correcting them
to `trust 4` / `take[3]=3` was **free at the shipped d5b20** (1,060,000 units either way, byte-identical
play) and halved the d3 deficit. See `analysis-Angels.md`.

Hinata2 and KittyEquipment carry the same "good at d5, bad at d3" signature and were screened before
this defect class was understood. Their metadata was derived the same way. Worth an hour to check
whether their blocker is also two keys rather than the shape-gate design question.
