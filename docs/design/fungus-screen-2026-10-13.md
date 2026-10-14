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
