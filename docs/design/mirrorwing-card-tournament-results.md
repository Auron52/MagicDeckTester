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
| 2/1 and 1/2 Twinflame/Libation mixes | measured | **ELIMINATED** | Dose response is monotone in both directions (≈0.005t per copy at 20 life, ≈0.005t the other way at 30), so a mix always lands strictly between the two pure options. |
| **4 Ancestral Anger** (incumbent) | measured | **ELIMINATED at 20 life; alive at 30** | 6σ+ behind the leader at 20 life in both 2-slots. At 30 life *4 Anger + 2 Oracle* ties for first (+0.0019t, t=+0.7, margin −19). Flat +X/+0 off graveyard copies — raw damage, no synergy tail, worth most when the damage requirement is largest. See 5b. |
| **4 Oracle's Restoration** | measured | **take at 30 life; 2-slot at 20** | Leader at 30 as *4 Oracle + 2 Draught*. At 20 life it is 0.0136t behind 4 Draught (t=+5.2). Its `cast_lifegain: 1` per copy is Draught fuel, not a rate upgrade — which is why it wants to be the small half of the pair at 20. |
| 2 Anger / 2 Oracle mix | measured | **ELIMINATED** | Dead last at 20 life (+0.0329t behind the leader, −1,037, 11.5σ). See the escalation argument in 5b. |
| 2 Anger / 2 Draught / 2 Oracle mix | measured | **ELIMINATED** | Behind at both life totals on both numbers (+0.0132t / 6.8σ at 20; +0.0071t / 3.5σ at 30). |
| **2 Scale the Heights** (incumbent) | measured | **ELIMINATED (2026-08-20)** | Loses to its replacement in 9 of 10 measured contexts, by up to −0.0191t / +769 (11.5σ). The one exception has its two numbers disagreeing inside the apparatus band. Full table in 5b. |
| **4 Fortifying Draught** | measured | **take at 20 life** | Leader at 20 as *4 Draught + 2 Oracle* (−0.0248t vs the old incumbent, +938, 10.4σ). At 30 life it drops to 6th — it self-fuels into a *consistent* board rather than a big single swing. |
| **4 Oracle + 2 Draught** | measured | **leader at 30 life** | −0.0110t / +575 (6.8σ) vs the old incumbent. Beaten at 20 life by the reversed split. |
| **4 Draught + 2 Oracle** | measured | **leader at 20 life** | Beats *4 Oracle + 2 Draught* by −0.0136t (t=−5.2, +195 margin) at 20 and loses to it by +0.0094t (t=+2.9, −536) at 30. Another life-total sign flip. |
| 4 Draught + 2 Scale | measured | superseded by the Scale cut | Isolated Draught count with no Oracle fuel: 2nd at 20 (+0.0027t behind), 9th at 30. Draught's own value does not need Oracle, but the 2-slot is worth more as Oracle than as Scale. |
| 4 Anger + 2 Oracle | measured | **the 30-life contender** | Tied for first at 30 (+0.0019t, t=+0.7). 6th at 20. |
| **4 Impolite Entrance** | **not measured** | **open — and now REACHABLE for free** | Hypothesis with card-data support: Entrance grants haste, Libation's Citizens arrive summoning-sick, so Entrance→Libation is a one-sided enabler (Twinflame's tokens already have haste). Cutting Scale frees a bucket; Entrance can take the cap-4 bucket with the other tricks at 2. See 2c — the 9,293-cell price no longer applies to the form that matters. |
| Expedite | not tried here | n/a | The shipped deck's card; every tournament arm plays Impolite Entrance in its place. It survives only as the bucket Entrance is aliased into. |

### 1b-0. Decision rule: unmodelled upside breaks ties toward Impolite Entrance

**User directive, 2026-08-20:** *"if Impolite Entrance is very close to others we might keep more
copies of it because of the incidental trample that is not modelled."*

The rule is sound because the asymmetry is one-sided, and an audit of every card still in
contention confirms Entrance is the only one with upside the engine cannot see:

| card | unmodelled aspect | does it bias the comparison? |
|---|---|---|
| **Impolite Entrance** | **`gains trample` — not modelled** | **yes, understates Entrance** |
| Impolite Entrance | sorcery-vs-instant not modelled | no — makes it parameter-identical to Expedite |
| Luxurious Libation | token colour | no — nothing in the deck reads colour |
| Oracle's Restoration | ~~`you control` not enforced~~ — **the note was wrong; see below** | no |
| Draught, Twinflame, Gold Rush, Expedite, Zada, Mirrorwing | none noted | — |

So: **a tie or a near-tie involving Entrance should be resolved toward more Entrance**, and only a
loss clearly outside the apparatus band argues against it. What the engine measured in run C is
strictly "4 cheap haste cantrips beat 2" — Entrance is parameter-identical to Expedite here, so
the screen cannot distinguish the two cards, and the real Entrance can only be better than what
was measured.

**Correction, same day, prompted by the user:** *"Oracle's Restoration does matter. There are
games with opponent's creatures, so we must not target them."* The `cards.json` note claiming
`you control` was unmodelled is **wrong**, and it pointed the deviation the wrong way.
`ResolveSoloTargetTrick` filters `p.controller_index == controller` before anything resolves
([SpellEffects.h](../../src/core/SpellEffects.h)), so **no solo-target trick can ever be pointed at
an opponent's creature** — Oracle's restriction is enforced in play. If anything the engine is
*over*-restrictive: Ancestral Anger, Impolite Entrance, Fists of Flame and Gold Rush all read
"target creature" (any creature) and are confined to own creatures too. That is harmless for play
strength and does not bias an Oracle-vs-Anger screen, since both sides are confined identically —
but the old note would have had a reader discount Oracle for a drawback it does not have. Opponent
creatures are not hypothetical in this engine (`etb_opp_creates_tokens` gifts them, Massacre Wurm
sweeps them); they are merely absent from the goldfish *this* deck is measured against.

**How much better is bounded, though, and by the deck's own contents.** Fists of Flame — 4 copies,
fixed in every arm — *also* grants trample, and it is a `solo_target_trick`, so on a magnet fan-out
it gives trample to the whole board. Entrance's trample is therefore redundant on any turn a Fists
is cast. Measured over run C's won games:

| life | won games that cast Fists at least once |
|---|---:|
| 20 | 57.0–57.3% |
| 30 | 61.5–61.8% |

So the unmodelled upside is real but partial: it can only be worth anything in the ~40% of wins
with no Fists, and less than that, since "cast during the game" over-counts "granted trample on
the lethal turn". Treat it as a tie-breaker, which is exactly how the user framed it — not as a
thumb heavy enough to overturn a measured 0.016t gap.

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
| arm | life | games | run A mean | run B mean | identical games |
|---|---:|---:|---:|---:|:--:|
| tf3lib0_a4s2 | 20 | 10,000 | 5.0718 | 5.0718 | YES |
| tf3lib0_a4s2 | 30 | 10,000 | 5.4568 | 5.4568 | YES |
<!-- /AUTO:bridge -->

If that check ever fails, every cross-map statement in this document is void.

### 2c. Eliminating a card frees a bucket — the reachable space, exactly

Derived from the shipped table's own cells (`comp` maxima), the flexible part of the deck is
**four buckets with capacities 3 + 4 + 2 + 2 = 11**, and the arms use exactly 11 slots:

| bucket | cap | native card | what may be aliased in |
|---:|---:|---|---|
| 2 | 3 | Twinflame | Luxurious Libation |
| 9 | **4** | Ancestral Anger | Oracle's Restoration, Fortifying Draught, **Impolite Entrance** |
| 3 | 2 | Scale the Heights | Fortifying Draught, Oracle's Restoration |
| 15 | 2 | Expedite | Impolite Entrance |

The other thirteen buckets are the fixed 49 cards. So **any** decklist of the shape
`3 A + 4 B + 2 C + 2 D`, with A/B/C/D drawn from {Twinflame, Libation, Anger, Oracle, Draught,
Entrance, Scale, Expedite} and assigned one per bucket, is reachable with **zero generation** —
every such arm has the identical bucket-count signature `(3,4,2,2)`, so if one is covered they all
are. Roughly 180 arms, of which the tournament has explored about 30.

What was actually blocked was **Entrance at 4 *while keeping* a 4-count trick**: that needs two
cap-4 buckets and there is one. Section 1a previously priced this at 9,293 new cells. With Scale
cut, the interesting form is reachable instead: give Entrance the cap-4 bucket and drop the trick
suite to `2 + 2`, i.e. rotate which of {Draught, Oracle, Entrance} is the 4-of. Two of the three
rotations are already measured. **The Entrance question is free after all** — see section 8.

The general rule, worth carrying to the next deck: *cutting a card does not just shorten the arm
list, it returns that card's bucket capacity to the pool.* Elimination buys reachability.

### 2d. What the caps decide for you — the target list, enumerated

The list the user is converging on (2026-08-20) is **8 cards from {Oracle, Draught, Entrance} plus
3 from {Twinflame, Libation}**, with Entrance at 2 as a floor. That is exactly the 11 flexible
slots, so the caps above enumerate the whole space:

- **The 3 trick cards are free.** They all sit in bucket 2 (cap 3), so *any* Twinflame/Libation
  split — 3/0, 2/1, 1/2, 0/3 — is reachable, and all four are already measured under Entrance = 2.
- **The 8 draw/pump cards have only three legal splits.** Each card takes one bucket, and the
  available capacities are 4, 2, 2 — so the counts must be a permutation of **(4, 2, 2)**:

  | split | run | verdict |
  |---|---|---|
  | 4 Oracle + 2 Draught + 2 Entrance | A (`a0o4_draught`) | leader at 30 life |
  | 4 Draught + 2 Oracle + 2 Entrance | B (`d4o2`) | leader at 20 life |
  | **4 Entrance + 2 Oracle + 2 Draught** | **C** | in flight |

- **Everything else is measurable too — this was my error, corrected 2026-08-20.** I first wrote
  that a 3-count of a draw/pump card, or Entrance at 0, was "structurally unreachable without
  generation". That confused *cells* with *hands*. Over-filling a bucket does not error: the policy
  answers `present=false` and those hands fall through to the heuristic keep. What matters is how
  often, and the missing cells are always the **rarest** ones — all copies of a small-count card in
  the opening seven:

  | 8-card split (Entrance at 2 unless shown) | cap-4 bucket | cells short | **share of hands** |
  |---|---|---:|---:|
  | 4 O + 2 D, 2 O + 4 D, 2 O + 2 D + 4 E | either | 0 | fully covered |
  | 5 O + 1 D *(or 1 O + 5 D)* | the 5 | 134 | 0.0004% — 1 in 260,072 |
  | 6 O + 0 D *(or 0 O + 6 D)* | the 6 | 134 | 0.0022% — 1 in 44,700 |
  | 3 O + 3 D | either | 3,689 | 0.102% — 1 in 978 |
  | 4 O + 4 D + 0 E | either | 3,571 | 0.388% — 1 in 258 |

  Against effects of 0.010–0.014t, a 0.1% fallback bounds the bias at roughly **0.0002t**. So the
  *whole* space is measurable now; only the two 0.388% shapes are worth a second thought. The
  driver reports the hand-weighted rate per arm and refuses above 1% (`tourney_alias.verify`),
  rather than refusing on a single absent cell.

- If a miss rate ever *is* material, the fix is the user's suggestion (2026-08-20): **bucket by card
  number instead of name**, so copies 44–45 of a card sit in one bucket and 46 in another. That
  pins the deck's bucket sizes at (3,4,2,2) for any composition, making everything exactly
  reachable at zero generation, at the cost of a blurrier policy — the same hand *content* then
  maps to different cells depending on which physical copies were drawn. It needs a small engine
  change: `ExhaustiveKeepPolicy::Decide` takes names today, though the call site already has
  `m_number` in hand.

So the target list is a lookup, not a search, and generation stays unspent.

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
| option | life | games | mean turn | vs baseline | t | better | worse | margin | s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **4 Anger + 2 Scale** (baseline) | 20 | 40,000 | 5.0607 | -- | -- | -- | -- | -- | -- |
| 4 Anger + 2 Oracle | 20 | 40,000 | 5.0527 | -0.0080 | -4.9 | 1,735 | 1,324 | **+411** | +7.4 |
| 2 Anger + 2 Draught + 2 Oracle | 20 | 40,000 | 5.0491 | -0.0116 | -4.9 | 3,261 | 2,714 | **+547** | +7.1 |
| 4 Draught + 2 Scale | 20 | 40,000 | 5.0387 | -0.0220 | -8.7 | 3,604 | 2,970 | **+634** | +7.8 |
| 4 Draught + 2 Oracle | 20 | 40,000 | 5.0359 | -0.0248 | -8.8 | 4,563 | 3,625 | **+938** | +10.4 |
| **4 Anger + 2 Scale** (baseline) | 30 | 40,000 | 5.4600 | -- | -- | -- | -- | -- | -- |
| 4 Anger + 2 Oracle | 30 | 40,000 | 5.4510 | -0.0091 | -4.8 | 1,977 | 1,424 | **+553** | +9.5 |
| 2 Anger + 2 Draught + 2 Oracle | 30 | 40,000 | 5.4562 | -0.0039 | -1.4 | 3,620 | 3,288 | **+332** | +4.0 |
| 4 Draught + 2 Scale | 30 | 40,000 | 5.4616 | +0.0016 | +0.5 | 3,908 | 4,084 | **-176** | -2.0 |
| 4 Draught + 2 Oracle | 30 | 40,000 | 5.4584 | -0.0016 | -0.5 | 4,952 | 4,745 | **+207** | +2.1 |
<!-- /AUTO:runB -->

Twinflame → Libation ladder under map B, as an independent replication of section 4's ladder on a
different apparatus:

<!-- AUTO:runB_ladder -->
| step | life | games | turns | t | margin | s |
|---|---:|---:|---:|---:|---:|---:|
| 3 Twinflame -> 2 Twin / 1 Lib | 20 | 50,000 | -0.0085 | -8.1 | **+298** | +7.0 |
| 3 Twinflame -> 1 Twin / 2 Lib | 20 | 50,000 | -0.0144 | -10.0 | **+486** | +8.3 |
| 3 Twinflame -> 3 Libation | 20 | 50,000 | -0.0190 | -10.7 | **+554** | +7.9 |
| 2 Twin / 1 Lib -> 1 Twin / 2 Lib | 20 | 50,000 | -0.0059 | -5.9 | **+217** | +5.2 |
| 2 Twin / 1 Lib -> 3 Libation | 20 | 50,000 | -0.0104 | -7.2 | **+312** | +5.4 |
| 1 Twin / 2 Lib -> 3 Libation | 20 | 50,000 | -0.0045 | -4.3 | **+112** | +2.7 |
| 3 Twinflame -> 2 Twin / 1 Lib | 30 | 50,000 | +0.0020 | +1.5 | **-103** | -2.2 |
| 3 Twinflame -> 1 Twin / 2 Lib | 30 | 50,000 | +0.0044 | +2.4 | **-309** | -4.6 |
| 3 Twinflame -> 3 Libation | 30 | 50,000 | +0.0109 | +5.0 | **-724** | -8.9 |
| 2 Twin / 1 Lib -> 1 Twin / 2 Lib | 30 | 50,000 | +0.0025 | +1.9 | **-164** | -3.3 |
| 2 Twin / 1 Lib -> 3 Libation | 30 | 50,000 | +0.0090 | +4.9 | **-558** | -8.1 |
| 1 Twin / 2 Lib -> 3 Libation | 30 | 50,000 | +0.0065 | +5.0 | **-359** | -7.3 |
<!-- /AUTO:runB_ladder -->

---

## 5b. Standings, and who is out

The bridge above is identical game-for-game, so run A and run B arms can be ranked against each
other on the same seeds. Every combination measured so far, against the leader at its life total:

<!-- AUTO:standings -->

**20 life** — leader: **4 Draught + 2 Oracle**

| # | combination | mean turn | behind leader | t | margin | s |
|---:|---|---:|---:|---:|---:|---:|
| 1 | **4 Draught + 2 Oracle** | 5.0359 | — | — | — | — |
| 2 | 4 Draught + 2 Scale | 5.0387 | +0.0027 | +1.7 | **-328** | -6.2 |
| 3 | 2 Anger/2 Draught/2 Oracle | 5.0491 | +0.0132 | +6.8 | **-222** | -3.7 |
| 4 | 4 Oracle + 2 Draught | 5.0496 | +0.0136 | +5.2 | **-195** | -2.4 |
| 5 | 4 Anger + 2 Draught | 5.0520 | +0.0160 | +6.1 | **-488** | -5.8 |
| 6 | 4 Anger + 2 Oracle | 5.0527 | +0.0168 | +6.6 | **-497** | -6.2 |
| 7 | 2 Anger/2 Oracle + 2 Draught | 5.0566 | +0.0206 | +7.6 | **-458** | -5.5 |
| 8 | 4 Anger + 2 Scale | 5.0607 | +0.0248 | +8.8 | **-938** | -10.4 |
| 9 | 4 Oracle + 2 Scale | 5.0687 | +0.0327 | +11.4 | **-973** | -10.8 |
| 10 | 2 Anger/2 Oracle + 2 Scale | 5.0689 | +0.0329 | +11.5 | **-1,037** | -11.5 |

**30 life** — leader: **4 Oracle + 2 Draught**

| # | combination | mean turn | behind leader | t | margin | s |
|---:|---|---:|---:|---:|---:|---:|
| 1 | **4 Oracle + 2 Draught** | 5.4490 | — | — | — | — |
| 2 | 4 Anger + 2 Oracle | 5.4510 | +0.0019 | +0.7 | **-19** | -0.2 |
| 3 | 2 Anger/2 Draught/2 Oracle | 5.4562 | +0.0071 | +2.4 | **-295** | -3.5 |
| 4 | 4 Oracle + 2 Scale | 5.4566 | +0.0076 | +3.2 | **-388** | -5.3 |
| 5 | 2 Anger/2 Oracle + 2 Draught | 5.4578 | +0.0088 | +6.5 | **-357** | -9.0 |
| 6 | 4 Draught + 2 Oracle | 5.4584 | +0.0094 | +2.9 | **-536** | -5.8 |
| 7 | 4 Anger + 2 Scale | 5.4600 | +0.0110 | +3.9 | **-575** | -6.8 |
| 8 | 2 Anger/2 Oracle + 2 Scale | 5.4604 | +0.0114 | +4.3 | **-533** | -6.7 |
| 9 | 4 Draught + 2 Scale | 5.4616 | +0.0126 | +3.7 | **-908** | -9.3 |
| 10 | 4 Anger + 2 Draught | 5.4650 | +0.0160 | +8.5 | **-618** | -11.4 |
<!-- /AUTO:standings -->

### Eliminated — do not put these back in a comparison

**Scale the Heights — OUT (user call, 2026-08-20).** It was the incumbent 2-slot card. It is
behind its replacement in nine of the ten contexts it was measured in:

<!-- AUTO:scale -->
| context | 2 Scale becomes | life | turns | t | margin | s | Scale |
|---|---|---:|---:|---:|---:|---:|:--:|
| 4 Anger | 2 Draught | 20 | -0.0088 | -4.4 | **+440** | +6.7 | loses |
| 4 Anger | 2 Oracle | 20 | -0.0080 | -4.9 | **+411** | +7.4 | loses |
| 2 Anger / 2 Oracle | 2 Draught | 20 | -0.0123 | -6.1 | **+580** | +8.7 | loses |
| 4 Oracle | 2 Draught | 20 | -0.0191 | -9.2 | **+769** | +11.5 | loses |
| 4 Draught | 2 Oracle | 20 | -0.0027 | -1.7 | **+328** | +6.2 | loses |
| 4 Anger | 2 Draught | 30 | +0.0050 | +2.1 | **-58** | -0.8 | **wins** |
| 4 Anger | 2 Oracle | 30 | -0.0091 | -4.8 | **+553** | +9.5 | loses |
| 2 Anger / 2 Oracle | 2 Draught | 30 | -0.0026 | -1.1 | **+163** | +2.3 | loses |
| 4 Oracle | 2 Draught | 30 | -0.0076 | -3.2 | **+388** | +5.3 | loses |
| 4 Draught | 2 Oracle | 30 | -0.0032 | -1.7 | **+406** | +7.3 | loses |
<!-- /AUTO:scale -->

The single exception (4 Anger, 30 life, Scale vs Draught) is not evidence for Scale: the two
numbers disagree there — the mean favours Scale by +0.0050t while the count margin runs the other
way at −58 (−0.8σ) — and +0.0050t is well inside the 0.0187t apparatus band for 30 life. That is
the signature of noise around zero, not of a card holding its slot. Everywhere the two numbers
*agree*, they agree against Scale, by up to −0.0191t / +769 games (11.5σ). It is also last or
second-to-last in the standings at both life totals.

**Mixed trick suites — OUT.** Every mix is beaten by a pure option:

- *2 Anger / 2 Oracle* is dead last at 20 life (+0.0329t behind the leader, −1,037, 11.5σ) and
  never leads at 30. Splitting the 4-slot halves both escalation curves and completes neither.
- *2 Anger / 2 Draught / 2 Oracle* is +0.0132t (6.8σ) behind at 20 and +0.0071t (3.5σ) behind at
  30 — the three-way split loses on both numbers at both life totals.
- *Twinflame / Libation mixes* (2/1 and 1/2) sit on a monotone dose response in both ladders, so a
  mix is always strictly between the two pure options and never better than the right one.

This is a general result, not three coincidences: the payoffs here are **escalating**, and an
escalating payoff is superlinear in its own copy count. Splitting a slot is therefore
value-destroying by construction, and future comparisons should not spend arms on mixes.

**Ancestral Anger — OUT at 20 life, ALIVE at 30.** At 20 life every Anger arm is 6σ or worse
behind the leader (+0.0160t with 2 Draught, +0.0168t with 2 Oracle). At 30 life *4 Anger + 2
Oracle* is statistically tied for first (+0.0019t, t=+0.7, margin −19, −0.2σ). Its flat +X/+0 off
graveyard copies is raw damage with no synergy tail, which is worth most exactly when the required
damage is largest. Keep it in the 30-life bracket only.

### Still contending

| slot | 20 life | 30 life |
|---|---|---|
| 4-count trick | **Fortifying Draught** (leader, and ahead of Oracle by −0.0136t / +195) | **Oracle's Restoration** (leader) or **Ancestral Anger** (tied) |
| 2-count trick | Oracle over Draught, but only −0.0027t (t=−1.7) — the 2-slot barely matters at 20 | Draught |
| 3-count trick | **Luxurious Libation** (−0.0190t over Twinflame) | **Twinflame** (+0.0109t over Libation) |
| Impolite Entrance | fixed at 2 in every arm so far — **untested**, see section 8 | same |

Both open calls are **life-total splits, not measurement gaps**: more games will not resolve them,
because the two life totals genuinely want different cards. The 4-slot flips (Draught at 20,
Oracle/Anger at 30) for the same reason the trick slot flips — see the ceiling-vs-consistency
mechanism in section 6.

---

## 5c. Results — run C (alias map C): Impolite Entrance at 4

Bridge verified first: the ship arm came out **10,000/10,000 identical** under maps B and C at both
life totals, so map C chains to the rest.

`2 O + 2 D + 4 E` against the two splits it replaces (negative = the Entrance-heavy list is faster):

| baseline | 20 life | 30 life |
|---|---:|---:|
| `4 O + 2 D + 2 E` (run A) | +0.0024 (t=+0.9) | **−0.0184 (t=−5.6, +385, 5.3σ)** |
| `4 D + 2 O + 2 E` (run B) | +0.0160 (t=+4.6) | **−0.0277 (t=−6.5, +809, 7.8σ)** |

**At 30 life the four Entrance-4 arms take ranks 1–4 outright**, ahead of the previous leader. At
20 life Entrance-4 is 5th: it *ties* `4 O + 2 D` (t=+0.9 — and by the tie-break rule in 1b-0 that
tie goes to Entrance) but loses clearly to `4 D + 2 O`.

| | best list | mean turn |
|---|---|---:|
| 20 life | `4 D + 2 O + 2 E` + 3 Libation | 5.0259 |
| 30 life | `2 O + 2 D + 4 E` + 2 Twinflame / 1 Libation | 5.4275 |

### The haste interaction, confirmed

The user predicted (2026-08-20) that if Twinflame is held *as a haste source*, its value should
fall as Entrance rises, because Entrance supplies the same thing. It does, monotonically:

| context | Twinflame's edge at 30 life |
|---|---:|
| Entrance 2, with 4 Oracle | +0.0187t (t=+3.7) |
| Entrance 2, with 4 Draught | +0.0130t (t=+2.7) |
| **Entrance 4** | **+0.0058t (t=+1.2 — no longer significant)** |

At 4 Entrance the trick slot almost stops mattering at 30 life: the whole Twinflame→Libation range
is 5.4275–5.4346. This is the first *predicted-in-advance* interaction the tournament has
confirmed, and it is the mechanism behind the trick slot's life-total flip: Twinflame's edge was
never about the copies, it was about haste, and haste is purchasable elsewhere.

---

## 5d. Results — run D: the space is closed

42 jobs, 420,000 games, rc=0 in 1.91 h. With the 4-copy limit and the user's floors (Entrance ≥ 2,
Draught ≥ 2) there are exactly nine legal shapes, and all nine are now measured at both life
totals.

**Aliasing control first.** `3 O + 3 D + 2 E` ran under map A *and* map B — same 60 cards, different
bucketing, different missing cells. Pooled over three trick contexts and both life totals the
difference is **+0.0038t (t=+1.5, n=60,000)**, with ~90% of games coming out identical. Not
significant, and the same size as the fit tilt measured in section 3. The aliasing choice does not
decide anything here.

### Every legal list, best trick context per shape

| shape | 20 life | 30 life |
|---|---:|---:|
| `2 O + 4 D + 2 E` | **5.0259** ← best | 5.4530 |
| `3 O + 3 D + 2 E` | 5.0397 | 5.4471 |
| `1 O + 4 D + 3 E` | 5.0437 | 5.4576 |
| `4 O + 2 D + 2 E` | 5.0454 | 5.4408 |
| `2 O + 3 D + 3 E` | 5.0458 | 5.4503 |
| `2 O + 2 D + 4 E` | 5.0459 | **5.4275** ← best |
| `1 O + 3 D + 4 E` | 5.0502 | 5.4463 |
| `3 O + 2 D + 3 E` | 5.0503 | 5.4455 |
| `0 O + 4 D + 4 E` | 5.0516 ← worst | 5.4600 ← worst |

**The structure is clean and it inverts with life total.** At 20 life the top three shapes all have
Draught at 4 or 3 and Entrance at 2; at 30 life the top three all have **Draught at 2**. So:

- **20 life wants Draught maxed and Entrance minimal** — the user's prior, confirmed exactly.
- **30 life wants Draught minimal and Entrance maxed** — the reverse.
- **Oracle wants to be 2 in both.** `0 O + 4 D + 4 E` is *last at both life totals*, which is the
  Oracle-depends-on-Draught finding running the other way: Draught also needs Oracle, because
  Oracle's `cast_lifegain: 1` per copy is what starts Draught's escalation above 2.

Trading Oracle for Entrance costs at 20 life and is free at 30, which is the same inversion seen
from the other side:

| trade | 20 life | 30 life |
|---|---:|---:|
| 4 D: Entrance 2 → 3 (Oracle 2→1) | +0.0151 (t=+4.0) | +0.0002 (t=+0.0) |
| 4 D: Entrance 3 → 4 (Oracle 1→0) | +0.0085 (t=+2.8) | +0.0018 (t=+0.5) |
| 3 D: Entrance 3 → 4 (Oracle 2→1) | +0.0053 (t=+1.3) | −0.0064 (t=−1.3) |

### The three answers

| if you are optimising for | list | mean |
|---|---|---:|
| **20 life** | 3 Libation · 2 Oracle · 4 Draught · 2 Entrance | 5.0259 |
| **30 life** | 2 Twinflame / 1 Libation · 2 Oracle · 2 Draught · 4 Entrance | 5.4275 |
| **both equally** | 2 Oracle · 2 Draught · 4 Entrance (trick free) | 10.4805 sum |

`2 O + 2 D + 4 E` takes the top **four** places on the two-life-total sum, and across its four trick
contexts the sum spans only 10.4805–10.4883 — so if one list must serve both, the trick slot is
close to a free choice and the Entrance-heavy build is the compromise. The tie-break rule in 1b-0
pushes the same way, since Entrance's trample is upside the engine cannot see.

It does **not** rescue Entrance at 20 life, though: the gap from `2 O + 4 D + 2 E` to the nearest
Entrance-3 list is 0.0151t at t=+4.0, far outside a tie.

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

**High ceiling vs consistency — Twinflame against Libation.** From 300 verified case games per life
total, pooled across both directions so the numbers are about the cards rather than about which side
was selected:

| | 20 life | | 30 life | |
|---|---:|---:|---:|---:|
| | 3 Twinflame | 3 Libation | 3 Twinflame | 3 Libation |
| biggest attack, mean | 17.05 | 12.64 | 27.16 | 21.24 |
| biggest attack, **max** | **205** | 39 | **125** | 91 |
| creatures at that attack | 6.06 | 5.31 | 7.87 | 6.52 |
| win turn (9 = unwon) | 6.65 | **6.27** | 6.59 | 6.52 |

Twinflame copies the whole board, so its ceiling is enormous (a 205-damage swing) and its floor is
whatever the board happened to be. Libation is flat: +X/+X for the mana paid, plus a Citizen. **At
20 life the ceiling is wasted — you only ever needed 20 — so consistency wins. At 30 life the
ceiling starts getting used.** That is the sign flip, and it is visible game by game in
`logs/tourney/cases/tf_tf3lib0_vs_tf0lib3_L{20,30}/`.

The same lens explains Anger vs Oracle. Without Draught at 20 life Oracle builds a marginally bigger
board (5.91 creatures vs 5.18, ceiling 115 vs 80) and still **kills later** (6.26 vs 5.97) — a
bigger board it cannot convert. With Draught at 30 life the ceiling is what matters and Oracle's
reaches **321** against Anger's 185.

### Case logs

`logs/tourney/cases/` — five bundles, 300 games each, both arms replayed on the same seed with full
traces, every replay verified to reproduce the measured win turn. `README.md` there explains the
layout; open `tools/replay/index.html` and drag a trace onto it.

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

- ~~Run B: does `4 Draught + 2 Oracle` beat `4 Oracle + 2 Draught`?~~ **Answered:** yes at 20 life
  (−0.0136t, t=−5.2), no at 30 (+0.0094t, t=+2.9). A life-total split like the trick slot.
- **Entrance at 4** and its interaction with Libation — **run C**, and it is free (section 2c). The
  design rotates which of {Draught, Oracle, Entrance} takes the cap-4 bucket while the other two sit
  at 2, crossed with Twinflame vs Libation at 3:

  | arm | 3-of | 4-of | 2-of | 2-of | status |
  |---|---|---|---|---|---|
  | `d4` | TF or Lib | Draught | Oracle | Entrance | measured (run B `d4o2`) |
  | `o4` | TF or Lib | Oracle | Draught | Entrance | measured (run A `a0o4_draught`) |
  | `e4` | TF or Lib | **Entrance** | Draught | Oracle | **new, zero generation** |

  The Entrance→Libation interaction is then `(e4 − d4)` under Libation minus the same under
  Twinflame — a difference-in-differences on the same seeds, which is exactly the one-sided-enabler
  claim and not merely "is 4 Entrance good". Run C needs its own alias map (Entrance in bucket 9),
  so it must carry its own bridge arm and re-measure `d4`/`o4` under that map rather than chaining
  to runs A/B — which doubles as a third independent check on the fit tilt.
- **Run D — the rest of the legal space.** Three constraints, all the user's (2026-08-20), close
  the frame completely: **at most 4 copies** of a card (deck construction), **Entrance ≥ 2**, and
  **Draught ≥ 2** — *"so there are only 4 spots remaining to fill."* Those 4 free spots distribute
  over the three cards nine ways, and nine is the entire space:

  | free spots (O/D/E) | O | D | E | where |
  |---|---:|---:|---:|---|
  | 4 / 0 / 0 | 4 | 2 | 2 | run A |
  | 3 / 1 / 0 | 3 | 3 | 2 | **run D** |
  | 3 / 0 / 1 | 3 | 2 | 3 | **run D** |
  | 2 / 2 / 0 | 2 | 4 | 2 | run B |
  | 2 / 1 / 1 | 2 | 3 | 3 | **run D** |
  | 2 / 0 / 2 | 2 | 2 | 4 | run C |
  | 1 / 2 / 1 | 1 | 4 | 3 | **run D** |
  | 1 / 1 / 2 | 1 | 3 | 4 | **run D** |
  | 0 / 2 / 2 | 0 | 4 | 4 | **run D** |

  - **The Draught floor is measured, not stylistic.** Oracle is dependent on Draught — without it,
    Oracle is the *worst* option at 20 life (+0.0080t vs Anger), because its `cast_lifegain: 1` per
    copy is Draught fuel rather than a rate upgrade. A Draught-light arm does not merely lose
    Draught, it stops the Oracle beside it working, so that corner is doubly unpromising.
  - **Trick contexts are weighted toward Libation** — 3 Lib, 2 Lib / 1 Twin, and 3 Twin as the
    30-life reference. An O/D/E optimum found only under 3 Twinflame is an optimum for a list we
    may not build.
  - **1 Twinflame is back as a candidate, on mechanism rather than interpolation.** Section 5b
    eliminated mixes on a monotone dose response; the user's reason for keeping one is different
    — *"1 Twinflame to mitigate the haste loss."* Twinflame's token copies have haste; Libation's
    Citizens arrive summoning-sick and need Entrance to attack the turn they land. One Twinflame is
    haste that does not compete for the Entrance slots. That also predicts an **interaction**: if
    Twinflame is being held as a haste source, its value should *fall* as Entrance rises, since
    Entrance supplies the same thing. Run D crosses Entrance 2/3/4 with all three trick contexts,
    so the prediction is directly readable.
  - The cap-4 bucket goes to the largest count, which forces each arm's map; only the symmetric
    3 O + 3 D has a choice, and it runs **both ways** as the aliasing control.
  - 42 jobs, 420,000 games, ~3.1 h. Miss rates 0.102% for a 3-of outside the cap-4 bucket and
    0.388% for 0 O + 4 D + 4 E, printed per arm.
- Mixes are eliminated (5b), so run C spends no arms on them: 3 rotations × 2 tricks × 2 life
  totals = 12 jobs.
- Whether 30 life needs its own keep table, or whether the 20-life one transfers. The bracket says
  the table is worth 0.135t at 30 life, so the question is live but the *fit* tilt is small.
- Creature-creation count is held at 3 throughout and has not been contested.
