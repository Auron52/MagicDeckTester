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

## FOLLOW-UP (same session): the escalation ladder pays for work the table already rejected

USER, on being told the crossover is consulted *after* escalating: *"Wait, you are saying we still
escalate to a heuristic depth that provides worse quality than our current value-leaf? If so, that is
a real cost bug that we need to address. There is no point in doing work with the heuristic that is
not expected to outperform what we have already done with the value-leaf."*

Correct, and it is confirmed in the code rather than inferred from the artifact.

`MulliganProfile.h:277` — *"At runtime, **after escalating** a leaf line committed at depth c, the
hybrid TAKES the heuristic iff hcommitted >= value_fallback_take_at[c], else it keeps the leaf … A
value > value_fallback_max_depth means **never fall back at this c (leaf >= any heuristic)**."*

So where the table says never-take, the escalation is **provably wasted**: it computes a line the
ladder is then guaranteed to discard.

**A gate for exactly this already exists — and is off, and is FIT-only.**
`TurnSolver.cpp:41394`, `MTG_ESC_FIT_CROSSOVER`, whose own comment (`:40970`) says *"default OFF
pending the A/B … Where the table says a pass at `dpass` could NOT be taken, running it is provably
wasted: the ladder would compute the identical line and discard it."* Its measured prize on the FIT
path was large — breaching `take_at[5]=6` against a FIT reach of 5 made **all 3 of its passes
rejectable, ~92% of its search time, byte-identical play**.

**The ESCALATION ladder has no equivalent gate at all.** It is the path that *does* read the table,
and it reads it too late.

### How big is it, honestly — BUILT AND MEASURED 2026-09-18

The gate is now implemented (`MTG_ESC_XO_SKIP`, below) and the answer is: **the defect is real, the
gate is correct and free, and its production prize is about 0.4% on exactly one deck.**

**RETRACTION — the exposure estimate in the first draft of this section was wrong.** It said the
11 no-trust decks were the exposed population and named their never-take entries: Creature Giving
`take[7..8]=6`, Mirrorwing `take[6..8]=6`, StompySurprise `take[7..8]=6`, Dragons `take[6..8]=6`.
**Those entries are unreachable and are never consulted.** `TakeAtForCommitted` clamps the committed
depth into the table, and `committed <= depth`; at a shipping depth of 5 or 6 no lookup can ever land
on index 7 or 8. Three of the four named decks (Dragons, Mirrorwing, StompySurprise) additionally
ship `leaf: none`, which takes the `line_constant` branch — `taken = hcommitted >= 1` — so they do
not read the table at all. I read never-take entries off the artifacts without checking whether the
index was in range, which is the same failure as reading a units ratio instead of the rule's input.

The reachable population, recomputed at each deck's **actual** shipping `target_depth`, is five decks
— and every one of them is a **trust** deck, the opposite of the claim:

| deck | D | trust | dead committed depths (`take_at[c] > D`) |
|---|---|---|---|
| Angels | 5 | 5 | **3, 4** |
| Auras | 5 | 5 | 4 |
| BreachingDragonstorm | 5 | 4 | 3 |
| CritterLifegain | 5 | 5 | 4 |
| Knights | 5 | 5 | 4 |

Reachable is not the same as reached. Measured escalation counts (250 games/deck, `MTG_HYBRID_STATS`):

```
                 b3                      b10                     b20
angels    1432 dec  29 esc  54 skip   1365   1 esc  12 skip   1353   0 esc   1 skip
auras      319 dec  44 esc   0 skip    287  12 esc   0 skip    277   2 esc   0 skip
breaching  256 dec   0 esc   0 skip    255   0 esc   0 skip    255   0 esc   0 skip
critter    333 dec  77 esc   0 skip    272  16 esc   0 skip    263   7 esc   0 skip
knights    272 dec   7 esc   0 skip    267   2 esc   0 skip    265   0 esc   0 skip
-- no-trust decks, where escalation is FREQUENT --
melira     679 dec 426 esc   0 skip    581 327 esc   0 skip
hinata     673 dec 376 esc   0 skip    603 310 esc   0 skip
cr.giving  379 dec 126 esc   0 skip    332  79 esc   0 skip
antilife   307 dec  54 esc   0 skip    291  38 esc   0 skip
```

**Only Angels ever trips the gate.** The four other reachable decks escalate at committed depths the
table can reach, so nothing is skippable; the no-trust decks escalate constantly (Melira on 63% of
decisions) and skip **zero**, because their tables never demand an unreachable depth.

A/B on Angels, 12,000 games per arm, three budgets, `value_play` shape as shipped:

| budget | units OFF | units ON | delta | escalations skipped |
|---|---|---|---|---|
| b3 | 17,505,130 | 17,024,488 | **−2.75%** | 582 |
| b10 | 20,366,727 | 20,228,976 | −0.68% | 79 |
| b20 | 23,593,068 | 23,493,309 | −0.42% | 19 |

**12 of 12 play digests byte-identical**, which is the result the construction predicts and therefore
the one that confirms the premise: that work was computed and thrown away. The saving scales with how
starved the search is — largest exactly where budget is scarce, which is the right direction, but
Angels ships at b20/b40 where it is ~0.4%.

### Why the prize is small, and why that is not a coincidence

The two conditions the gate needs are **anti-correlated by construction**. A deck earns never-take
entries when its leaf is strong relative to the heuristic; that same strength is what earns it a high
`value_trust_depth` — both are fit from the same depth matrix. But trust is evaluated *first*, at the
`escalate` decision, so on a strong-leaf deck trust has already closed the door the gate would close.
Conversely, a weak-leaf deck escalates constantly but has a permissive table with nothing to skip.

Angels is the exception only because its table is anomalously restrictive for a deck of its trust
level — and that anomaly is precisely the zero-signal fit documented at the top of this file. **The
defect and the only deck it reaches both trace back to the same bad matrix.**

This also answers item 3 of the original plan: the gate does **not** make `value_trust_depth`
redundant — the reverse. Trust does nearly all the suppression, and the gate collects a residue.

### Why this interacts with trust, and what it implies

Trust and the crossover gate **overlap**: both avoid an escalation, by different reasoning. Trust
says *"a line committed this deep needs no verification"*; the gate says *"no reachable heuristic
depth could beat this line, so verifying is pointless."* The gate is the better-founded of the two —
it is a statement about the measured table rather than a blanket depth threshold, and it needs no
non-inferiority A/B to justify because it is play-neutral by construction (the ladder would discard
the result anyway).

**That suggests the ordering we actually want is: gate first, trust second.** A correct
pre-escalation gate would capture most of what trust buys, without trust's quality risk — which is
precisely the risk the user flagged for depth < 5. It might make shallow trust unnecessary rather
than merely better-proven.

### The gate, as built — ADOPTED 2026-09-18, default ON

`MTG_ESC_XO_SKIP` (default ON, `=0` to disable), at the `escalate` decision in
`TurnSolver::FullSearchLineHybrid`:

```cpp
const bool xo_esc_dead = s_esc_xo_skip && !line_constant
                      && !value_fallback_take_at.empty() && s_vto_override < 0
                      && TakeAtForCommitted(value_fallback_take_at, committed) > depth;
const bool escalate = esc_wanted && !xo_esc_dead;
```

**Why `depth` is a sound bound.** `hcommitted` cannot exceed `depth` on either escalation path: the
ladder searches `esc_depth = min(depth, MTG_ESC_DEPTH_CAP)`, and the single/FIT path caps at
`clamp(escalation_cap, 1, depth)`. So `take_at[committed] > depth` *proves* the take can never fire.
`depth` is deliberately the **loose** bound — the tighter per-path cap is only known inside the block
— so the gate skips a strict subset of the provably-dead escalations and can never skip a live one.

The three guards are copied verbatim from the FIT gate for the reason that gate gives: `line_constant`
takes any heuristic line (`hcommitted >= 1`, table not consulted) and `s_vto_override >= 0`
(`MTG_VALUE_TRUST_OFFSET`) makes the take decision judge by the uniform offset instead of the table.
Gating on the table in either case is exactly the drift `TakeAtForCommitted` exists to prevent.

**One instrumentation trap, worth recording.** The first version counted `xo_esc_dead`
unconditionally. `xo_esc_dead` is a property of `(committed, depth, table)` alone, so it is true on
masses of decisions that were never going to escalate — the first run reported **1175 skips on a run
whose `redos` was 0 in both arms**, i.e. a saving the arm had not made, while units were identical to
the byte. Counting `esc_wanted && xo_esc_dead` fixed it. A counter that agrees with the flag rather
than with the units is worse than no counter.

### Verification

* **Liveness traced before reading the A/B** — Angels b3, 582 escalations actually removed.
* **Play-neutral** — 12/12 digests identical on Angels across b3/b10/b20 (12,000 games/arm).
* **Fleet-neutral** — with the flag ON: smoke 83/83 and regression 113/113 configs unchanged vs
  committed GT, 0 play-changed, references unchanged.
* **Inert at its default** — smoke 83/83, 0 changed, both before and after the default was flipped ON.
  Play is identical either way, so no GT key moved and no rebaseline was needed.

### Follow-up NOT taken: the tighter bound

The gate uses `depth`, the universal bound. The **per-path** bound is tighter and known before the
escalation runs: `min(depth, MTG_ESC_DEPTH_CAP)` on the ladder, `clamp(escalation_cap, 1, depth)` on
the single/FIT path. Using it would fire strictly more often — e.g. FiveColour ships `target_depth: 6`
with `escalation_cap: 5` and `take_at[5] = 6`, so committed=5 is dead against the real cap of 5 while
looking alive against `depth` of 6. It is left undone deliberately: both cap values are computed
*inside* the escalation block and would have to be hoisted, the `eff_single_deck` path is only taken
under further conditions, and the measured prize for getting it right is a fraction of the ~0.4% the
loose bound already collects. A wrong tighter bound would skip a **live** escalation, which is a
quality regression rather than a missed saving — an asymmetry that argues for leaving it alone until
something makes the prize worth the care.
