# Fungus candidate-b screen, 2026-10-13: results and list recommendation

**Status: rounds 1a-4 COMPLETE. Round 5 (composite verification) IN FLIGHT.** No list has been
adopted and no new apparatus has been generated.

USER framing: *"we will be testing everything out of candidate B and only switch to mono green if
black seems not that helpful."* And on scope: *"We should not be generating any new tables during
this process"*, *"I don't want to do another mulligan table. That is too expensive for this set of
tests. It should wait until we have a list recommendation."*

## Headline

1. **Keep black.** Mono-green is a NULL in 2HG and only a small gain in std; cutting Peat Bog
   outright is WORSE in both formats. The colour axis is worth 2-8x less than the five-drop axis.
2. **The real win is `Beastmaster Ascension 2 -> 4`, paid for out of the five-drops.** It is the one
   finding present in every well-measuring arm of every round.
3. **Three different lists tie.** a2 / b2 / z1 are a statistical plateau, so the choice among them is
   not a measurement question.

## Method, and the caveats that matter when comparing rounds

* `scripts/deck_compare.py`, d5 / 20 ms, 4,000 paired games per cell, two formats (std
  `life 20 heads 1`, 2hg `life 30 heads 2`), every arm against **its own** base in ONE pooled batch.
* **Apparatus:** candidate-b's shipped keep table (K=17, R=30), `"pool_table": false`, worst-arm
  composition fall-through 0.388-0.390% against a 1.00% limit. **Nothing was generated.**
* **Round 1b is the exception and is NOT on the other rounds' scale.** Two of its arms introduce
  Thallid / Sporesower Thallid, which the shipped table does not bucket, so the driver dropped the
  table from EVERY arm -- symmetric and internally valid, but table-free AND bottoming-free, i.e.
  not shipped play. (It also cost ~7x the compute per game, which is the documented price of having
  no keep table: every mulligan plays each candidate bottom out as a full game.) The profile-level
  `mulligan.keep_table_alias` does NOT prevent this -- `deck_compare` tests coverage against the
  TABLE's own bucket names and cannot see a profile alias.
* **Engines differ across rounds** and absolute deltas must not be pooled across them: rounds 1a/1b
  on `src 0b33d259`, round 2+3 on `e1180df7`, rounds 4/5 on `f816bc46`. Origin's
  `MTG_ESC_SKIP_SINGLE` adoption landed mid-screen but is **structurally inert for Fungus**:
  its gate is `eff_single_deck = !s_esc_single_env && escalation_cap > 0` and Fungus sets no
  `escalation_cap` in either its profile or its value sidecar, so the lever cannot fire here.
* **Seeds are disjoint per round** (941000 / 943000 / 960000 / 970000 / 980000), so every replicated
  arm doubles as a held-out confirmation of a selection-biased winner.
* `d*` is the cell-value scatter that would be NEEDED to fake an effect, benchmarked against a
  measured 0.054t. Bigger = harder to fake. **Misfit is one-way** -- it can only make an edit look
  SLOWER -- so negative deltas are trustworthy in sign and positive ones may be artifacts.

## The five-drop axis (the actual win)

Round 2+3, shipped table, seed 960000. Negative = faster.

| arm | std | 2hg | d* (std) |
|---|---|---|---|
| `z1` Mycoloth 4->0, Beast->4, Vitaspore->4 | **-0.0922** | **-0.1875** | 0.190t |
| `b2` Doubling Season 4->2, Beast->4 | -0.0858 | -0.1827 | 0.178t |
| `a2` Mycoloth 4->2, Beast->4 | -0.0785 | -0.1735 | 0.171t |
| `bal9` all three ->3, Beast->4, Vita->3 | -0.0532 | -0.1330 | 0.142t |
| `bal10` Season->3, Burst->3, Beast->4 | -0.0517 | -0.1375 | 0.140t |
| `z6` Beast->4 ONLY (paid outside the 5-drops) | -0.0135 (t=-1.7, null) | -0.0983 | 0.067t |
| `s2` Saproling Burst 4->2, Beast->4 | -0.0123 (t=-1.7, null) | -0.0747 | 0.065t |
| `z5` Mycoloth 4->0, Beast left at 2 | **+0.0305** | **+0.0460** | 0.098t |

**`best two: z1 vs b2 = -0.0065 +-0.0101 (t = -0.64)`** -- a dead null, and `a2` is within ~0.014 of
both in both formats. A PLATEAU, not a peak.

Four conclusions:

* **Concentrated trims beat spread trims.** Taking 2 copies off ONE five-drop (a2/b2, ~-0.08/-0.18)
  beats spreading the same total across three (bal10/bal9, ~-0.05/-0.13).
* **WHICH five-drop barely matters, except that it must not be Saproling Burst.** a2 ~ b2 ~ z1;
  but `s2` (trimming Burst) is a null in std, and round 1b independently ranked Burst the worst of
  the three to trim despite its being the most-cast (0.46/game).
* **The 2x2 deconfounds Beastmaster from the trim, and they INTERACT.** Beastmaster->4 alone (`z6`)
  is a null in std and -0.0983 in 2hg; Mycoloth->0 alone (`z5`) is *worse* in both; together (`z1`)
  is the best arm. So the freed five-drop slots are not general-purpose -- at least two must go to
  Beastmaster. (`z5`'s badness is softer than its t=+4.25 suggests, since positive deltas can be
  apparatus misfit. It is certainly not better. It is also 2x the cost -- 1853 vs 938 ms/game in
  2hg -- from Psychotrope Thallid's library-reading branching.)
* **Everything good is worth ~2x more in 2HG** (2hg-std of -0.085 to -0.097, t = -12 to -16).

## The black axis (the question that was asked)

| arm | std | 2hg |
|---|---|---|
| `r2a` Peat Bog 4->**0** | +0.0158 (t=+1.6) | **+0.0333 (t=+3.0) -- worse** |
| `r2b` Peat Bog 4->**2** | -0.0195 (t=-2.6) | -0.0165 (t=-2.0) |
| `r2c` Secluded Courtyard 4->0 | -0.0257 (t=-4.3) | -0.0323 (t=-4.3) |
| `r2e` BOTH restricted lands out | +0.0118 | +0.0245 -- worse |
| `r2d` no black cards (confounded) | -0.0352 | -0.0155 |
| `r2f` full mono-green | -0.0418 (t=-3.2) | **-0.0227 (t=-1.5) -- NULL** |

* **Mono-green does not win.** Null in 2HG, small in std, and it has the worst `se` and lowest ident
  (55.6% in 2hg) of any arm because it changes the most.
* **Peat Bog has an interior optimum near 2.** 4 is slightly too many; **0 is worse than either**.
* **Secluded Courtyard 4->0 is the only land tweak supported in both formats**, and it has the best
  ident/se of the black arms. Mechanically consistent: `colored_creature_only` means its coloured
  mana cannot pay the `{G}` pip of 13 noncreature cards.
* **The two land cuts are strongly NON-ADDITIVE** -- either alone is fine-to-good, both together is
  worse than either. The deck needs the ramp; 8 nonbasics -> Forests is not free.
* **Black is worth MORE in 2HG.** Every black-cut arm shifts positive in 2hg (`r2d` +0.0198
  t=+3.30, `r2f` +0.0190, `r2a` +0.0175) while every good arm shifts negative. The mechanism is in
  the card data, not inferred: Slimefoot's `dies_trigger_damage_each_opponent` *"multiplies the face
  damage by OpponentHeads()"*, so each Saproling death is **2 damage at `heads 2` vs 1 at `heads 1`**.

### The two black cards, isolated (round 4, seed 970000, no aliases, 0.000% fall-through)

| arm | std | 2hg | ident |
|---|---|---|---|
| `k1` Slimefoot 1->**0** | +0.0095 (t=+2.8) | **+0.0270 (t=+6.0)** | 95.7% |
| `k2` Deathspore 2->**1** | -0.0003 (t=-0.2) | -0.0008 (t=-0.5) | **99.0%** |
| `k3` both | +0.0103 | +0.0293 | 94.8% |

* **Slimefoot earns its slot**, and ~3x more in 2HG -- the format shift (+0.0175, t=+4.96) matches
  the `OpponentHeads()` multiplier exactly. USER's prediction, confirmed by measurement.
* **Deathspore 2 vs 1 is a perfect null** (ident 99%). Measurement cannot choose inside the USER's
  stated 1-2 range, and there is no measured reason to trim to 1.
* `k3 ~ k1`: the SECOND Deathspore adds nothing to the pair. The combo value rides Slimefoot plus
  the FIRST Deathspore.
* **`r2d` (Deathspore 0) measured better, and that is the modelling gap, not a finding.** Deathspore's
  goldfish half IS modelled (`sac_outlet_minus_power/-tough`, and its own note warns *"THE -1/-1
  OUTLET IS NOT INERT, AND READING IT AS INERT IS THE TRAP"* -- pointed at our OWN Saproling it is a
  second free death, i.e. a second Slimefoot drain). What is NOT modelled is -1/-1 as real
  INTERACTION against an opposing board, which has no referent against a passive goldfish. So the
  engine under-rates the card and every arm cutting it is flattered. USER ruling, which stands on
  grounds the engine cannot represent: *"Deathspore Thallid has upside that is not effectively
  represented by the engine. If we play black it will be a 1-2 of for sure."*

## RECOMMENDED LIST (pending round 5's additivity check)

Changes to candidate-b, 4 cards:

```
  Beastmaster Ascension   2 -> 4      (+2)   the robust win; must accompany any 5-drop trim
  Mycoloth                4 -> 2      (-2)   pays for it; also V1's own count
  Secluded Courtyard      4 -> 0      (-4)   the one supported land tweak
  Forest                  4 -> 8      (+4)
```

Held deliberately: **Saproling Burst 4** (trimming it is a null), **Doubling Season 4** (no reason to
cut the higher-ceiling card instead), **Slimefoot 1** (cutting it is worse), **Deathspore 2** (USER
ruling; 1-vs-2 unmeasurable), **Peat Bog 4** (a cut pairs badly with the Courtyard cut -- the
non-additive case), **20 lands**.

**Why a2 and not z1, given they tie:** nothing in the data separates them, so the tiebreak is
non-measurement. Mycoloth 4->2 is the smaller structural change, is replicated across two rounds and
two engines (1a -0.0838, 2+3 -0.0785/-0.1735), and matches V1's count on an axis where V1's counts
keep being vindicated. Mycoloth->0 buys nothing measurable over it while removing the deck's devour
line entirely. **This is a judgement call on a plateau and should be overridden freely.**

**Round 5 (seed 980000, IN FLIGHT)** measures the composite as a composite, because additivity is
untested and `r2e` proved stacking is not safe to assume: `c_a2_only` (additivity baseline),
`c_a2_court0` (the recommendation), `c_a2_bog2` (the alternative land pairing), `c_z1_court0` (the
plateau's other end through the same land change).

## Out of scope, and why -- not nulls

* **A fair mono-green head-to-head.** V1 (`decks/Fungus/Fungus.cod`) is ALREADY mono-green (zero
  `{B}` pips, 19 Forest + 3 Simic Growth Chamber) and has its own fitted artifacts, so mono-green
  does not need synthesizing -- but a list that is mono-green AND carries candidate-b's improvements
  needs an apparatus fitted to it. Deferred by USER directive until there is a list recommendation.
  Note `r2f` is a weak proxy for a reason the coverage gate CANNOT see: the conversion empties
  buckets `[11] Peat Bog, Hickory Woodlot` (8 cards -> 4) and `[13] Forest, Blooming Marsh`
  (8 -> 16), so the table's land advice is applied to a base it was never fitted to -- yet
  `fallback_rate` reports 0.388%, because Forest's bucket already holds >= 7 copies so the
  `min(count, 7)` cap saturates. Coverage is not calibration.
* **`--floor`** (the apparatus-bias bracket) cannot run at all this phase: it generates each deck's
  OWN table at `floor_R` (default 10). So the table-FIT bias is unmeasured; only the coverage channel
  is bounded. The best effects (`d*` 0.17-0.19t vs a 0.054t benchmark, ~3x) are the ones least
  likely to be apparatus; the land tweaks (`d*` 0.08-0.09t, ~1.6x) are the ones most exposed.
* **Thallid.** Round 1b's best arm added Thallid, but Thallid is unbucketed by candidate-b's table
  (V1's table buckets it natively -- the two tables are nearly complementary). `z1` showed the
  structural win is reachable WITHOUT Thallid, so no bucket ruling is needed to proceed. Whether
  Thallid beats Vitaspore as the fill is untested upside, not a blocker.
* **The Mycotyrant** -- not in `cards.json`; scoped in `docs/design/mycotyrant-scope.md`, where the
  black argument is narrowed (it argues for a black SOURCE, not the black PACKAGE).
* **Sporesower Thallid** -- round 1b's `c3` was a 2HG null (t=-1.69) and by far the most expensive
  arm (slow games at 1.1-7.5M units, one at 143 s). The one V1 count the screen does NOT vindicate.
