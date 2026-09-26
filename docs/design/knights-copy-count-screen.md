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
| 6 | open axes on top of `c4_all`, WhiteKnights | `logs/wk_screen/d_whiteknights.json` | **DONE** |
| 7 | DECOMPOSITION of the Knights winner | `logs/wk_screen/e_knights.json` | **DONE** — it was one change, not three |
| 8 | round F, WhiteKnights (stacking screen 6's wins) | `logs/wk_screen/f_whiteknights.json` | **DONE + CONFIRMED — −0.2417 t** |

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

This is `f_ad4_av0_ac0`, the confirmed round-F winner at **−0.2417 late-weighted**:

| n | card | change | evidence |
|---|---|---|---|
| 20 | Plains | **22 → 20** | 22 was optimal on the shipped list; the Vial cut moved the total |
| 4 | Remote Farm | **new** | rf ladder monotone in 3 formats; d* > 0.84 t |
| 4 | Dauntless Bodyguard | — | cutting it measured **+0.0179 WORSE** |
| 4 | Venerable Knight | — | (Knights' analogue cut was a null/worse) |
| 4 | Worthy Knight | — | cutting it measured +0.0042 worse |
| — | Acclaimed Contender | **3 → 0** | the best cut source at every step |
| — | Valiant Knight | **1 → 0** | its `{3}{W}{W}` grant is near-unaffordable |
| 4 | Knight Exemplar | — | 4→3 was −0.0102 on the OLD list; **untested on this one** |
| 3 | Hero of Bladehold | — | a 4th is **+0.0037 worse**; 3→2 untested |
| 1 | Sol Ring | — | |
| 1 | Swords to Plowshares | — | **deliberately never tested** — goldfish cannot price interaction |
| 1 | Unexpectedly Absent | — | same |
| 4 | Accorder Paladin | — | already 4; battle cry is the most goldfish-inflated number here |
| 2 | Silverblade Paladin | **1 → 2** | a 3rd is only −0.0071 |
| **4** | **Adeline, Resplendent Cathar** | **1 → 4** | **−0.048 / −0.044 / −0.024 per copy** |
| — | Aether Vial | **3 → 0** | **−0.0423**, no sign flip (unlike Knights) |
| — | Lightning Greaves | **1 → 0** | matches the reference bench and the Angels campaign |
| 4 | Benalish Marshal | **2 → 4** | |

60 cards, 24 lands, still **mono-white**. Twelve cards differ from the shipped list.

**Knights' equivalent**: screen 7 decomposed the confirmed −0.0479 arm and showed Haytham's 4th copy
contributes nothing, so the change is **Acclaimed Contender 4→2, Adeline 1→3** —
**held-out confirmed (seed 4460000) at std −0.0285 / 2HG −0.1316 / long −0.0539, pooled over 40,000
paired games per format → late-weighted −0.0721.** Do **not** cut Venerable Knight, and Remote Farm
does **not** go in this list.

### Screens 6–8 — Adeline is the deck's most underplayed card, and Vial should go

**Screen 6** (`logs/wk_screen/d_whiteknights.json`, out `d_wk.log`) tested the axes left open on top of
`c4_all`, which it also re-measured as a reference: **−0.1371 late-weighted against the earlier run's
−0.1357, replicating on a third disjoint seed block.** Marginals vs `c4_all`:

| arm | late-weighted | marginal |
|---|---|---|
| `d_ad3_ac0` Contender 1→0, **Adeline 2→3** | −0.1811 | **−0.0440** |
| `d_av2_ad3` Vial 3→2, **Adeline 2→3** | −0.1808 | −0.0437 |
| `d_av0_p21` **cut all 3 Vial** → Plains 21 | −0.1733 | −0.0362 |
| `d_sp3_ac0` Contender 1→0, Silverblade 2→3 | −0.1580 | −0.0209 |
| `d_av2_p19` Vial 3→2, Plains 19 | −0.1525 | −0.0154 |
| `d_av2_ac2` Vial 3→2, Contender 1→2 | −0.1383 | −0.0012 (nothing) |

**Round F** (`f_whiteknights.json`, out `f_wk.log`) stacked those. Matched-pair marginals: cutting all
3 Vial for Plains **−0.0423**, a 4th Adeline **−0.0242**, a 3rd Silverblade only −0.0071, a 4th Hero
**+0.0037 still worse**. Winner `f_ad4_av0_ac0` at **−0.2418**, held-out confirmed (seed 5430000) with
shrinkage indistinguishable from zero in all three formats — pooled **std −0.1718 / 2HG −0.3360 /
long −0.2131 → late-weighted −0.2417 over 40,000 paired games per format.**

**That is larger than this deck's exhaustive keep table (−0.1985) — the biggest improvement measured on
it by any means.**

**The Adeline ladder, and it is the campaign's headline:**

| copies | marginal (late-weighted) |
|---|---|
| 1 → 2 | **−0.048** |
| 2 → 3 | **−0.044** |
| 3 → 4 | **−0.024** |

Diminishing but strongly positive all the way to the legal maximum, for a **legendary** creature whose
extra copies cannot add a second battlefield instance. Nearly all the value is *seeing her earlier*.
**Screen 7 on Knights (`e_knights.json`) reproduces this independently**: its confirmed 3-card winner
decomposes to ONE change — `e_ac3_ad2` (Contender 4→3, Adeline 1→2) measures −0.0402 against the
3-card arm's −0.0405, so **Haytham's 4th copy and the second Contender cut contribute nothing** — and
a 3rd Adeline again nearly doubles it (−0.0714). Knights' cut source matters too but far less:
Contender −0.0402 > Exemplar −0.0368 > Vial −0.0254 >> **Venerable Knight 0.0001, a null** (do not cut
it; +0.0314 at 20 life).

**Aether Vial: cut it in WhiteKnights, keep it in Knights.** WhiteKnights' Vial cut is −0.0423 with
*no* sign flip (−0.0440 / −0.0322 / −0.0355 across std/2HG/long). Knights' is the campaign's biggest
sign flip and was rejected. Same card, third case of the two decks wanting opposite things.

**A fourth card-score inversion:** in Knights, cutting Acclaimed Contender beats cutting Aether Vial
(−0.0402 vs −0.0254) even though Vial's marginal (−0.364) is far worse than Contender's. See
[[card-scores-invert-against-play]].

### The apparatus question is CLOSED by d*, and an earlier caveat here was too pessimistic

**Correction to the screen-1 note above.** It said Remote Farm's bias floor "cannot be measured on the
approved route". That is true of `--floor` (the empirical bracket, which would have to *generate*), but
`d*` asks the same question analytically and was available all along. It was missing from every Remote
Farm screen for a mundane reason: `deck_compare` guards the block on `raw_sidecar(spec.profile)`, which
resolves `.raw.json.gz` directory-relative off the **profile** path, and the alias scratch dir held only
the profile, value model and table. Five screens ran without it. `setup_alias.sh` now copies the raw
sidecar too; `logs/wk_screen/dstar.py` recomputes it after the fact (the alias does not touch the raw,
so it is the same number the driver would have printed).

Computed against the 0.054 t measured reference (burn's Skullcrack→Bolt), bigger = safer:

| arm | effect | d* | verdict |
|---|---|---|---|
| `f_ad4_av0_ac0` | −0.2417 | **> 0.84 t** | the bound never reaches the effect — **apparatus cannot explain it** |
| `f_ad3_av0_ac0` | −0.2176 | > 0.84 t | same |
| `d_ad3_ac0` | −0.1753 | > 0.84 t | same |
| `c4_all` | −0.1371 | > 0.84 t | same |
| `c1_ad2` | −0.0451 | 0.133 t | safe (2.5x the reference) |
| `rf4` | −0.0252 | > 0.84 t | same |
| `rf2` | −0.0142 | 0.158 t | safe |
| `rf1` | −0.0081 | 0.050 t | below the reference — could be apparatus (not a recommendation) |

So for `rf4` the accounting is now **complete**: the weighting half is reproduced exactly by
reweighting, composition coverage is measured at **0.0000%**, and the rollout half is bounded with room
to spare. The only arm the apparatus could account for is `rf1`, which nothing recommends.

### Where this STOPS being copy-count tuning

`ident` falls to **59–73%** on the round-F arms and the candidate differs from the shipped list in 12 of
60 cards. Two biases still run **against** the candidate, which is the safe direction — the shared keep
table was fitted to the SHIPPED library, and composition fall-through (7.85% on the winner, the highest
in the screen, vs 4.90% on the arm below it) penalises the arm holding the most raised copies. The
winner won anyway, by 0.024. But further rounds under this apparatus buy less and less: the honest next
step is not another screen, it is generating a real mulligan profile and value leaf for the candidate
and measuring it standalone — which is the **adoption** path and the user's decision.

> **Superseded by round G (below), and the "safe direction" reading was right but incomplete.** The
> keep-table half of that next step was authorised by the user and has now been done: every arm plays a
> table generated for its own counts. The two biases did run against the candidate — worth **0.039 t** in
> total — so the shared apparatus was *understating* the effect, not producing it. What is still
> outstanding from this paragraph is the **value leaf**, which round G does not touch: every arm still
> plays the shipped list's `.value.json`. That remains part of adoption.

### The one axis left open

~~**Aether Vial 3→2 in WhiteKnights.**~~ **CLOSED by screens 6 and F**: cutting Vial to 2 is −0.0154
with a 19th Plains and ~0 with a 2nd Contender, and cutting it to **0** is −0.0423. Vial comes out of
WhiteKnights entirely.

What is left untested on the *final* candidate, in rough order of promise: **Knight Exemplar 4→3**
(−0.0102 on the shipped list, never retested on the new one), **Hero of Bladehold 3→2** (only 3→4 was
tested, and it was worse), and the land count at 24 (20 Plains + 4 Remote Farm; the shipped list's 22
was optimal, but the Vial cut changed the mana-source total). Each is a small effect against an
apparatus that is now stretched, which is the argument for generating real artifacts before chasing
them.

> **ALL THREE ARE NOW CLOSED by round G**, on real per-arm tables and held-out confirmed:
> **Knight Exemplar 4→3 ADOPT** (it is the worst card in the list), **Hero of Bladehold 3→2 a null**
> (stay at 3), **land count 24 CORRECT** (25 is worse, 22–23 is worth nothing net of the card that
> replaced the land). The argument in the last sentence was the right one: generating the artifacts first
> is what turned a −0.0102 hint into a replicated result.

## Round G — the real-table round, and the alias was hiding something

**User mandate, 2026-09-26:** *"If you need to generate a new mulligan profile or two, you probably can,
since the cost is relatively low. Just try to do it primarily for cases that are difficult to represent,
like type changes or significant sets of changes."* And, a few minutes later: *"Land count is one of
those things we'll likely need multiple profiles for."*

That is exactly right, and it names the reason rounds A–F could not answer the land question at all: the
exhaustive keep table's **buckets ARE the land/spell partition**, and its cells are compositions over
them. A table fitted to a 22-Plains list does not merely weight a 20-Plains list slightly wrongly — it
is a different grid.

### The route (and why it needs no new authority)

`scripts/deck_compare.py --floor <tags>` with **`"bracket": "generate"`** already does this: it calls
`gen_table` per arm and builds a keep table **for that arm's own counts**, next to that arm's decklist
under `logs/deckcmp/<Deck>/<tag>/`. Three things make it the right instrument here rather than
`mullgen.sh`:

* **It is the approved screening route**, so no `MTG_ALLOW_UNTESTED_DECK=1` is involved — that gate
  lives in `mullgen.sh`/`valueleaf.sh`, and this path calls `mtg-analyze` directly. Nothing had to be
  waived, and no candidate list went into `decks/` ([[never-commit-screening-lists]]).
* **`floor_R` is set to each deck's SHIPPED `effective_R`** — 40 for WhiteKnights, 60 for Knights —
  instead of the driver's default 10. A default-R bracket is the "throwaway low-R table that plays
  ~0.032–0.06 t weaker", which is the single biggest reason a generated bracket *overstates* its floor.
  At the shipped R the `own` column stops being only a bound and becomes a **result**: each list
  measured on a table fitted to itself, i.e. as it would actually ship.
* **The tables are generated once and reused across all three formats.** `spec.out` is keyed on the
  base decklist's basename, not the spec filename, so three single-format specs over one base share
  `logs/deckcmp/WhiteKnights/` and `gen_table` reuse (keyed on the arm's exact counts + R) fires for
  formats two and three. Three specs are necessary because `floor()` calls `spec.job(..., fmt="")` and
  therefore cannot span a named `formats` map — there is no multi-format bracket in this driver.

Measured generation cost on this box (32 cores, saturated — load average 32.4/32): **~10 min** for the
candidate's K=12 table, **~18 min** for the shipped list's K=14 one. "Relatively low" was correct.

### The timing is unusually clean

`git log 083c4108..HEAD -- src/` is **empty** — zero engine commits since WhiteKnights' shipped keep
table was generated. So for this deck the driver's standard *"NOTE this bracket mixes engine states"*
warning is a false positive in the only sense that matters: the generated tables and the shipped one
come from the same engine. That is not true of Knights, whose table dates to `1b3c94fb` (2026-07-14),
**1,328 `src/` commits back** — see the Knights subsection.

### THE BUCKET STRUCTURE CHANGES SHAPE, AND THAT IS ITSELF THE HEADLINE

The candidate's discovery found **K=12**, against the shipped list's **K=14**. The guard in
`ExhaustiveKeep.cpp:905` refused until `MTG_KEEP_ACCEPT_K=1` was set, because `gen_table` copies the
*shipped* deck's `.value.json` in beside the arm and that file carries `expected_buckets = 14` — a
contract belonging to a different decklist. **This is not a bucket ruling** and
[[bucket-ruling-is-user-only]] is not in tension with it: no `<stem>.buckets.json` was installed, no
`expected_buckets` was edited anywhere, and K=12 is *K as discovered*, which that same note names as the
correct default. It happened in an isolated scratch directory, which is the recipe the note prescribes.

| | shipped list (K=14) | candidate (K=12) |
|---|---|---|
| 0 | Lightning Greaves + Swords + Unexpectedly Absent | Swords + Unexpectedly Absent |
| 1 | Dauntless Bodyguard + Venerable Knight | Dauntless Bodyguard + Venerable Knight |
| | Acclaimed Contender · Aether Vial · Valiant Knight | — *(all three cut)* |
| | Plains | Plains |
| | *(Remote Farm had no bucket; the alias put it in Plains)* | **Remote Farm — ITS OWN BUCKET** |

14 − 3 cut buckets + 1 new bucket = 12. The hand space goes from **68,377 distinct hands to 39,774**, a
42% reduction — so at equal R the candidate's table is both cheaper to build and better covered per unit
of generation cost. That is a real shippable property of the list, not an artifact.

### The alias was a mis-specification, and it ran in five screens

**Equivalence discovery put Remote Farm in a bucket of its own — it is NOT behaviourally equivalent to
a Plains.** Every Remote Farm screen in this campaign (screens 1, 2 and the combination rounds) used
`scripts/alias_card_into_bucket.py` to file it under Plains, because that was the only way to give an
introduced card a bucket without generating. The card is *played* correctly either way — the
implementation is unaffected — but the keep/mulligan DECISION on a hand holding one was made by looking
up the cell for that many **Plains**, and Remote Farm enters tapped, carries two depletion counters and
sacrifices itself. Those are exactly the properties a keep decision cares about.

**Which direction that error runs, and why the earlier results survive it.** A mis-specified cell value
degrades decision quality; it does not systematically flatter. The arm keeps hands it should have
mulliganed, and the arm holding the mis-bucketed card is the only one that pays. So the alias is a
**one-sided handicap on the Remote Farm arms** — the same one-way structure as the `d*` misfit — and
those arms won anyway. Round G removes the handicap instead of bounding it, which is the difference
between [[isolate-the-axis-dont-difference-decks]]'s "measure the conclusion" and bounding the bias.

**The generate route also retires the alias entirely for future screens.** An introduced card gets its
own bucket by construction when the table is generated for the arm that holds it, so
`alias_card_into_bucket.py` is only needed when one wants to avoid generating.

### What round G measures

One spec per format (`logs/wk_screen/g_whiteknights_{long,2hg,std}.json`, built by `mkspec_g.py`),
base = the **shipped** list, six arms, 20,000 games each, seed block 6.2M, `max_turns` 18. Read with
`logs/wk_screen/gread.py`.

| arm | what it is | lands |
|---|---|---|
| `g_cand` | the confirmed round-F winner `f_ad4_av0_ac0` | 24 |
| `g_l22` | Plains 18, Silverblade 4 | 22 |
| `g_l23` | Plains 19, Silverblade 3 | 23 |
| `g_l25` | Plains 21, Silverblade 1 | 25 |
| `g_ke3` | Knight Exemplar 4→3, Silverblade 3 | 24 |
| `g_hob2` | Hero of Bladehold 3→2, Silverblade 3 | 24 |

**Silverblade Paladin is the swap partner on purpose.** A land count cannot be screened alone — 61 cards
is not a deck, so every land added is a spell cut and the measured effect is always the *pair*. The
partner therefore wants the flattest marginal available, and Silverblade's 3rd copy measured **−0.0071**
in round F, the smallest live marginal left in the list. Read `g_ke3` and `g_hob2` net of that −0.0071 to
recover the copy axis alone.

**The arm-vs-arm arithmetic on the `own` column is exact, and worth being precise about.** For two arms
X and Y of one invocation, `own(X) − own(Y) = X@own_X − Y@own_Y`, because `base@own_base` is literally
the same job on the same 20,000 game indices and cancels rather than merely averaging out. The point
estimate of every ladder step is therefore exact; its standard error is *not* recoverable from the
printed per-arm ses (they share the base cell's variance, so `sqrt(se_X² + se_Y²)` overstates it), and
`gread.py` labels that column a **bound**, never the se.

Two biases are known to run against the raised-count arms and are left in place because they are
one-sided in the safe direction: `g_ke3`/`g_hob2` raise Silverblade to 3 against a shipped table that
enumerates 0..1, so **12.79%** of their hands fall through on the *shared* column (hence
`max_fallback: 0.15`, which exists only to stop the driver swapping in a pool table and breaking
comparability with rounds A–F). Their `own` column has no fall-through at all, by construction — which
is why only the `own` column is read for those two.

### Round G headline — the real table makes the candidate BETTER, by 0.039 t

`logs/wk_screen/out/g_wk_long_headline.log`, `long` (40 life), 20,000 paired games, seed block 6.2M:

| apparatus | delta | se | ident |
|---|---|---|---|
| shared (aliased shipped) table | −0.2100 | 0.0050 | 68.0% |
| **each arm's own R=40 table** | **−0.2487** | 0.0053 | 63.8% |
| apparatus bias | −0.0386 | 0.0046 | **t = −8.31** |
| floor = \|bias\| + 2se | 0.0479 | | effect/floor **4.39x** |

Three things to take from this, in order of importance.

**1. The shared apparatus was UNDERSTATING the candidate, not inventing it.** Round F's held-out `long`
figure was −0.2131 and round G's shared column is −0.2100 on a fourth disjoint seed block — a clean
replication. Moving to real per-arm tables then moves the effect *further from zero*, to −0.2487. So the
apparatus was never the source of the result; it was masking about a sixth of it. The `d*` bound
("> 0.84 t — the apparatus cannot explain it") is now superseded by a direct measurement that agrees and
goes one better: it says which way the residual ran.

**2. The per-arm nulls are the cleanest control this campaign has produced.**

| null (own table vs shared) | value | reading |
|---|---|---|
| base | **−0.00135 ± 0.00306** | the freshly generated R=40 table for the shipped list is **indistinguishable from the shipped table itself** |
| `g_cand` | **−0.03995 ± 0.00365** | the candidate plays 0.040 t better on a table built for it |

The base null being zero is the load-bearing check. It says the generation route *reproduces the shipped
artifact's strength* — as it should, given 0 `src/` commits and matching R=40 — so the two columns' levels
are directly comparable and the whole −0.0386 bias is attributable to the candidate's side alone. A
bracket where the control null is non-zero cannot say that.

**3. The alias's error is now measured, and it ran the way predicted.** 0.040 t, against the arm holding
Remote Farm. That is the concrete cost of filing a tapped, self-sacrificing, depletion-countered land
under the Plains bucket for the keep decision: not a bias that flattered the new card, a **handicap** on
the arm that played it. Five screens carried it, and the candidate won all of them anyway.

### WHERE the gain comes from — it is a tail collapse, not a faster nut draw

A mean says how much, never where. The per-game data (`logs/wk_screen/dist.py` over the preserved batch
stderr — `run_batch` rewrites `floor.err` per invocation, so `snap_floor.sh` copies each one aside) gives
the whole curve, both decks on their own tables, `long`, 20,000 paired games:

| win turn | 4 | **5** | **6** | 7 | 8 | 9 | 10 | 11 | unwon |
|---|---|---|---|---|---|---|---|---|---|
| shipped list | 671 | 12,585 | 5,476 | 997 | 197 | 57 | 15 | 2 | **0** |
| **candidate** | 839 | **16,264** | **2,368** | 416 | 90 | 14 | 9 | 0 | **0** |

**Nothing goes unwon** — across all 280,000 games of the six-arm run the slowest win is turn 12 against an
18-turn cap, so the loss-penalized mean (`max_turns + 1` for an unwon game) is doing no work at all here
and every delta in this round is a pure win-turn delta. Worth stating because it is not generally true;
on other decks the penalty carries part of the effect.

**The candidate converts turn-6 kills into turn-5 kills and cuts the turn-7-and-later population by
~60%.** Turn-4 wins — the nut draws — improve only modestly (671 → 839). Per game: **28.1% faster, 63.7%
identical, 8.1% slower**, a 3.5:1 ratio, with the shift histogram concentrated at ±1 turn (4,641 one-turn
gains against 1,394 one-turn losses).

**That distribution is the strongest form this result could have taken.** The gain sits in mana
consistency and deployment — the stumbling games — which is the part of the curve a goldfish simulator
models most reliably. It is *not* concentrated in the nut-draw ceiling, which is exactly where
goldfishing is known to overstate an all-in deck ([[goldfish-bias-has-two-readouts]], and the Stompy
lesson that the sim overstates all-in deploy). An edit that won by raising its best-case would deserve
the goldfish discount; this one does not.

### A free secondary finding: the candidate is 39% cheaper to simulate

Per-job core-ms, from the batch result table the driver does not report
(`logs/wk_screen/out/floor_jobs.log`, captured by `snap_out.sh`):

| cell | ms/game | digest |
|---|---|---|
| shipped list on its own table | **68.1** | `5d7b4ae6a8e4da9f` |
| candidate on its own table | **41.7** | `35cfb1b1be7c6da3` |
| shipped list on the shared table | 81.2 | `583279701d9ad226` |
| candidate on the shared table | 56.2 | `90f14384734bcadb` |

Cutting three Aether Vial is most of it — Vial's activation branching is what the search pays for, and it
is also why the shipped list took **32 minutes** to build a keep table against the candidate's **10**.
This matters beyond tidiness: `suite_gate.py --cost` gates suite membership on per-game core-ms at the
deck's worst searched case under the **3x rule**, so an adopted candidate would be a cheaper suite member
than the list it replaces, not a more expensive one.

Two smaller notes from the same table. The shipped list is *pricier on its own generated table than on
the shipped one* (68.1 vs 81.2 — the other direction) while landing at the same mean (5.3853 vs 5.3866)
and a different digest, so the two tables of that deck disagree on some keeps without either being
better. And the candidate's 56.2 → 41.7 saving on moving to its own table is the 7.85% fall-through going
away: those hands were dropping to the heuristic fallback, which the skill prices at ~22x per game.

### Round G, all six arms, all three formats — NO sign flips anywhere

`own` column = each arm on a keep table generated for its own counts, R=40, 20,000 paired games per
format, seed block 6.2M. Late-weighted long 0.5 / 2hg 0.3 / std 0.2.

| arm | long | 2hg | std | **late-wtd** | on the shared table |
|---|---|---|---|---|---|
| **`g_ke3`** Exemplar 4→3, Silverblade 3 | −0.2654 | −0.4148 | −0.2460 | **−0.3063** | −0.2579 |
| `g_l22` 22 lands, Silverblade 4 | −0.2572 | −0.4018 | −0.2376 | −0.2967 | −0.2338 |
| `g_hob2` Hero 3→2, Silverblade 3 | −0.2524 | −0.4056 | −0.2422 | −0.2963 | −0.2510 |
| `g_l23` 23 lands, Silverblade 3 | −0.2560 | −0.4011 | −0.2376 | −0.2959 | −0.2419 |
| `g_cand` the round-F candidate | −0.2487 | −0.3956 | −0.2337 | −0.2898 | −0.2436 |
| `g_l25` 25 lands, Silverblade 1 | −0.2326 | −0.3796 | −0.2258 | −0.2753 | −0.2354 |

**The candidate is −0.2898 late-weighted on real tables, against −0.2436 on the shared one** — and that
shared figure independently replicates round F's −0.2417 on a fourth disjoint seed block. So the real
apparatus makes the candidate **19% better**, in the same direction in all three formats. The control
null is clean in all three too: base measures **−0.00135 / −0.00320 / +0.00070** against the shipped
table, i.e. zero everywhere.

Every arm's bias runs in the expected direction (each table flatters the deck it was fit to) at
t = −7.0 to −18.2, and every arm clears its floor by 2.0–5.9x.

### The land count: FLAT from 22 to 24, clearly wrong at 25

Ladder steps on the `own` column (exact point estimates — the base cell is one job and cancels; the last
column is an upper *bound* on the se, not the se):

| step | long | 2hg | std | **late-wtd** | se bound |
|---|---|---|---|---|---|
| 22 lands (Silverblade 4) − 24 | −0.0085 | −0.0062 | −0.0039 | **−0.0069** | 0.0047 |
| 23 lands (Silverblade 3) − 24 | −0.0073 | −0.0055 | −0.0039 | **−0.0061** | 0.0046 |
| **25 lands (Silverblade 1) − 24** | **+0.0161** | **+0.0160** | **+0.0079** | **+0.0144** | 0.0046 |

**25 lands is worse by +0.0144 — 3.1x the se bound and the same sign in all three formats. Do not go up.**

The 22 and 23 rungs measure *better* by ~0.006–0.007, but that is the whole point of the Silverblade
confound and it has to be read honestly: those arms gained Silverblade copies, and a 3rd Silverblade
measured −0.0071 on its own in round F. The apparent gain from cutting lands is therefore **fully
accounted for by the card that replaced them**. There is no evidence that the 24-land mana base is wrong,
and the 25-land rung says positively that it is not too small.

**The matched pairs settle it without leaning on round F at all.** `g_l23`, `g_ke3` and `g_hob2` all hold
**Silverblade 3**, so Silverblade cancels exactly between them and the difference is purely *which card
paid for it*:

| what paid for the 3rd Silverblade | late-wtd vs `g_cand` | vs paying with a Plains |
|---|---|---|
| **a Knight Exemplar** (`g_ke3`) | **−0.0166** | **−0.0105** |
| a Hero of Bladehold (`g_hob2`) | −0.0066 | −0.0005 |
| the 24th land (`g_l23`) | −0.0061 | — |

So: **the 4th Knight Exemplar is the worst card in the candidate list.** Cutting it for a 3rd Silverblade
Paladin is worth **−0.0166 late-weighted**, consistent in all three formats (−0.0167 / −0.0192 / −0.0123)
at 3.6x the se bound, and it beats paying with a land by 0.0105. Net of Silverblade's own −0.0071 that
puts **Knight Exemplar 4→3 at about −0.0095 — which replicates the −0.0102 measured on the shipped list**
in an earlier round, by an independent route.

Hero of Bladehold 3→2 is a **null** (−0.0005 against the land, after Silverblade cancels). Combined with
the earlier finding that a 4th Hero is +0.0037 worse, **3 is right and the axis is closed.**

### The updated candidate

`g_ke3` — the round-F list with **Knight Exemplar 4→3 and Silverblade Paladin 2→3** — measures
**−0.3063 late-weighted** on its own table. That is **1.54x this deck's exhaustive keep table (−0.1985)**,
and it is 13 of 60 cards changed from the shipped list.

### Held-out confirmation — and the winner got BIGGER, not smaller

Seed block 6.6M, same six arms, same generated tables (`gen_table` reuse is keyed on the arm's counts + R,
not the seed, so the confirmation cost only its batches). Pooled over both blocks = **40,000 paired games
per format** (`logs/wk_screen/pool_g.py`):

| arm | long | 2hg | std | **pooled late-wtd** | step vs `g_cand` |
|---|---|---|---|---|---|
| **`g_ke3`** | −0.2638 | −0.4115 | −0.2412 | **−0.3036** | **−0.0184** |
| `g_hob2` | −0.2470 | −0.3997 | −0.2373 | −0.2908 | −0.0056 |
| `g_l23` | −0.2516 | −0.3956 | −0.2308 | −0.2906 | −0.0055 |
| `g_l22` | −0.2517 | −0.3947 | −0.2298 | −0.2902 | −0.0050 |
| `g_cand` | −0.2442 | −0.3916 | −0.2280 | −0.2852 | — |
| `g_l25` | −0.2304 | −0.3771 | −0.2210 | −0.2725 | **+0.0127** |

**`g_ke3`'s marginal over the candidate GREW on held-out seeds: −0.0166 → −0.0202**, and it is negative in
all three formats on both blocks (−0.0225 / −0.0206 / −0.0140 on the confirmation). A screen winner is
selection-biased and normally shrinks; this one did not, which is about as strong as a six-arm screen's
winner can come back. [[replicate-trades-before-ruling]] killed 3 of 6 adoptable-looking trades once —
this is the opposite outcome.

**Everything else replicates too, including one useful shrink.** 25 lands stays clearly worse (+0.0144 →
+0.0109, pooled **+0.0127**, positive in all six format-blocks). Hero 3→2 stays a null. And the 22-land
rung **shrank to nothing** (−0.0069 → −0.0031, with 2hg and std landing at +0.0002 / +0.0004) — which is
exactly what the Silverblade-confound reading predicted: at 22 lands the *4th* Silverblade is what came
along, and a 4th Silverblade is worth nothing.

**Pooled matched pairs at Silverblade 3, where Silverblade cancels exactly:**

| what paid for the 3rd Silverblade | pooled, vs paying with the 24th land |
|---|---|
| **a Knight Exemplar** | **−0.0129** |
| a Hero of Bladehold | −0.0002 (a dead null) |

**The land count is settled: 24 (20 Plains + 4 Remote Farm) is right.** Moving up to 25 is worse,
moving down to 22–23 is worth nothing once the card that replaced the land is accounted for, and the one
real gain on the axis is not a land at all — it is that **the 4th Knight Exemplar is the worst card in the
list**, and a 3rd Silverblade Paladin should have its slot.

### FINAL WhiteKnights candidate — `g_ke3`, −0.3036 late-weighted

Thirteen of 60 cards differ from the shipped list. Still mono-white, still 24 lands, 60 cards.

| n | card | change from shipped |
|---|---|---|
| 20 | Plains | 22 → 20 |
| 4 | Remote Farm | **new** |
| 4 | Dauntless Bodyguard | — |
| 4 | Venerable Knight | — |
| 4 | Worthy Knight | — |
| **3** | **Knight Exemplar** | **4 → 3** *(round G)* |
| 3 | Hero of Bladehold | — *(confirmed: 2 is a null, 4 is worse)* |
| 1 | Sol Ring | — |
| 1 | Swords to Plowshares | — *(never tested — goldfish cannot price interaction)* |
| 1 | Unexpectedly Absent | — *(same)* |
| 4 | Accorder Paladin | — |
| **3** | **Silverblade Paladin** | **1 → 3** *(round F took it to 2, round G to 3)* |
| 4 | Adeline, Resplendent Cathar | 1 → 4 |
| 4 | Benalish Marshal | 2 → 4 |
| — | Acclaimed Contender | 3 → 0 |
| — | Aether Vial | 3 → 0 |
| — | Valiant Knight | 1 → 0 |
| — | Lightning Greaves | 1 → 0 |

**−0.3036 late-weighted is 1.53x this deck's exhaustive keep table (−0.1985)**, which was previously the
largest single improvement measured on it. It is also **~1.7x cheaper per game to simulate** than the list
it would replace.

### Round G on Knights — and the land count goes the OTHER WAY

Knights' shipped keep table was built at `1b3c94fb` (2026-07-14), **1,328 `src/` commits** before HEAD, so
unlike WhiteKnights this deck has never been screened on an apparatus its own engine produced. Six tables
at **R=40**, deliberately not its shipped R=60 — that choice was made in advance, on the measurement that
WhiteKnights' Aether-Vial-holding list cost 3.2x more to generate than the Vial-less one, and Knights
holds **four** Vial in every arm. R does not bias the `own` column (both sides of every comparison are
generated at the same R); what it costs is the bias line, which was never going to be clean for a deck
whose shared table is 1,328 commits stale.

Pooled over both blocks (measurement 6.4M, held-out 6.8M — 40,000 paired games per format,
`logs/wk_screen/pool_gkn.py`), against screen 7's confirmed winner as the reference:

| arm | long | 2hg | std | **pooled** | step vs winner | meas. | conf. |
|---|---|---|---|---|---|---|---|
| **`g_kn_l22`** Contender→0, **22 lands** | −0.0633 | −0.1671 | −0.0649 | **−0.0947** | **−0.0245** | −0.0271 | −0.0220 |
| **`g_kn_ac1_ad4`** Contender→1, **Adeline 4** | −0.0607 | −0.1760 | −0.0422 | **−0.0916** | **−0.0214** | −0.0194 | −0.0234 |
| `g_kn_ac0_ad4h` +Haytham 4 | −0.0527 | −0.1775 | −0.0365 | −0.0869 | −0.0166 | −0.0164 | −0.0169 |
| `g_kn_l21` Contender→1, 21 lands | −0.0572 | −0.1512 | −0.0520 | −0.0843 | −0.0141 | −0.0149 | −0.0133 |
| `g_kn_ac2_ad3` **screen 7's winner** | −0.0505 | −0.1303 | −0.0295 | −0.0702 | — | | |

**Screen 7's winner replicates on a real table: −0.0702 pooled against the −0.0721 it measured on the
shared one.** That is the third independent block for this arm and it has not moved.

**1. Knights' Adeline ladder ALSO runs to the legal maximum.** Adeline 3→4 is a further **−0.0214**
pooled, negative in all six format-blocks, se bound 0.0021 — a 10:1 margin, and it *grew* on held-out
seeds (−0.0194 → −0.0234). This independently reproduces the campaign's headline in the second deck:
extra copies of a legendary creature keep paying, because the value is seeing her earlier.

**2. Knights wants MORE lands — the opposite of WhiteKnights.** 20 → 21 → 22 is monotone
(−0.0141, −0.0245) and **has not turned over**. WhiteKnights, meanwhile, is correct at 24 with 25 worse.
That is the **fourth axis on which these two decks want opposite things**, after Remote Farm, Aether Vial
and the Plains count.

The confound is the payer: the ladder buys lands with Acclaimed Contender, and screen 7 found Contender
cuts beyond the second contribute nothing, so the gain is most likely the lands. That inference leans on
a shared-apparatus finding and is *not* eliminated here — which is why round H pays for the 23rd land a
second way, with a Marshal of Zhalfir.

**3. The two ideas cannot be combined without choosing.** Cutting Contender to 0 frees exactly four
slots; Adeline absorbs at most three of them (1→4) and Plains the rest. So "22 lands + Adeline 3" and
"21 lands + Adeline 4" both spend all four, and 23 lands requires cutting something round G never touched.
Round H tests the one combination the arithmetic allows (`h_kn_ad4_l21`).

**4. A clean format split, and it makes mechanical sense.** Relative to the winner, the *land* arm's gain
is concentrated at 20 life (std −0.0354) while the *Adeline* arm's is concentrated in 2HG (2hg −0.0457,
std only −0.0127). Against 60 points of life across two heads an extra threat compounds; at 20 life what
matters is curving out without stumbling. The same reason the two arms are close overall and want
different formats.

**5. The bias runs POSITIVE here, exactly as predicted, and that is the R gap not a defect.** Every null
is positive — both decks play slightly *weaker* on a fresh R=40 table than on Knights' shipped R=60 one —
with the base null at +0.004 quantifying the gap. Consequence: for Knights the stale shared table was
mildly **flattering** the variants (own −0.0702 vs shared −0.0735 on the winner), the opposite direction
to WhiteKnights. Both are small, and in both cases the real-table number is the one to use.

### Round H on WhiteKnights — Knight Exemplar keeps getting worse, and the deck runs out of cards

Pooled over blocks 7.0M / 7.4M, reference = round G's winner `g_ke3` (its table reused, so it sits on the
*same* seed block as the new arms — that is what makes the pair below exact):

| arm | long | 2hg | std | **pooled** | step vs `g_ke3` | meas. | conf. |
|---|---|---|---|---|---|---|---|
| **`h_wk_ke2_sp4`** Exemplar 3→**2**, Silverblade 4 | −0.2737 | −0.4211 | −0.2411 | **−0.3114** | **−0.0120** | −0.0131 | −0.0109 |
| `h_wk_hob2_sp4` Hero 3→2, Silverblade 4 | −0.2594 | −0.4108 | −0.2389 | −0.3007 | −0.0013 | −0.0017 | −0.0010 |
| `g_ke3` | −0.2626 | −0.4042 | −0.2342 | −0.2994 | — | | |

Both new arms hold **Silverblade 4**, so Silverblade cancels exactly and the difference isolates the
payer: **Exemplar 3→2 minus Hero 3→2 = −0.0107**. Hero 3→2 is a measured null (round G: −0.0002), so

* **Knight Exemplar 3→2 is worth a further −0.0107** — and the ladder has *still* not turned over
  (4→3 was ~−0.0095, 3→2 is −0.0107, if anything steeper).
* **The 4th Silverblade is worth ~−0.0013**, i.e. nothing — confirming round G's read that the 22-land
  rung's apparent gain was the *3rd* Silverblade and the 4th adds nothing.

So the finding is about one card: **Knight Exemplar is simply bad in this deck, and what replaces it barely
matters.** That is a coherent story — the lord's indestructible-and-first-strike half is inert against a
passive opponent, leaving a 2/2 body and a +1/+1 anthem that Benalish Marshal already provides more
cheaply.

**And this is where the card pool runs out.** Cutting a 3rd Exemplar needs somewhere to put the slot, and
in `h_wk_ke2_sp4` every remaining option is already refuted or unmeasurable:

| candidate for the slot | status |
|---|---|
| Bodyguard / Venerable / Worthy / Accorder / Silverblade / Adeline / Marshal | **already at 4** |
| Hero of Bladehold 3→4 | measured **+0.0037 worse** |
| Plains 21 (25 lands) | measured **+0.0127 worse** |
| Swords to Plowshares, Unexpectedly Absent | **deliberately never tested** — goldfishing cannot price interaction |
| Sol Ring 1→2+ | untested, and raising it is a format/character decision, not a copy count |

**`h_wk_ke2_sp4` at −0.3114 pooled is therefore the end of the road for WhiteKnights on the current card
pool** — 14 of 60 cards changed, still mono-white, still 24 lands. The Exemplar ladder is still sloping,
which is a live lead, but following it needs a card the deck does not own. That is precisely the
*"I will let you know later if there are any other cards I would like to try"* decision.

> **CORRECTION, 2026-09-26 22:00Z — the two paragraphs above are WRONG, and the table is right but
> misread.** The error is one of kind, not degree, and it is worth naming precisely because it is an easy
> one to repeat.
>
> **A sink price is not a refutation.** Hero of Bladehold's 4th copy at **+0.0037** and the 25th land at
> **+0.0127** were each compared against *zero* and marked "refuted". But they are not being asked to be
> good on their own — they are being asked to be **cheaper than the cut that pays for them**. Knight
> Exemplar 3→2 is worth **−0.0107**, so `Exemplar 2→1 + Hero 3→4` projects to **−0.0107 + 0.0037 ≈
> −0.0070**, still a clear gain; even the worst sink, a 25th land, nets ≈ +0.0020, i.e. roughly break-even
> rather than "refuted". **The ladder was never blocked.** What the table actually shows is that
> WhiteKnights is *sink-starved* — its cheapest slot costs +0.0037 — which raises the bar on every future
> cut but does not close the axis.
>
> **And "already at 4" answered the wrong question.** It is a correct *sink* argument (you cannot add a
> 5th) and it was silently read as a *screening* claim. It is not one: **Dauntless Bodyguard, Venerable
> Knight, Worthy Knight and Accorder Paladin — 16 of the candidate's 60 slots — have never been varied in
> any round.** They were never cut-tested, only never-addable. Compare
> [[cut-ladders-never-ask-what-to-add]], which is this same blind spot pointing the other way.
>
> **The sharpest part: two of those four are, in this simulator, the same card, and both are blank.**
> From `src/cards/data/cards.json`:
>
> | card | cost | body | modelled text |
> |---|---|---|---|
> | Dauntless Bodyguard | `{W}` | 2/1 Human Knight | **none** — its own bracket note says the ETB choice and sacrifice-for-indestructible are *inert in goldfishing*: the passive opponent deals no damage and casts no removal, so indestructible never matters and sacrificing a 2/1 is never correct |
> | Venerable Knight | `{W}` | 2/1 Human Knight | **none** — bracket note again: the death trigger never fires, because creatures never die here (no blockers, no opponent removal) |
>
> Same cost, same body, same subtypes — so Worthy Knight's *"whenever you cast a Knight spell"* and
> Adeline's creature count treat them identically. **The deck runs eight copies of a vanilla 2/1 Knight
> for `{W}`.** That is the Knight Exemplar shape exactly — a card carried for text this apparatus cannot
> reward — except Exemplar at least keeps its +1/+1 lord half and these two keep nothing at all.
>
> This is what **round J** measures; the correction, not the original claim, is the live state.

### The current best WhiteKnights list, in full — `h_wk_ke2_sp4`, −0.3114 late-weighted

Already on disk as `logs/deckcmp/WhiteKnights/h_wk_ke2_sp4/WhiteKnights.txt` (the driver writes each arm's
decklist), so adoption step 3 is a copy, not a transcription. **Provisional until round J lands.**

| n | card | vs shipped |
|---|---|---|
| 4 | Dauntless Bodyguard | — |
| 4 | Venerable Knight | — |
| 4 | Worthy Knight | — |
| 4 | Accorder Paladin | — |
| 4 | **Adeline, Resplendent Cathar** | **1 → 4** |
| 4 | **Benalish Marshal** | **2 → 4** |
| 4 | **Silverblade Paladin** | **1 → 4** |
| 3 | Hero of Bladehold | — |
| 2 | **Knight Exemplar** | **4 → 2** |
| 1 | Sol Ring | — |
| 1 | Swords to Plowshares | — |
| 1 | Unexpectedly Absent | — |
| 20 | **Plains** | **22 → 20** |
| 4 | **Remote Farm** | **0 → 4** (introduced) |
| | *gone:* Acclaimed Contender 3, Aether Vial 3, Valiant Knight 1, Lightning Greaves 1 | |
| **60** | | **14 cards changed** |

Still mono-white, still 24 lands, curve unchanged at the top (`{2}{W}{W}`). **The shape of the change is
that the singletons got resolved.** The shipped list held *seven* one-ofs — Valiant Knight, Silverblade
Paladin, Adeline, Lightning Greaves, Sol Ring, Swords to Plowshares, Unexpectedly Absent — plus a
two-of Benalish Marshal. Of the four the screen is allowed to price, **every one went to 0 or to 4**
(Valiant Knight and Greaves out, Silverblade and Adeline to four), Marshal went 2→4, and the only
singletons left standing are the three it cannot price: Sol Ring on legality grounds and the two
interaction spells because goldfishing cannot value interaction.

So the candidate is not a different deck; it is **the same deck with the variance taken out** — which is
exactly what the win-turn distribution below shows, and it is why the one thing it loses is the outlier
draw.

### WHY Knight Exemplar is the worst card — from `cards.json`, not from a story

The Exemplar result has been explained so far as *"its indestructible-and-first-strike half is inert
against a passive opponent, leaving a 2/2 and an anthem Benalish Marshal provides more cheaply."* That is
true but it is not the reason, and the real reason is a single line of card data:

| card | modelled anthem | what it pumps |
|---|---|---|
| **Knight Exemplar** | `lord_effect`, `subtypes_affected: ["Knight"]`, +1/+1 | Knights only |
| **Benalish Marshal** | `lord_effect`, `affects_all_creatures: true`, +1/+1 | **everything** |

and every token this deck makes is **not a Knight**:

| token source | `*_token_subtypes` | pumped by Exemplar? | by Marshal? |
|---|---|---|---|
| Adeline, Resplendent Cathar (per attack) | `["Human"]` | **no** | yes |
| Hero of Bladehold (two per attack) | `["Soldier"]` | **no** | yes |
| Worthy Knight (per Knight cast) | `["Human"]` | **no** | yes |

**So Knight Exemplar's anthem covers a shrinking fraction of the board exactly as the deck's damage moves
into tokens — and the candidate moved it there on purpose,** taking Adeline 1→4 and Benalish Marshal 2→4.
On a turn-4 attack (the candidate's modal win) with Adeline and a Hero deployed, a large part of the
attacking power is Humans and Soldiers that Exemplar does not see and Marshal does. Exemplar's remaining
printed text is `keywords: ["First Strike"]`, which is modelled but worthless with no blockers, plus the
indestructible grant, which does not appear in its `parameters` at all — so it is genuinely not modelled,
exactly as the bracket note claims.

**The deck holds exactly three +1/+1 lords, and the campaign sorted them by this mechanism before anyone
had articulated it:**

| lord | cost | filter | what eight rounds of screening did to it |
|---|---|---|---|
| Valiant Knight | `{3}{W}` | `subtypes_affected: ["Knight"]` | **cut 1→0** (screen 3) |
| Knight Exemplar | `{1}{W}{W}` | `subtypes_affected: ["Knight"]` | **cut 4→2** (rounds G, H) — still sloping |
| Benalish Marshal | `{W}{W}{W}` | **`affects_all_creatures: true`** | **raised 2→4** (screen 3) |

Both Knight-only lords were cut and the all-creatures lord was maxed, in **three separate screens, on
three different seed blocks**, with the mechanism read out of `cards.json` only afterwards — so it cannot
have steered any of them. That is about as good an independent coherence check as this campaign has
produced, and it is the kind [[dont-rationalize-a-measured-cut]] actually allows: the numbers came first
and the explanation has to fit all three or none.

**This explains three results with one fact:** why Marshal 2→4 was a gain, why Valiant Knight was cut
outright, and why Exemplar is the list's worst card. It also **predicts** rather than merely rationalises,
which is the test
[[dont-rationalize-a-measured-cut]] sets: if Exemplar's anthem is being diluted by tokens, its marginal
must get *worse* as the token count grows. It does — the ladder steepens (4→3 ≈ **−0.0095**, 3→2 =
**−0.0107**) rather than flattening as a saturating lord would. And it predicts that **2→1 keeps paying**,
which is `j_wk_ke1_hob4` in round J. If that rung comes back flat, this mechanism is wrong.

**A near-miss worth recording, because it would have invalidated the campaign's headline.** Hero of
Bladehold's bracket note says its token count is flat *"so `attack_tokens_per_opponent` is false, **unlike
Adeline's**"* — yet Adeline's JSON carries no such key at all. If an absent key defaulted to `false`,
Adeline would be under-modelled in 2HG, which is both the format where her measured gain is largest and a
0.3 weight in the ranking. It does not: `src/cards/CardDatabase.h:761` declares
`attack_tokens_per_opponent = true` and `CardDatabase.cpp:855` reads it with a `true` default, explicitly
to preserve the historical unconditional `OpponentHeads()` multiply — Adeline is named in that comment.
So Adeline really does make **two** tokens per attack at `opponent_heads: 2`, faithful to her *"for each
opponent"* clause, while Hero's flat two Soldiers correctly do not scale.

**That turns the format split from a curiosity into a mechanism.** Adeline's gain concentrates in 2HG in
*both* decks (WhiteKnights 2hg −0.4211 vs std −0.2411; Knights' Adeline arm −0.0457 in 2hg) because she
genuinely doubles her token output there — and since her power is `power_equals_creature_count`, the
tokens she makes also grow her own power, so the effect compounds rather than adding. The 0.3 weight 2HG
carries is therefore pricing a real card behaviour, not an artifact of the life total. Worth stating
plainly because [[2hg-opponent-heads]] warns that heads are **often inert** and must be proven live rather
than assumed — here they are provably live, and this is the card that makes them so.

**A related audit, free from the same data.** Exactly **8 of the candidate's 60 slots** are cards the
engine declares to have no abilities whatsoever — `"template": "vanilla_creature"` with empty `keywords`
*and* empty `parameters` — and they are precisely **Dauntless Bodyguard ×4 and Venerable Knight ×4**.
Every other card in the list carries parameters. The two differ in no modelled field at all: same
`{W}`, same 2/1, same `["Human","Knight"]`, same empty everything, with **`name` and `oracle_text` the
only fields that differ between their JSON entries**. That is why round J's
`j_wk_dbg3_hob4` / `j_wk_vk3_hob4` pair is an exact null control rather than an approximate one.

### The final list's CHARACTER changed, and it gave up the nut draw to do it

From round H's preserved per-game data (`logs/wk_screen/out/floor_WhiteKnights_09.err` = held-out std,
`_08.err` = held-out 2hg; `dist.py`, 20,000 paired games each, both arms on their **own** R=40 table):

| win turn | **std** shipped | **std** `h_wk_ke2_sp4` | **2hg** shipped | **2hg** `h_wk_ke2_sp4` |
|---|---|---|---|---|
| 3 | **76** | **0** | **10** | **0** |
| 4 | 12,979 | **17,600** | 3,880 | **10,143** |
| 5 | 6,421 | 2,239 | 13,479 | 9,124 |
| 6 | 461 | 140 | 2,220 | 602 |
| 7 | 53 | 4 | 344 | 111 |
| 8+ | 10 | 0 | 67 | 20 |
| unwon | 0 | 0 | 0 | 0 |
| mean | 4.3736 | **4.1293** | 4.9617 | **4.5373** |
| | *28.4% faster / 65.7% same / 6.0% slower* | | *42.5% faster / 50.2% same / 7.3% slower* | |

Two things to take from this, one good and one that is a genuine cost:

* **The gain is still a tail collapse, and it got cleaner.** Turn 5 → turn 4 is now the dominant
  conversion (std turn-4 wins up **35%**), turn 6 is down 70%, turn 7 down 92%, and past turn 7 the
  candidate simply does not appear. Zero unwon games on either side, so the loss penalty does no work
  anywhere in this campaign. This is the part of the curve a goldfish simulator models most reliably.
* **THE CANDIDATE HAS NO TURN-3 KILL AT ALL.** The shipped list wins on turn 3 in 76 of 20,000 std games
  (0.38%) and 10 of 20,000 2hg games; the candidate does it **zero** times in either. That is a real
  change in the deck's character, not a rounding artifact, and it points the *opposite* way to everything
  else here: the candidate is strictly more consistent and strictly less explosive. It also runs straight
  into the known blind spot — a goldfish sim **overstates** all-in deploy (see
  [[stompy-list-settle-resume]]) — so the 0.38% it is giving up is the part of the shipped list the
  apparatus was *already* flattering, which makes the trade better in reality than it looks here, not
  worse. Worth the user's attention regardless, because "I want the nut draw" is a taste, not a number.
* **Likely donor: Aether Vial (3→0) — but this is a HYPOTHESIS, not a measurement.** Vial is the only card
  in the shipped list that converts a hand into a board faster than mana allows, which is what a turn-3
  kill requires. Per [[dont-rationalize-a-measured-cut]] a mechanism invented after the numbers explains
  them and does not extend them, so it is flagged rather than asserted. It is cheap to settle and has not
  been: every arm in rounds G–J holds Vial 0, so no existing snapshot isolates it. The test is a
  `--log-dir` probe over the shipped list's turn-3 games ([[goldfish-bias-has-two-readouts]]).

The apparatus understatement is also visible here as raw counts rather than a delta: on the stale shared
table the candidate wins on turn 4 only **16,501** times instead of **17,600** (std). The old table was
mulliganing away ~1,100 of its turn-4 hands.

### Round J on WhiteKnights — the 16 unscreened cards, and the rung round H left open

Generator `logs/wk_screen/mkspec_j.py`, runner `logs/wk_screen/run_j.sh <pid>` (chained on round H's
captured PID, because `bracket: generate` must be alone on the box). Seeds **8.6M / 9.0M**, `floor_R: 40`,
`max_fallback: 0.30`, seven arms, **six new tables**, 16 cells in **one** pooled batch.

**The design in one line: every arm holds the same one-slot sink and cuts one card, so the sink price is
common to all of them and cancels exactly in every arm-vs-arm difference.** The sink is Hero of Bladehold
3→4, the cheapest one the deck has (+0.0037). That leaves the *level* against the reference carrying the
sink's price, and the *differences* carrying pure card-vs-card reads — which is the round H trick
(Silverblade 4 on both arms) generalised to five payers at once.

| arm | change from `h_wk_ke2_sp4` | what it answers |
|---|---|---|
| **`h_wk_ke2_sp4`** | — (reference; round H's table **reused**) | puts every pair on *this* round's seed block |
| `j_wk_ke1_hob4` | Exemplar 2→**1**, Hero 3→4 | does the Exemplar ladder pay a **third** time? |
| `j_wk_dbg3_hob4` | Dauntless Bodyguard 4→**3**, Hero 3→4 | the one-drops, never screened |
| `j_wk_vk3_hob4` | Venerable Knight 4→**3**, Hero 3→4 | **the same change again** — see below |
| `j_wk_wor3_hob4` | Worthy Knight 4→**3**, Hero 3→4 | the token engine — expected to be *good* |
| `j_wk_acc3_hob4` | Accorder Paladin 4→**3**, Hero 3→4 | battle cry 3/1 — expected to be *good* |
| `j_wk_ke1_l25` | Exemplar 2→**1**, Plains 20→**21** | matched pair vs `j_wk_ke1_hob4`: **re-prices the sink** in *this* list |

Three things about that table are load-bearing:

* **`j_wk_dbg3_hob4` vs `j_wk_vk3_hob4` is a free null control.** Because the two cards are identical in
  every modelled respect, these arms make *the same change to the same list* — expectation exactly zero —
  yet their games are independent, since numbering and shuffle key on card identity. So the gap between
  them is pure apparatus noise **measured on this round's own seeds**, which is a stronger check than any
  analytic bound, and it costs one table. Pooling the two also doubles the sample on *"cut a one-drop"*,
  which is the answer we actually want.
* **The two two-drops are in there to be refutations.** If Worthy Knight and Accorder Paladin also measure
  as cuts, the round is not finding bad cards — it is finding that the apparatus rewards *removing
  anything*, and the whole ladder is suspect. A screen where every arm can only confirm the hypothesis is
  not a screen. These two are the arms that can falsify it.
* **`j_wk_ke1_l25` exists because the sink prices are stale.** Both were measured in a list with Exemplar
  3 and Silverblade 3; the curve has moved twice since. If the 25th land is now the *cheaper* home, every
  rung above it is understated.

**Why a ladder here and a combination round in Knights.** The two decks fail in opposite directions.
Knights has slots to spare (Contender 4→0 frees four) and **competing sinks**, so its arms must allocate.
WhiteKnights is **sink-starved** — everything good is at 4 and the entire sink inventory is two entries —
so only one slot is ever in play per arm, there is nothing to allocate, and a combination round would only
blur the reads.

**One arm deliberately absent: Sol Ring.** It is the only untested sink, and probably the most likely card
in the deck to move the number — it pays the `{2}` of Hero of Bladehold and the `{1}` of both two-drops,
which is the same "land that makes two mana" logic that made 4 Remote Farm a *gain* (Remote Farm taps for
`{W}{W}`, and the deck is full of `{1}{W}{W}` three-drops plus a `{W}{W}{W}` Marshal). But a 2nd Sol Ring
is illegal in every format this list could be read as — banned in Modern and Legacy, restricted in
Vintage, singleton in Commander — and Sol Ring at exactly 1 next to Haytham Kenway is what makes the pool
read as Commander-flavoured casual. So it is a **question for the user, not an arm**: one word, one table.

### ADOPTION RUNBOOK for WhiteKnights — what it costs, and the order that cannot be rearranged

**Nothing here has been done, and nothing here should be started before the user says so.** Written out
because the ordering constraints are the kind that are cheap to honour in advance and expensive to
discover halfway through.

**First, the thing to understand before deciding: adoption THROWS AWAY the shipped artifacts.** The
shipped `WhiteKnights.keepmodel.exhaustive.profile.json.gz` (37 min, K=14, 42,271 compositions, worth
**−0.1985 t** on its own) and the shipped `WhiteKnights.value.json` are both **fitted to the shipped
decklist's play**. A 14-card change invalidates both — they are engine-state-and-decklist fingerprints,
not portable models. So the price of a −0.3114 list is regenerating the whole pipeline. That is the real
decision, and it is why this is the user's call and not a "clean win" adopted on evidence: per
[[fresh-full-was-not-a-clean-win]], *clean* means no regression on any axis, and the candidate does
regress on one (no turn-3 kill at all) while also owing hours of regeneration.

**The suite gate is already satisfied** — `suite_gate.py --require decks/WhiteKnights` returns rc=0,
regression key `whiteknights` (plus `whiteknights2hg`). So both generators will start, and
`MTG_ALLOW_UNTESTED_DECK` is **not** needed. (The CLAUDE.md note recording that WhiteKnights once shipped
outside the suite describes a state that has since been fixed.) The **3x cost rule** is comfortably clear
in the right direction too: the candidate is **~1.7x cheaper per game** than the shipped list.

| # | step | why it is where it is |
|---|---|---|
| 0 | **Wait for round J.** | The list is not final. If a one-drop or the Exemplar rung pays, the decklist changes and every step below would have to be redone. |
| 1 | `git mv` the shipped list + all five sidecars into **`decks/WhiteKnights/v1-vial3-exemplar4/`** | The archive convention (`CritterLifegain/v1-thune4-basilica`). The slug names the two things the candidate most decisively removes. |
| 2 | `git mv references/WhiteKnights/claude_s*_gi*.json` → **`references/WhiteKnights/v1-vial3-exemplar4/`** (all **10**) | A reference belongs to the list it was played on (`server.js` `refsOnArchivedList`). Leaving them at top level silently credits the OLD list's 10/10-exact hand-played games to the NEW one. **`git mv` only** — never `rm`, never `checkout`; references are commit-only. |
| 3 | Write the new `decks/WhiteKnights/WhiteKnights.cod` | The only step that is a decklist edit. |
| 4 | **profile** — `scripts/analyze_deck.py decks/WhiteKnights/WhiteKnights.cod` | Nothing downstream is meaningful without it, and the screen's own Rule 0 is that a profile must be attached to every measurement. |
| 5 | **value leaf** — `bash scripts/valueleaf.sh run decks/WhiteKnights` | Must precede the mulligan: `mullgen` reads `mull_gen_depth` / `mull_gen_budget_ms` / `expected_buckets` from `.value.json`, which this step writes. Run it before and the gen silently inherits the *play* depth. |
| 6 | **mulligan** — `bash scripts/mullgen.sh run decks/WhiteKnights` | Last, and **alone on the box** — as must 4 and 5. Not scheduling politeness: sidecar *presence* activates the hybrid in play, so a table dropped mid-run changes the very play a neighbour is fitting. |
| 7 | **GT** — rebaseline `whiteknights`, `whiteknights2hg` and accept | The deck's play genuinely changed, so GT **must** move. This is a real rebaseline, not a rubber stamp; do not hand-edit. |
| 8 | `suite_gate.py --measure-all` to refresh `test/suite_cost.json` | Needs an **exclusive box** — it is a timing measurement. One pooled tier run, never a per-deck loop. |

**Steps 4–6 are strictly serial and each runs alone.** Steps 1–3 are free. Step 8 is bookkeeping.

**Two sign-offs to collect at adoption time, neither blocking any of the above:**

* **`expected_buckets` 14 → 12.** The candidate discovers **K=12** where the shipped list finds 14, because
  Remote Farm earns its own bucket. Recording discovered K is the user's call
  ([[bucket-ruling-is-user-only]]); the guard never fired, so nothing needs regenerating either way.
* **Whether both `Knights` and `WhiteKnights` continue to ship**, which has been open since the analysis.

## RESUME HERE — the road to a FINAL Knights list

**State at 2026-09-26 21:15Z.** WhiteKnights is finished (above). Knights' round H is **in flight**:
`logs/wk_screen/run_h.sh` (pid 91714) started its Knights measurement at 20:19Z — three R=40 tables at
~33 min each, then three batches, then the held-out confirmation at seed 7.6M. Expect ~22:50Z. Logs land
at `logs/wk_screen/out/h_kn_{long,2hg,std}.log` and `hc_kn_*.log`; read with
`python3 logs/wk_screen/pool_h.py h_kn hc_kn g_kn_ac2_ad3`.

**What round H answers:** the one combination the slot arithmetic allows (`h_kn_ad4_l21` — Contender 0,
Adeline 4, 21 lands), and whether the land ladder turns over at 23, paid for two independent ways
(`h_kn_ad2_l23` with Adeline copies, `h_kn_moz3_l23` with a Marshal of Zhalfir).

**What is STILL MISSING for a final Knights list, in priority order:**

1. **Knight Exemplar in Knights is the big untested axis.** Screen 7 measured 4→3 at **−0.0368** on the
   shared apparatus — the second-best cut in that deck after Acclaimed Contender — but it has **never been
   combined** with the Contender/Adeline/land changes, and never measured on a real table. WhiteKnights'
   round H makes this urgent: that deck's Exemplar ladder is still sloping at 3→2, and Knights runs the
   same four copies with the same passive-opponent inertness. **This is almost certainly the largest
   remaining gain in Knights and it should be round I.**
2. **Extend whichever ladder round H leaves sloping** (lands, or Adeline, or both).
3. **Knights' Exemplar and land axes compete for the same slots**, exactly as Contender and Adeline did,
   so round I has to be a combination round rather than a set of independent rungs.
4. **Not blocking, but unresolved:** the land ladder's payer confound (Acclaimed Contender) is only
   partly broken by `h_kn_moz3_l23`; and Knights' sideboard promotions remain out of scope by the user's
   *"optimize on the set we have for now"*.

**Round I is already written**, parameterised on round H's endorsed land count:

```
python3 logs/wk_screen/mkspec_i.py <L>        # prints the arms + the tag list
python3 scripts/deck_compare.py logs/wk_screen/i_knights_long.json --floor <tags> --dry-run
```

Its five arms hold Contender 0 and Adeline 4 (both settled) and vary **Knight Exemplar** 4 / 3 / 2 / 0
against the remaining sinks, reading the Exemplar axis off **matched pairs** rather than a moving base.
Two arms pay the second Exemplar slot differently — one with a land, one with Haytham's 4th copy (a
measured null) — which is what separates *"Exemplar is bad"* from *"lands are good"*. The generator
**asserts the slot ledger balances** for every arm (frees = uses); that assertion is the check that would
have caught round F's 57-card arm. At L=22 the reference arm is deliberately named `h_kn_ad4_l21` because
it is byte-identical to round H's, which makes it free.

**Do not re-run anything already on disk.** Thirteen keep tables exist under `logs/deckcmp/{WhiteKnights,
Knights}/<tag>/` and `gen_table` reuses them on (counts, R) — reference arms and held-out blocks are free.
Note `deck_compare` holds a per-deck `.lock` and will refuse a second run on the same deck, including a
`--dry-run`: that guard exists because two runs would rewrite each other's `numbering.json` while jobs are
still reading it.

**All of `logs/wk_screen/` is gitignored** (repo convention: log/output dirs live under `logs/`), so the
specs and readers are on disk only — this document is the committed record. The readers are
`gread.py` (one bracket log → a table), `pool_g.py` / `pool_gkn.py` / `pool_h.py` (pool two blocks),
`dist.py` (per-game win-turn distribution), `snap_floor.sh` and `snap_out.sh` (preserve each batch's
per-game data and per-job cost, both of which the driver otherwise overwrites per invocation).

## Open questions for the user (surfaced, not blocking)

1. **Ranking weights.** The Angels campaign ranked arms late-weighted — `long` 0.5 / `2hg` 0.3 /
   `std` 0.2, *"not by 20-life speed"* (user, 2026-09-19). Applied here by default; all three per-format
   deltas are reported raw so it can be re-weighted.
2. **Do both lists stay?** `Knights` and `WhiteKnights` share a Knight core and the ledger's open
   question 4 already asks this. Optimising copy counts in both assumes both stay.
3. **Knights' sideboard is full of main-deck candidates** — Hero of Bladehold, 2 Accorder Paladin,
   Kinsbaile Cavalier, Student of Warfare, Valiant Knight. Promoting any is an *introduced card* for
   that list and needs its own alias; it is out of scope for screens 1–4 and would be screen 5.
   **Round G removes the apparatus obstacle**: an introduced card gets its own bucket by construction
   when the table is generated for the arm holding it, so no alias is needed and the `own` column would be
   clean. What is left is a scope question, not a method one — *"I will let you know later if there are
   any other cards I would like to try, but let's optimize on the set we have for now"* (user,
   2026-09-26) reads as holding this back, and a sideboard card is arguably already "the set we have".
   **Not run; say the word and it is one spec.**
4. **Adoption.** `g_ke3` at −0.3036 is a bigger measured gain than anything else on this deck, and its
   keep table already exists (`logs/deckcmp/WhiteKnights/g_ke3/`). Adoption still owes it a **value leaf**
   — round G left every arm on the shipped list's `.value.json` — plus a real mulligan profile in the deck
   folder, suite cases and ground truth, and the predecessor archived with its references moved. None of
   that was started: no `decks/` folder was created ([[never-commit-screening-lists]]).
5. **`expected_buckets` for the candidate is 12, not 14.** If `g_ke3` is adopted its `value_play`
   block needs that number, and per [[bucket-ruling-is-user-only]] recording discovered K is the user's
   call to make. Nothing has been edited — the scratch generations ran under `MTG_KEEP_ACCEPT_K=1` in an
   isolated directory.
