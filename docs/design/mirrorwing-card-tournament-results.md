# Mirrorwing card tournament — decision register and results

**Purpose: so nobody re-treads ground.** Scan the register, find the option, read why it was
accepted, rejected, or left open. Measurements below it are the evidence; the reasons are the point.

Numbers in the `AUTO` blocks are regenerated straight from each run's stderr — never hand-copied:

```
python3 scripts/tourney_ledger.py            # refresh every measured section
```

Prose outside those blocks is hand-written and the script leaves it alone. Companion documents:
`mirrorwing-card-tournament.md` (the plan and apparatus), `mirrorwing-trick-suite-result.md` (the
earlier screening this supersedes).

Metric throughout is **mean turn-to-kill; negative = better/faster**. `margin` is
better-minus-worse game counts and `s` is that margin in its own noise units
(margin / sqrt(divergent)) — a count bar means different things at different N, so both are given.

---

## 1. Register — what has been tried

### 1a. Card and configuration options

| option | status | verdict | why |
|---|---|---|---|
| **3 Twinflame** (incumbent) | measured | **keep at 30 life, cut at 20** | Loses to Libation at 20 life (−0.0145t, +472 margin, 6.0σ) and beats it at 30 (+0.0138t, −1,067, 11.8σ). Sign flip is consistent across both trick contexts, so it is a life-total effect, not an interaction. **Conditional on Entrance = 2** — see 1b. |
| **3 Luxurious Libation** | measured | **take at 20 life** | Mirror of the above. Makes a 1/1 Citizen per resolved copy, so it grows the board rather than the turn's damage — which is why it fades when 30 damage is needed on one swing. |
| 2/1 and 1/2 Twinflame/Libation mixes | measured | **no reason to prefer** | Dose response is monotone in both directions (≈0.005t per copy at 20 life, ≈0.005t the other way at 30). A mix is never better than the appropriate pure option. |
| **4 Ancestral Anger** (incumbent) | measured | **only without Draught** | Beats Oracle at 20 life by +0.0080t (t=+5.0) in the Scale context, with the count margin agreeing (−126, −2.4σ). Its +X/+0 escalates off graveyard copies, so it is raw damage with no synergy tail. |
| **4 Oracle's Restoration** | measured | **take, but only alongside Draught** | *Worst* option at 20 life without Draught (+0.0080t vs Anger). Best with it. Its `cast_lifegain: 1` per copy is Draught fuel, not a rate upgrade. |
| 2 Anger / 2 Oracle mix | measured | **rejected** | Worst of the three at 20 life — worse than either pure option (+0.0064t vs 4 Anger, and 4 Oracle is +0.0036t better than the mix). Splitting the trick suite dilutes both escalation curves. |
| **2 Scale the Heights** (incumbent) | measured | **cut** | Loses to Draught at 20 life by −0.0134t with a +1,789 margin (15.6σ) — the largest single effect measured. |
| **2 Fortifying Draught** | measured | **take** | Wins decisively at 20 life. At 30 life the two numbers disagree: +493 margin (4.0σ) but t=−1.3 — wins slightly more often, loses slightly harder. |
| **4 Oracle + 2 Draught** | measured | **best combination so far** | Only configuration winning on both numbers at both life totals: −0.0112t / +686 (8.7σ) at 20, −0.0110t / +575 (6.8σ) at 30. |
| **4 Draught + 2 Oracle** | *in flight* | — | Run B. Motivated by Draught self-fuelling (below): more payoff may beat more fuel. |
| 4 Draught + 2 Scale | *in flight* | — | Run B. Isolates Draught count with no Oracle fuel at all. |
| 4 Anger + 2 Oracle | *in flight* | — | Run B. Oracle as a small fuel package behind Anger's damage. |
| **4 Impolite Entrance** | **not measured** | **open** | Blocked by the apparatus, not by choice — see 2b. Hypothesis with card-data support: Entrance grants haste, Libation's Citizens arrive summoning-sick, so Entrance→Libation is a one-sided enabler (Twinflame's tokens already have haste). Would move Test 1 toward Libation. Costs 9,293 new cells to settle. |
| Expedite | not tried here | n/a | The shipped deck's card; every tournament arm plays Impolite Entrance in its place. It survives only as the bucket Entrance is aliased into. |

### 1b. Standing caveats on the results above

- Every Test 1 and Test 2 result is measured with **Impolite Entrance fixed at 2**. The Entrance
  interaction in 1a is the reason that is not a safe assumption.
- All effects are **0.3–0.7% of a turn**. The margins are large because N is large (60k–120k paired
  games per comparison), not because the cards are far apart. For scale, the keep table itself is
  worth **0.15t** — roughly ten times any card choice here.
- Test 4 as originally scoped ("Draught for *all* of the draw+pump slot") is what run B measures.

### 1c. Method options tried

| approach | status | why |
|---|---|---|
| Union **deck** (one superset decklist holding every candidate) | **rejected — banned term** | User directive. Cells scale as C(K+6,7) and a superset's extra cells are unreachable by any real 60-card list. Cost 5+ h before it was stopped. See `never-build-a-union-deck` and deck-screening Rule 0a. |
| Union **table** over 60 real arms (K=20, R=10) | **abandoned at 13.5 h** | Adaptive refinement selects for expensive cells — the confident ones freeze first, leaving ambiguous hands that fail to go off. Per-rollout cost rose 7x (0.221s → 1.5s) and the remaining refine projected to ~112 h. Floor pass banked in the journal (1,517,015 cell records), resumable. |
| Four sequential per-test A/Bs | superseded | A 60-arm factorial gives each comparison 12–15x the paired games for the same wall clock, and measures every slot in every context so interactions cannot hide. |
| **Aliased shipped table** (K=17, R=40, new names folded into existing buckets) | **adopted** | Zero generation. Fit tilt measured at ≈0 (see 3), so it is not merely cheaper — with 4x the rollouts per cell it may be the better apparatus. |
| Using the shipped profile *unchanged* (no aliasing) | rejected | An unbucketed card yields `present=false` and drops that hand to the generic heuristic, so the challenger arm would play 32–60% of its hands under a different mulligan policy than the incumbent (31.5% at 3 Libation, 39.9% at 4 Oracle). Uncontrolled asymmetry. |
| No-table arms as a **verdict** source | rejected | User: unrealistic policy; agreement under it does not license the realistic one. Used only as a bracket (section 3). |
| Running two saturating batches concurrently | **rejected — measured** | Lending 12 of 32 cores to a second batch dropped keep-generation from ~210 rollouts/s to ~16/s. A 12x collapse for a 1.4x core share. Always sequential. |
| Generating a 30-life value leaf | **not needed** | Jobs run the leaf in ladder mode, so the committed pass stays pure heuristic (coupling 0.0008t). A 20-life-fitted leaf cannot decide a 30-life line. |

---

## 2. Apparatus

### 2a. What each run used

| run | apparatus | arms | why this map |
|---|---|---|---|
| **A** — `logs/tourney/run_alias/` | shipped table, alias map A: Libation→Twinflame, **Oracle→Ancestral Anger** (cap 4), **Draught→Scale** (cap 2), Entrance→Expedite | 24 (`tf` × `ao` × {scale, draught}) + bracket | Puts Oracle in the cap-4 bucket, so up to 4 Oracle fits. |
| **B** — `logs/tourney/run_B/` | shipped table, alias map B: **Draught→Ancestral Anger** (cap 4), **Oracle→Scale** (cap 2) | 20 (`tf` × 5 trick configs) | Flips which card gets the cap-4 bucket, so up to 4 Draught fits. |

### 2b. Why some options are unreachable without generating

The shipped table has exactly **one cap-4 trick bucket and one cap-2**. Whichever card is aliased
into the cap-4 bucket can appear 4 times; the other is limited to 2. That is why `4 Oracle + 2
Draught` and `4 Draught + 2 Oracle` need different maps, and why `4 Impolite Entrance` fits neither
— once the trick slots are placed, no spare cap-4 bucket remains for it.

Cross-map results are chained through arms that hold **no aliased card at all**, which are identical
under both maps:

<!-- AUTO:bridge -->
_(bridge check pending — run B still in flight)_
<!-- /AUTO:bridge -->

If that check ever fails, every cross-map statement in this document is void.

---

## 3. What the mulligan apparatus is worth (the bracket)

Two decklists run under the aliased table **and** under no table at all, same seeds, same pooled
batch. This is what makes "too close to call" a measured quantity instead of a judgement.

| quantity | 20 life | 30 life |
|---|---:|---:|
| table worth (no table − aliased table) — incumbent list | **+0.1550** ± 0.0191 | **+0.1348** ± 0.0229 |
| table worth — challenger list | +0.1638 ± 0.0198 | +0.1535 ± 0.0232 |
| **fit tilt** (incumbent − challenger) | −0.0088 ± 0.0103 | −0.0187 ± 0.0120 |

**Having a table is worth ~0.15t. Which table barely matters** — the fit tilt is under 2 se at both
life totals and, if anything, points at the challenger rather than the incumbent.

Two consequences:

1. The one-sided reading ("a challenger that wins under the incumbent's table wins conservatively")
   is **not supported in either direction**. Read comparisons symmetrically.
2. The practical recipe is **generate rarely, reuse aggressively, alias new cards into existing
   buckets**. Generation is only worth it where a card cannot be aliased at all (Entrance) or where
   an effect sits inside the apparatus band.

---

## 4. Results — run A (alias map A)

Every trick-slot combination against the baseline **4 Anger + 2 Scale**, pooled over all four
Twinflame/Libation ratios. Negative `vs baseline` = the option is faster.

<!-- AUTO:runA -->
| option | life | games | mean turn | vs baseline | t | better | worse | margin | s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **4 Anger + 2 Scale** (baseline) | 20 | 40,000 | 5.0607 | -- | -- | -- | -- | -- | -- |
| 4 Anger + 2 Draught | 20 | 40,000 | 5.0520 | -0.0088 | -4.4 | 2,395 | 1,955 | **+440** | +6.7 |
| 2 Anger/2 Oracle + 2 Scale | 20 | 40,000 | 5.0689 | +0.0081 | +6.6 | 697 | 909 | **-212** | -5.3 |
| 2 Anger/2 Oracle + 2 Draught | 20 | 40,000 | 5.0566 | -0.0042 | -1.8 | 2,937 | 2,542 | **+395** | +5.3 |
| 4 Oracle + 2 Scale | 20 | 40,000 | 5.0687 | +0.0080 | +5.0 | 1,270 | 1,396 | **-126** | -2.4 |
| 4 Oracle + 2 Draught | 20 | 40,000 | 5.0496 | -0.0112 | -4.5 | 3,458 | 2,772 | **+686** | +8.7 |
| **4 Anger + 2 Scale** (baseline) | 30 | 40,000 | 5.4600 | -- | -- | -- | -- | -- | -- |
| 4 Anger + 2 Draught | 30 | 40,000 | 5.4650 | +0.0050 | +2.1 | 2,483 | 2,541 | **-58** | -0.8 |
| 2 Anger/2 Oracle + 2 Scale | 30 | 40,000 | 5.4604 | +0.0004 | +0.3 | 875 | 854 | **+21** | +0.5 |
| 2 Anger/2 Oracle + 2 Draught | 30 | 40,000 | 5.4578 | -0.0022 | -0.9 | 3,217 | 3,034 | **+183** | +2.3 |
| 4 Oracle + 2 Scale | 30 | 40,000 | 5.4566 | -0.0034 | -1.9 | 1,552 | 1,357 | **+195** | +3.6 |
| 4 Oracle + 2 Draught | 30 | 40,000 | 5.4490 | -0.0110 | -3.9 | 3,906 | 3,331 | **+575** | +6.8 |
<!-- /AUTO:runA -->

Twinflame → Libation dose response, pooled over the trick configurations:

<!-- AUTO:runA_ladder -->
| step | life | games | turns | t | margin | s |
|---|---:|---:|---:|---:|---:|---:|
| 3 Twinflame -> 2 Twin / 1 Lib | 20 | 60,000 | -0.0061 | -6.3 | **+260** | +5.4 |
| 3 Twinflame -> 1 Twin / 2 Lib | 20 | 60,000 | -0.0107 | -7.9 | **+414** | +6.3 |
| 3 Twinflame -> 3 Libation | 20 | 60,000 | -0.0145 | -8.8 | **+472** | +6.0 |
| 2 Twin / 1 Lib -> 1 Twin / 2 Lib | 20 | 60,000 | -0.0046 | -4.8 | **+189** | +4.0 |
| 2 Twin / 1 Lib -> 3 Libation | 20 | 60,000 | -0.0085 | -6.3 | **+284** | +4.4 |
| 1 Twin / 2 Lib -> 3 Libation | 20 | 60,000 | -0.0039 | -4.0 | **+111** | +2.4 |
| 3 Twinflame -> 2 Twin / 1 Lib | 30 | 60,000 | +0.0027 | +2.2 | **-183** | -3.4 |
| 3 Twinflame -> 1 Twin / 2 Lib | 30 | 60,000 | +0.0069 | +4.0 | **-497** | -6.6 |
| 3 Twinflame -> 3 Libation | 30 | 60,000 | +0.0138 | +6.7 | **-1,067** | -11.8 |
| 2 Twin / 1 Lib -> 1 Twin / 2 Lib | 30 | 60,000 | +0.0042 | +3.4 | **-250** | -4.5 |
| 2 Twin / 1 Lib -> 3 Libation | 30 | 60,000 | +0.0110 | +6.5 | **-784** | -10.3 |
| 1 Twin / 2 Lib -> 3 Libation | 30 | 60,000 | +0.0069 | +5.7 | **-490** | -9.1 |
<!-- /AUTO:runA_ladder -->

---

## 5. Results — run B (alias map B): does more Draught beat more fuel?

<!-- AUTO:runB -->
_(pending — batch in flight)_
<!-- /AUTO:runB -->

Twinflame → Libation ladder under map B, as an independent replication of section 4's ladder on a
different apparatus:

<!-- AUTO:runB_ladder -->
_(pending — batch in flight)_
<!-- /AUTO:runB_ladder -->

---

## 6. Mechanisms found

**Fortifying Draught self-fuels, and that is most of its value.**
`cast_lifegain: 2` then `pump_per_life_gained_power: 1`, gain-first-then-count. Under a magnet with
N creatures, copy *k* gains 2 life and *then* pumps by the running total, so the copies escalate off
one another: +2, +4, +6 … +2N — **N(N+1) power from one card**. It does not need an enabler; an
enabler only raises where the ladder starts.

**Oracle's Restoration is fuel, not rate.** Flat +1/+1 against Ancestral Anger's escalating +X/+0,
so on raw damage it simply loses. Its `cast_lifegain: 1` per copy raises Draught's starting counter
by N. That is the whole of its edge — and it is why the Anger-vs-Oracle comparison is a *mixture*:

| 4 Anger → 4 Oracle | 20 life | 30 life |
|---|---:|---:|
| in the **Scale** context (no Draught) | **+0.0080** (t=+5.0), margin −126 | −0.0034 (t=−1.9), +195 |
| in the **Draught** context | −0.0024 (t=−1.5), +232 | **−0.0160** (t=−8.5), +618 |

Pooled, those cancel into a meaningless +0.0028 (t=+2.4). **Reporting the pooled figure was an
error** — the same one this project made with Draught before (see `never-report-a-null-unstratified`),
and on a pair the user had already flagged as interacting. Stratify before quoting.

**Twinflame vs Libation is *not* a mixture.** Both trick contexts agree in sign at each life total
(−0.0170 / −0.0120 at 20 life; +0.0129 / +0.0146 at 30). The flip is genuinely about how much damage
one swing must deliver, not about what else is in the deck.

**Impolite Entrance → Libation is a haste enabler (hypothesis, untested).** Libation creates a 1/1
Citizen per resolved copy, and those arrive summoning-sick. Casting Libation first and then Entrance
at the magnet fans haste onto the new Citizens. Twinflame's token copies already have haste, so the
help is one-sided — it should push Test 1 toward Libation at Entrance 4. Entrance's per-copy
`cast_draw` also feeds Fists of Flame.

---

## 7. Corrections made along the way

Recorded because each one changed a conclusion or cost real time.

- **Reported Anger vs Oracle pooled** when it was a sign change across the Draught context. Caught
  by the user asking why Oracle lost at 20 life.
- **Called Anger vs Oracle at 20 life a "genuine tie"** by leaning on the count margin (1.4σ) while
  the mean said t=+2.4 — choosing the number that suited a conclusion already drawn.
- **Estimated generation from the floor pass only**, ignoring the refine, and quoted ~7 h for what
  projected to ~112 h.
- **Waved `K CHANGED 17 -> 20` through** without pricing it: 2.61x the cells.
- **Blamed a keepgen slowdown on core contention** and stopped a run on that basis; the rate did not
  recover when the other job stopped, so the diagnosis was wrong and the stop was unnecessary.
- **Built a union deck** after it had already been rejected. 5+ h of a saturated box.

---

## 8. Open

- Run B: does `4 Draught + 2 Oracle` beat `4 Oracle + 2 Draught`?
- **Entrance at 4** and its interaction with Libation — needs 9,293 new cells (4.6% on top of the
  shipped table), the only genuinely generation-blocked question left.
- Whether 30 life needs its own keep table, or whether the 20-life one transfers. The bracket says
  the table is worth 0.135t at 30 life, so the question is live but the *fit* tilt is small.
- Creature-creation count is held at 3 throughout and has not been contested.
