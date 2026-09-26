# Knights copy-count campaign — WhiteKnights (mono-white) and Knights (R/W)

**User request, 2026-09-26:** *"I would like to start analyzing which cards in the deck (and in the
other deck) should change number of copies. I would like to include Remote Farm in the test first,
then move on to creatures. … I would like to leave this deck mono-white for the time being and likely
going forward as well."*

Scope read: copy-count screening (`deck-screening.md`) over the two Knight decks' **existing** pools,
plus one introduced card (Remote Farm). **WhiteKnights stays mono-white** — no arm in this campaign
adds a non-white source or a non-white spell, and that constraint is standing, not per-screen.
"The other deck" is read as `decks/Knights/` (the R/W list that shares this one's Knight core); say so
if a third list was meant.

This is the **per-combination** loop, not adoption. A combination that wins here still owes its own
mulligan profile, value leaf and regression ground truth before it ships.

---

## The apparatus, and why each choice is forced

| | WhiteKnights | Knights |
|---|---|---|
| keep table | K=14, 42,271 cells, R=40 | K=13, 43,038 cells, R=60 |
| `hand_score_threshold` | `-1e18` → **NO_GATE**, `card_scores` unreachable | **0.2453 → LIVE gate** |
| `value_play` | `{expected_buckets: 14}` only → no adopted policy, so the screen runs the **engine default d5/b20**, which is also its d5 suite cell | `target_depth 5, budget_ms 20, escalation_cap 5` |
| play settings used | d5 / 20 ms | d5 / 20 ms |

**Remote Farm is aliased into the Plains bucket, not generated for.** It is an introduced card, so the
shipped table does not bucket it, and `Decide`/`DecideBottom` would return `present=false` for any hand
holding it — a silent one-sided fall-through to the static keep on the only arm that plays the card.
The approved route (`deck-screening.md`, user directive 2026-09-02) is
`scripts/alias_card_into_bucket.py`, which adds the name to the bucket of the card it replaces. K is
unchanged and nothing is generated.

Measured, that is exact rather than approximate: because every Remote Farm arm trades Plains 1-for-1,
the land bucket's total is constant, so the arm's composition space is **identical** to base's and the
fall-through rate is **0.0000% on all four arms** (`logs/wk_screen/fbrate.py`).

* **The assumption this route makes, stated plainly:** the mulligan decision treats Remote Farm *as* a
  Plains. That holds keep/bottom policy fixed across arms by construction, which is what isolates the
  in-play difference — but it cannot see that a tapped, self-sacrificing double-land might want a
  *different* keep policy. If the thesis were "this land changes which hands you keep", this route
  would not measure it.
* **Both sidecars are copied next to the aliased table.** The engine resolves sibling models
  directory-relative off the *profile* path, so a scratch profile without its `.value.json` silently
  detaches the value leaf — worth 1.35–84.8x.
* **`"pool_table": false`** on every spec: a union table is banned (Rule 0a) and with the alias in place
  nothing should ever want one. The flag makes that impossible rather than unlikely.

**`max_fallback: 0.02` on the creature specs, from the measured number.** Cuts never overflow; a
*raise* past the table's enumerated cap makes untabled hands reachable on that arm alone. Measured:
Benalish Marshal 2→3 is 0.1023%, 2→4 is 0.3876%, Hero 3→4 is 0.0072%, and the two 1-of→2-of raises
(Silverblade Paladin, Adeline) are **1.1864% / 1.1936%** — just over the 1% default, which would have
sent the whole screen to a pool table. At ~0.0007 t of one-sided bias that is a tenth of the measured
apparatus floor, so raising the budget is the right call and dropping the table (~0.063 t on *both*
arms, ~22x wall) would be far worse.

**Three formats, per arm** (`std` 20 life / `2hg` 30 life + 2 heads / `long` 40 life), `max_turns 18`,
each compared to **its own** format's base. This follows the Angels campaign, where the ordering of
arms *widened* with game length and one arm outright **sign-flipped** between 20 and 60 life. Ranking
on 20-life speed alone would have adopted that arm. Compare deltas across formats, never levels.

---

## Why these arms — the evidence each hypothesis rests on

The `card_scores` marginals in each profile are a **hand-score fit against a passive goldfish**, not a
win-turn contribution. They are used here only to *choose what to measure*.

### WhiteKnights (`logs/wk_screen/cr_whiteknights.json`)

The ledger (`analysis-WhiteKnights.md` §2, reading 3) already names this screen as the required
follow-up: *"Several second copies price negative (Dauntless Bodyguard −0.202, Worthy Knight −0.125,
Venerable Knight −0.080) … the right follow-up is `deck-screening.md`, not a hand edit — and per the
cut-ladder lesson it should test **adding** a third/fourth copy of an expensive-to-cut card too, not
only cuts."*

Cut candidates, by marginal score: Valiant Knight −0.123 (its `{3}{W}{W}` activation measured
nearly unaffordable), Acclaimed Contender −0.115, Aether Vial −0.083, Hero of Bladehold −0.064,
Knight Exemplar −0.045, and the negative second copies above.

The sink is **Benalish Marshal** on group A — a `{W}{W}{W}` anthem the deck runs only 2 of, which is
exactly the card a mono-white constraint makes affordable and a splashed list cannot. Group B holds the
cut fixed (Valiant Knight → 0) and varies the sink instead, so "what is the free slot worth to each
candidate" is answered independently of "which card should leave". Group C moves the land count, which
cannot be tested alone: a 60-card deck makes every cut also an add.

`a_lg0_bm3` cuts **Lightning Greaves**, on two independent grounds: the reference bench found the
engine correctly declining it (*"Greaves is `{2}` — the whole turn — and adds no power; its shroud is
inert vs a passive opponent and its haste only pays on a creature cast that same turn"*), and the
Angels campaign measured cutting Greaves better in all three formats.

### Knights (`logs/wk_screen/cr_knights.json`)

Two signals here are much louder than anything in WhiteKnights:

* **`Aether Vial` is the worst card in either deck** — −0.113 for the first copy and **−0.364** for the
  second, at 4 copies. Laddered 4→3→2→0.
* **`Plains` prices NEGATIVE on every copy** (−0.002 / −0.083 / −0.121) while all three nonbasics price
  positive, because Plains cannot cast Inspiring Veteran or Haytham Kenway in an R/W deck. So the Vial
  slots are laddered into Plains *and* the Plains count is moved in both directions.

Knights is nearly all 4-ofs, so its only legal raises are Haytham Kenway 3→4, Adeline 1→2, and Plains.
That is why Haytham is the default sink — see the caveat below.

---

## What the measurement CANNOT say (state these with any result)

1. **Do not read "cut the interaction".** Swords to Plowshares (−0.257) and Unexpectedly Absent
   (−0.229) are the worst-scoring cards in WhiteKnights because the passive opponent presents nothing
   to target. No arm in this campaign cuts them, and no future arm should on this evidence.
2. **Aether Vial's real value is partly unmodelled.** The engine models the charge/deploy heuristic,
   but not flashing in creatures to dodge sorcery-speed answers or countermagic. A Vial cut is
   therefore *flattered* here. The 4th copy's −0.364 is still a very large in-model signal.
3. **Haytham Kenway is the most under-modelled sink.** Its protection and ETB exile are both inert in
   goldfishing (the opponent's creatures never block or attack). What remains — the +2/+2 Knight lord
   and the legend rule at 3 copies — *is* modelled, so arms that add Haytham are under-credited
   relative to real play, not over-credited. That is the safe direction for a cut and the wrong one
   for an add: read an arm that *adds* Haytham as a lower bound.
4. **Battle cry is the most goldfish-inflated number in the WhiteKnights ledger** (0.20 t), per the
   user: *"Accorder Paladin is notably better in goldfishing than in real play"* — a 3/1's toughness is
   free against an opponent that never blocks. Accorder Paladin is already at 4 and no arm moves it,
   but any conclusion that leans on battle-cry density inherits that bias.
5. **Acclaimed Contender's one residue is provably inert here** ("legendary artifact" is a
   supertype+type pair `etb_dig_subtypes` cannot express; neither deck holds one), so Contender cut
   arms are NOT flattered by a modelling gap. Its `Knight/Aura/Equipment` filter was widened this
   session for exactly this reason.
6. **A screen's `base` level is not the deck's strength.** The paired shuffle moves every opening hand.
   Quote deltas, never levels, and never diff a level against the ledger's 4.320.

---

## Run order and state

| # | screen | spec | state |
|---|---|---|---|
| 1 | Remote Farm, WhiteKnights (rf1–rf4) | `logs/wk_screen/rf_whiteknights.json` | **DONE + CONFIRMED — rf4 wins, see below** |
| 2 | Remote Farm, Knights (rf1–rf4) | `logs/wk_screen/rf_knights.json` | **DONE — do NOT add it, see below** |
| 3 | creature/count, WhiteKnights (14 arms) | `logs/wk_screen/cr_whiteknights.json` | **DONE + top two CONFIRMED** |
| 4 | creature/count, Knights (11 arms) | `logs/wk_screen/cr_knights.json` | **DONE + top two CONFIRMED** |
| 5 | COMBINATION, WhiteKnights (cumulative + leave-one-out) | `logs/wk_screen/comb_whiteknights.json` | **DONE + CONFIRMED — −0.136 t** |

### Screen 1 result — Remote Farm on WhiteKnights: ADOPT 4, and the ladder never turns over

20,000 paired games per cell, 15 cells in ONE pooled batch, d5/20 ms, `max_turns 18`,
`logs/wk_screen/out/rf_wk.log`. Negative = faster. Every one of the **12 cells is negative and
monotone in copies, in all three formats**:

| arm | Δ @20 | Δ @2HG | Δ @40 | late-weighted | ident |
|---|---|---|---|---|---|
| rf1 | −0.0097 | −0.0115 | −0.0054 | −0.0081 | 97.5% |
| rf2 | −0.0169 | −0.0184 | −0.0105 | −0.0142 | 95.5% |
| rf3 | −0.0237 | −0.0263 | −0.0146 | −0.0199 | 93.5% |
| **rf4** | **−0.0308** (t −14.7) | **−0.0344** (t −15.6) | **−0.0218** (t −9.8) | **−0.0274** | 91.6% |

**Held-out confirmation** (`--confirm rf4`, seed 1430000, disjoint from the screen's 930000,
same apparatus — `logs/wk_screen/out/rf_wk_confirm.log`). This is why the step is not optional:

| format | screen | held out | driver's verdict | USE |
|---|---|---|---|---|
| std | −0.0308 | −0.0248 | does **NOT** reproduce, shrinkage +0.0060 (t +2.04) | **−0.0248** (held-out) |
| 2hg | −0.0344 | −0.0308 | reproduces within noise | **−0.0326** (pooled, 40,000 games) |
| long | −0.0218 | −0.0202 | reproduces within noise | **−0.0210** (pooled, 40,000 games) |

**Late-weighted on the confirmed numbers: −0.0252 turns.** Modest in absolute terms — this deck's
keep table was worth −0.1985 and battle cry ~0.20 — but consistent, and free (it is a land swap).

**Two reasons to believe it beyond the t-statistic:**
1. **Clean dose-response across 4 arms × 3 formats.** An apparatus artifact does not produce a
   monotone ladder; `ident` also falls 97.5% → 91.6% as copies rise, so the arms genuinely diverge
   more with more copies.
2. **The `long−std` column is POSITIVE** (+0.0042 → +0.0092, t +3.4…+4.5), i.e. Remote Farm is worth
   *less* the longer the game. That is the signature of an **acceleration** card and it is what the
   mechanism predicts independently: `{W}{W}` off one land accelerates the `{W}{W}{W}` Benalish
   Marshal and the 4-drops, which matters most in a short race and decays as the race lengthens. The
   measurement and the card-level story agree without being fitted to each other.

**Stated limitation: the bias floor is UNMEASURED and cannot be measured on the approved route** (an
introduced card has no cells to reweight, and `--floor` would fall back to generating a table). At
−0.025 against a typical 0.005–0.01 floor the margin is ~2.5–5x, i.e. near the "treat under 3x as
unresolved" line. What replaces the measurement is structural, not statistical: the alias yields
**0.0000% fall-through on every arm**, so all arms run one table over an *identical* composition
space — the asymmetry a floor brackets is absent by construction here, not merely small. That is an
argument, and it is offered as one.

**Cost:** long::rf4 is 105 ms/game against std::base's 6.5 — 16.1x. Worth knowing before sizing any
follow-up at 40 life.

### Screen 2 result — Remote Farm on Knights: DO NOT ADD (the same card, the opposite answer)

Same ladder, same apparatus shape, 20,000 paired games per cell, `logs/wk_screen/out/rf_kn.log`:

| arm | Δ @20 | Δ @2HG | Δ @40 | late-weighted |
|---|---|---|---|---|
| rf1 | −0.0028 (t −2.6) | −0.0078 (t −6.9) | −0.0030 (t −2.7) | −0.0053 |
| rf2 | −0.0049 (t −3.3) | −0.0133 (t −8.5) | **−0.0019 (t −1.3)** | −0.0059 |
| rf3 | −0.0064 (t −3.6) | −0.0203 (t −10.8) | −0.0034 (t −1.9) | −0.0091 |
| rf4 | −0.0060 (t −2.9) | −0.0249 (t −11.6) | −0.0028 (t −1.4) | −0.0101 |

**Only the 2HG column is real.** At 20 life the effects are 0.003–0.006 t, i.e. at or below the
apparatus floor, and **the ladder does not order**: rf3 (−0.0064) beats rf4 (−0.0060), their direct
comparison being t = −0.35. At 40 life it is worse than that — rf2 is the *worst* arm and rf3 the
best, every |t| < 2.8. That is noise with a monotone-looking 2HG column next to it, and on the user's
late weighting (long 0.5) the verdict is a clear no.

**Why the same card splits the two decks — the mechanism, which was not fitted to the numbers:**

| | WhiteKnights | Knights |
|---|---|---|
| Plains | 22 of 22 lands | 8 of 20 |
| lands entering tapped | 0 | **0** — Unclaimed Territory, Secluded Courtyard and Tournament Grounds all enter untapped |
| colour fixing needed | none (mono-white) | R/W, and Plains **cannot** cast Inspiring Veteran or Haytham Kenway |
| free-deploy engine | 3 Aether Vial | **4 Aether Vial** |
| `{W}{W}{W}` payoff | 2 Benalish Marshal | 4 Benalish Marshal |

So Knights is the deck with *more* triple-white payoff and it still does not want the card, which rules
out "more Marshals ⇒ more value" as the driver. What separates them is that **mana is not Knights'
binding constraint**: its 12 nonbasics already fix without ever entering tapped, and 4 Vial deploys
around mana entirely — so a tapped land is a tempo cost it cannot recoup. WhiteKnights, at 22 untapped
Plains with real `{W}{W}{W}` and 4-drop demand, converts the same land into pure acceleration.

**And the 2HG exception fits the same story rather than contradicting it:** 2HG games run a full turn
longer (base 5.03 vs 4.35 at 20 life), which amortises the enters-tapped turn — the one condition
under which Knights can afford it. Note this is the reverse of WhiteKnights' `long−std` gradient,
where the card decays as the game lengthens; Knights needs length to pay the entry cost at all, and
then the 40-life column shows the gain does not keep growing.

**No held-out confirm was run for Knights, on purpose.** The arms that would need confirming are the
ones not being recommended, and the std/long results are nulls selected on nothing. If Knights is ever
tuned *for 2HG specifically*, `--confirm rf4` on that spec is the first thing to run.

### Screen 3 result — WhiteKnights counts: THE VALUE IS IN THE SINK, NOT THE CUT

42 cells (14 arms + base × 3 formats) in ONE pooled batch, 20,000 paired games each,
`logs/wk_screen/out/cr_wk.log`. The shipped table was kept on every arm — worst-arm composition
fall-through **1.19%** against the `max_fallback: 0.02` budget, which the driver prices at
**~0.00075 t of one-sided bias**.

**Group B is the whole finding.** Hold the cut fixed (Valiant Knight 1→0) and vary only where the
freed slot goes:

| the slot becomes | Δ std | Δ 2HG | Δ long | late-weighted |
|---|---|---|---|---|
| **2nd Adeline** | −0.0233 | **−0.0776** (t −32.8) | −0.0403 | **−0.0481** |
| 2nd Silverblade Paladin | −0.0190 | −0.0295 | −0.0257 | −0.0255 |
| a 23rd Plains | −0.0101 | −0.0028 | **+0.0058** | −0.0016 |
| 3rd Benalish Marshal | −0.0097 | −0.0004 | −0.0031 | −0.0037 |
| 4th Hero of Bladehold | +0.0065 | +0.0036 | +0.0089 | **+0.0072 WORSE** |

Same card leaving, a 13x spread in what replaces it. **A cut ladder alone would have found none of
this** ([[cut-ladders-never-ask-what-to-add]]): "cutting Valiant Knight is worth −0.004" and "cutting
Valiant Knight is worth −0.048" are the same cut.

**The rest of the screen:**

| arm | Δ std | Δ 2HG | Δ long | late | read |
|---|---|---|---|---|---|
| `a_ac1_bm4` Contender 3→1, Marshal 2→4 | −0.0244 | −0.0154 | −0.0226 | −0.0208 | best of group A |
| `a_av2_bm3` Vial 3→2, Marshal 2→3 | −0.0140 | −0.0078 | −0.0206 | −0.0154 | — |
| `a_ac2_bm3` Contender 3→2, Marshal 2→3 | −0.0140 | −0.0099 | −0.0149 | −0.0132 | half of ac1 |
| `a_lg0_bm3` cut Lightning Greaves | −0.0073 | −0.0080 | −0.0184 | −0.0131 | grows with length; matches Angels |
| `a_ke3_bm3` Exemplar 4→3 | −0.0037 | −0.0081 | −0.0110 | −0.0102 | — |
| `c_p21_bm3` Plains 22→21 | +0.0047 | +0.0048 | +0.0011 | +0.0031 | **don't** |
| `a_wk3_bm3` Worthy Knight 4→3 | +0.0013 | +0.0072 | +0.0040 | +0.0042 | **don't** |
| `a_db3_bm3` Bodyguard 4→3 | **+0.0179** | +0.0069 | +0.0063 | +0.0087 | **don't — see below** |
| `c_p20_bm4` Plains 22→20 | +0.0198 | +0.0204 | +0.0149 | +0.0173 | **don't** |

**Held-out confirmation of the top two** (`--confirm`, seed 2430000 vs the screen's 1930000). Both
reproduce and mostly GROW:

| arm | std | 2HG | long | late-weighted, confirmed |
|---|---|---|---|---|
| `b_vk0_ad2` | −0.0233 (pooled 40k) | **−0.0797** (pooled 40k) | −0.0432 (pooled 40k) | **−0.0502** |
| `a_ac1_bm4` | −0.0266 (pooled 40k) | −0.0154 (pooled 40k) | −0.0296 (held-out; screen's −0.0226 did not reproduce, held-out is LARGER) | −0.0247 |

#### The two results that contradict a card score, and why the screen wins

* **Dauntless Bodyguard's 2nd copy priced −0.202, the worst marginal in the deck — and cutting to 3
  measures +0.0179 WORSE at 20 life.** A `card_scores` marginal is a *hand-quality* fit against a
  passive goldfish; it prices how much a card improves a KEEP decision, not what the body does in the
  race. A 2/1 for `{W}` is 4 power across two slots at one mana each, and this deck's clock is bodies.
  Do not cut it. This is the concrete case for treating card scores as hypothesis generators only.
* **A 2nd Adeline is the biggest effect in the campaign, and she is LEGENDARY.** The second copy
  cannot add a second instance to the battlefield, so almost all of its value is *seeing her more
  often* — and its `long−std` of −0.0169 (t −9.69) says the value grows with game length, which is
  what a creature whose power equals your creature count should do. Compare
  [[rank-decklists-late-weighted]]'s note that the legend rule caps Lyra ArchDawn while Thune stacks:
  a legend being capped in play is not a reason to run one copy.

#### Two genuine cross-format SIGN FLIPS — the reason the 3-format axis is not decoration

* `b_vk0_p23` (a 23rd land): **−0.0101 at 20 life, +0.0058 at 40** (long−std +0.0159, t +7.87).
* `a_vk0_bm3`: −0.0097 → −0.0031, decaying (long−std +0.0066, t +4.26).

Ranking on 20-life speed would have adopted the 23rd land. It is the wrong card for a long game.

#### How to read the `d*` column (it is NOT "delta vs floor")

`d*` is **how badly scattered the shared table's rollout values would have to be for the apparatus to
have invented the effect**, against a measured reference of **0.054 t** (burn's Skullcrack→Bolt). Bigger
`d*` = safer. And the misfit is **one-way — it can only make an edit look SLOWER**. Consequences:

* every **faster** arm above is safe from this bias by sign alone; if anything it is understated;
* the **slower** arms share the bias's direction, so only those with `d*` above 0.054 are established:
  `a_db3_bm3` (0.074) and `c_p20_bm4` (0.078) are. `c_p21_bm3` (0.041) and `b_vk0_hob4` (0.051) are
  **NOT** — record those two "don'ts" as unresolved, not proven.

### Screen 4 result — Knights counts: one good change, and the campaign's biggest SIGN FLIP

36 cells in ONE pooled batch, 20,000 paired games each, `logs/wk_screen/out/cr_kn.log`.

| arm | Δ std | Δ 2HG | Δ long | late-weighted |
|---|---|---|---|---|
| **`k_ac2_hay4_ad2`** Contender 4→2, Haytham 3→4, **Adeline 1→2** | −0.0138 | **−0.0717** | −0.0257 | **−0.0471** |
| `k_av2_p10` Vial 4→2, Plains 8→10 | −0.0261 | +0.0053 | −0.0065 | −0.0069 |
| `k_av2_hay4_ad2` Vial 4→2, Haytham 4, Adeline 2 | +0.0043 | −0.0313 | +0.0037 | −0.0067 |
| `k_ac3_hay4` Contender 4→3 | +0.0009 | −0.0027 | +0.0022 | +0.0005 (null) |
| `k_ke3_hay4` Exemplar 4→3 | +0.0075 | +0.0029 | +0.0055 | +0.0051 |
| **`k_av0_p12`** cut ALL 4 Vial → 4 Plains | **−0.0364** | **+0.0331** | +0.0105 | **+0.0084** |
| `k_bm3_hay4` Marshal 4→3 | +0.0123 | +0.0077 | +0.0175 | +0.0134 |
| `k_av3_hay4` Vial 4→3 | +0.0077 | +0.0158 | +0.0149 | +0.0137 |
| `k_p6_hay4_ad2` Plains 8→6 | +0.0397 | −0.0188 | +0.0256 | +0.0151 |
| `k_wk3_hay4` Worthy Knight 4→3 | +0.0154 | +0.0137 | +0.0211 | +0.0175 |
| `k_p7_hay4` Plains 8→7 | +0.0236 | +0.0196 | +0.0230 | +0.0221 (worst) |

**`k_av0_p12` is the finding.** Cutting all four Aether Vial for Plains is the **best** arm at 20 life
(−0.0364, t −12.5) and among the **worst** at 2HG (+0.0331, t +10.4): `long−std = +0.0469, t +17.02`,
the largest cross-format swing in the campaign. Both blocks of the held-out confirm agree on all three
formats, so the flip is replicated, not noise. On the user's late weighting it is **+0.0084, i.e. a
rejection** — and ranking on 20-life speed alone would have cut all four Vials.

**Held-out confirmation** (seed 2460000 vs 1960000), both arms, all three formats reproducing:

| arm | std | 2HG | long | late-weighted, confirmed |
|---|---|---|---|---|
| `k_ac2_hay4_ad2` | −0.0146 | **−0.0719** | −0.0268 | **−0.0479** |
| `k_av0_p12` | −0.0353 | +0.0344 | +0.0103 | +0.0084 (reject) |

## THE CARD SCORES INVERTED THREE TIMES OUT OF THREE — read them as hypothesis generators only

Every case where a `card_scores` marginal made a confident prediction and the screen disagreed, the
screen won:

| the marginal said | the screen measured |
|---|---|
| Knights `Aether Vial` −0.113 / **−0.364** (worst card in either deck) | cutting it is good ONLY at 20 life; **worse** at 2HG and 40. Rejected on the late weighting |
| Knights `Plains` **negative on every copy** (−0.002 / −0.083 / −0.121) | cutting Plains is the single worst change tested: **+0.0236** (8→7), **+0.0397** (8→6) |
| WhiteKnights `Dauntless Bodyguard` 2nd copy **−0.202** | cutting 4→3 is **+0.0179 worse** at 20 life |

The mechanism is not mysterious and it is not a bug: a `card_scores` marginal is a fit against the
**hand-score / keep** decision. It prices how much holding a card improves a KEEP, against a passive
goldfish. It says nothing about what the card does in the race once kept. Plains scoring negative in
Knights is *correct* as a statement about keeps (it is the land that cannot cast Inspiring Veteran);
it is silent on the fact that the deck still needs to hit its land drops. Use the marginals to choose
what to measure — which is exactly what they were used for here, and they picked good candidates —
and never as a finding.

Each is ONE pooled `mtg --batch` (arms × formats in one queue, never a wave per arm or per format).
Screens run **serially** — two concurrent screens would halve the box and corrupt each other's
per-arm ms/game, which is the figure used to size the next run.

**Floor brackets.** `--floor` is available and free for **cut-only** arms (the default bracket
re-weights the shipped R=40/R=60 raw to the arm's counts — deterministic, 1.5 s, no generation). It is
**unavailable** for any Remote Farm arm (an introduced card has no cells to reweight) and for the raise
arms (a bucket past the source grid), where it would fall back to *generating* a table — which the
approved route forbids. So: bracket the cut arms, and report the Remote Farm floor as **unmeasured**,
noting only that the alias makes every arm share one table over an identical composition space, which
bounds the asymmetry by construction rather than by measurement.

**The winner of any multi-arm screen is selection-biased** — confirm it with
`--confirm <tag>` on the disjoint held-out block before recommending anything.

### Screen 5 result — the changes STACK, and the combination is worth −0.136 t

Cumulative ladder plus three leave-one-out arms, 24 cells in ONE pooled batch, 20,000 paired games
each, `logs/wk_screen/out/comb_wk.log`. Monotone at every step:

| arm (cumulative) | Δ std | Δ 2HG | Δ long | late-weighted |
|---|---|---|---|---|
| `c1_ad2` Valiant Knight 1→0, Adeline 1→2 | −0.0209 | −0.0794 | −0.0341 | −0.0451 |
| `c2_ac1bm4` + Contender 3→1, Marshal 2→4 | −0.0494 | −0.1023 | −0.0631 | −0.0721 |
| `c3_rf4` + Plains 22→18, Remote Farm 0→4 | −0.0877 | −0.1462 | −0.0898 | −0.1063 |
| **`c4_all`** + Greaves 1→0, Silverblade 1→2 | **−0.1035** (t −31.0) | **−0.1711** (t −42.7) | **−0.1268** (t −32.1) | **−0.1354** |

**Held-out confirmation** (seed 3430000 vs 2930000) reproduces in all three formats with shrinkage
indistinguishable from zero (t +0.58 / −0.90 / +0.17). Pooled over 40,000 paired games per format:
**std −0.1022, 2HG −0.1737, long −0.1263 → late-weighted −0.1357.**

**No interference between the changes.** Each leave-one-out arm gives that change's marginal value
*inside* the full list, and each matches its standalone measurement:

| change | marginal in the full list | standalone | verdict |
|---|---|---|---|
| Valiant Knight → 2nd Adeline | −0.0480 | −0.0502 | holds |
| 4 Remote Farm for 4 Plains | −0.0321 | −0.0252 | **slightly better in context** |
| Contender 3→1 + Marshal 2→4 | −0.0256 | −0.0247 | holds |
| Greaves → 2nd Silverblade | −0.0291 | not measured as a pair alone | (c3→c4 step) |

For scale: **−0.136 t is in the same class as this deck's exhaustive keep table (−0.1985 t)**, which
was the largest single gain ever measured on it — and far above battle cry's 0.20 t for a whole card
mechanic. Composition fall-through on the widest arm is 2.75% (three 1-of raises stacked), ~0.0017 t of
one-sided bias, i.e. two orders of magnitude below the effect.

### The candidate list (NOT created as a deck folder — screening lists are never committed)

Per [[never-commit-screening-lists]] this stays here until the user adopts it; on adoption it becomes
`decks/WhiteKnights/` with the predecessor archived as `decks/WhiteKnights/v1-<slug>/` **and its
references moved to `references/WhiteKnights/v1-<slug>/`**, then earns its own mulligan profile, value
leaf and regression ground truth (a screen's number is a ranking, not that deck's strength).

| n | card | change |
|---|---|---|
| 18 | Plains | **22 → 18** |
| 4 | Remote Farm | **new** |
| 4 | Dauntless Bodyguard | — (cutting it measured WORSE, +0.0179) |
| 4 | Venerable Knight | — |
| 4 | Worthy Knight | — (cutting it measured worse, +0.0042) |
| 1 | Acclaimed Contender | **3 → 1** |
| — | Valiant Knight | **1 → 0** |
| 4 | Knight Exemplar | — |
| 3 | Hero of Bladehold | — (a 4th measured worse, +0.0072) |
| 1 | Sol Ring | — |
| 1 | Swords to Plowshares | — **deliberately never tested** (goldfish cannot price interaction) |
| 1 | Unexpectedly Absent | — same |
| 4 | Accorder Paladin | — (already 4; battle cry is the most goldfish-inflated number here) |
| 2 | Silverblade Paladin | **1 → 2** |
| 2 | Adeline, Resplendent Cathar | **1 → 2** |
| 3 | Aether Vial | — (see the open axis below) |
| — | Lightning Greaves | **1 → 0** |
| 4 | Benalish Marshal | **2 → 4** |

60 cards, still **mono-white**.

**Knights' equivalent**, confirmed at −0.0479 late-weighted: Acclaimed Contender 4→2, Haytham Kenway
3→4, Adeline 1→2. Remote Farm does **not** go in this list.

### The one axis left open

**Aether Vial 3→2 in WhiteKnights.** It measured −0.0154 standalone, but in that arm the freed slot
went to a 3rd Benalish Marshal — and Marshal is already at 4 in `c4_all`, so the sink is taken and the
change was never tested on top of the winning list. A follow-up needs a different destination for that
slot (a 19th land, Contender back to 2, or an untested Adeline 2→3). Everything else in the pool has
been measured in both directions.

## Open questions for the user (surfaced, not blocking)

1. **Ranking weights.** The Angels campaign ranked arms late-weighted — `long` 0.5 / `2hg` 0.3 /
   `std` 0.2, *"not by 20-life speed"* (user, 2026-09-19). Applied here by default; all three per-format
   deltas are reported raw so it can be re-weighted.
2. **Do both lists stay?** `Knights` and `WhiteKnights` share a Knight core and the ledger's open
   question 4 already asks this. Optimising copy counts in both assumes both stay.
3. **Knights' sideboard is full of main-deck candidates** — Hero of Bladehold, 2 Accorder Paladin,
   Kinsbaile Cavalier, Student of Warfare, Valiant Knight. Promoting any is an *introduced card* for
   that list and needs its own alias; it is out of scope for screens 1–4 and would be screen 5.
