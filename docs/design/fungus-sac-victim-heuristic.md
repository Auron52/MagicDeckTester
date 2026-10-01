# Fungus sac-victim ranking: the USER's rule, built and measured

**Status 2026-10-01: BUILT, MEASURED, both halves stay DEFAULT OFF.** One half is
outcome-neutral despite firing 2.39 million times; the other is *structurally redundant* — the
pre-existing rank already implemented it. Neither is a bug; both are closed findings.

## 1. The rule

USER, 2026-10-01:

> "The mana sac should just be used as a mana source. The only different saprolings would be
> Shroofus Sproutsire (should be kept), 1/1 saprolings and Saproling Burst saprolings. Most of the
> time we should prioritize 1/1 saprolings unless the Saproling Burst saprolings are the same size
> or smaller. (or 1 P/T larger in the second main)"

and, correcting an over-reading of it:

> "To be clear, I don't mean never shroofus, but Shroofus should be the last to go."

So: **defer Shroofus**, and **prefer eating a doomed Saproling Burst token** when it is the same
size or smaller than a 1/1.

## 2. Where it had to go, and why there is only ONE rank

`SacExpendabilityRank` (`src/core/SpellEffects.h`) is **the single rank shared by four sites**:
`CanonicalSacVictim`, `DevourRankOrder`, the Birthing-Pod victim-emission order in `TurnSolver`, and
the payment-side `SacPayFodderRank`. Its own comment says that sharing is load-bearing — the Pod
emission order must agree with the canonical pick or equal-value ties commit a different victim in
the two worlds. So the rule went into that one rank and `DoomedTokenCreators(state)` was threaded
through **all four** callers rather than splitting the rank.

Two levers, both default OFF, because both MOVE PLAY:

| lever | effect on the rank |
|---|---|
| `MTG_SAC_VICTIM_DOOMED` | a token whose creator destroys it on leaving (`fade_ltb_destroys_created_tokens` = Saproling Burst) gets `rank -= 1` |
| `MTG_SAC_VICTIM_ENGINE` | a `combat_damage_tokens_per_damage > 0` body (Shroofus) joins the `scaling` defer tier, `rank += 1000` |

**The magnitude of the `-1` is the user's sentence, not taste.** The base metric is
`EffectivePower`, so `-1` makes a doomed token win at *equal* power and lose at *+1* power — which
is exactly "the same size or smaller".

**NOT IMPLEMENTED: the second-main clause.** *"(or 1 P/T larger in the second main)"* needs the
PHASE, which this rank does not receive. Recorded rather than guessed at.

## 3. A board-width quadratic, re-introduced and removed

`DoomedTokenCreators` is an O(board) scan. Resolving each token's creator *inside* the rank puts
that scan inside an `O(board log board)` sort — the board-width quadratic this deck has already
been bitten by four times ([[board-level-scan-per-creature-family]]). It was hoisted out of the Pod
comparator correctly and then **left in place inside `SacPayFodderRank`, which is called from a
per-permanent loop** in `src/ai/ManaPayment.cpp` (~768). Fixed by hoisting to a `pay_doomed` local.
The rank takes `const std::vector<int>*` so every caller hoists.

## 4. Measurement

### 4.1 The first attempt was void, and the reason matters

Two **sequential** A/B runs gave **opposite signs**:

```
run A:  off = 87.48 s   on = 129.40 s   -> "on is 48% SLOWER"
run B:  off = 126.50 s  on =  96.04 s   -> "on is 24% FASTER"
```

The flags-off baseline itself moved 87.48 → 126.50 on *identical configuration*. Host contention
swamped the effect ([[box-is-shared-interleave-ab-arms]]). **No conclusion, in either direction** —
and the "48% slower, caused by the quadratic" attribution that run A seemed to support was never
established.

### 4.2 What fixed the method: a deterministic cost metric, and one pooled queue

Wall clock was the wrong metric. `mtg --batch` prints `units` per job, and **units are
deterministic** — the budget is converted to a unit allowance via `NODES_PER_VIRTUAL_MS`, not read
off the clock, which is why the regression harness gets reproducible digests at `b10`. Verified
here: identical `units` for the same arm across jobs that ran at different times under different
load. So `units` is a contention-immune work metric and `avg` is a contention-immune quality
metric. Neither needed a quiet box.

Both levers were then wired into `heurarm` (`src/ai/HeuristicArm.h`) so **one pooled batch runs
every arm**. They are read inside a sort comparator via a function-local `static const bool`, so a
process can only ever BE one arm, and pricing them one process per arm is the per-arm WAVE pattern
CLAUDE.md forbids — it is also precisely how §4.1 failed.

**Plumbing verified in BOTH directions before any arm was trusted**, because a silently-baseline arm
reads as "measured" while running the control:

| job | env | per-job flag | digest |
|---|---|---|---|
| `v_envon` | `DOOMED=1` | — | `9bc14beac813c5ce` |
| `v_forceon` | `DOOMED=1` | `true` | `9bc14beac813c5ce` |
| `v_forceoff` | `DOOMED=1` | `false` | `d77b6040a9b319d9` |
| `w_base` | clean | — | `d77b6040a9b319d9` |
| `w_on` | clean | `true` | `9bc14beac813c5ce` |

Per-job `true` reproduces the env arm exactly; per-job `false` reproduces the baseline exactly.

### 4.3 The 2x2 factorial

Both levers touch the same rank, so they are tested **jointly**
([[paired-heuristics-must-be-tested-jointly]]). One batch, 32 jobs, 9,600 games, candidate-b (the
new list). Seeds spaced by `games`; every arm reuses the same blocks, so comparisons are paired.

d0, 2,000 games/arm:

| arm | avg | delta |
|---|---|---|
| base | 6.3675 | — |
| doomed | 6.3670 | **-0.0005t** |
| engine | 6.3670 | **-0.0005t** |
| both | 6.3665 | **-0.0010t** |

d3/b10, 400 games/arm:

| arm | avg | delta | units | ratio |
|---|---|---|---|---|
| base | 5.3150 | — | 45,041,010 | 1.00000x |
| doomed | 5.3175 | **+0.0025t** | 45,284,515 | **1.00541x** |
| engine | 5.3150 | **+0.0000t** | 45,041,168 | 1.00000x |
| both | 5.3175 | +0.0025t | 45,285,601 | 1.00543x |

**The real cost of `DOOMED` is +0.54% of deterministic work.** Not 48%, not -24%. Quality is flat
in both directions at both depths — the d3 `+0.0025t` is one game out of 400 landing a turn later.

### 4.4 The reach census — why flat does not mean inert

A flat average has three causes: no effect, never ran, or two effects cancelling
([[unchanged-average-three-causes]]). `MTG_SAC_VICTIM_PROBE` (default OFF, verified byte-identical
when off) counts, at the `CanonicalSacVictim` site, how often each rule's precondition is on the
board and how often it actually **flips** the chosen victim. 400 games, d3/b10, both levers on:

```
calls = 66,866,445
doomed:  reach = 26,595,677 (39.77%)   flip = 2,390,792 (3.575%)
engine:  reach =  7,129,415 (10.66%)   flip =         0 (0.000%)
```

**`DOOMED` is reached in 39.8% of victim picks and changes the victim 2.39 MILLION times — and the
deck does not care.** That is a real finding, not a null: among the bodies the search actually
reaches for, a doomed Burst token and an equal-power 1/1 Saproling are near-interchangeable as
fodder. The rule is sound and it fires constantly; the outcome is simply insensitive to it.

**`ENGINE` flips the victim exactly ZERO times, and that is structural, not a sample-size
accident.** Shroofus Sproutsire is a **non-token** 1/1 Saproling (`subtypes: ['Saproling']`, a real
card in the 60). Every *token* Saproling already gets `rank -= 1000`. So a non-token Shroofus is
already ranked 1000 behind every token Saproling: **the user's "Shroofus should be the last to go"
was already implemented by the pre-existing token bonus.** The `+1000` can only bite when Shroofus
is the *only* eligible Saproling — and then there is nothing to defer to. Hence exactly 0.

**Scope limit, stated rather than glossed:** the probe instruments ONE of the four sites sharing
this rank. `ENGINE` does move a few d0 digests and 161 d3 units, so it has *some* effect — through
one of the other three sites, most plausibly `DevourRankOrder` (the list holds 4 Mycoloth).
**Inferred, not instrumented.**

## 5. Verdict

| lever | quality | cost | verdict |
|---|---|---|---|
| `MTG_SAC_VICTIM_DOOMED` | flat (-0.0005t d0, +0.0025t d3) on 9,600 games | **+0.54% units** | **stays OFF** — it buys a measured 0.000 and costs real work |
| `MTG_SAC_VICTIM_ENGINE` | flat | free | **stays OFF** — redundant at the victim site by construction |

Both are kept in the tree rather than reverted: they are the user's rule, correctly encoded, and
the finding (2.39M flips → 0.000t) is only reproducible with them.

**Do not spend more games on these.** 2.39 million victim flips producing no measurable outcome
change is not an underpowered sample; it is the answer. More games would narrow a confidence
interval around zero.

## 6. Open follow-up

The second-main clause — *"(or 1 P/T larger in the second main)"* — remains unimplemented because
the rank does not receive the phase. Given §4.4, it should be expected to measure flat for the same
reason: it refines a tie-break the deck's outcomes are insensitive to. Worth saying before anyone
spends a day threading the phase through four call sites.
