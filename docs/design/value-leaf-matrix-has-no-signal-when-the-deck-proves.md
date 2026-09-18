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

### How big is it, honestly

For Angels, currently: **near zero, because trust is masking it.** The adopted `value_trust_depth: 5`
already suppresses escalation at c >= 5, which is where every never-take entry of
`[2, 3, 3, 6, 6, 6, 6, 6]` sits bar one. The single remaining never-take rung is c=4, and trust 4 vs
trust 5 measured **identical units (1,060,000 both)** — so there is nothing left for a gate to save
here.

That is a statement about Angels, not about the defect. The exposure is on decks that ship **no trust
at all**, where nothing suppresses the escalation and the table's never-take entries are paid in full
on every hit — **11 of the 21 modelled decks** (Anti-Lifegain, Creature Giving, Dragons, Dragonstorm,
Hinata2, KittyEquipment, Melira Pod, Mirrorwing Dragon, StompySurprise, treasure_hunt, and the rest
carrying `trust=None`). Several have never-take entries: Creature Giving `take[7]=take[8]=6`,
Mirrorwing `take[6..8]=6`, StompySurprise `take[7]=take[8]=6`, Dragons `take[6..8]=6`.

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

### Proposed, NOT done

1. Add the pre-escalation crossover gate to the **escalation ladder** (mirroring `xo_live` /
   `xo_need` at `TurnSolver.cpp:41394`), behind its own flag, default OFF.
2. A/B it on the decks with never-take entries and no trust. Expect **byte-identical play** by
   construction; the deliverable is the units saved. If play is NOT identical, the gate's premise is
   wrong and that is the more interesting result.
3. If it lands, re-ask whether `value_trust_depth` is still needed anywhere, or only as a tiebreak
   for the case the user identified: *"trust overrides cases that are close, but where the value-leaf
   barely loses in quality over the heuristic"* — i.e. where the table says take-the-heuristic by a
   hair and the escalation is not worth the hair. That case is real but narrow, and on Angels it
   never arose (trust 4 == trust 5, byte-identical).
