# Mirrorwing Dragon — final screen results (2026-08-29)

Self-contained results summary for the deck owner. The full method narrative, per-card notes and
the screen-by-screen history live in `mirrorwing-instigator-slot-screen.md`; this file is just the
numbers and how to read them.

All figures are **avg turn-to-win vs the shipped list**, paired. **Negative = faster = better.**

---

## Recommendation

**`or4_ie0_an3`** — staged at `decks/Mirrorwing Dragon/v3-heroism-draught/`.

```
4  Mirrorwing Dragon        4  Oracle's Restoration     8  Forest
4  Zada, Hedron Grinder     4  Fortifying Draught       4  Game Trail
4  Frontline Heroism        3  Ancestral Anger          3  Sandstone Needle
4  Ignoble Hierarch                                     2  Gruul Turf
4  Elvish Mystic                                        2  Mountain
4  Fists of Flame                                       2  Rootbound Crag
4  Gold Rush
```

**Impolite Entrance 0 · Luxurious Libation 0 · Goblin Instigator 0.** Seven cards out, seven in.

It is statistically tied with `ie0_an4` (Oracle 3 / Anger 4) — see *The tie* below. Either is
defensible; `or4_ie0_an3` is the one carrying held-out confirmation at **both** life totals.

---

## Best five arms

Composition is written `Oracle / Entrance / Anger / Libation`; every arm has Gold Rush 4, Fists 4,
Draught 4, Heroism 4 and the same 21 lands. The four counts always total 7.

### 20 life — all blocks pooled (inverse-variance)

| # | arm | pooled | se | blocks | composition |
|---|---|---|---|---|---|
| 1 | `ie0_an4` | −0.5392 | 0.0042 | 3 | 3 / 0 / 4 / 0 |
| 2 | `or4_ie1_an2` | −0.5370 | 0.0052 | 2 | 4 / 1 / 2 / 0 |
| 3 | `or4_ie0_an3` | −0.5366 | 0.0052 | 2 | 4 / 0 / 3 / 0 |
| 4 | `ie1_an3` | −0.5297 | 0.0052 | 2 | 3 / 1 / 3 / 0 |
| 5 | `or4_ie1_lib1_an1` | −0.5272 | 0.0072 | 1 | 4 / 1 / 1 / 1 |

### 30 life — all blocks pooled

| # | arm | pooled | se | blocks | composition |
|---|---|---|---|---|---|
| 1 | `ie0_an4` | −0.4830 | 0.0083 | 2 | 3 / 0 / 4 / 0 |
| 2 | `or4_ie0_an3` | −0.4784 | 0.0083 | 2 | 4 / 0 / 3 / 0 |
| 3 | `or4_ie1_an2` | −0.4775 | 0.0084 | 2 | 4 / 1 / 2 / 0 |
| 4 | `or4_ie1_lib1_an1` | −0.4742 | 0.0112 | 1 | 4 / 1 / 1 / 1 |
| 5 | `ie1_an3` | −0.4704 | 0.0083 | 2 | 3 / 1 / 3 / 0 |

**The same five arms occupy both life totals**, merely reordered, and the top three sit inside one
se of each other in both. Every arm in either top five has Entrance ≤ 1 except one, and the top two
of both lists have **Entrance 0**.

### Every block, for audit

```
20 life
  ie0_an4        oracle4: -0.5422   oracle4_confirm: -0.5357   corner: -0.5396
  or4_ie0_an3    corner:  -0.5399   corner_confirm:  -0.5332
  or4_ie1_an2    oracle4: -0.5390   corner:          -0.5349
  ie1_an3        oracle4: -0.5316   corner:          -0.5278

30 life
  ie0_an4        entanger: -0.4898  or4_grid:        -0.4764
  or4_ie0_an3    or4_grid: -0.4768  or4_grid_confirm:-0.4799
  or4_ie1_an2    entanger: -0.4875  or4_grid:        -0.4679
  ie1_an3        entanger: -0.4794  or4_grid:        -0.4617
```

Seeds 6,500,000 / 7,000,000 / 7,500,000 / 8,000,000 / 8,500,000 / 9,000,000 — all disjoint. No
winner is confirmed on the seeds that selected it.

---

## The tie, and why single numbers mislead here

`or4_ie0_an3` and `ie0_an4` cannot be separated. The *ordering flips depending on which view you
take*, which is itself the evidence:

| view | says |
|---|---|
| corner screen block (both on identical games) | `or4_ie0_an3` ahead by 0.0003 |
| pooled marginals, all blocks | `ie0_an4` ahead by 0.0026 |
| **paired head-to-head, 20 life** | **−0.0003 ± 0.0015 (t = −0.16)** |
| **paired head-to-head, 30 life** | **−0.0004 ± 0.0021 (t = −0.20)** |

The paired contrast is the sharp instrument — it differences the *same games*, so its se (0.0015–
0.0021) is 3–4× tighter than the marginal deltas (0.0052–0.0083). It says tie at both life totals.

### Winner's curse is present and symmetric

Each arm was the **selected winner of its own screen**, and each shrank on held-out seeds by
almost exactly the same amount:

```
ie0_an4       screen -0.5422  ->  held out -0.5357     shrinkage +0.0065  (t = +0.63)
or4_ie0_an3   screen -0.5399  ->  held out -0.5332     shrinkage +0.0067  (t = +0.64)
```

So a screen-block number like −0.5399 is the *optimistic* end of an arm's range, not its estimate.
Both arms are inflated by the same ~0.0066 in their own screens, so comparing one arm's screen
number against another's pooled number is not a fair comparison. Pool, or compare head-to-head.

---

## Impolite Entrance

**Entrance→Anger runs monotonically to ZERO at both life totals.**

```
20 life   -0.0090 / -0.0164 / -0.0266 / -0.0372   (t = -15.5 across the ladder)
30 life   -0.0049 / -0.0119 / -0.0213 / -0.0316
```

A pre-registered prediction that the optimum would be interior (1–3 Entrance) fails at both.

**Even one Entrance costs about 0.005–0.009 turns.** An earlier draft of this analysis reported
that a single copy was free, on the strength of one block measuring t = −1.32. Two later disjoint
blocks contradicted it (behind by 0.0050 and 0.0089, ~3–4 se). That earlier claim promoted a single
below-threshold result to a positive finding and is **withdrawn**.

### The caveat that cuts the other way

**Impolite Entrance's trample is not modelled.** The goldfish opponent never blocks, so trample
changes no damage and the card is parameter-identical to Expedite under this engine. Every Entrance
figure above is therefore a **lower bound**, and this is a modelling limit — more games will not
reduce it.

Haste, the other thing Entrance provides, was checked separately and is *not* the reason to keep
it: removing every haste source moves board exposure 2.60 → 2.57 turns and the "kill from nowhere"
rate 19.9% → 19.9%. Heroism's Soldiers already enter hasted.

So: the engine measures Entrance as costly at any count, and the one merit it cannot see is
trample. That is a judgment call for the deck owner — but it is now a call against a measured cost,
not against a tie.

---

## Luxurious Libation — cut, with a mechanism

**20 life: four matched 1:1 swaps into Ancestral Anger. Anger won all four.**

```
Or4 / E1   A0 L2 -0.5148  ->  A1 L1 -0.5272  ->  A2 L0 -0.5390    -0.0121/copy, monotone
Or3 / E3                                                          -0.0090/copy
Or3 / E2                                                          -0.0086/copy
Or4 / E2                                                          -0.0038/copy  (weak, ~1.6 se)
```

**30 life: the ladder inverts — but the inversion is not Libation.** The mix `or4_ie1_lib1_an1`
(−0.4742) beat all-Anger `or4_ie1_an2` (−0.4679) by 0.0063, which looked like the last copy earning
its slot in long games. 500 unbiased game pairs were replayed with full logs and conditioned on
whether Libation was actually cast:

```
stratum                  pairs    mix     all-Anger    diff
A cast Libation            110   5.482      5.482     +0.000
A never cast Libation      390   4.956      4.964     -0.008
ALL                        500   5.072      5.078     -0.006    (screen said -0.0063)
```

The sample reproduces the screen delta, so it is a fair window. **In the games where Libation was
actually cast, the two lists are dead level.** The entire advantage sits in games where it never
appeared — it is not Libation doing anything.

Against Impolite Entrance, Libation was level at Oracle 3 (−0.5050 vs −0.5050) and 0.0014 *behind*
at Oracle 4. Grid-wide ordering: **Anger > Entrance ≥ Libation.**

---

## Other pre-registered questions

* **Gold Rush — vindicated.** Every cut loses; `gr2_an2` was the worst of sixteen arms. It had been
  an untested premise of fifteen prior screens and is now measured.
* **The 4th Oracle pays** at every Anger count (−0.0175 / −0.0109 / −0.0176, se ≈ 0.0024),
  consistent with its +1 life rider enabling Fortifying Draught at Draught 4. At the Entrance-0
  corner it becomes interchangeable with the 4th Anger (t = −0.16 / −0.20).
* **Frontline Heroism is 89% of the total gain** (+0.4523 of +0.5093). Everything since is ~0.06.

---

## How to read any number in this file

**These are SCREEN deltas.** Every arm shares one apparatus, so a delta is a **ranking**, not the
deck's measured strength. The absolute win-turn of the adopted list is only known after its own
mulligan profile and value leaf are generated.

Pairing is tight but not perfect: an unbiased 500-pair replay measured **95.8%** of opening-hand
cards matching by position between two arms (88.3% on the divergent subset). Pairing quality
affects **power, not validity** — `se` is computed from the observed paired differences, so every
`t` above already prices in whatever decorrelation exists.

---

## Provenance — which engine these numbers were measured on

Every block in this file was run on engine commit **`0f8771cb`** (recorded in each run's
`*.results.json` as `engine_commit`), with a single build verified byte-identical by the smoke
gate. At the time of writing, `origin` is **61 commits ahead** of that point, touching 22 files
under `src/` (+1707 lines) -- including mulligan-generation, Minotaur cost reduction, and GT
rebaselines. None of that upstream work is in these measurements.

That does not invalidate anything here: every arm was measured against every other arm on the SAME
engine, so the rankings are internally consistent and that is what a screen is for. But the
**absolute** deltas are properties of `0f8771cb`, and the adopted list's real strength must come
from its own mulligan profile and value leaf generated on whatever commit is frozen for adoption.
If a rebase lands engine changes before adoption, re-run the smoke byte-identity check before
trusting any comparison against these figures.

## Status

* Staged list verified at 60 cards; scenario gate **35/35** after the swap.
* Engine smoke **42/42, 0 configs changed, byte-identical**.
* Four new scenario fixtures cover Frontline Heroism and the Oracle→Draught enabler, each verified
  *tight* (opponent_life + 1 makes all four fail).

**Not yet done:** primary `.cod` swap and archiving the current list as `v2-instigator-libation`;
ground-truth rebaseline; mulligan profile and value leaf for the new list. Two fixtures need
repointing at the archived v2 on swap (`draught_magnet_escalation`, `libation_x_lands_not_dorks`).

---

# WHERE EACH CARD EARNS ITS SLOT — from replayed game logs

Deltas say *that* a list is better. These come from replaying individual games with full logs under
both decklists and slicing the result. Every pair below was verified against its recorded win turn
and dropped on mismatch (**500/500** and **300/300** faithful); the samples are `--sample random`,
because a divergence-selected pool is balanced across both tails by construction and its
conditional means are selected rather than estimated.

## The unusual thing: Luxurious Libation is a TRAP before turn 4

Splitting the 110 games in which Libation was actually cast by the turn it was first cast:

```
first cast T2-T3      n=34    A-B = +0.382   se 0.125   t = +3.06     <- casting it LOSES
first cast T4+        n=76    A-B = -0.171   se 0.096   t = -1.79     <- casting it wins
difference                            +0.553   se 0.157   t = +3.52
```

**Casting Libation on turn 2 or 3 costs roughly a third of a turn.** The mechanism is plain from
the card: X is paid from whatever mana is left, so a turn-2 Libation buys +1/+1 and a 1/1 Citizen
for the entire turn — a rate the all-Anger list beats trivially by casting a 1-mana cantrip trick
and continuing to develop. Held to turn 4 or later, the same card is mildly positive.

This is consistent across the neighbouring cards, and the differences track the mechanism:

```
Ancestral Anger   first cast T2 +0.000   T3 +0.091   T4 -0.136     flat +1/+1 cantrip: early is cheap
Impolite Entrance first cast T2 +0.200 (n=5)                       haste is dead with no board
Luxurious Libation first cast T2/T3 +0.333/+0.462                  X-scaling: early is near-worthless
```

Only the X-spell shows a large early penalty, which is what the mechanism predicts.

**Caveat, stated plainly: the T4 boundary was chosen after seeing the per-turn buckets.** The
mechanism motivates it independently (X is tiny before turn 4), but a post-hoc split on a
ten-bucket table is not the same as a pre-registered test. Treat `t = +3.52` as a strong hypothesis
that deserves a confirmation run, not a settled finding.

It does not change the recommendation — Libation is cut regardless. What it *may* indicate is a
**play-heuristic issue rather than a card-quality one**: the search appears willing to spend an
early turn on an X spell whose X is not yet worth paying. If that generalises to other X spells in
other decks it is worth a look on its own terms.

## Impolite Entrance is not bad — it is ABSENT

`or4_ie0_an3` (Anger 3 / Entrance 0) vs `or4_ie1_an2` (Anger 2 / Entrance 1), 300 unbiased pairs:

```
ALL                            300 pairs   -0.003   t = -0.3      (the established tie)
  fast games (<=T4)            123 pairs   -0.033   t = -2.0      Anger-only is better
  T6                            40 pairs   +0.100   t = +1.7      the Entrance list is better
  T7+                           38 pairs    0.000                  identical

cast Impolite Entrance          54 pairs   -0.074   t = -1.4      when cast, it HELPS
never cast it                  246 pairs   +0.012   t = +1.1
```

Two things fall out. First, **Entrance appears in only 18% of games** (54/300) while Anger appears
in 45% (136/300) — a single copy simply does not show up. Second, in the games where it *is* cast
it is mildly positive. The card is not being measured as weak; it is being measured as **rare**,
and the aggregate tie is dominated by the 82% of games where it never appears and the two lists are
near-identical.

There is also a crossover worth noting rather than over-reading: the Anger-heavy list wins the fast
games (t = −2.0) and the Entrance list wins the T6 bucket (t = +1.7). Neither survives a
multiple-comparison correction across five buckets on its own, but the direction is consistent with
Anger being the cheaper, more frequently-castable card and Entrance mattering only once there is a
board worth hasting.

**This is the strongest support yet for the deck owner's instinct.** The engine cannot see
Entrance's trample, and the log evidence shows the card is positive when it actually gets cast. A
measured aggregate cost of 0.005–0.009 turns is what you get when a mildly-positive card is drawn
too rarely to matter — not what you get from a bad card.
