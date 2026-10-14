# Fungus candidate-b screen, 2026-10-13: results and list recommendation

**Status: rounds 1a-6 COMPLETE; round 7 (Slimefoot x Mycoloth + the pin's price) specified.** No list
has been adopted and no new apparatus has been generated. One ENGINE change has landed since round 6:
the Mycoloth Main-2 pin fix (`MTG_FUNGUS_M2_HASTE`), so round 7 is NOT on rounds 1a-6's engine.

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
  on `src 0b33d259`, round 2+3 on `e1180df7`, rounds 4/5 on `f816bc46`. Three escalation changes
  landed from origin mid-screen, and they are NOT all inert -- the distinction matters:
  - `MTG_ESC_SKIP_SINGLE` (`4456f12e`) **is structurally inert for Fungus**: its gate is
    `eff_single_deck = !s_esc_single_env && escalation_cap > 0`, and Fungus sets no
    `escalation_cap` in either its profile or its value sidecar, so the lever cannot fire.
  - `escalation_skip_r` (`6096acda`) is a per-deck sidecar **OPT-IN** that Fungus has not taken.
  - **`MTG_ESC_SKIP_PREDICT` (`15ffae16`, default ON) CAN fire for Fungus.** `esk_on` reads the
    global flag (or a per-job arm), with no `escalation_cap` gate. It was adopted at 01:45, so
    **rounds 1a/1b predate it and rounds 2+3/4/5 include it.** Each round is still internally valid
    (base and arms share one engine inside one batch), but this is a second reason -- on top of the
    table-free apparatus -- why round 1b's absolute deltas sit on a different scale from round 2+3's.
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

## Round 5: the composite verifies, and it breaks the plateau

Seed 980000, shipped table, engine `f816bc46`. The composite had to be measured as a composite
because `r2e` proved stacking is not safe to assume.

| arm | std | 2hg | d* (std) | ident (std) |
|---|---|---|---|---|
| `c_z1_court0` Myco->0, Beast->4, Vita->4, Court->0 | **-0.1343** | **-0.2387** | 0.240t | 71.4% |
| `c_a2_court0` Myco->2, Beast->4, Court->0 | -0.1045 | -0.2105 | 0.204t | 82.2% |
| `c_a2_bog2` Myco->2, Beast->4, Peat Bog->2 | -0.0948 | -0.1830 | 0.195t | 80.3% |
| `c_a2_only` Myco->2, Beast->4 (additivity baseline) | -0.0730 | -0.1730 | 0.165t | 89.2% |

**1. ADDITIVITY HOLDS, slightly super-additive.** Predicted `c_a2_only + r2c` = -0.0730 + -0.0257 =
-0.0987 std; **observed -0.1045**. In 2hg: -0.1730 + -0.0323 = -0.2053; **observed -0.2105**. The
spell edit and the Courtyard cut stack cleanly -- unlike the two *land* cuts, which did not.

**2. The core edit replicates across three seed blocks AND three engines.** `Mycoloth 4->2 +
Beastmaster 2->4` measured **-0.0838** (1a, seed 941000, `0b33d259`), **-0.0785** (2+3, seed 960000,
`e1180df7`), **-0.0730** (5, seed 980000, `f816bc46`). That is the most robust number in the screen.

**3. Courtyard beats Peat Bog as the land tweak**, confirmed inside one batch:
`c_a2_court0` vs `c_a2_bog2` = **-0.0097 std / -0.0275 2hg**, both favouring Courtyard. The Bog cut
also stacks *sub*-additively in 2hg (predicted -0.1895, observed -0.1830) while Courtyard is
super-additive in both.

**4. THE PLATEAU IS BROKEN, weakly but consistently.**
`c_z1_court0` vs `c_a2_court0` = **-0.0297 +-0.0113 (t = -2.62)** in std and
**-0.0283 +-0.0145 (t = -1.95)** in 2hg. Neither is decisive alone, but the direction replicates:
the same comparison in round 2+3 was -0.0137, on a different seed block and a different engine. So
the Mycoloth->0 core is **probably** better by ~0.02-0.03, not provably. `c_z1_court0` also has the
highest `d*` of any arm measured (0.240t std / 0.360t 2hg -- 4.4x and 6.7x the 0.054t benchmark) and
is the cheapest arm per game (366 vs base 385 std; 691 vs 836 2hg), since cutting Mycoloth removes
the devour `k` axis.

## RECOMMENDED LIST

**Primary -- 5 changes to candidate-b** (`c_z1_court0`, measured **-0.1343 std / -0.2387 2hg**):

```
  Beastmaster Ascension   2 -> 4      (+2)   the robust win; must accompany any 5-drop trim
  Mycoloth                4 -> 0      (-4)   the 2x2 says its slots are what pay for Beastmaster
  Vitaspore Thallid       2 -> 4      (+2)   the fill; already bucketed, a spore outlet
  Secluded Courtyard      4 -> 0      (-4)   the only land tweak supported in both formats
  Forest                  4 -> 8      (+4)
```

Resulting list (60 cards, 20 lands, 8 five-drops):

```
  8 Forest              4 Beastmaster Ascension   2 Deathspore Thallid    1 Brightcap Badger
  4 Blooming Marsh      4 Doubling Season         2 Tukatongue Thallid    1 Concordant Crossroads
  4 Hickory Woodlot     4 Saproling Burst         2 Wild Growth           1 Psychotrope Thallid
  4 Peat Bog            4 Sporecrown Thallid                             1 Shroofus Sproutsire
                        4 Undercellar Myconid                            1 Slimefoot, the Stowaway
                        4 Utopia Mycon                                   1 Sol Ring
                        4 Vitaspore Thallid
```

**Conservative alternative** (`c_a2_court0`, **-0.1045 / -0.2105**): identical except `Mycoloth 2`,
`Vitaspore 2`. Choose this if the ~0.03 edge at t=2.0-2.6 is not worth cutting a marquee card, or if
Mycoloth's devour line has value the goldfish cannot express -- the Deathspore situation exactly.
**That is a USER call; the measurement prefers the primary but not decisively.**

Held deliberately in both: **Saproling Burst 4** (trimming it is a null, and round 1b ranked it
worst to cut), **Doubling Season 4** (b2 ties a2, so no reason to cut the higher-ceiling card
instead), **Slimefoot 1** (cutting it is worse, and worse still in 2HG), **Deathspore 2** (USER
ruling; 1-vs-2 is a perfect null), **Peat Bog 4** (its cut is dominated by the Courtyard cut and
pairs badly with it), **20 lands**.

**Untested upside, not a blocker:** round 1b's best arm used **Thallid** rather than Vitaspore as the
fill. Thallid is unbucketed by candidate-b's table, so testing it needs either a bucket ruling or
V1's table. `c_z1_court0` shows the win does not depend on it.

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

---

## Round 6 (Slimefoot fills) and the CONFOUND the USER found in it

USER design for round 6: *"Just fill in other spots with Slimefoot."* / *"Don't try to move anything
else around."* So every arm changed only the card being cut and Slimefoot.

Seed 990000, 4000 games/cell, `max_fallback` 0.07 declared (Slimefoot is a 1-of in its own bucket, so
raising it to 3 costs ~3.4% fall-through -- the documented count-coverage cliff). Negative = faster.

| arm | std | 2hg | ms/game (2hg) |
|---|---|---|---|
| `y4` Myco 0 + Beast 4 + **Vitaspore** 4 (fill CONTROL) | **-0.0800** | -0.1747 | **1635** |
| `y2` Myco 0 + Beast 4 + **Slimefoot** 3 | -0.0685 | **-0.1822** | **837** |
| `y3` Myco 0 + Slime 4 + Beast **3** | -0.0238 | -0.1062 | 1026 |
| `y1` Myco 2 + Slime 3 (no Beastmaster change) | +0.0075 (t=+1.4, NULL) | -0.0192 | 1051 |

* Vitaspore-vs-Slimefoot as the fill is a **format split and essentially a tie**: `y4` wins std by
  -0.0115 +-0.0048 (t=-2.40), `y2` wins 2hg by -0.0075 +-0.0060 (t=-1.25, null). Consistent with
  `dies_trigger_damage_each_opponent` multiplying Slimefoot's drain by `OpponentHeads()`.
* `y2` is **half the cost** of `y4` (837 vs 1635 ms/game in 2hg) for the same result. Relevant to the
  3x suite-cost rule if this list is ever adopted.
* **Beastmaster -> 4 outranks a 4th Slimefoot** (`y3` drops Beastmaster to 3 and is much worse).

### ...but the Slimefoot reading is confounded, and the USER named why

USER 2026-10-13: *"Slimefoot is probably also pretty dependent on cards like Mycoloth that produce a
lot of Saprolings."*

**Correct, and the engine models the dependency more strongly than the phrasing suggests.** Three
channels, all verified in the card data and `SpellEffects.h`:

1. **Devour sacrifices ARE deaths.** `ApplyDevourAsEnters` removes the victims and DEFERS their death
   triggers; `FireDeferredDevourDeaths` routes each through `OnCreatureDies`, which is exactly where
   Slimefoot's `dies_trigger_damage` fires. Eating k Saprolings with a Slimefoot out is k face damage
   and k life on the spot -- **2k in 2HG**.
2. **The upkeep flood is the fuel.** `upkeep_tokens_per_plus_one_counter` reads Mycoloth's LIVE
   counters, so devour 3 -> 6 counters -> 6 Saprolings every upkeep (12 counters / 24 tokens under
   one Doubling Season, and the deck runs four).
3. **The deck cashes them for free.** Vitaspore, Deathspore, Utopia Mycon and Psychotrope all cost
   exactly "Sacrifice a Saproling" -- no mana, no `{T}` -- so with Slimefoot out the token pile IS a
   clock that ignores summoning sickness and ignores blockers.

**And every arm that added Slimefoot also cut Mycoloth.** Base is Mycoloth 4 / Slimefoot 1; `y1` is
Mycoloth 2, and `y2`/`y3`/`y4` are Mycoloth 0. There is no arm in rounds 1a-6 that adds Slimefoot
while keeping Mycoloth at 4 -- so the headline `y2`-vs-`y4` comparison was made in the one
configuration where Slimefoot's best enabler is absent. The only clean reading in the other
direction is round 4's `k1` (Slimefoot 1 -> 0 against the Mycoloth-4 base: **+0.0095 / +0.0270**,
Slimefoot earns its slot), which is the sign the hypothesis predicts.

**There is no non-interacting donor in this list, and that is the substantive finding.** Every slot
is a Saproling-engine slot: Sporecrown pumps the tokens, Wild Growth ramps to the five-drop,
Utopia/Psychotrope/Deathspore are the outlets that cash Slimefoot in, Tukatongue has a measured
devour anti-synergy (`c5`), Doubling Season doubles both halves of Mycoloth. So an "add Slimefoot"
arm **always** pays with a Slimefoot enabler -- which is itself part of why `y2`-vs-`y4` read as a
tie, and why a plain difference-in-differences with a held-fixed donor cannot be built here.

### Round 7's design: hold the CONTRAST fixed instead of the donor

`logs/fungus_screen/build_spec_r7.py`, seed 1000000 (held out from 941000-990000). Within each
Mycoloth bracket the two arms differ by **exactly** `{Slimefoot 3, Vitaspore 2}` vs
`{Slimefoot 1, Vitaspore 4}`, and Beastmaster is constant within each bracket (2 at Mycoloth 2,
where only two slots are freed; 4 at Mycoloth 0, where four are). The builder ASSERTS that contrast,
so a later edit cannot silently change the question.

| arm | Myco | Slime | Vita | Beast | role |
|---|---|---|---|---|---|
| `w1_m2_slime3` | 2 | **3** | 2 | 2 | Slimefoot fill at Mycoloth 2 |
| `w2_m2_vita4` | 2 | 1 | **4** | 2 | Vitaspore fill at Mycoloth 2 |
| `w3_m0_slime3_beast4` | 0 | **3** | 2 | 4 | Slimefoot fill at Mycoloth 0 (= `y2`) |
| `w4_m0_vita4_beast4` | 0 | 1 | **4** | 4 | Vitaspore fill at Mycoloth 0 (= `y4`/`z1`) |

Interaction = `(w1 - w2) - (w3 - w4)`. Under the hypothesis the Slimefoot fill does *relatively*
better at Mycoloth 2. Round 6 already measured the Mycoloth-0 half (+0.0115 std / -0.0075 2hg), so
the Mycoloth-2 half is the new information.

**The apparatus bias cancels in that contrast**, which is what makes the design worth its arms: the
~3.4% Slimefoot fall-through penalty is one-way and falls on the arm playing the extra copies, and
BOTH Slimefoot-3 arms carry it, so it subtracts out of the difference-of-differences.

## The Mycoloth Main-2 pin was a LOSSY GATE (USER-found), and it is now FIXED

USER 2026-10-13: *"For Mycoloth I have some concern that we might actually be understating it a
bit. One reason is that it should be playable before combat iff we have a haste provider."* ... *"In
cases where this can give more damage + lethal this would be incorrect."* ... *"The problem is that
you would want to try it in both spots when haste is available."* ... *"(and leave up enough to give
it haste)"*

`FungusProvider::MainPhaseOverride` took a `const GameState&` whose parameter was **unnamed** and
returned `MainPhase::Main2` for any `devour > 0` card unconditionally. That is a predicate in the
GATE, consulted at every budget including unlimited, so the pre-combat devour-and-attack line was
unfindable at **any** budget -- the shape ruled out for `MTG_EQUIP_ROUTE_TOPMAIN` (see
`budget-may-reprice-not-remove`). Devour 2 eating three Saprolings is a 16/16 (32/32 under one
Doubling Season), so with haste this is a lethal-turn loss, not a rounding one.

**Fix (`MTG_FUNGUS_M2_HASTE`, default ON):** answer `MainPhase::Both` when
`FungusHasteRouteForEntrant` finds a battlefield haste route -- `grants_haste` (Concordant
Crossroads' blanket grant) or `sac_outlet_grants_haste` (Vitaspore Thallid) **with legal fodder**.
Four design points worth recording:

* **`Both`, never `Main1`.** Which phase wins is arithmetic -- haste damage now against eating the
  spent attackers later -- and the search sums both sides exactly. A doctrine pin either way would
  be the one-sided assertion the Faeburrow/Mana-Cannons rule already forbids.
* **The USER's "leave enough up to give it haste" needs no code.** Devour `k` is an existing searched
  axis (one cast variant per `k = 0..own`), so with two Saprolings the search already enumerates
  `k=1` (eat one, sacrifice the other for haste) alongside `k=2` (eat both, no haste) and prices
  them. Reserving a body in the predicate would instead DELETE one of those two lines.
* **Battlefield-only is sufficient, not a concession.** The override is consulted per NODE in
  `CollectActions`, so "cast the granter first, then Mycoloth" is covered by the search's own
  sequencing: at the node after Crossroads (`{G}`) or Vitaspore (`{1}{G}`) resolves, the granter IS
  on the battlefield and the override answers `Both` there.
* **`SecondMainUnproductive`'s gate had to learn about `Both` as well.** It tested
  `MainPhaseOverride(...) != Main2 -> continue`, so a `Both` answer would have stopped justifying the
  post-combat phase -- **the fix would silently have deleted the post-combat devour line the pin
  exists for.** Only a `Main1` answer (or none) now disqualifies. `HasteAccessThisTurn` was
  deliberately NOT widened: it misses `sac_outlet_grants_haste` and has two other callers.

**Validation.** 587/587 unit tests, including five new cases in `test/unit/test_fungus_revision.cpp`
(no route; Crossroads; Vitaspore without fodder; Vitaspore with fodder; the opponent's board; and
every non-devour card staying `Main1` while a route is live). Smoke: **2 games faster, 0 slower**,
3 same-score line changes, and every other deck byte-identical. `fungusb_smoke_d3` improved -0.0267t.
The incumbent `fungus` list holds **no haste source at all** (Mycoloth 2, no Crossroads, no
Vitaspore), so the fix is provably inert there -- the containment is by mechanism, not luck.

The real games show the intended line directly. `fungusb_smoke_d3 gi18`, identical kept hand and
draws, Crossroads already down on T3:

```
T4  old: Saproling Burst; ATTACK   [opp 16]
T4  new: Mycoloth; ATTACK          [opp 11]
```

**What this means for the earlier rounds.** The pin penalised every arm holding Mycoloth, so it
biased the five-drop axis in the direction that FLATTERED cutting Mycoloth -- and it biases round 7's
Slimefoot contrast in the direction that would fake the USER's hypothesis being false, which is why
it was fixed before round 7 ran. Round 7's `w5`/`w6` pair prices what is left of it: identical
60-card lists (Mycoloth 4, Vitaspore 4, Doubling Season 2 -- the most Mycoloth and the most haste
routes the list can hold) with the lever flipped via `arm_flags`, so the only difference between the
two arms is the lever and even the Season/Mycoloth interaction is common to both.

---

# Rounds 9-13 (2026-10-14): the USER hands over the list decision, and the screen finds the wrong axis had been under test

**THE GOVERNING CHANGE IS NOT A MEASUREMENT.** USER 2026-10-14: *"From my perspective my part is
just to give you constraints on cards not to remove. The rest is up to you."* Their only reserved
input is now **1 Deathspore Thallid, and only if the list holds black** -- so mono-green is REOPENED,
having been recorded as closed by that floor. Everything else is the agent's to settle on numbers,
and *"Only when your screening is done should we stop."* Two further standing priors from the same
exchange: *"multiples are not super good"* applies across the list, not just to Doubling Season; and
*"some of them may be easier to play, but just not as strong"* -- i.e. the goldfish overprices
castability and prices interaction at zero, and a report must say which way the model errs per card
rather than routing around it.

## Round 9 (seed 1020000): the primary-vs-conservative margin was the devour pin

| arm | std | 2hg | ms/game std |
|---|---|---|---|
| c_m2 Mycoloth 2 / Vitaspore 2 (= round 5's conservative `c_a2_court0`) | **-0.0877** (t=-9.99) | **-0.2112** (t=-19.11) | 339 |
| c_m1 Mycoloth 1 / Vitaspore 3 (never measured before) | -0.0865 | -0.2035 | 380 |
| c_m0 Mycoloth 0 / Vitaspore 4 (= round 5's primary `c_z1_court0`) | -0.0853 | -0.1960 | 407 |
| s_slime2_death1 Slimefoot 1->2 off the spendable Deathspore | +0.0015 (t=+0.37) | -0.0143 (t=-2.98) | 436 |

**c_m2 vs c_m0 = -0.0025 +-0.0087 (t=-0.29) std / -0.0152 +-0.0110 (t=-1.38) 2hg.** Round 5 had c_m0
ahead by -0.0297 +-0.0113 (t=-2.62). The Mycoloth Main2 devour pin was worth the entire margin, and
the correction accrued where round 9 predicted -- wholly to the Mycoloth-holding side -- carrying it
just past parity. The ladder is monotone in both formats and the new middle rung sits between the
ends, so no optimum hides at 1.

**DECIDED: the Mycoloth-2 shape is the working base.** Tie-broken on nominal direction in both
formats plus **17% cheaper per game**, which matters on its own against the 3x suite-cost rule.
To be re-confirmed on held-out seeds against the final composite rather than treated as settled.

Slimefoot 2 is a live candidate, not banked: its `d*` of 0.066t is only 1.2x the 0.054t measured
scatter reference, so the 2hg gain is not clearly above the apparatus floor.

## Round 11 (seed 1040000): THE DECK IS MANA-STARVED, NOT BODY-STARVED

Every up-count arm pays the one Psychotrope, so each reads against the control `u_vita3`
(-0.0152 std / -0.0173 2hg) as `value(this copy) - value(Vitaspore #3)`.

| raise ONE copy | std | NET | 2hg | NET |
|---|---|---|---|---|
| **Sol Ring 1->2** | **-0.0945** (t=-12.9) | **-0.0793** | **-0.1140** | **-0.0967** |
| Beastmaster Ascension 2->3 | -0.0565 | -0.0413 | -0.1090 | -0.0917 |
| Concordant Crossroads 1->2 | -0.0475 | -0.0323 | -0.0527 | -0.0354 |
| Brightcap Badger 1->2 | -0.0372 | -0.0220 | -0.0493 | -0.0320 |
| Shroofus Sproutsire 1->2 | -0.0333 | -0.0181 | -0.0643 | -0.0470 |
| Tukatongue 2->3 | -0.0257 | -0.0105 | -0.0267 | -0.0094 |
| Wild Growth 2->3 | -0.0227 | -0.0075 | -0.0340 | -0.0167 |
| Slimefoot 1->2 | -0.0110 | +0.0042 | -0.0293 | -0.0120 |
| Deathspore 2->3 | -0.0110 | +0.0042 | -0.0147 | +0.0026 |

| cut | std | 2hg | verdict |
|---|---|---|---|
| Doubling Season 4->3 | -0.0032 (t=-1.09) | +0.0065 (t=+1.61) | FREE; replicates round 8's -0.0037/+0.0022 |
| Doubling Season 4->2 | -0.0070 (t=-1.70) | +0.0063 (t=+1.14) | ALSO FREE -- two copies spendable |
| **Utopia Mycon 4->3** | **+0.0005 (t=+0.14)** | **-0.0003 (t=-0.06)** | dead null; the USER asked for this proven |
| Utopia Mycon 4->2 | +0.0083 | +0.0127 | cheap, not free |
| Undercellar Myconid 4->2 | +0.0602 | +0.0710 | expensive; round 8's rung 1 was +0.0265, so additive |
| Saproling Burst 4->2 | +0.0715 | +0.0960 | expensive; rung 1 +0.0305, so additive |
| **21 lands** (Vitaspore 2->1, Forest 4->5) | **-0.0343** | **-0.0403** | more lands is BETTER |
| 19 lands (Peat Bog 4->3, Vitaspore 2->3) | +0.0310 | +0.0460 | fewer is worse |

**The three best edits in the round are all about having mana sooner.** That reframes a screen which
had been a five-drop-trimming exercise, and it is why Cryptolith Rite became the add to watch.
**FIVE free slots exist, not one**: Psychotrope, two Doubling Seasons, one Utopia Mycon, and the
Deathspore the USER's floor permits.

**Two reading errors in round 8 that this round corrected.** (1) "Only Psychotrope is free"
understated the room by two slots. (2) Round 8's four LAND rows paid into Forest, so they rank the
lands against each other and do NOT free a spell slot -- counting Courtyard -0.0095 or Peat Bog
-0.0120 as spendable was a category error, and the land COUNT axis was untested until round 11.

## Round 12 (seed 1050000): the add census, on the SHIPPED table via bucket aliases

Twelve arms, every one `Psychotrope 1->0 + <new card> 0->1`, with `a_vita3` as the control. Four
candidates needed no implementation at all (Sporesower Thallid, Thallid, Thallid Shell-Dweller, The
Mycotyrant) and seven were cards.json-only, so breadth cost card data and nothing else.

**The apparatus is the part worth recording.** USER: *"We should not be generating any new tables
during this process"* and *"You obviously have to alias them."* A `pool_table: true` run would
GENERATE an R=10 table and move the round off the R=60 apparatus rounds 4-11 used, so levels would
stop comparing. Instead `logs/fungus_screen/stage_aliased_table.sh` copies the shipped table and adds
each new card's NAME to the bucket of the card it most resembles -- the mechanism `deck_compare.py`
actually sees, since a profile-level `mulligan.keep_table_alias` is INVISIBLE to its coverage check
(that is what made round 1b run table-free). K stays at 17; an alias never adds a bucket. Result:
`keep table shared, on every arm (all cards covered)`, worst-arm fall-through 1.19%.

The alias assumption is real and bounded: the mulligan decision treats the substitute like its host,
which holds the keep/bottom policy FIXED across arms by construction -- exactly what isolates the
in-play difference -- but it cannot say a new card would want a different keep policy.

## Round 13 (seed 1060000): laddering the mana levers

Sol Ring 2/3/4 and the land count 21/22/23, plus Crossroads 2->3 and Beastmaster 2->4. Each Sol Ring
rung spends one more documented free donor and is paired with its OWN same-donor control, so a rung
reads as `value(Sol Ring #k) - value(the filler it displaced)` instead of being flattered by donors
that gain. Fall-through runs 1.19% / 3.35% / 6.32% as the 1-of climbs (the declared `max_fallback`
0.07 exists for this; the default 0.01 refuses), one-way against the ladder, so every rung is a
LOWER bound -- about 0.004t of understatement at four copies against an effect of 0.079. The land
arms carry none of it: bucket [13] already holds 8 copies and a bucket at >= 7 cannot overflow.

## Still untested after round 13

* **Doubling Season 4->1.** Both 4->3 and 4->2 are free; a third free copy would be a sixth slot.
* **The composite**, which is the only thing that can confirm the greedy pairing of adds against
  donors is additive -- round 5 already caught strong non-additivity between two land cuts.
* **Mono-green post-fix**, reopened by the USER's conditional Deathspore floor.

## The swap arithmetic, and why it is exact rather than a splice

Every round in this screen fills with Vitaspore Thallid, which turns out to make the assembly
algebra cancel cleanly. Writing `v3` for `value(Vitaspore #3)`:

* A round-8 donor arm (cut X by one, add one Vitaspore) measured `d_X = v3 - cost(X)`.
* A round-11/12 up-count or add arm (cut Psychotrope, add A) measured
  `delta_A = value(A) - cost(Psychotrope)`, and the control `u_vita3 = v3 - cost(Psychotrope)`,
  so the NET column is `NET_A = delta_A - u_vita3 = value(A) - v3`.

Therefore an arbitrary swap "cut X, add A" has

        value(swap) = value(A) - cost(X) = NET_A + d_X

-- both terms directly measured, with `v3` cancelling. No absolute card value is ever needed, which
is just as well, since a screen cannot produce one.

**It has a free consistency check, and the check passes.** Predicting the one swap that was measured
directly: `NET(Sol Ring #2) + d_psycho0 = -0.0793 + (-0.0123) = -0.0916`, against the measured
`u_solring2 = -0.0945`. Agreement to 0.003t -- about two standard errors -- so the additive model is
good to roughly that precision at one-slot scale. That is the licence for a GREEDY assembly (adds
sorted by `NET` ascending against donors sorted by `d_X` ascending, taking pairs while the sum is
negative), and the bound on how far to trust it: the composite round must still MEASURE the result,
because round 5 caught strong non-additivity between two land cuts and nothing here rules out the
same between, say, a 21st land and a second Sol Ring -- both of which do the same job.

**Predict the composite before running it.** Writing the prediction down first is what makes
non-additivity visible instead of absorbable: a composite that lands on its predicted sum is
evidence the pairing is sound, and one that misses names the interacting pair.

## CORRECTION: Sol Ring is capped at ONE copy, and that voids the screen's best measured edit

USER 2026-10-14: *"Sol Ring is not allowed due to my 'number of cards must be legal in Modern,
Legacy or Vintage' rule"* / *"So only 1 is allowed, because it is Restricted in Vintage and illegal
everywhere else."* Scryfall confirms exactly that -- `not_legal / banned / restricted` -- and across
this entire pool **Sol Ring is the ONLY card capped below 4**. Every other card the screen has
touched, including all the Modern-illegal old ones (Concordant Crossroads, Saproling Burst, Hickory
Woodlot, Peat Bog, Brightcap Badger, Shroofus Sproutsire, Undercellar Myconid, Wild Growth, Primal
Vigor), is Legacy/Vintage-legal and therefore still a 4-of. "Old card" does not imply "restricted".

**Consequences, stated plainly because this cost a recommendation:**

* Round 11's `u_solring2` (-0.0945/-0.1140) is **VOID AS A LIST EDIT**. The provisional list built on
  it is retracted.
* It is **NOT void as evidence.** It remains the sharpest single result showing this deck's binding
  constraint is MANA rather than bodies, which is what redirected the screen toward the land count,
  Concordant Crossroads and Cryptolith Rite. Keep the diagnosis, drop the edit.
* Round 13's first draft laddered Sol Ring to 3 and 4 copies. It was rewritten before it ran; the
  mana axis is now served by the legal levers -- Crossroads 1->2->3->4, Wild Growth 2->3->4, the land
  count 21/22/23 -- plus Beastmaster 2->4 priced on its own donors and the untested third free
  Doubling Season copy.
* **The cap is now a GATE rather than a memory.** `logs/fungus_screen/fetch_legality.py` POSTs the
  pool to Scryfall's `/cards/collection` and caches `{name: max copies}`; every spec builder asserts
  each arm against it and names the USER rule in the failure message. There is also an explicit
  `"Sol Ring" not in any arm` assertion, because a cap of 1 on a card already at 1 is invisible to a
  count check that only looks at maxima.
* The deck's existing single Sol Ring stays: round 8 priced cutting it as the most expensive single
  cut in the list (+0.0958/+0.1260).

**Re-derived provisional list, legal edits only** (predicted via `NET_A + d_X`, to be MEASURED):

| edit | funded by | predicted std |
|---|---|---|
| Mycoloth 4->2, Beastmaster 2->4, Courtyard 4->0, Forest 4->8 | internal (round 9's c_m2) | -0.0877 |
| Concordant Crossroads 1->2 | Psychotrope 1->0 | -0.0446 |
| Brightcap Badger 1->2 | Doubling Season 4->3 | -0.0252 |
| Shroofus Sproutsire 1->2 | Doubling Season 3->2 | -0.0219 |
| Tukatongue Thallid 2->3 | Utopia Mycon 4->3 | -0.0100 |
| Wild Growth 2->3 | Deathspore 2->1 | -0.0090 |
| 21st land (Vitaspore 2->1, Forest 8->9) | internal | -0.0343 |
| | **predicted total** | **~ -0.233** |

Only ~0.025t worse than the illegal version, because the loss is spread across many small legal
edits rather than concentrated in one banned card. The bottom four rows are thin enough that round
12's adds and round 13's ladders are likely to displace them.

## Round 12 RESULT: The Mycotyrant wins the add census; Cryptolith Rite, my own pick, loses

Control `a_vita3` = -0.0158 std / -0.0145 2hg. NET = `value(card #1) - value(Vitaspore #3)`, directly
comparable with round 11's NET column -- the two rounds ran disjoint seeds on the identical
apparatus and their controls agree to 0.003.

| add, 1 copy, paid by Psychotrope | std | NET | 2hg | NET |
|---|---|---|---|---|
| **The Mycotyrant** | -0.0465 (t=-10.3) | **-0.0307** | -0.0500 (t=-9.9) | **-0.0355** |
| Yavimaya Sapherd | -0.0375 | -0.0217 | -0.0370 | -0.0225 |
| Saproling Migration | -0.0355 | -0.0197 | -0.0362 | -0.0217 |
| Thallid | -0.0318 | -0.0160 | -0.0395 | -0.0250 |
| Spore Swarm | -0.0283 | -0.0125 | -0.0355 | -0.0210 |
| Sporesower Thallid | -0.0205 | -0.0047 | -0.0187 | -0.0042 |
| Deathbloom Thallid | -0.0155 | +0.0003 | -0.0165 | -0.0020 |
| Parallel Lives | -0.0080 | +0.0078 | -0.0063 | +0.0082 |
| Primal Vigor | -0.0053 | +0.0105 | -0.0053 | +0.0092 |
| Cryptolith Rite | -0.0010 | +0.0148 | -0.0047 | +0.0098 |
| Thallid Shell-Dweller | -0.0015 | +0.0143 | +0.0020 | +0.0165 |

Six adds beat a third Vitaspore; five lose to it.

**CRYPTOLITH RITE WAS THE PRE-REGISTERED FAVOURITE AND IT LOST.** Recorded without rescue, per the
standing lesson that a mechanism invented after the numbers explains them. The one caveat that was
declared BEFORE the run stands: that arm reuses Brightcap Badger's grant, so it is scoped to
[Fungus, Saproling] (25 of 26 creature cards) and to {G} rather than any colour, and therefore cannot
pay the list's three black pips. Both omissions remove mana the real card provides, so the faithful
version is better than this number -- but the headroom is one creature card and three pips, which
does not plausibly cover a 0.015t gap. Not worth the MEDIUM engine work (a `LiveManaGrant` union
merge plus an any-colour face, and the agent showed an any-colour face BREAKS the dup-collapse fold
that currently folds a dozen interchangeable Saprolings into one class).

**Two design-pass predictions landed exactly**, which is worth recording because it says the
pre-measurement reasoning was load-bearing rather than decorative:
* Thallid Shell-Dweller is the WORST add. Defender is really enforced, and it forfeits not just
  damage but every Beastmaster Ascension quest counter, since those come one per DECLARED attacker.
* Deathbloom Thallid is a null. Its death trigger is reachable almost only through Mycoloth's
  devour, because all three "Sacrifice a Saproling" outlets gate on the subtype and this is a Fungus.

**Both token doublers lose** -- the same signal as Doubling Season 4->2 measuring free. This deck
does not want more doublers. Note that Primal Vigor's arm is the ONLY one in the census biased FOR
its card (its symmetric drawback is worth zero against a goldfish), and it still lost.

**Saproling Migration and Spore Swarm are LOWER BOUNDS**: no kicker, and `EvalCard` has no
`cast_creates_tokens` branch so both are valued as cantrips in the arm that plays them. Fixing that
is adopt-and-measure (it moves GT for both Fungus configs) and is the thing to do if the composite's
margin turns out thin -- the two cards sit mid-table, so it cannot change the top of the ranking.

Keep-score marginals inverted against play again: Cryptolith Rite -0.2299 and Thallid Shell-Dweller
-0.2846 were near the bottom, while the census WINNER scored 0.1725. The table fits the keep
decision, not the race.

## Round 13 RESULT: Beastmaster does not need the Mycoloth cut, and the land ladder has not turned

Controls by donor COUNT: `r_ctl1` -0.0150/-0.0190, `r_ctl2` -0.0203/-0.0138, `r_ctl3` -0.0372/-0.0215.
Each ladder rung is read against its matched control.

| edit | slots | NET std | NET 2hg |
|---|---|---|---|
| **Beastmaster Ascension 2->4, on FREE donors** | 2 | **-0.0682** | **-0.1740** |
| Concordant Crossroads 1->4 | 3 | -0.0488 | -0.0495 |
| Concordant Crossroads 1->3 | 2 | -0.0472 | -0.0492 |
| Concordant Crossroads 1->2 | 1 | -0.0295 | -0.0325 |
| Wild Growth 2->4 | 2 | -0.0172 | -0.0319 |
| Wild Growth 2->3 | 1 | -0.0130 | -0.0225 |

| vs base directly | std | 2hg |
|---|---|---|
| **23 lands** | **-0.0645** | **-0.0800** |
| 22 lands | -0.0455 | -0.0573 |
| 21 lands | -0.0230 | -0.0310 |
| Doubling Season 4->1 | -0.0248 | -0.0008 |

**THE HEADLINE IS A CONFOUND CAUGHT ONE LEVEL UP.** Round 9's `c_m2` (-0.0877/-0.2112) bundles
Beastmaster 2->4 with Mycoloth 4->2 and Courtyard->Forest, and the screen had been reading that as
evidence for trimming a five-drop. Priced on its own free donors, **Beastmaster 2->4 alone is
-0.0885/-0.1878 vs base** -- so the Mycoloth cut plus the land swap contribute only ~-0.020/-0.037 on
top, and the expensive half of the structural edit is optional. This is exactly the shape round 2+3's
z5/z6 2x2 was built to catch (a fill present in every arm that measures well), re-emerging at the
composite level. It is why round 14's `w_A` is deliberately **Mycoloth-NEUTRAL**.

**Crossroads wants three.** The third copy adds -0.018; the fourth adds -0.002, i.e. nothing.

**The land ladder is still climbing at 23** (~0.022/slot, near-constant across 21/22/23), so the
optimum is not yet sited and an open ladder cannot be assembled from. Round 14's `w_land25` extends
it to 25 as a diagnostic.

**Doubling Season 4->1 is a FORMAT SPLIT, not a free third copy:** -0.0248 std but -0.0008 2hg, with
a +0.0240 (t=+3.94) split. Since the formats are co-equal the assembly cuts Season to 2 and treats
the third as marginal.

**Cross-round replication is good.** Round 11 vs round 13 on disjoint seeds: Crossroads #2 NET
-0.0323/-0.0354 vs -0.0295/-0.0325; Wild Growth #3 -0.0075/-0.0167 vs -0.0130/-0.0225. The control
has now read -0.0152, -0.0158 and -0.0150 in std across three seed blocks, which is what licenses
pooling every NET in the screen.

**Beastmaster's format split is the largest in the screen:** -0.0993 (t=-15.60) more in 2hg. Any
list built for both formats should treat Beastmaster 4 as non-negotiable.

## Round 14: the composite, rebuilt (the first draft was killed 26 seconds in)

The first draft assembled five greedy swaps on top of `c_m2` and was stopped as soon as round 13
landed, because `c_m2`'s Beastmaster fill turned out to be most of its value. The rebuilt round has
eight arms: `w_ctl1` (the control's FOURTH reading), `w_beast4` (held-out replication of the screen's
biggest edit, which now carries the assembly), `w_land25` (extend the open ladder), `w_A` (the
Mycoloth-neutral candidate on free donors only), `w_B` (`w_A` + lands to 23), `w_C` (`w_B` +
Mycoloth 4->2, with the freed copy going to a SPELL so the Mycoloth axis is isolated), `w_D` (the
alternative spend for the Mycotyrant and Crossroads-#3 slots) and `w_mono` (mono-green, reopened).

**Predictions on the record before the run: `w_A` -0.195 std / -0.305 2hg; `w_B` -0.253 / -0.375;
`w_C - w_B` -0.0133 / -0.0530.** Worst-arm fall-through is 4.90%, one-way against the candidate
arms, so the measured gains are LOWER bounds by roughly 0.003t.

## Round 14 RESULT: the composite holds to ~8%, mono-green wins std, and two of my calls were wrong

| arm | std | 2hg | mean |
|---|---|---|---|
| **w_mono** mono-green | **-0.2752** | -0.3595 | -0.3174 |
| **w_D** Crossroads 2 + Shroofus 2 + Badger 2 | -0.2480 | **-0.3972** | **-0.3226** |
| w_C w_B + Mycoloth 4->2 | -0.2507 | -0.3760 | -0.3134 |
| w_B the predicted candidate | -0.2365 | -0.3475 | -0.2920 |
| w_A Mycoloth-neutral, 20 lands | -0.1765 | -0.2790 | -- |
| w_land25 five extra lands only | -0.1042 | -0.1212 | -- |
| w_beast4 Beastmaster 2->4 only | -0.0983 | -0.1953 | -- |

**THE ADDITIVITY ANSWER, which is the point of the round.** Predicted w_A -0.195/-0.305 and w_B
-0.253/-0.375; measured -0.1765/-0.2790 and -0.2365/-0.3475. Short by 6.5-9.5%, in the SAME
direction in both formats and both arms, and ~0.003t of that is the one-way fall-through penalty. So
`value(cut X, add A) = NET_A + d_X` carried an eight-round screen with a small, consistent,
CONSERVATIVE bias. Writing the prediction down first is what made that legible rather than absorbable.

**My Crossroads-3 + Mycotyrant choice was worse than Shroofus-2 + Badger-2:** `w_D - w_B` =
-0.0115 std / -0.0497 2hg in favour of the alternative. I had already decided to cut Crossroads to 2
on the unmodelled-drawback argument below, so the measurement and the judgement agree independently.
Note this is TWO changes at once, so it does NOT establish which half was weak -- round 15's `x_E`
puts The Mycotyrant back uncontested.

**MONO-GREEN WINS std** by -0.0245 +-0.0119 (t=-2.06) and loses 2hg to `w_D` by -0.0377. The colour
question the USER reopened is genuinely live, and the two co-equal formats disagree about it.

**Beastmaster 2->4 replicated held-out** (-0.0983/-0.1953 here vs -0.0885/-0.1878 in round 13),
which matters because it now carries the assembly. Format split again -0.0970 (t=-14.9).

**Lands are real but second-rate per slot.** `w_land25` spends the same five slots as `w_A` for
-0.1042 against -0.1765. The ladder is dead linear at ~0.020/land through 25 and has not turned; a
good spell buys ~0.035/slot. So lands take the slots spells do not want. `w_land25` is also the only
arm whose format split is small (-0.0170 vs ~-0.10 everywhere else): lands help both formats about
equally, while every spell edit is worth roughly twice as much in 2hg.

### The Concordant Crossroads judgement, recorded because it is not a number

Crossroads carries two unmodelled properties and both bite only at TWO OR MORE copies:

1. It is a **World enchantment** (CR 704.5m: all but the most recently played World permanent go to
   their owners' graveyards). cards.json's own note already says the rule is not modelled and that
   the invariant "GOES LIVE AT TWO OR MORE COPIES, where a second would kill the first in paper and
   the engine would keep both".
2. Its haste grant is **symmetric** -- "All creatures have haste", both sides. `HasHasteFromLords` is
   controller-scoped, which is sound here only because the goldfish's bodies never attack.

The world-rule gap turns out to be nearly inert for a specific reason: haste does not stack, so a
second copy on the battlefield adds nothing in either world, and the measured gain from extra copies
is DRAW CONSISTENCY, which is legitimate. (If anything the model slightly UNDER-rates multiples in a
Mycotyrant list, since in paper the replaced copy reaches the graveyard and counts toward descend.)
The symmetric grant is the one that matters in paper, and it is the mirror image of the USER's
warning -- a card that measures well partly because its downside is invisible. Hence the call to run
2 rather than 3, which `w_D` then supported independently; round 15's `x_G` prices going to 1.

---

## Round 15 RESULT (seed 1080000): both winners replicate, and TWO readings turn over

Base 5.1787 std / 5.6065 2hg. Nine cells per format in one pool, 32/32 workers at the heartbeat.

| arm | std | 2hg | mean | what it was for |
|---|---|---|---|---|
| `x_mono` | **−0.2993** | −0.3860 | −0.3427 | CONFIRM round 14's std winner, held-out |
| `x_monoE` | −0.2885 | −0.4108 | **−0.3497** | the mono-green TWIN of `x_E` |
| `x_F` | −0.2755 | **−0.4245** | **−0.3500** | 25 lands |
| `x_E` | −0.2602 | −0.4065 | −0.3334 | `x_D` + The Mycotyrant, uncontested |
| `x_H` | −0.2587 | −0.4015 | −0.3301 | 27 lands |
| `x_G` | −0.2497 | −0.4027 | −0.3262 | Crossroads to 1 |
| `x_D` | −0.2410 | −0.4015 | −0.3213 | CONFIRM round 14's 2hg winner, held-out |
| `x_ctl1` | −0.0168 | −0.0208 | — | the control's SIXTH reading |

### The confirm, which was owed and is not optional

|  | round 14 (seed 1070000) | round 15 (seed 1080000) | drift |
|---|---|---|---|
| `w_D` / `x_D` | −0.2480 / −0.3972 | −0.2410 / −0.4015 | +0.0070 / −0.0043 |
| `w_mono` / `x_mono` | −0.2752 / −0.3595 | −0.2993 / −0.3860 | −0.0241 / −0.0265 |

Neither winner was selection-bias inflation; `w_mono` read **better** held-out by ~0.025. The control
has now been read six times (−0.0152, −0.0158, −0.0150, −0.0190, −0.0168, −0.0208), so the apparatus
floor is a stable ~0.017 and every composite here is 14–25x it.

### 1. The colour "disagreement" was an artifact of comparing unequal lists

Round 14 had mono winning std and `w_D` winning 2hg, from lists assembled differently. The twin pair
-- same spend, same 23 lands, black removed and nothing else changed -- reads:

* std: `x_monoE` over `x_E` by **−0.0283** (t ≈ −2.7)
* 2hg: by **−0.0043** (t ≈ −0.36, a null)

Mono-green wins std and ties 2hg. Nothing disagrees. The earlier split was a confound, not a fact
about the formats, and it is the same shape of error as round 14's `w_D`-vs-`w_B` two-changes-at-once.

**The apparatus bias here runs in TWO directions and the honest statement needs both.** Converting an
arm to mono-green empties bucket `[11] Peat Bog, Hickory Woodlot` from 8 members to 4, so the table's
advice for those compositions is applied to a base it was never fitted to -- a mis-calibration
`fallback_rate` provably cannot see, because bucket `[13] Forest, Blooming Marsh` goes 18→22 and is
already past the `min(count, 7)` cap, so it reports ~0. That channel runs AGAINST mono. But the
fall-through channel runs the other way: at round 16's preflight the black 25-land arm shows **2.76%**
of hands falling through against the mono arms' **1.58%**, and that penalty is one-way against the arm
that has it -- worth roughly 0.003 turns at this scale. So ~10% of the 0.028 std margin is bias
favouring mono, and the unquantified calibration channel pushes back the other way. The effect is an
order of magnitude larger than the part we can price; it is not an artifact of either channel.

### 2. THE LAND LADDER TURNED OVER

Five rounds of 21 −0.0230, 22 −0.0455, 23 −0.0645, 25 −0.1042 looked dead linear at ~0.020/land.
It is not:

* 25 beats 23 by **−0.0152 / −0.0180**
* 27 is **worse than 25** by +0.0168 / +0.0230, and worse than 23 outright

So the optimum is at ~25 and the ladder is done climbing. Caveat on the turn-over point: `x_H` paid
for 27 by cutting the last Tukatongue *and* a Wild Growth, so the 25→27 loss is the land gain net of
those two cuts rather than a pure land price. The direction is unambiguous; the exact peak needed
round 16's one-copy rungs (24, 26), which the 23→25→27 jump had skipped.

### 3. The Mycotyrant belongs once it competes with nothing

`x_E` over `x_D` by −0.0192 std / −0.0050 2hg. So round 14's `w_D`-beats-`w_B` was **not** the
Mycotyrant being the weak half of that pair -- it was the third Crossroads and the missing
Shroofus #2 / Badger #2. Priced on its own donor (Mycoloth 3→2) it pays, mildly.

### 4. The second Crossroads is worth less than the floor

`x_G` (Crossroads to 1) loses by only **+0.0105 / +0.0037** -- *below* the ~0.017 control floor, in
the one place the model is documented to flatter the card (the World rule is unmodelled and goes live
at exactly two copies; the symmetric haste grant is a paper drawback priced at zero). A cost
indistinguishable from zero, in a spot where the engine is known to be generous, is not a reason to
keep the copy. Round 16 re-prices it on the mono list as `z_cross1`.

### What round 15 leaves for round 16

The two top lists finished **0.0003 apart** on the co-equal mean and won on different axes:
`x_F` (black, 25 lands) −0.3500 and `x_monoE` (mono, 23 lands) −0.3497. Their combination --
**mono-green at 25 lands** -- had never been measured. That is `z_m25`, round 16's predicted winner.

---

## Round 16 RESULT (seed 1090000): 24 lands, mono-green wins both formats, and a CORRECTION

Base 5.1805 std / 5.6238 2hg. Ten cells per format in one pool, 32/32 at the heartbeat.

| arm | std | 2hg | mean |
|---|---|---|---|
| `z_m24` mono, 24 lands | **−0.3040** | **−0.4467** | **−0.3754** |
| `z_marsh2` Blooming Marsh 4→2 | −0.2948 | −0.4447 | −0.3698 |
| `z_m25` mono, 25 lands | −0.2920 | −0.4405 | −0.3663 |
| `z_m26` mono, 26 lands | −0.2883 | −0.4425 | −0.3654 |
| `z_mono23` mono, 23 lands | −0.2888 | −0.4258 | −0.3573 |
| `z_b25` black, 25 lands | −0.2652 | −0.4193 | −0.3423 |
| `z_cross1` Crossroads to 1 | −0.2627 | −0.4190 | −0.3409 |
| `z_hick2` Hickory 4→2 | −0.2360 | −0.3695 | −0.3028 |
| `z_ctl1` the control, 6th reading | −0.0147 | −0.0175 | — |

### The prediction, scored

Predicted `z_m25` at −0.3037 / −0.4288, mean **−0.3663**. Measured mean **−0.3663** — exact. But the
format split was mis-apportioned by about 0.012 in each direction (std came in 0.012 worse than
predicted, 2hg 0.012 better). So the swap algebra now has a track record of predicting the *mean*
well while being unreliable about *which format* a gain lands in. Worth knowing, since the ranking
rule weights the two formats equally: a mean prediction is usable, a per-format one is not.

And `z_m24` was not predicted at all — the 24-land rung existed only because of the USER's
"screen in increments of ONE" rule. The 23→25→27 jump had hidden the peak.

### The land count is really a DONOR ranking, not a land ranking

* 23 → 24 (spending **Doubling Season #2**): −0.0152 / −0.0210 — the land wins
* 24 → 25 (spending **Tukatongue #2**): +0.0120 / +0.0063 — the *card* wins
* 25 → 26 (spending the last Tukatongue): +0.0037 / −0.0020 — flat

So "the ladder peaks at 24" is a statement about two particular donors, not about lands. The honest
form is: a marginal Forest is worth more than a second Doubling Season and less than a second
Tukatongue. [[isolate-the-axis-dont-difference-decks]] — which is precisely why round 17 runs the
donor census with a neutral filler instead of extending the ladder again.

Note also that the BLACK list peaked at 25 while the mono list peaks at 24. That is consistent:
dropping Peat Bog removes a land that taps for two but dies, so the mono mana base is smoother and
needs one less.

### Hickory Woodlot is load-bearing, and Blooming Marsh is not

`z_hick2` (Hickory 4→2, land-for-land) is the round's worst arm by a wide margin: **+0.0560 /
+0.0710** against `z_m25`. The two-mana-from-one-land accelerant is doing real work and 4 copies
stay. `z_marsh2` (Marsh 4→2, land-for-land) goes the other way at −0.0027 / −0.0043, which is the
predicted direction: in a mono-green deck Blooming Marsh produces nothing a Forest does not and
enters tapped once three other lands are out, so it is strictly dominated. Round 17 measures 4→0.

### Mono-green wins BOTH formats at equal land count

`z_m25` over `z_b25`, same 25 lands, in the same pool: **−0.0267 std / −0.0213 2hg.** Predicted
−0.028 / −0.004 — std exact, 2hg much stronger than predicted. With round 15's twin at 23 lands
(−0.0283 / −0.0043) that is two independent readings, std replicating closely (−0.0283, −0.0267)
and 2hg moving from a null to a clear win. The colour question is settled: **mono-green**, and the
black list's higher fall-through (2.76% vs 1.58%) accounts for only ~0.003 of the margin.

### CORRECTION: the 2nd Concordant Crossroads DOES earn its slot

Round 15 read `x_G` (Crossroads 2→1) at +0.0105 / +0.0037 and I inferred from that the second copy
was worth less than the apparatus floor, so cutting it was nearly free. **That inference was wrong,
and the error was reading a net as a price.** `x_G` filled the freed slot with Yavimaya Sapherd — a
real card, two bodies for three mana — so +0.0105 was the net of *losing Crossroads and gaining
Sapherd*, not the price of Crossroads. Round 16's `z_cross1` fills with Vitaspore, the screen's
standing near-null, and the same cut now costs **+0.0293 std / +0.0215 2hg** — comfortably above
the ~0.016 floor. [[sink-price-is-not-a-refutation]]: price a cut against the replacement, not
against zero. Differencing the two arms says Sapherd recovers about 0.019 of the 0.029 lost.

So the second Crossroads stays, and the Concordant Crossroads judgement resolves the other way from
round 15's reading: the measured 0.025 mean is an **upper** bound (the unmodelled World rule and the
symmetric haste grant both flatter it), but an upper bound of 0.025 is not zero, and nothing in the
pool replaces it for less. The disclosure stands; the cut does not.

Round 17's census carries one more bias note worth recording, because it is the cleanest the screen
has managed: fall-through is **1.58% on 19 of 22 arms**, so arm-vs-arm differences are nearly
bias-free. The exceptions are `y_cut_concordantcr` (0.40%) and `y_cut_beastmastera` (1.30%), whose
lower rates *flatter cutting those two cards* by roughly 0.003 turns — so a census verdict of "keep"
on either is conservative, and a verdict of "cut" must be discounted.

---

## Round 17 RESULT (seed 1100000): the donor census on `z_m24`, 22 arms, no algebra

This is the round the USER asked for in the handover -- *"step by step remove cards by testing
swaps"*, *"considering almost all options to start and only skipping replacement tests of card x
when it is clear that card is crucial"*. Every arm is `z_m24 − 1x <card> + 1x Vitaspore`, with
`z_m24` itself in the pool as `y_ref`, so `arm − y_ref` **is** the measured cost of that one edit.
Nothing is summed; no additivity assumption appears anywhere in the round.

`y_ref` = −0.2868 std / −0.4022 2hg. Negative below = **cutting the card HELPS**.

| edit | std | 2hg | mean |
|---|---|---|---|
| Doubling Season 1→0 | −0.0080 | −0.0058 | **−0.0069** |
| Utopia Mycon 3→2 | −0.0057 | −0.0073 | **−0.0065** |
| Blooming Marsh 4→0 (land-for-land) | −0.0020 | −0.0030 | **−0.0025** |
| Mycoloth 2→1 | −0.0050 | +0.0027 | −0.0012 (null) |
| Blooming Marsh 4→2 | −0.0017 | −0.0005 | −0.0011 |
| `y_land` one Forest → one Vitaspore | −0.0007 | +0.0040 | +0.0017 |
| Wild Growth 2→1 | +0.0093 | +0.0175 | +0.0134 |
| Tukatongue 2→1 | +0.0158 | +0.0147 | +0.0152 |
| Undercellar Myconid 4→3 | +0.0151 | +0.0182 | +0.0166 |
| Saproling Burst 4→3 | +0.0136 | +0.0217 | +0.0176 |
| Thallid 1→0 | +0.0178 | +0.0182 | +0.0180 |
| Yavimaya Sapherd 1→0 | +0.0188 | +0.0177 | +0.0182 |
| Shroofus Sproutsire 2→1 | +0.0118 | +0.0267 | +0.0192 |
| Brightcap Badger 2→1 | +0.0153 | +0.0262 | +0.0207 |
| Saproling Migration 1→0 | +0.0196 | +0.0282 | +0.0239 |
| Concordant Crossroads 2→1 | +0.0371 | +0.0240 | +0.0306 |
| Beastmaster Ascension 4→3 | +0.0116 | +0.0547 | +0.0332 |
| Sporecrown Thallid 4→3 | +0.0565 | +0.0462 | +0.0514 |
| Hickory Woodlot 4→2 (land-for-land) | +0.0496 | +0.0572 | +0.0534 |
| **Sol Ring 1→0** | **+0.0848** | **+0.0907** | **+0.0877** |

### What it settles

* **Sol Ring is the single most valuable card in the deck**, by a factor of 1.7 over the next
  card -- and the USER's Modern/Legacy/Vintage rule caps it at one copy. That is the cleanest
  possible vindication of the retraction in the Sol Ring section above: the illegal 1→2 edit
  looked like the screen's best find *because the card really is that good*. The cap binds on the
  best card in the list, and it is a format rule, not a measurement.
* **Three cuts are free or better than free**, which is what round 18 spends.
* **The land ladder is exhausted.** `y_land` says the 24th Forest is worth the same as a blank
  Thallid (+0.0017). With round 16's rungs (23→24 gains, 24→25 loses) the count is settled at 24
  and no further rung deserves an arm.
* **Round 16's Crossroads correction holds on held-out seeds**: cutting copy #2 costs +0.0306
  mean here against +0.0254 in round 16 — consistent, and both far above the ~0.016 floor. The
  round-15 inference that it was nearly free is dead twice over.
* **Beastmaster's format split is the screen's largest, again**: +0.0116 std vs +0.0547 2hg. Four
  copies are non-negotiable for a list serving both formats, exactly as round 13 found.
* The USER's two named asks both hold up at the composite: **Brightcap Badger #2 (+0.0207) and
  Shroofus #2 (+0.0192)** are each worth more than a third Tukatongue or a second Wild Growth.

### The bias audit, which runs the conservative way

Fall-through was **1.58% on 19 of 22 arms** -- the flattest the screen has managed, so arm-vs-arm
differences are nearly bias-free. The two exceptions both *flatter cutting* the card:
`y_cut_concordantcr` at 0.40% and `y_cut_beastmastera` at 1.30%. Both verdicts came back "keep",
so the bias cannot have manufactured them; the true costs are if anything higher than printed.

### A constraint on how any of this may be quoted

`z_m24` measured −0.3040 / −0.4467 in round 16 and −0.2868 / −0.4022 as round 17's `y_ref`: a
drift of **0.017 std / 0.044 2hg** between seed blocks, which is larger than most of the effects
being chased. So **absolute deltas versus base are not comparable across rounds** -- only
within-pool paired differences are, since those share seeds and the apparatus. Every number in
this section is within-pool, and the final recommendation quotes one pool rather than splicing.

---

## Round 18 RESULT (seed 1110000): every arm beats `z_m24`

The three free cuts from round 17 (Doubling Season 1→0, Utopia Mycon 3→2, Blooming Marsh 4→0 with
the four slots staying land as Forest) free **two spell slots**. Each arm spends them differently;
`f_ref` is `z_m24` in the same pool, so every number below is a within-pool paired difference.

| arm | the two slots | std | 2hg | mean vs `z_m24` |
|---|---|---|---|---|
| **`f_E`** | `f_A` + Mycoloth 2→1 → Badger #3 | **−0.0740** | **−0.0645** | **−0.0692** |
| `f_A` | Crossroads #3 + Saproling Migration #2 | −0.0597 | −0.0470 | −0.0533 |
| `f_C` | Yavimaya Sapherd #2 + Thallid #2 | −0.0459 | −0.0465 | −0.0462 |
| `f_B` | Brightcap Badger #3 + Shroofus #3 | −0.0382 | −0.0507 | −0.0444 |
| `f_D` | 2 Spore Swarm | −0.0297 | −0.0333 | −0.0315 |
| `f_free` | both slots BLANK (2 Vitaspore) | −0.0185 | −0.0150 | −0.0167 |

### The predictions, scored

* `f_free` predicted **−0.014**, measured **−0.0167**. The three cuts measured −0.0069, −0.0065
  and −0.0025 separately, so an exactly additive composite is −0.0159 and this ran **5%
  super-additive** — against round 14's 6.5–9.5% sub-additive. Both are within noise of exact, so
  the swap algebra is well calibrated for means over a wide range of composite sizes.
* The best *spend* arm was predicted at **−0.045 to −0.055**; `f_A` landed at **−0.0533**, inside
  the band. `f_E`, added as an extra, beat it.

### `f_A` and `f_E` won while carrying the round's only handicap

Raising Crossroads to 3 makes untabled compositions reachable, so those two arms ran at
**3.75% / 3.77%** fall-through against **1.58%** on every other arm — a one-way penalty *against*
them. They won anyway, which makes both wins **lower bounds**. That is the direction that makes a
result safe to build the next round on.

### What `f_E` decomposes to

`f_E − f_A` is exactly (Mycoloth 2→1) + (Badger 2→3) = −0.0143 / −0.0175, mean −0.0159. Round 17
priced Mycoloth 2→1 at −0.0012, a null. So **Brightcap Badger #3 is worth about −0.015 alone**.

The structural story is coherent and it is round 11's finding one level on. Round 11 said the deck
was *mana-starved, not body-starved*; with 24 lands and Sol Ring the mana is now there, and the
payoff has become **cheap bodies plus haste** — the deck keeps asking for more Badger, Crossroads
and Migration while shedding Doubling Season, Utopia Mycon and Mycoloth. Note Mycoloth 4→0 was
round 1b's single best arm (`a3`, −0.1502/−0.2243) *before* the Main-2 devour pin was fixed, and
round 9 then found the 2/1/0 ladder monotone the other way *after* the fix. Round 19 re-tests 1→0
on a list that has moved 0.41 turns since either reading — the only honest way to settle a number
that has already flipped sign once.

### `f_E`, the leading list (60 cards, 24 lands, mono-green)

```
1 Sol Ring              2 Utopia Mycon          1 Mycoloth
4 Sporecrown Thallid    2 Tukatongue Thallid    3 Brightcap Badger
4 Undercellar Myconid   2 Shroofus Sproutsire   4 Beastmaster Ascension
2 Wild Growth           4 Saproling Burst       3 Concordant Crossroads
2 Saproling Migration   1 Thallid               1 Yavimaya Sapherd
LANDS (24)   20 Forest  4 Hickory Woodlot
```

No Doubling Season at all — a card the USER likes and expected to keep ("*I like Doubling Season
and think it is strong*"), though they also said reducing it "*may very well be defensible*" and
that multiples are not super good. The screen took it to zero on numbers: the last copy was worth
−0.0069 to CUT in round 17, and nothing in round 18 wanted it back.

---

## Round 19 RESULT (seed 1120000): the model is good now, and the one card I mispriced

**The confirm first.** `f_E` is the selection-biased winner of a six-arm screen, so it re-ran here
as `g_ref` on a disjoint seed block with its `replace` map carried verbatim (same card numbering,
so it is a replication of the same list rather than a re-derived cousin):

| | round 18 | round 19 | drift |
|---|---|---|---|
| `f_E` vs base | −0.3618 / −0.4605 | −0.3583 / −0.4688 | +0.0035 / −0.0083 |

Clean. Read against `g_ref`, with the predictions written into the builder before the run:

| arm | edit | std | 2hg | mean | predicted | error |
|---|---|---|---|---|---|---|
| **`g_D`** | **Tukatongue 2→1 → Migration #3** | **−0.0152** | **−0.0175** | **−0.0164** | **+0.0050** | **−0.0214** |
| `g_E` | Mycoloth 0 + Mycon 1 → Sapherd #2 + Thallid #2 | −0.0179 | −0.0112 | −0.0145 | −0.0100 | −0.0045 |
| `g_F` | `g_A` + `g_B` | −0.0042 | −0.0242 | −0.0142 | −0.0180 | +0.0038 |
| `g_A` | Mycoloth 1→0 → Badger #4 | −0.0075 | −0.0170 | −0.0123 | −0.0120 | −0.0003 |
| `g_B` | Mycon 2→1 → Shroofus #3 | +0.0020 | −0.0177 | −0.0078 | −0.0080 | +0.0002 |
| `g_C` | Wild Growth 2→1 → Crossroads #4 | +0.0035 | +0.0131 | +0.0083 | +0.0120 | −0.0037 |

Four of six predictions landed within 0.004, **including both pre-registered losses**. A census
that only tests its favoured direction cannot find the edge of it; `g_C` and `g_D` were run
specifically to find that edge, and `g_C` duly confirmed round 13's "Crossroads #4 is dead".
(`g_C` also carried a 6.74% fall-through against the reference's 3.77%, a handicap running against
an arm already predicted to lose — so it is not clean evidence against a 4th Crossroads, only
consistent with the earlier reading.)

### The misprediction, which is the round's actual finding

I predicted `g_D` at **+0.0050** — a loss — on the reasoning that Tukatongue #2 costs +0.0152 in
the census and a third Saproling Migration would not return it. It returned more than twice it.
Backing the donor out, **Saproling Migration #3 is worth about −0.032**, the most valuable single
addition since the Beastmaster fill.

**And Migration's number is a LOWER bound, which makes this more interesting rather than less.**
Two documented omissions push the same way against the card: the **kicker is not modelled at all**
(`grep kicker src/` is empty), and the kicked mode is four tokens for `{5}{G}` — a cost this
24-land deck reaches on exactly the turns its wins cluster on; and `EvalCard` has no
`cast_creates_tokens` branch, so the card is valued as a cantrip in the arm that plays it. This is
the one place in the whole screen where building a missing mechanic could **change a ranking**
rather than merely tidy a disclosure.

### Additivity, third reading — and it is drifting

`g_F` predicted −0.0180 from `g_A` + `g_B` = −0.0201 additive; measured −0.0142, i.e. **29%
sub-additive**, against round 14's 6.5–9.5% sub and round 18's 5% super. The reason is visible
rather than mysterious: `g_A` and `g_B` both spend into the same "cheap bodies" theme and compete
for the same marginal turns. Round 20 therefore applies an explicit 25% sub-additive discount to
every composite prediction, and says so per arm.

---

## Round 20 RESULT (seed 1130000): Utopia Mycon goes to ZERO, and the archetype changes

Read against `h_ref` = f_E in-pool. Predictions written into the builder before the run, with an
explicit 25% sub-additive discount applied to every composite after round 19's drift.

| arm | edit | std | 2hg | mean | predicted | error |
|---|---|---|---|---|---|---|
| **`h_F`** | `h_A` + Sapherd #2 + Thallid #2 off **Utopia Mycon 2→0** | **−0.0447** | **−0.0464** | **−0.0456** | −0.0280 | −0.0176 |
| `h_B` | `h_A` + Mycon 1 + Shroofus 3 | −0.0204 | −0.0377 | −0.0290 | −0.0270 | −0.0020 |
| `h_D` | `h_B` + Migration #4 | −0.0207 | −0.0354 | −0.0280 | −0.0330 | +0.0050 |
| `h_E` | `h_A` + Migration #4 off Undercellar | −0.0227 | −0.0302 | −0.0265 | −0.0250 | −0.0015 |
| `h_C` | `h_A` + Migration #4 off Wild Growth | −0.0197 | −0.0277 | −0.0237 | −0.0290 | +0.0053 |
| `h_A` | `g_A` + `g_D` | −0.0142 | −0.0275 | −0.0208 | −0.0220 | +0.0012 |

### The structural prediction held, and it is the one that licenses the method

**|`h_C` − `h_E`| = 0.0028**, against a written "< 0.006". Those two arms buy the *identical*
Migration #4 rung off different donors (Wild Growth #2 at +0.0134 and Undercellar #4 at +0.0166 in
the round-17 census). Their agreement is direct evidence that the census's donor prices **transfer
to this composite** rather than being local to `z_m24` — the thing round 16's land ladder had
assumed and could not show. Five of six point predictions also landed within 0.005.

**Migration stops at 3.** The #4 rung is worth only −0.003 to −0.006 against #3's −0.032, and in
the full composite `h_B` (Migration 3) edges `h_D` (Migration 4) by −0.0010. The ladder is flat.

### Utopia Mycon goes to zero — the USER's own named test case

*"For example, I don't expect Utopia Mycon to go, but that should be proven with numbers."* The
numbers say it goes, in four independent steps across four seed blocks:

| step | reading |
|---|---|
| round 11: 4th copy | +0.0005 (t=+0.14) — a dead null |
| round 17: 3→2 | −0.0065 — FREE |
| round 19 `g_B`: 2→1 | cheap (the pair with Shroofus #3 nets −0.0078) |
| round 20 `h_F`: 2→0 | **gains 0.025 over the same arm that kept it** (`h_F` vs `h_B`) |

### This is an archetype change, not a tuning

`h_F` has shed all three of the deck's engine cards — **zero Mycoloth, zero Utopia Mycon, zero
Doubling Season** — and with them *every sacrifice outlet in the list* (Mycon, Psychotrope,
Deathspore, Vitaspore and Mycoloth's devour are all at 0). candidate-b was a ramp/doubler deck;
this is a cheap-bodies-plus-haste aggro deck that happens to share a card pool. The Saprolings
simply attack.

It is also **7.6x cheaper to simulate**: 50.5 ms/game against the base's 382.4, and 2.2x cheaper
than `f_E` at 112.5. That is not incidental — the devour k-axis and the sac-outlet powerset were
this deck's two documented cost centres, and both are gone. If this list is ever adopted it walks
into the 3x suite-cost rule with enormous headroom, where candidate-b was the expensive case.

### `h_F`, the leading list (60 cards, 24 lands, mono-green)

```
1 Sol Ring              4 Sporecrown Thallid    1 Tukatongue Thallid
4 Brightcap Badger      4 Undercellar Myconid   2 Shroofus Sproutsire
4 Beastmaster Ascension 2 Wild Growth           4 Saproling Burst
3 Concordant Crossroads 3 Saproling Migration   2 Thallid
2 Yavimaya Sapherd
LANDS (24)   20 Forest  4 Hickory Woodlot
```

---

## Round 21 RESULT (seed 1140000): CONVERGED -- every remaining trade loses

`h_F` re-ran as `i_ref` on a disjoint seed block (replace map carried verbatim). Round 20 read it
at −0.4020 / −0.5152; round 21 reads **−0.3900 / −0.5272**, a drift of +0.0120 / −0.0120 — a wash,
so the eight-arm screen's winner is not selection-bias inflation.

Every candidate trade spends the cheapest remaining donor on the best remaining add. **All six
lose:**

| arm | edit | std | 2hg | mean | predicted |
|---|---|---|---|---|---|
| `i_C` | Wild Growth 2→1 → Thallid #3 | +0.0060 | +0.0010 | +0.0035 | +0.0030 |
| `i_F` | Undercellar 4→3 → Sapherd #3 | +0.0005 | +0.0104 | +0.0055 | +0.0070 |
| `i_D` | Wild Growth 2→1 → Migration #4 | +0.0083 | +0.0052 | +0.0067 | +0.0090 |
| `i_A` | Wild Growth 2→1 → Shroofus #3 | +0.0242 | +0.0160 | +0.0201 | +0.0000 |
| `i_B` | Wild Growth 2→1 → Sapherd #3 | +0.0202 | +0.0299 | +0.0250 | +0.0030 |
| `i_E` | Wild Growth 2→0 → Sapherd #3 + Thallid #3 | +0.0310 | +0.0429 | +0.0370 | +0.0050 |

A screen is finished when its best available trade is no longer positive, and that is now measured
rather than asserted. **`h_F` is the recommendation.**

### A NEGATIVE result on the method, recorded because it matters

The donor-transfer check **failed here**, having passed in round 20. `i_B` and `i_F` buy the
identical Yavimaya Sapherd #3 rung off two donors the round-17 census priced 0.003 apart; the
predicted gap was ~0.004 and the measured gap is **0.0195**. So Wild Growth #2 is worth far more in
`h_F` than the census (measured on `z_m24`) said — and the mechanism is visible rather than
mysterious: `z_m24` still had Utopia Mycon for mana, and `h_F` has none, so Wild Growth is now one
of only three mana accelerants left (with Sol Ring and Hickory Woodlot). **A donor price is a
marginal, and it moves when the list around it moves.** Round 20's check passed because `h_A` was
much closer to `z_m24` than `h_F` is. The practical rule: a census transfers across small composite
steps and must be re-measured across large ones.

Also note my three predictions for `i_A`/`i_B`/`i_E` were 0.020–0.032 too optimistic, all in the
same direction — I over-rated the remaining adds, which is the same error as round 19's `g_D`
misprediction with the sign reversed.

---

# Round 22 (2026-10-14): the USER asks whether the BUDGET made these calls

USER: *"we might need some additional higher budget evidence to ensure that our engine is not
deleting things because they cost budget."*

**This is the right question and it threatens the headline, so it is recorded as a live alternative
hypothesis rather than defended against.** All 21 rounds ran at `depth 5, budget_ms 20`. The
winner is **7.6x cheaper per game** than the base (50.5 ms vs 382.4). And the three cards the
screen drove to zero are precisely the three this repo documents as the deck's cost centres:

* **Mycoloth** — the devour k-axis ("one cast variant per k = 0..(own creatures)"), which
  `MTG_FUNGUS_DEVOUR_LETHAL` exists to collapse; cards.json calls it "THE DECK'S ENGINE".
* **Utopia Mycon** — the sac-outlet count pool, whose adoption was "worth 2.29x on the heaviest
  cell", over "a powerset over identical bodies".
* **Doubling Season** — multiplies every token and counter event, inflating downstream enumeration.

Under a binding budget those three lose their best lines first, and a screen would then rank them
down for being *expensive* rather than for being *weak*. CLAUDE.md is explicit that the test belongs
at the top of the budget range: *"A BUDGET MAY RE-PRICE WORK, IT MAY NOT MAKE A LINE UNFINDABLE —
COMPLETENESS AT LARGE BUDGET IS THE INVARIANT."*

The screen's budget was **not** below a shipped deeper policy — Fungus's `value_play` carries only
`mull_gen_depth 1 / mull_gen_budget_ms 3` with no `target_depth`, so the leaf never drives play and
d5/b20 was a deliberate screening choice. That is not a defence: it means 20 ms was simply never
validated as non-binding for this deck.

### The ladder (running)

Three pooled batches, one per budget — `budget_ms` is spec-level in `deck_compare`, so separate
batches are unavoidable and each is a genuinely different measurement, not a wave of one job. All
16 cells of each budget pool into one batch; chained by captured PID.

| budget | games | seed |
|---|---|---|
| 20 ms (the screen's own setting, as the in-ladder control) | 2000 | 1150000 |
| 80 ms | 2000 | 1160000 |
| 320 ms | 1200 | 1170000 |
| 20 ms again — the **seed-matched control**, added once the ladder was already running | 1200 | 1170000 |

**Why the fourth rung exists.** The three ladder rungs each carry their own seed base, which makes
each one an independent replication but leaves the cross-rung comparison **unpaired**: a margin
measured at 20 ms and a margin measured at 320 ms sit on different games, so comparing them adds
between-block variance (measured at 0.017 std / 0.044 2hg in rounds 16–17 — *larger than most
effects in this screen*) on top of each rung's own se ~0.015. That resolves a collapse of the
margin and nothing subtler. `spec_round22_b20x.json` is byte-identical to the 320 ms spec except
`budget_ms` 320 → 20, so **the 20 ms and 320 ms readings run on the identical 1200 games with
budget as the only difference** — and each arm's own `avg` becomes comparable across the two
budgets, which is the direct measurement of how much more budget is worth *to that list*. That
per-arm budget elasticity is the real answer to the user's question: if `z_m24` gains much more
from 16× budget than `h_F` does, then 20 ms was under-serving the engines and the screen's ranking
was partly a budget artifact.

Seven arms, spanning the whole trajectory by engine count (Mycoloth / Mycon / Season):

| arm | engines |
|---|---|
| `k_z_m24` | 2 / 3 / 1 — intact |
| `k_f_E` | 1 / 2 / 0 — partly cut |
| `k_h_F` | 0 / 0 / 0 — gone (the recommendation) |

plus `k_cut_myco`, `k_cut_season`, `k_cut_mycon` — the three most suspect single cuts re-priced on
`z_m24` against a neutral Vitaspore fill, exactly as round 17 built them at 20 ms.

**What would falsify the screen:** the `k_h_F`-over-`k_z_m24` margin SHRINKING as budget rises, or
any of the three "free" cuts turning into a LOSS. **Prediction on the record: the margin shrinks
but survives** — the engines genuinely do cost budget, so some of the measured gain must be the
cheap list spending its 20 ms better, but `h_F` beat `z_m24` by ~0.10 mean, 6x the apparatus floor
and more than a plausible budget-allocation effect. The three single cuts are the ones I am
genuinely unsure about, each being within 0.008 of zero; at these sample sizes they are read for
**sign and trend only** and must not be quoted as settled.

### Round 22 results — the ladder, and why the UNPAIRED reading misled me

| | b20 (s1150000) | b80 (s1160000) | b320 (s1170000) |
|---|---|---|---|
| `h_F` over `z_m24`, std | −0.0985 | −0.0785 | −0.0783 |
| `h_F` over `z_m24`, 2hg | −0.0925 | −0.0750 | −0.0550 |
| **single cuts on `z_m24`** (positive = the CUT loses) | | | |
| Mycoloth 2→1 | −0.0120 / +0.0075 | +0.0105 / +0.0290 | −0.0025 / −0.0116 |
| Utopia Mycon 3→2 | +0.0060 / +0.0020 | +0.0145 / +0.0390 | +0.0158 / +0.0234 |
| Doubling Season 1→0 | +0.0115 / +0.0185 | −0.0030 / +0.0020 | +0.0033 / +0.0025 |
| apparatus floor (`ctl1` − base) | −0.0150 / −0.0155 | −0.0175 / −0.0110 | −0.0075 / −0.0166 |

Read across the three rungs this looks like ~20% margin erosion per 4× budget. **It is not.** Each
rung carries its own seed base, so that comparison is unpaired and absorbs the between-block drift
this screen measured at 0.017 std / 0.044 2hg in rounds 16–17 — larger than the effect being read.

**Two readings I got wrong off this table, both corrected by better data.** I reported a Mycoloth
flip from the b80 column alone; b320 puts its cut back to free-or-gaining, and an intermediate rung
disagreeing with both neighbours is noise, not a trend. And I called Doubling Season "the cut that
passed" when it is positive in 5 of 6 readings (mean +0.0058) — **USER-caught**: *"doubling season
1->0 was worse in both cases, so we don't cut it?"* It is well under the floor and does not resolve,
but a sign landing the same way 5 times of 6 is evidence and should not have been rounded to zero.

### Round 22's fourth rung — the seed-matched control, which is the result that counts

`spec_round22_b20x.json` is byte-identical to the b320 spec but for `budget_ms` 320 → 20: the same
1200 games, the same seed base 1170000. So budget is the ONLY difference and the comparison is
paired at the game level.

| | b20 | b320 (16×) | change |
|---|---|---|---|
| `h_F` over `z_m24`, std | −0.0841 | −0.0783 | **+0.0058** |
| `h_F` over `z_m24`, 2hg | −0.0659 | −0.0550 | **+0.0109** |

**Per-arm budget elasticity — how much each LIST gains from 16× budget** (negative = faster at
b320). This is the direct answer to the user's question:

| list | engines (Mycoloth/Mycon/Season) | std | 2hg |
|---|---|---|---|
| `base` | 4 / 4 / 4 | **−0.0367** | **−0.0709** |
| `ctl1` | 4 / 4 / 4 | −0.0342 | −0.0766 |
| `z_m24` | 2 / 3 / 1 | −0.0058 | −0.0134 |
| `f_E` | 1 / 2 / 0 | −0.0016 | −0.0125 |
| `h_F` | 0 / 0 / 0 | **+0.0000** | **−0.0025** |

The engine-dense lists ARE budget-starved — `base` gains 15–28× more from extra budget than `h_F`,
which is saturated at 20 ms. **So the user's hypothesis is confirmed as a mechanism and refuted as
an explanation:** giving the engine lists 16× more budget closes about a tenth of a ~0.07 margin.

### Rounds 23–24 — the engines re-priced ON `h_F`, at two donors and up to d7/b1000

Round 21's rule applies: a donor census does not transfer across a large composite step, and
`z_m24` → `h_F` is a large one (`z_m24` still has Utopia Mycon for mana). So the engines were
re-priced on `h_F` itself. Positive = the ADD loses. Seed 1180000, 2000 games, identical games
across all three settings.

| add | d5/b20 | d5/b320 | d7/b1000 |
|---|---|---|---|
| +1 Mycoloth (Sapherd donor) | +0.0085 / +0.0055 | +0.0090 / +0.0040 | +0.0095 / +0.0050 |
| +1 Mycoloth (Thallid donor) | +0.0085 / +0.0105 | +0.0090 / +0.0120 | — |
| +1 Utopia Mycon (Sapherd) | +0.0035 / +0.0055 | +0.0015 / −0.0005 | +0.0020 / −0.0010 |
| +1 Utopia Mycon (Thallid) | +0.0075 / +0.0140 | +0.0050 / +0.0110 | — |
| +1 Doubling Season (Sapherd) | +0.0085 / −0.0055 | +0.0090 / −0.0080 | +0.0095 / −0.0075 |
| +1 Doubling Season (Thallid) | +0.0110 / +0.0095 | +0.0115 / +0.0090 | — |
| composite `h_F` over `z_m24` | −0.0670 / −0.0685 | −0.0615 / −0.0535 | −0.0615 / −0.0485 |

**The budget-independence is the headline.** Mycoloth's add moves by 0.001 across a 50× budget
increase and two extra plies. No card's value here depends on budget. Construction check passed:
`m_ref` reproduced round 22's `k_h_F` (−0.3895/−0.4985 vs −0.3885/−0.4860).

Round 24 (d7/b1000) also came in far cheaper than projected — 21.6 s of core time per game index
across 14 cells, vs the ~56 s I estimated. Cost grew as budget^0.51 and the depth factor was well
under 2×, so the aggressive setting is affordable at scale.

### Round 25 — the held-out confirmation, which REVERSED the engine verdict

Seed 1200000 (held out), **6000 games**, d7/b1000. Pairwise se 0.0049 std / 0.0062 2hg.

| pair | std | 2hg |
|---|---|---|
| `h_F` + 1 Utopia Mycon over `h_F` | **−0.0117** (t ≈ −2.4) | **−0.0258** (t ≈ −4.2) |
| `h_F` + 1 Mycoloth over `h_F` | −0.0017 | −0.0082 |
| `h_F` + 1 Doubling Season over `h_F` | +0.0112 | +0.0028 |
| `h_F` over `z_m24` | −0.0718 | −0.0505 |

Full ranking: **`mycon` < `myco` < `h_F` < `season` < `z_m24`** — both creature engines landed on
the gaining side of `h_F`, Mycon clearly.

This contradicts rounds 23–24 on block 1180000 (+0.0020/−0.0010, a null) by ~0.014 ≈ 1.4 combined
se. Not a hard contradiction — it is seed-block drift again, and round 25 is the better measurement
on every axis (3× the games, held out, lower se). **It is not resolvable by preferring a block.**

**Note what this does and does not revive.** It does NOT revive "the budget deleted the engines" —
rounds 22–24 refuted that with budget-invariant effects across 50×. It raises a different and
narrower possibility: that the screen cut a card a better-powered measurement wants back.

### Round 26 — ABANDONED, and why `--with-floor` is not available here

Every engine effect above sits below the assumed floor (`ctl1`, −0.0098/−0.0175), so the verdict
rests on sign consistency rather than on any single measurement. `--with-floor` would MEASURE the
bias instead, and the screening skill prescribes it for exactly this case. It is **dropped**, for
two independent reasons:

1. It **generates per-arm R=10 keep tables**, and the standing USER constraint is *"We should not
   be generating any new tables during this process"*, restated 2026-10-14: *"We aren't generating
   mulligan tables are we? We should not be."* / *"We should still be aliasing an existing table."*
2. That generation **dies anyway** on `Error: bucket policy names a card this deck does not
   contain: 'Secluded Courtyard'` — `h_F` is mono-green and `Fungus.buckets.json` still groups the
   black mana base. Amending a bucket policy is a **USER-only ruling**; an agent must never install
   or amend a `buckets.json` unasked.

So the floor stays **estimated, not measured**, and must be reported that way. The mitigating
argument (not a substitute for the measurement): `ctl1` measures base-vs-arm bias, whereas the
comparisons under test are arm-vs-arm between lists that share a table and differ by two cards, so
most of the shared-table bias is common-mode and cancels.

Two tooling facts worth recording, each of which cost a 30-second failed run:
* `--with-floor` refuses on the **presence** of a `formats` key, not on its length — a one-entry
  `formats` is still refused. A single-format spec has **no** `formats` key and inlines
  `starting_life` / `opponent_heads` at the top level (`deck_compare.py:1029` rejects having both).
* `deck_compare` takes `logs/deckcmp/<Deck>/.lock`; two runs on one deck would both rewrite the
  arm decklists and `numbering.json` under each other. Chain by captured PID, never run two.

### Round 27 — the last open question (DESIGN; result below)

Does `h_F` want 1 Utopia Mycon? A **third** fresh held-out block (1210000), **10,000 games**,
d7/b1000, 16 cells, on the aliased R=30 table (`pool_table: false` — nothing generated).

| arm | list |
|---|---|
| `r_ctl1` | the apparatus floor, the standing instrument |
| `r_h_F` | the recommendation, as reference |
| `r_mycon1_S` | + 1 Utopia Mycon, donor Yavimaya Sapherd 2→1 |
| `r_mycon1_T` | + 1 Utopia Mycon, donor Thallid 2→1 |
| `r_mycon2` | + 2 Utopia Mycon, both donors — the RUNG check |
| `r_myco1_S` | + 1 Mycoloth, donor Sapherd 2→1 |
| `r_z_m24` | the engines-intact anchor |

At 10,000 games the pairwise se scales to ~0.0038 std / 0.0048 2hg, so round 25's effect lands at
t ~3.1 / ~5.4 if real and ~0 if it was drift. The 2-copy rung is the corroborating check: a card
that genuinely belongs should show a **rung** (second copy worth less than the first), not a flat
jump that looks like a fixture of one block.

**Prediction on the record: Mycon goes back in at 1 copy.** It is the only card in `h_F` that turns
surplus saprolings into mana and `h_F` has zero sacrifice outlets — a gap named in round 23's own
docstring before any of these numbers existed, which I then let two under-powered blocks talk me
out of. I have mispredicted swap signs in rounds 19 and 21 by over-rating adds; this would be the
first time I under-rated one.

### Round 27 RESULT (seed 1210000, 10,000 games, d7/b1000): round 25 REPLICATES — Mycon goes back in

Finished 2026-10-14 14:10Z, 2 h 00 m, 32/32 workers the whole way. The pre-registered contrasts,
read off the printed pairwise matrices (negative = **the ADD gains**):

| contrast | std | 2hg |
|---|---|---|
| `r_mycon1_S` − `r_h_F`  (+1 Mycon, donor Yavimaya Sapherd 2→1) | **−0.0107** | **−0.0226** |
| `r_mycon1_T` − `r_h_F`  (+1 Mycon, donor Thallid 2→1) | −0.0050 | −0.0069 |
| `r_mycon2` − `r_h_F`  (+2 Mycon, both donors) | −0.0056 | −0.0179 |
| `r_myco1_S` − `r_h_F`  (+1 Mycoloth, donor Sapherd 2→1) | +0.0032 | −0.0080 |
| `r_ctl1` (the apparatus floor, ESTIMATED not measured) | −0.0103 | −0.0081 |

Printed pairwise se for the closest pair: **0.0052 std / 0.0058 2hg** — larger than the 0.0038 /
0.0048 I pre-registered, so the t on the headline contrast is ≈ **−2.1 std / −3.9 2hg**, not the
−3.1 / −5.4 I projected. The *effect* replicated; my *precision* estimate was optimistic.

**The replication, which is the whole point of this round.** `+1 Utopia Mycon` on the Sapherd donor,
across every block it has been measured on:

| block | games | setting | std | 2hg |
|---|---|---|---|---|
| 1180000 | 2,000 | d5/b20 | +0.0035 | +0.0055 |
| 1180000 | 2,000 | d5/b320 | +0.0015 | −0.0005 |
| 1180000 | 2,000 | d7/b1000 | +0.0020 | −0.0010 |
| 1200000 | 6,000 | d7/b1000 | −0.0117 | −0.0258 |
| 1210000 | 10,000 | d7/b1000 | **−0.0107** | **−0.0226** |

The two well-powered blocks agree to within 0.001 std / 0.003 2hg. Block 1180000 carried pairwise se
~0.0085 / ~0.0107, so its "+0.0020" sits about 1.5 se from −0.0107 — **not a contradiction, just
three readings of an underpowered block that I reported as a settled null.** That is the same error
as the unpaired budget ladder in round 22, in a different dress: an effect smaller than the block's
own noise was read as zero rather than as unresolved.

**The 2-copy rung behaves correctly.** Two copies (−0.0056 / −0.0179) give back roughly half of one
copy's gain (−0.0107 / −0.0226) in both formats, so the second copy is negative-valued. That is the
diminishing shape a card belonging at exactly 1 copy must show; a flat jump would have suggested a
fixture of the block rather than a card effect. The rung was the corroborating check and it passes.

**Three caveats, all of which belong in the recommendation.**

1. **The donor carries part of the gain.** The Thallid donor buys the identical rung and gains only
   −0.0050 / −0.0069 — less than half the Sapherd donor's. So some of the Sapherd arm's margin is
   the **second Yavimaya Sapherd being the worst card in the list**, not Mycon's value. What the
   two-donor design was built to test is the **sign**, and both donors agree on it in both formats.
2. **In std the gain does not clear the floor.** −0.0107 against an estimated floor of −0.0103 is
   1.0×; in 2hg, −0.0226 against −0.0081 is 2.8×. std alone would not carry this call. It is carried
   because std and 2hg are **co-equal** and 2hg is clear, and because `ctl1` measures *base-vs-arm*
   bias while this contrast is *arm-vs-arm* between two lists that share one table and differ by two
   cards — most of that bias is common-mode and cancels. **The floor remains ESTIMATED, never
   measured** (round 26: `--with-floor` is unavailable here).
3. **Selection bias is flagged by the driver** (`r_mycon1_S` wins both formats). It does not apply
   the usual way: this round *was* the held-out confirmation of round 25's winner, on a third
   disjoint block, with the arm fixed in advance.

**Mycoloth stays at zero.** +0.0032 std / −0.0080 2hg — signs split, both at or under the floor,
consistent with round 25's −0.0017 / −0.0082. Three blocks have now failed to show the first
Mycoloth earning its slot.

**The prediction in round 27's docstring held.** I had written that Mycon would come back at 1 copy
because it is the only card in the list that converts surplus saprolings into mana and the list has
zero sacrifice outlets — a structural gap named in round 23's docstring, before any of these numbers
existed, which two underpowered blocks then talked me out of. Recorded because the *mechanism* was
available three rounds before the measurement caught up, and I down-weighted it for noise.

## FINAL RECOMMENDATION (rounds 1–27, 2026-10-13/14)

**`h_F` + 1 Utopia Mycon − 1 Yavimaya Sapherd** — 60 cards, 24 lands, mono-green:

```
1 Sol Ring            4 Beastmaster Ascension      20 Forest
1 Utopia Mycon        2 Wild Growth                 4 Hickory Woodlot
4 Sporecrown Thallid  4 Saproling Burst             3 Saproling Migration
1 Tukatongue Thallid  3 Concordant Crossroads       2 Thallid
4 Brightcap Badger                                  1 Yavimaya Sapherd
4 Undercellar Myconid
2 Shroofus Sproutsire
```

**Margin over the shipped-style anchor `z_m24`** (2 Mycoloth / 3 Utopia Mycon / 1 Doubling Season):
−0.0761 std / −0.0608 2hg at d7/b1000 on 10,000 held-out games — about 8–12× the estimated apparatus
floor, and stable from 20 ms to d7/b1000.

**Legality:** Sol Ring at exactly **1** (Restricted in Vintage, illegal in Modern/Legacy), nothing
else over its cap. Checked against `logs/fungus_screen/legality.json` in every round-27 arm.

**Named model biases that the numbers cannot price, and which a human should weigh:**

* **Saproling Migration's kicker is unmodelled**, so its 3 copies are a **LOWER bound** — the real
  card is better than the measured one.
* **Concordant Crossroads grants haste symmetrically.** The goldfish has no opponent board to hand
  haste to, so its 3 copies carry a real paper cost the measurement cannot see. This is the one
  number in the list I would expect a human to overrule downward.
* **The apparatus floor is ESTIMATED from `ctl1`, not measured.** `--with-floor` generates per-arm
  R=10 keep tables (forbidden by the standing no-new-tables constraint) and dies anyway on
  `Fungus.buckets.json` naming `Secluded Courtyard`, a card this mono-green list does not contain.
  Amending a bucket policy is a **user-only** ruling.
* **0 Deathspore Thallid.** The user's 1-copy floor was explicitly conditional (*"at least if we have
  a list with black"*) and this list is mono-green, so it does not bind. Flagged, not assumed.

**Deliberately not done** (each is a user-owned next step, not a gap in the screen): a keep table and
value leaf fitted to this list; Saproling Migration's kicker plus the `EvalCard`
`cast_creates_tokens` branch; the convoke / `ManaDefOf` phantom-mana defect.
