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
> 5th) and it was silently read as a *screening* claim. It is not one. Compare
> [[cut-ladders-never-ask-what-to-add]], which is this same blind spot pointing the other way.
>
> **CORRECTION TO THE CORRECTION, same day: the claim "Dauntless Bodyguard, Venerable Knight, Worthy
> Knight and Accorder Paladin — 16 of 60 slots — have never been varied" was WRONG, and overstated the
> gap by half.** Checking the screen-3 table above rather than trusting the phrase:
>
> | card | actually varied? |
> |---|---|
> | Dauntless Bodyguard | **YES** — `a_db3_bm3`, 4→3 measures **+0.0087 worse** (+0.0179 at 20 life), recorded "do not cut it" |
> | Worthy Knight | **YES** — `a_wk3_bm3`, 4→3 measures **+0.0042 worse**, recorded "don't" |
> | Venerable Knight | Not in WhiteKnights — but screen 7 tested it in **Knights** (a null, 0.0001; +0.0314 at 20 life, "do not cut it"), *and* it is provably the same card as Dauntless Bodyguard in this engine, so its answer is already known by identity |
> | Accorder Paladin | **NO — never varied in either deck** |
>
> So the untested block is **8 slots, not 16**, and after the identity argument the one genuinely open
> card is **Accorder Paladin**. Worse, I had already been told about it: the user said in an earlier round
> *"Accorder Paladin is notably better in goldfishing than in real play"* — a 3/1's toughness is free
> against an opponent that never blocks — and it is recorded near the top of this very document.
>
> **That asymmetry is the useful part, and it is the mirror of the Knight Exemplar case.** Accorder Paladin
> is **over**-valued here, so the two possible results are not equally trustworthy: a measured *"cut it"*
> would be **strong** evidence (bad even while flattered), while a measured *"don't cut it"* is **weak**
> (exactly what an inflated card produces). State which of those the number is before reading it.
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

### USER RULING 2026-09-26 — KNIGHT EXEMPLAR STAYS AT 4, and the measurement was never evidence against it

> *"Knight Exemplar is not really under consideration for cutting. The indestructible is too effective to
> give up against removal and sweepers."* … *"though it would be cuttable if it wasn't also a lord."*

**This ruling is correct and the campaign's numbers do not contradict it — they never addressed it.** Knight
Exemplar's indestructible grant does not appear in its `parameters` at all: the card carries only
`subtypes_affected`, `power_bonus`, `tough_bonus` and a `First Strike` keyword. It is *genuinely
unmodelled*, because the passive opponent casts no removal and no sweepers, so there is nothing for it to
protect against. Every figure this campaign produced for cutting Knight Exemplar — −0.0095, −0.0107,
−0.0166 — priced **a 2/2 Knight-only lord and nothing else**.

**The error was mine and it was an error of reading, not of arithmetic.** The card's own bracket note says
*"indestructible grant + first strike inert in goldfishing"*, and I quoted it repeatedly as *support* for
cutting the card. It is the opposite: a bracket note is a statement that **this apparatus cannot see part
of the card**, i.e. a warning that the measurement is a floor on the card's value, not an estimate of it.
[[bracket-notes-are-the-judgement-call]] says exactly this — *an under-modelled card flatters the arm that
cuts it* — and I built a mechanism section (below) that made the wrong conclusion more persuasive rather
than testing it. The user's second remark is the precise correction: the lord half **is** weak, which is
what the sim measured; the card is kept because the lord half plus the indestructible half together earn
the slot, and only the first of those is on the record.

**So the right output for a card like this is a PRICE, not a verdict.** Knight Exemplar's rate cost is
about **−0.0095 to −0.0107 t per copy** — that is what the indestructible insurance costs in goldfish
speed, and whether the insurance is worth it is a judgement about the metagame that this engine cannot
make. Presenting it as *"the list's worst card"* was the mistake.

#### The audit this forces: which other cuts rest on unmodelled text?

Every card the campaign moved, checked against its `cards.json` entry:

| card | campaign verdict | is the cut resting on unmodelled text? |
|---|---|---|
| **Knight Exemplar** 4→2 | cut | **YES — indestructible grant absent from `parameters`.** REVERSED by the user |
| **Lightning Greaves** 1→0 | cut | **YES — "shroud documented-inert (the passive opponent never targets our permanents)". Shroud is removal protection.** Haste *is* modelled, so the cut is partly earned — but only partly |
| Acclaimed Contender 3→0 | cut | No. The `PARTIAL` is the "legendary artifact" clause, and it is genuinely inert *in this deck* — Sol Ring and Aether Vial are both plain Artifacts. The dig itself is fully modelled |
| Aether Vial 3→0 | cut | No. Fully modelled; its note describes an AI heuristic, not a gap |
| Valiant Knight 1→0 | cut | No. *"Both clauses modelled"*, including the activated double-strike pump. **User agrees: "Valiant Knight is an easy cut"** |
| Plains 22→19/20 | cut | No. Lands are fully modelled |
| Adeline 1→4 | **added** | No gap — but **OVER-valued**, see below |
| Benalish Marshal 2→4 | **added** | No. Fully modelled. **User agrees it should be a 4-of** |
| Silverblade Paladin 1→3 | added | No. Soulbond fully modelled; the partner *ranking* is a disclosed narrowing |
| Accorder Paladin, Hero of Bladehold, Worthy Knight | untouched | No. All fully modelled |
| Remote Farm 0→4 | added | No. Fully modelled |
| Swords to Plowshares, Unexpectedly Absent | never tested | Correctly excluded — goldfishing cannot price interaction |

**Two of the campaign's cuts rest on unmodelled protective text, and both are now suspect: Knight Exemplar
(reversed) and Lightning Greaves.** Greaves is a 1-of so the stakes are small, but the reasoning is the
same and it should be the user's call, not the screen's.

**And the two never-screened one-drops are in the same class.** Dauntless Bodyguard's entire text is
*"sacrifice this: another creature gains indestructible"* and Venerable Knight's entire text is a **death**
trigger — both documented inert here for exactly the reason Exemplar's grant is, namely that nothing ever
kills our creatures. Round J′ measures them, but the result is *the rate cost of that insurance*, not a
verdict. Stated in advance so the number cannot be misread the way Exemplar's was.

#### The bias runs BOTH ways — Adeline is OVER-valued for a symmetrical reason

The user, unprompted, supplied the mirror image:

> *"Part of her problem is that she is overstated in goldfish, because she does damage equal to the number
> of critters on board and cannot be chump blocked."* … *"However, I agree she is good nonetheless."*

This is right, and it is the same defect seen from the other side. Adeline's power is
`power_equals_creature_count`, so she is always the largest attacker in the list — and **a rational
opponent chump-blocks the largest attacker.** One 1/1 token denies her entire contribution, and vigilance
does not help against blockers. This engine has **no blockers at all**, so every point of her power
connects every turn. Worse for the estimate: Silverblade Paladin's soulbond partner is picked by highest
effective power, so Adeline is usually the paired creature and usually has **double strike** — doubling the
damage that a single chump block would deny.

So the apparatus **under**-values protection (Exemplar, Greaves, Bodyguard, Venerable Knight) and
**over**-values a lone oversized unblockable-in-practice attacker (Adeline), and the two biases point in
opposite directions on the same list. Neither is a reason to distrust the *ranking* of fully-modelled
cards against each other; both are reasons the absolute numbers are not deckbuilding verdicts.

**What is measurable about it, and what is not.** Estimating what the deck would do against a real blocker
is not measurable here — any such number would be a model invented for the occasion. What *is* exactly
measurable is the **exposure**: Adeline's power IS the creature count, and each turn's total damage is
recorded, so her share of the lethal attack can be computed from board state with no combat
reimplementation. `logs/wk_screen/adeline_probe.py` reports it at both 1× and 2× (double strike), alongside
how often a 4th copy is **stranded** in hand under the legend rule — the user's other objection.
`run_l.sh` runs it on the Adeline-4 and Adeline-3 lists once round J′ frees the box.

#### What the list becomes with Exemplar locked at 4

Round G already measured three Exemplar-4 arms, so this costs nothing to answer:

| Exemplar-4 arm | late-wtd | |
|---|---|---|
| `g_l22` 22 lands, Silverblade 4 | **−0.2967** | |
| `g_hob2` Hero 3→2, Silverblade 3 | **−0.2963** | |
| `g_l23` 23 lands, Silverblade 3 | **−0.2959** | ← round J′'s base |
| `g_cand` round-F candidate, Silverblade 2 | −0.2898 | |
| `g_l25` 25 lands, Silverblade 1 | −0.2753 | |

The top three are **within 0.0008 of each other**, far inside the ~0.0046 se bound — a genuine three-way
tie. All three beat `g_cand` by ~0.007, and round G's own reading is that *all* of that is the 3rd
Silverblade (−0.0071 standalone). So the honest statement is: **the Exemplar-4 list is the round-F
candidate plus a 3rd Silverblade Paladin, and which slot pays for it — the 24th land, or a Hero of
Bladehold — is a tie the apparatus cannot break** ([[a-null-must-not-break-a-tie]]). Locking Exemplar at 4
costs about **0.015 t** against the retracted `h_wk_ke2_sp4`, not the ~0.029 a naive chain of the two
Exemplar rungs would suggest, because the Silverblade copies can be bought elsewhere.

### USER RULING 2026-09-26 — ADELINE AT 3, and the reasoning is worth preserving

> *"I think that those objections make me prefer Adeline at 3 rather than 4. She is really quite good, but
> the effects that are worse against removal or blockers shouldn't be understated."* … *"And it helps that
> the 4th copy seems to add less in this simulation."*

Both halves of that are supported by what is on the record. Adeline's per-copy marginals were **−0.048 /
−0.044 / −0.024** for copies 2/3/4 — so the 4th copy is worth **about half** what the 2nd and 3rd are, and
it is the copy most exposed to the two biases: it is the one most likely to be a **stranded duplicate**
under the legend rule (which the engine does enforce, `EnforceLegendRule`, CR 704.5j), and the marginal
value it does have is the inflated kind, since her power is the creature count and nothing here ever chump
blocks her. **Taking the copy whose measured value is smallest AND whose measured value is least
trustworthy is the right one to give up.**

**The settled counts, all four by user ruling:** Knight Exemplar **4**, Benalish Marshal **4**, Adeline
**3**, Valiant Knight **0**.

#### The current best WhiteKnights list under all four rulings

`j2_ad3_sp4` — Adeline 3 with the freed slot to a 4th Silverblade Paladin (a measured null at −0.0013, so
the swap is close to free on the modelled axis):

| n | card | vs shipped |
|---|---|---|
| 4 | Dauntless Bodyguard | — |
| 4 | Venerable Knight | — |
| 4 | Worthy Knight | — |
| 4 | Accorder Paladin | — |
| 4 | **Silverblade Paladin** | **1 → 4** |
| 4 | **Benalish Marshal** | **2 → 4** |
| 4 | Knight Exemplar | — *(ruling: stays)* |
| 3 | **Adeline, Resplendent Cathar** | **1 → 3** *(ruling: not 4)* |
| 3 | Hero of Bladehold | — |
| 1 | Sol Ring | — |
| 1 | Swords to Plowshares | — |
| 1 | Unexpectedly Absent | — |
| 19 | **Plains** | **22 → 19** |
| 4 | **Remote Farm** | **0 → 4** (introduced) |
| | *gone:* Acclaimed Contender 3, Aether Vial 3, Valiant Knight 1, Lightning Greaves 1 | |
| **60** | | **11 cards changed** |

23 lands, still mono-white, curve still topping at `{2}{W}{W}`. Its pooled figure lands with round J′; the
Adeline-4 version of the same chassis measured **−0.2959**, and the 4th Adeline is worth ~−0.024 of that, so
expect roughly **−0.27** — better than the shipped list by more than this deck's entire exhaustive keep
table (−0.1985), on 11 changed cards rather than 14.

**A limitation to state rather than bury.** Round J′ was launched before the Adeline ruling, so its four
cut-arms (Bodyguard, Venerable Knight, Worthy Knight, Accorder Paladin) sit on an **Adeline-4** chassis.
Their arm-vs-arm differences remain exact — `j2_dbg3_sp4 − j2_ad3_sp4` is precisely *"is cutting a Bodyguard
better or worse than cutting the 4th Adeline"* — but their **levels** are against a list that will not ship.
Adeline's count does interact with the other creatures (her power is the creature count, so cheap bodies
feed her), though only to second order. The round was left running rather than restarted a second time;
anything that looks adoptable gets re-measured on the Adeline-3 chassis before it is believed.

### "ATTACKS RECEIVED" may be the metric that explains the whole campaign — INDICATIVE, round L confirms

The user, on the settled list: *"I'll admit to being a little surprised that Hero of Bladehold is holding
up, but I guess the remote farm + sol ring acceleration is helping it out?"* and then the sharper version:
***"It is a good card, but you need to deploy it early enough for it to be good in goldfish."***

That second remark names a statistic no screen in this campaign has ever reported, and it is exactly
computable from `--log-dir` logs. **A creature cast on turn C in a game won on turn W attacks `W − C`
times**, because it is summoning sick the turn it lands. So a card cast on the winning turn contributes
*literally nothing*, and for Hero of Bladehold — whose entire payload is an attack trigger — zero attacks
means a 3/4 that did nothing at all. `logs/wk_screen/curve_probe.py` reports it.

**⚠ THE NUMBERS BELOW ARE INDICATIVE ONLY — do not cite them as results.** They come from the 79 logs left
by the turn-3 replay, which were *selected* for having been turn-3 kills in a different apparatus. That
selection biases the sample toward fast, accelerated draws, and it is the **shipped** list, not the settled
one (hence Remote Farm at 0%). `run_l.sh` reruns this on 3,000 unselected games of the settled list.

| card | never cast | casts that **never attacked** | mean attacks |
|---|---|---|---|
| Accorder Paladin | 24% | **17%** | 1.52 |
| Worthy Knight | 57% | 21% | 1.32 |
| Silverblade Paladin | 87% | 30% | 0.80 |
| Adeline, Resplendent Cathar | 86% | 36% | 0.73 |
| **Hero of Bladehold** | **81%** | **53%** | **0.60** |
| **Knight Exemplar** | **72%** | **60%** | **0.44** |

**If this holds on an unbiased sample it is the unifying mechanism of the entire campaign**, and it is a
better one than any of the card-by-card stories told above, because it predicts all of them from one fact:
*a card that arrives after the race is over contributes nothing, so value is dominated by mana cost.*

* It explains why every attempt to cut a **one- or two-drop** measured *worse* — Dauntless Bodyguard
  (+0.0087), Worthy Knight (+0.0042). Those are the cards that actually attack.
* It explains **Knight Exemplar's weak lord half** without the token-subtype story needing to do any work:
  60% of its casts never attack at all. A lord that is not on the battlefield during the attack pumps
  nothing, whatever its subtype filter says.
* It explains why the **4th Hero is worse than nothing** (+0.0037) while the 3rd is a null: extra copies of
  the most expensive card in the deck are the most likely to be stranded.
* It explains why the **25th land is clearly wrong** (+0.0144) — more lands means fewer of the cheap
  threats that do the work.
* And it is the same fact the campaign already stated about its biggest result, from the other direction:
  *"nearly all the value is seeing her earlier"* for Adeline. **Arrival time, not card quality, is what
  this apparatus is mostly measuring.**

**On the acceleration question specifically: Sol Ring was in play at 40% of Hero casts against a base rate
of ~16–18% at turns 2–4 — a ~2.4× enrichment.** So the user's hypothesis has support: Sol Ring genuinely
does deploy Hero ahead of curve, via the `{2}` of `{2}{W}{W}` (T1 Sol Ring → T2 two lands → Hero on turn
**two**). Remote Farm should do the same one turn later (it enters tapped but taps for `{W}{W}`, so
T1 Farm / T2 land / T3 land yields four mana on turn **three**), and it cannot be seen in this sample
because the shipped list holds none. **But the enrichment figure is exactly what the selection bias would
manufacture**, since turn-3 kills need acceleration by definition — so it must be re-measured before it is
believed.

#### CORRECTION, same session: attacks are NOT a uniform currency, and Hero needs exactly ONE

The user immediately found the flaw in the metric above: *"it does attack for 7 + 1 for each pre-existing
creature on board, so I suspect it usually wins on the turn it attacks."*

That arithmetic is exact. Hero attacks for **3** (himself) **+ 2×2** for the two Soldier tokens — they are
created *tapped and attacking* and, because both triggers fire on the same attack with tokens ordered
first, they receive their own **battle cry** pump — **= 7**, plus **+1 per pre-existing attacker** (battle
cry reads "each *other* attacking creature", so Hero does not pump himself). So counting "attacks received"
prices a Hero swing identically to a 2/1 swing, which is badly wrong, and the table above understates him.

Measuring it directly — same caveats, same biased 79-log sample:

| | turns | mean damage |
|---|---|---|
| Hero able to attack (cast on an earlier turn) | 9 | **20.00** |
| Hero not available | 232 | 7.85 |
| **gap** | | **+12.15** |
| LETHAL turn, Hero able | 7 | 22.86 |
| LETHAL turn, Hero not | 72 | 14.82 |

**The predicted package is 7 + 1 per pre-existing attacker; at the 4–5 bodies these boards hold that is
11–12, against a measured gap of +12.15.** The card-data arithmetic and the logged damage agree to within
noise, which is a genuine cross-check of the engine's battle-cry-on-tokens implementation as well as of the
claim. **And 7 of the 9 Hero-attacking turns were the lethal turn — 78%.** The user's *"usually wins on the
turn it attacks"* is supported.

**This resolves the apparent paradox in Hero's copy count.** He does not need to attack often; he needs to
attack **once**, and he does so in 47% of games. So the right reading of "mean 0.60 attacks" is not
*"weak"* but *"either on time or irrelevant"* — and that is exactly why the **3rd copy is a null and the
4th is negative**: in a deck that kills on turn 4–5 you only ever cast one, so extra copies buy *finding*
him, never *using* him. That is the same sharply-diminishing shape as Adeline's legend-rule problem,
reached by a completely different mechanism, and it suggests a general rule for this deck: **for anything
at the top of the curve, redundancy is worth only what it adds to the chance of having one on time.**

It also means the "attacks received" table must be read as *attacks × damage-per-attack*, never as a count.
Knight Exemplar's 60%-dead figure still stands as written, because a lord's payload is small and continuous
rather than a single large burst — but Hero's 53% does not mean what it looks like.

**A caution against over-reading this, too.** "Attacks received" is a property of the *apparatus*, not of
Magic. Against a real opponent the game lasts longer, so late arrivals matter more and the metric
compresses — which is another way of saying this engine systematically prefers cheap cards. That is worth
holding next to the fact that the campaign's recommendations are overwhelmingly *"more cheap threats"*.

### Round J′ RESULT — held-out confirmed, and the campaign's first exact null control

Pooled over 9.4M + 9.8M, reference `g_l23` (Exemplar 4, Adeline 4, Silverblade 3, 23 lands). Every arm
also carries the common sink Silverblade 3→4.

| arm | long | 2hg | std | **pooled** | step vs ref | meas. | conf. |
|---|---|---|---|---|---|---|---|
| `j2_dbg3_sp4` Bodyguard 4→3 | −0.2682 | −0.4083 | −0.2331 | −0.3032 | −0.0075 | −0.0043 | −0.0107 |
| `j2_wor3_sp4` Worthy Knight 4→3 | −0.2650 | −0.4103 | −0.2382 | −0.3032 | −0.0075 | −0.0063 | −0.0086 |
| `j2_vk3_sp4` Venerable Knight 4→3 | −0.2671 | −0.4068 | −0.2314 | −0.3018 | −0.0061 | −0.0021 | −0.0101 |
| `g_l23` | −0.2583 | −0.3987 | −0.2349 | −0.2958 | — ref | | |
| `j2_acc3_sp4` Accorder Paladin 4→3 | −0.2450 | −0.3833 | −0.2205 | −0.2816 | **+0.0142** | +0.0143 | +0.0141 |
| `j2_ad3_sp4` Adeline 4→3 | −0.2501 | −0.3462 | −0.2314 | −0.2752 | **+0.0205** | +0.0201 | +0.0210 |

* **THE NULL CONTROL WORKS, and it is the campaign's first experimentally-measured zero.** Bodyguard minus
  Venerable Knight — provably the same card, so expectation *exactly* zero — is **−0.0014** pooled (−0.0022
  on the measurement block alone). So the noise floor on a step of this shape is ~0.002 *as measured*,
  not as asserted by a formula. Every other step in this table should be read against that number.
* **Accorder Paladin 4→3 is +0.0142 and reproduced to within 0.0002** (+0.0143 / +0.0141) — the most
  stable number in the campaign. The last never-varied card is now measured, and 4 is correct. Read it as
  the WEAK direction though: the user flagged Accorder as *"notably better in goldfishing than in real
  play"*, and "don't cut it" is exactly what an inflated card produces.
* **Adeline 4→3 is +0.0205**, also reproduced (+0.0201 / +0.0210), and almost all of it is 2HG (+0.0525
  vs +0.0072 long / +0.0038 std) — i.e. concentrated where she makes **two** tokens per attack.
* **The payer does not matter; the gain is the Silverblade.** Three different cards paying for the 4th
  Silverblade land at −0.0075, −0.0075 and −0.0061 — a spread of 0.0014, which is exactly the null
  control's own noise. So this round found no adoptable cut: it found that the 4th Silverblade is worth
  ~−0.007 in this chassis (against −0.0013 in round H's), and the settled list already holds it.

**WhiteKnights is now sink-exhausted in the strict sense** of [[sink-price-is-not-a-refutation]]:
`min(sink price) > best cut`. The cheapest sink is Hero's 4th at +0.0037, everything good is at 4, and no
cut measures better than ~0 net. Further gains need a *new card*, or the user's ruling on Lightning
Greaves — not another round.

### Round L — both Adeline objections, measured

3,000 unselected games per list, each on its own generated table (`adeline_probe.py`).

| | Adeline 4 (`g_l23`) | Adeline 3 (settled) |
|---|---|---|
| reaches the battlefield | 48.1% | 39.6% |
| arrival turn 2 / 3 / 4 | 61 / 944 / 392 | 51 / 780 / 332 |
| **won before she ever landed** | **51.9%** | **60.4%** |
| ≥2 copies in hand+play at once | 10.8% | 6.2% |
| **stranded duplicate (copy in hand, one already out)** | **9.5%** | **5.3%** |

**CHUMP-BLOCK EXPOSURE, the user's objection, quantified.** At the lethal attack the mean creature count —
which *is* Adeline's power — is **6.95**, against mean lethal damage **18.45**. So her body is **37.7% of
the killing blow at 1×, and 75.4% at 2×** with soulbond double strike. Silverblade is at 4 and pairs by
highest effective power, so **2× is the common case: roughly three-quarters of the lethal attack is one
creature that a single 1/1 chump blocker would stop.** This engine has no blockers, so all of it lands,
every game. The user's *"she is overstated in goldfish … cannot be chump blocked"* is not a caveat, it is
the dominant fact about her measured value.

**And the stranding half holds too:** the 4th copy buys **+8.5pp** chance she appears at all and costs
**+4.2pp** of games where a copy rots in hand under the legend rule. Both reasons for Adeline 3 are
quantitatively supported, and she is absent entirely in **60%** of wins either way.

### Round L — the CONTENDER dig claim
> **CORRECTION 2026-09-27 — THIS SECTION'S CAUSE IS WRONG, AND THE OTHER AGENT WAS RIGHT.**
> The section below concludes the report was *"stale, not false"* because site 10 is default-ON and
> card-agnostic. Site 10 is card-agnostic only **within the set of casts no param-keyed class claims**, and
> `TurnSolver::ParamKeyedDrawClass` claims any `etb_dig_count > 0` cast — which is Acclaimed Contender, in
> both worlds. The class that *does* claim it, `MTG_ACQ_DIG`, is **depth-0-only by construction**, so at the
> d3/d5 depths every measurement here was taken at, the same-turn deploy never happens. The
> "SCOPE = d0 ONLY" note dismissed below as *"dated 2026-08-19 and superseded"* is **not superseded**; it is
> stated twice in current code and the measured depth ladder matches it exactly (d0 deploys, d1/d3/d5/d7/d9
> do not, at every budget to 5000 ms). A control with the same card placed in HAND instead of behind the dig
> casts it the same turn, which isolates mid-phase acquisition as the sole cause.
> **Consequence: the Acclaimed Contender cut (−0.0095) was measured against a handicapped Contender and
> should be re-opened once this is fixed** — an under-modelled card flatters the arm that cuts it
> ([[bracket-notes-are-the-judgement-call]]). The `dug_probe.py` 0-of-3,000 result below was not a thin
> sample either; it was structural.
> Full diagnosis, deterministic repro and fix directions: `docs/design/contender-same-turn-deploy-searched.md`.
> Green guard on the one working depth: `test/scenarios/whiteknights_contender_same_turn_deploy.json`.
> Everything below is kept as written, since the dig-target ranking findings still stand.

**Original section (cause superseded above):** stale, not false, and 1.8% in impact

Another agent reported *"a bug with Acclaimed Contender where the search cannot deploy creatures received
the same turn."* Verified against the tree rather than accepted ([[verify-done-claims-in-tree]]):

* **`MTG_BP_PUT_IN_HAND` — "THE GENERAL RULE" — is DEFAULT ON and armed in the ROLLOUT**, i.e. at searched
  depths: `TurnSolver.cpp:29510` sets `deferred_put_armed` whenever `HandGainedACard(hand_at_cast, state)`,
  card-agnostically, and `BpSiteMask()` returns `base | 0x400` (site 10) by default. This is exactly the
  user's framing — *"a breakpoint should open when we add the card"* — and it is what the code does.
* **The `MTG_ACQ_DIG` note that says "SCOPE = d0 ONLY" is dated 2026-08-19 and is superseded.** Its own
  successor closes with *"=0 is the hatch, and it is what every pre-2026-09-18 measurement in this file was
  taken under."* The code justifies opening it with the user's earlier correctness ruling:
  *"same-turn playability of the found card is a correctness requirement (USER 2026-09-06, 'we need to be
  able to play it'), not a search lever."*
* **WHICH card the dig takes is also searched**, not a heuristic: `MTG_ETBDIG_AXIS` default ON,
  `MTG_ETBDIG_WIDTH` default **3** at a measured knee (W3−W2 = −0.0099; W5−W3 = +0.0000 on every seed),
  against a measured spread of legal matches (5.9% one, 60.0% two, 6.9% three, 23.4% four, 3.7% five,
  mean 2.6).

**The measured residual, from 3,000 games of the shipped list:**

| | count |
|---|---|
| Acclaimed Contender casts | 876 |
| …where the dig landed a card in hand | 821 |
| …where that card was a 1-mana Knight | 186 |
| …and ≥1 untapped Plains remained (definitely castable) | 48 |
| …**but that turn was the LETHAL turn** (declining is harmless) | **32** |
| …**genuine missed same-turn deploys** | **16** |

**So the real impact is 16 of 876 Contender casts — 1.8% of casts, 0.5% of games.** Directionally the
report was onto something; on cause it was stale, and on magnitude it was off by a wide margin. Nothing
here moves the ~−0.0095 Contender result. *(`dug_probe.py` separately found 0 of 3,000 games casting a
card acquired mid-phase, so 0 of those 16 opportunities converted — a thin sample, but the honest reading
is that the class is armed and something downstream is not converting it.)*

**Where the user's proposed ranking WOULD pay.** Per-copy dig picks over 821 landed digs: Adeline **40**
and Lightning Greaves **39** (both 1-ofs, taken ~1.5× their share), Hero 29, Marshal 28, Accorder 27,
**Knight Exemplar 27**, Venerable 24, Worthy 24, Bodyguard 22, and a redundant Contender **16** — least of
all. So the outcome axis genuinely is choosing. But **Knight Exemplar is taken as often per copy as
Accorder Paladin despite 73% of its casts never attacking**, which is the width-3 window over *library
order* failing to reach the better candidate. The user's rule — *"take a 1-drop we can play immediately or
a lord that would be effective next turn"* — belongs in `DecisionProvider::EtbDigCandidates`, whose base
returns library order and whose own comment concedes *"the base rule cannot rank them."* Direct precedent:
`AttackDigPutCandidates` ranks Armored Skyhunter's dig by power granted. That is
`heuristic-optimization.md` work (propose, sweep, validate held-out, adopt in the archetype provider).

### Round L — the curve, on 3,000 unselected games of the settled list

Replaces the indicative table above. `never` = share of games the card is never cast; `dead` = share of
casts that never attack (`win_turn − cast_turn = 0`).

| card | never cast | **dead** | mean attacks |
|---|---|---|---|
| Accorder Paladin | 43% | **10%** | 1.64 |
| Worthy Knight | 61% | 28% | 1.26 |
| Hero of Bladehold | 81% | 28% | 0.91 |
| Adeline | 60% | 26% | 0.81 |
| Benalish Marshal | 58% | 48% | 0.72 |
| Silverblade Paladin | 65% | 47% | 0.69 |
| **Knight Exemplar** | **78%** | **73%** | **0.34** |

**Hero is far better than the biased sample suggested (28% dead, not 53%), and Knight Exemplar is the worst
card in the deck on this metric by a wide margin.** Which is consistent with every screen — and is exactly
the half of Exemplar the apparatus can see.

**Hero's acceleration, the user's hypothesis, split cleanly:**

| accelerant | present at a Hero cast | base rate at turns 2–4 | verdict |
|---|---|---|---|
| **Sol Ring** | **41.5%** | ~16–18% | **~2.5× enriched — confirmed** |
| Remote Farm | 28.6% | ~37.5% (weighted) | **below base — anti-correlated, refuted** |

And the clincher for Sol Ring: **89 of 598 Hero casts happen on turn 2**, which is arithmetically
impossible without it (T1 Sol Ring → T2 two lands = `{W}{W}{C}{C}`). Remote Farm fails for the reason that
makes it good elsewhere — it **enters tapped**, so having one in play means a turn of mana already spent;
it pays for `{W}{W}` costs, not for deploying the top of the curve early.

**"It usually wins on the turn it attacks" — confirmed at scale:** **425 of 538** turns where Hero could
attack were the lethal turn (**79%**, against 78% on the small sample). Mean damage 18.62 when he can
attack vs 8.66 when he cannot.

### BENALISH MARSHAL 2→4 WAS NEVER MEASURED — nine rounds of assumption, and round M

The user asked whether Marshal 2→4 was established empirically, noting their own agreement was *"just a
hypothesis … I still want to do it based on empirical data"*. Checking instead of assuming: **it was not.**

**What the record actually contains.** The only *isolated* measurement is screen 3's group B, where the cut
was held fixed (Valiant Knight 1→0) and only the sink varied — and the 3rd Marshal was the
**second-weakest of five**:

| the freed slot becomes | Δ std | Δ 2HG | Δ long | late-wtd |
|---|---|---|---|---|
| 2nd Adeline | −0.0233 | −0.0776 | −0.0403 | **−0.0481** |
| 2nd Silverblade Paladin | −0.0190 | −0.0295 | −0.0257 | −0.0255 |
| **3rd Benalish Marshal** | **−0.0097** | **−0.0004** | **−0.0031** | **−0.0037** |
| a 23rd Plains | −0.0101 | −0.0028 | +0.0058 | −0.0016 |
| 4th Hero of Bladehold | +0.0065 | +0.0036 | +0.0089 | +0.0072 *worse* |

**And the per-format split makes it worse, not better: the 3rd Marshal is a DEAD NULL in 2HG (−0.0004)
and near-null in long (−0.0031). All of its value is at 20 life** — i.e. the case is weakest in exactly
the two formats carrying **0.8 of the late weight**. That is a coherent shape for an anthem (it matters
most pushing the last few points of a short game, least when 60 damage is required), but it is not support
for a 4-of.

**The −0.0208 that has been quoted for it is a BUNDLE, not a Marshal number.** `a_ac1_bm4` is *Contender
3→1 **and** Marshal 2→4 together*; cutting Contender was independently good, and the half-sized
`a_ac2_bm3` (one swap each way) measured −0.0132. Quoting the bundle as evidence for the Marshal half was
an error, and it propagated into the candidate tables above.

**Why it nonetheless might be right now.** Marshal is the deck's **only** `affects_all_creatures` lord —
Knight Exemplar, Valiant Knight, Inspiring Veteran and Marshal of Zhalfir are all `["Knight"]` — so its
value scales with the **creature count**, and screen 3 measured it on the SHIPPED chassis with Adeline 1
and Silverblade 1. The settled list has far more creatures (Adeline 3 making a token per attack — two in
2HG — Worthy Knight 4, Silverblade 4). So the null may be an artifact of the old chassis. That is a
hypothesis with exactly the status the user's was.

**Round M** (`mkspec_m.py`, `run_m.sh`, seeds 10.2M / 10.6M, 4 arms, 3 new tables) decides it on the
settled chassis. The sink is the 24th Plains, and one arm exists purely to price it:

| arm | change from `j2_ad3_sp4` | purpose |
|---|---|---|
| `j2_ad3_sp4` | — | reference; its table comes from round J′, so it is free and on this round's seed block |
| `m_sp3_p20` | Silverblade 4→**3**, Plains 19→**20** | **prices the 24th land in this chassis**, paying with a card round H measured as a null (−0.0013) |
| `m_bm3_p20` | Marshal 4→**3**, Plains 19→**20** | minus the row above = **Marshal's 4th copy with the land cancelled exactly** |
| `m_bm2_p20_hob4` | Marshal 4→**2**, Plains 19→**20**, Hero 3→**4** | the 3rd copy as well; Hero's 4th has a measured price (+0.0037) |

Both user rulings are asserted as invariants of every arm in the generator (Exemplar 4, Adeline 3), so a
future edit cannot silently reopen a settled count.

### Every round tested all three formats, and 2HG's heads are provably LIVE

Recorded because the question was asked and because the repo's own convention
([[2hg-opponent-heads]]) warns that heads are **often inert** and must be proven rather than assumed.

| format | `starting_life` | `opponent_heads` | games per arm | weight |
|---|---|---|---|---|
| long | 40 | 1 | 20,000 paired | 0.5 |
| 2hg | **30** | **2** | 20,000 paired | 0.3 |
| std | 20 | 1 | 20,000 paired | 0.2 |

Every arm of every screen and every round ran all three, and the held-out block repeats all three. **The
heads are live for this deck, and Adeline is the proof:** `attack_tokens_per_opponent` defaults to `true`
(`src/cards/CardDatabase.h:761`, read at `CardDatabase.cpp:855`, whose comment names Adeline), so at
`opponent_heads: 2` she creates **two** tokens per attack, faithful to her *"for each opponent"* clause —
while Hero of Bladehold sets the flag `false` explicitly, so his flat two Soldiers correctly do not scale.
That asymmetry is why Adeline's gain concentrates in 2HG and it is a real card behaviour, not a life-total
artifact.

2HG is also where the largest effects appear (the candidate: −0.4211 in 2hg against −0.2411 in std), which
is why a card measuring **null there** — as the 3rd Marshal does — is a real problem for its case.

### WHY the Knight Exemplar LORD HALF is weak — from `cards.json`, not from a story

**Read this section as an explanation of the ~−0.010 rate cost only.** It explains why Exemplar's *anthem*
is the weakest of the deck's three, which is a real finding; it does **not** argue for cutting the card,
and the paragraphs below that treat "cut Exemplar" as a conclusion are superseded by the ruling above.

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

### Round H on Knights — the land ladder TURNS OVER, and Adeline outbids the land

Pooled over blocks 7.2M / 7.6M, reference = screen 7's winner `g_kn_ac2_ad3`. Every arm's held-out block
agrees with its measurement, and the top two **grew** on held-out rather than shrinking:

| arm | Contender / Adeline / lands | long | 2hg | std | **pooled** | step vs ref | meas. | conf. |
|---|---|---|---|---|---|---|---|---|
| **`h_kn_ad4_l21`** | 0 / **4** / **21** | −0.0753 | −0.2006 | −0.0650 | **−0.1108** | **−0.0383** | −0.0336 | −0.0430 |
| `g_kn_l22` | 0 / 3 / 22 | −0.0628 | −0.1654 | −0.0675 | −0.0946 | −0.0221 | −0.0197 | −0.0244 |
| `h_kn_moz3_l23` | 0 / 3 / 23, −1 MoZ | −0.0522 | −0.1666 | −0.0595 | −0.0880 | −0.0155 | −0.0147 | −0.0163 |
| `h_kn_ad2_l23` | 0 / **2** / 23 | −0.0478 | −0.1179 | −0.0681 | −0.0729 | **−0.0004** | −0.0006 | −0.0001 |
| `g_kn_ac2_ad3` | 2 / 3 / 20 | −0.0539 | −0.1316 | −0.0304 | −0.0725 | — ref | | |

* **THE LAND LADDER TURNS OVER AT 23, and it was paid for two independent ways to make sure.** Both
  23-land arms come in *worse* than the 22-land arm: −0.0880 paying with a Marshal of Zhalfir, −0.0729
  paying with an Adeline. Round G's monotone 20→21→22 does not extend, so the answer to the question round
  H existed to ask is **no**.
* **The binding constraint is not the land count — it is that ADELINE OUTBIDS THE LAND.**
  `h_kn_ad4_l21` (21 lands, Adeline 4) beats `g_kn_l22` (22 lands, Adeline 3) by **−0.0162**. And
  `h_kn_ad2_l23`, which buys the 23rd land by giving up an Adeline, is a **total null** (−0.0004): the land
  gives back precisely what the Adeline was worth. So Knights' land count settles at **21** *because* the
  4th Adeline wins the slot, not because 21 is a magic number.
* **`h_kn_ad4_l21` is the best Knights list measured: −0.1108 pooled**, Contender 4→0, Adeline 1→4,
  20→21 lands. Four slots freed, three to Adeline and one to a Plains, exactly as the slot arithmetic
  predicted was the only way to have both of round G's good ideas.

### Knights runs FIFTEEN Knight-only lords, and Knight Exemplar is dominated by two of them

The same `cards.json` read that explained WhiteKnights applies here, and it lands differently — which is
worth stating carefully, because the tempting move is to transfer the WhiteKnights conclusion wholesale.

| lord | cost | body | anthem | copies in Knights |
|---|---|---|---|---|
| Inspiring Veteran | `{R}{W}` | 2/2 | `["Knight"]` +1/+1 | 4 |
| Marshal of Zhalfir | `{W}{U}` | 2/2 | `["Knight"]` +1/+1 | 4 |
| **Knight Exemplar** | `{1}{W}{W}` | 2/2 | `["Knight"]` +1/+1 | **4** |
| Haytham Kenway | `{2}{W}{U}` | 3/3 | `["Knight"]` **+2/+2** | 3 |
| Benalish Marshal | `{W}{W}{W}` | 3/3 | **all creatures** | 4 |

**Knight Exemplar is DOMINATED on rate: it costs three mana for the identical effect that Inspiring
Veteran and Marshal of Zhalfir supply for two, and its only additional text — first strike, and the
indestructible grant that is not in its `parameters` at all — is inert against a passive opponent.** That
is a card-level dominance argument, which is the kind [[a-null-must-not-break-a-tie]] asks for, and it is
independent of any measurement. Screen 7's −0.0368 for 4→3 is consistent with it.

**But the WhiteKnights mechanism does NOT transfer wholesale, and this is the important half.** Over there
the Knight-only anthems were bad because the deck's damage had moved into Human and Soldier *tokens* that
those anthems cannot see. Knights is a genuine tribal deck: **15 of its 60 slots are Knight-only lords**,
and its only token source is Worthy Knight (Adeline aside), with no Hero of Bladehold in the main. Most of
a Knights board really is Knights, so the dilution effect that condemned Exemplar in WhiteKnights is much
weaker here. Knights is also not mono-white — Inspiring Veteran is `{R}{W}`, Marshal of Zhalfir `{W}{U}`,
Haytham `{2}{W}{U}`, which is what the Unclaimed Territory / Secluded Courtyard / Tournament Grounds mana
base exists to support. **The lords are the deck's identity, not a misload.** This is the same reason
Knights keeps Aether Vial where WhiteKnights cuts it, and the fifth axis on which the two decks disagree.

**So the interesting tension in Knights is the one round H just created.** Adeline is herself a Knight (so
the lords pump *her*), but her tokens are plain Humans that 15 of the deck's 60 cards cannot pump — and she
still won the slot four times over. That says the next Knights question is **not** "more Adeline" but
**which lord to cut**, and unlike WhiteKnights there are *four* lord types to rank rather than one.

### Round I as written is now MIS-SPECIFIED — do not run it

`logs/wk_screen/mkspec_i.py` was built before round H landed, and round H invalidated its sink. Every one
of its arms pays for an Exemplar cut **with a land**, walking the count to 22, 23 and 24:

| arm | Exemplar | lands |
|---|---|---|
| `h_kn_ad4_l21` (ref) | 4 | 21 |
| `i_kn_ke3_l22` | 3 | 22 |
| `i_kn_ke2_l23` | 2 | **23** |
| `i_kn_ke2_l22_h4` | 2 | 22 + Haytham 4 |
| `i_kn_ke0_l24_h4` | 0 | **24** + Haytham 4 |

Round H has just shown the 23rd land is **negative**, so two of those arms buy a good cut with a bad sink
and report the sum; the Exemplar axis would be confounded with a land axis that is now known to point the
wrong way. **The fix is the round J structure:** fix the *cheapest* sink — **Haytham Kenway 3→4, a measured
null from round G** — on every arm, and cut one card per arm, so the sink price is common and cancels in
every pair. With four lord types to rank that is also the only shape that can rank them: arms cutting one
Exemplar, one Inspiring Veteran, one Marshal of Zhalfir and one Haytham, all against the same sink.
See [[sink-price-is-not-a-refutation]]. The `L` parameter should be dropped entirely — its slot ledger only
balances at `L=22`, which is a coincidence of the one-land-per-cut design rather than a choice.

### Round M RESULT — BENALISH MARSHAL 4 IS WRONG; 3 is the count

Pooled 10.2M + 10.6M, reference `j2_ad3_sp4` (the settled list, Marshal 4). Every arm also takes the 24th
Plains; `m_sp3_p20` exists only to price that land, paying with a 4th Silverblade.

| arm | long | 2hg | std | **pooled** | step vs ref | meas. | conf. |
|---|---|---|---|---|---|---|---|
| `m_bm2_p20_hob4` Marshal **2**, +Hero 4 | −0.2547 | −0.3470 | −0.2232 | −0.2761 | **−0.0093** | −0.0065 | −0.0122 |
| `m_bm3_p20` Marshal **3** | −0.2519 | −0.3435 | −0.2291 | −0.2748 | **−0.0081** | −0.0065 | −0.0097 |
| `j2_ad3_sp4` Marshal 4 | −0.2425 | −0.3359 | −0.2234 | −0.2667 | — ref | | |
| `m_sp3_p20` Silverblade 4→3 (land control) | −0.2390 | −0.3362 | −0.2225 | −0.2648 | +0.0019 | +0.0033 | +0.0005 |

**The assumption-free read is the matched pair, where the 24th land cancels exactly:**

> `m_bm3_p20 − m_sp3_p20 = −0.0100` — the 4th Marshal is worth **0.0100 less than the 4th Silverblade.**

Round J′ put the 4th Silverblade at ≈ −0.007, so **the 4th Marshal is ≈ +0.003 — slightly harmful.** Cutting
it for the 24th land is **−0.0081 and it GREW on held-out** (−0.0065 → −0.0097). Marshal 2 adds only
−0.0012 more, so **3 is the count and 2 is inside noise of it.**

**So the user's hypothesis does not survive the test they asked for**, and asking was right: the −0.0208
previously quoted for Marshal was a *Contender + Marshal bundle*, and the only isolated figure on record
was the 3rd copy at −0.0037 — a **dead null in 2HG**.

**The honest counterweight, stated as a price not a verdict.** An anthem's `+1/+1` is in the class this
apparatus **under**-values: the power half is fully modelled and dominates a race, but the toughness half
pays only against blockers and damage-based removal, neither of which exists here. So the 4th Marshal costs
≈ 0.008 in goldfish speed and buys resilience the engine cannot see — the same shape as Knight Exemplar's
indestructible. User's call. See [[bracket-notes-are-the-judgement-call]].

#### WHAT THE 4th MARSHAL IS COMPARED AGAINST — asked by the user, 2026-09-27

The user's reaction was *"I'm a bit surprised on the Benalish Marshal, though. What are we comparing it
against?"* — the right question, because the round M table reports a step against a reference and the
reference is not "nothing". Three comparisons, ordered most- to least-robust:

| # | the 4th Marshal is measured against | figure | what it assumes |
|---|---|---|---|
| 1 | **the 24th land** (a 20th Plains) | **−0.0081**, held-out −0.0097 | nothing; direct arm vs reference |
| 2 | **the 4th Silverblade Paladin** | **−0.0100** | nothing; the land cancels in the matched pair |
| 3 | **zero** (absolute value of the card) | **+0.003 … +0.009** | imports Silverblade's own worth |

Row 2 is the one to quote. `m_bm3_p20` and `m_sp3_p20` each cut exactly one spell and add exactly the same
land, so the land's price cancels and no constant is imported: **the 4th Marshal is 0.0100 worse than the
4th Silverblade.** Row 3 is a range only because that anchor is **chassis-dependent** — the 4th Silverblade
measured −0.007 in round J′'s chassis and −0.0013 in round H's. **The SIGN is stable under either anchor**
(+0.003 vs +0.0087), so "mildly harmful in absolute terms" survives; only the magnitude moves. Do not quote
a point estimate for row 3.

**THE REFRAME THAT ANSWERS THE SURPRISE: nobody ever chose 4. It accumulated as a SINK.** The shipped
WhiteKnights list runs **Benalish Marshal 2**. Screen A used *"+1 Marshal"* as its **common sink** — the
cheapest way to absorb a freed slot without adding a new variable — and `a_ac1_bm4` used +2. Nine rounds
later the count sat at 4 with **no isolated measurement behind it**, and the −0.0208 quoted in its favour
was a **bundle** (`a_ac1_bm4` = Contender 3→1 *and* Marshal 2→4, where the Contender half was independently
good). Quoting that bundle as a Marshal number was my error and it propagated into the candidate tables.
The only isolated pre-M figure was the **3rd** Marshal as a sink: **−0.0037, second-weakest of five, and a
dead null in 2HG.** So round M is not cutting a card the evidence endorsed — **it is removing an artifact of
how the screens were constructed**, back toward the count the deck shipped with. That is why the result
should be surprising only if one believed the 4 was ever measured.

**A hypothesis, explicitly labelled as one** ([[dont-rationalize-a-measured-cut]]): the user's own
Exemplar-4 ruling raises the deck's anthem saturation — Exemplar 4 + Marshal 4 + Accorder 4 + Hero 3 is 15
pump sources in 60 cards, and since nearly every creature is a Knight, Exemplar's `+1/+1` already covers
most of what Marshal's does. It fits, but it was arrived at after the numbers and is not evidence for any
further cut.

### USER RULING 2026-09-27 — MARSHAL 4 STAYS; the 4th SILVERBLADE PALADIN pays instead

> *"I think I would use the marshal instead of the 4th Silverblade. Silverblade is much better in your
> simulations than in real games. In particular it relies too much on heavy double-strike damage and the
> survival of the 2/2 Silverblade."*

**AND THIS CORRECTS MY OWN FRAMING FROM THE SAME SESSION.** I had told the user the matched pair (−0.0100,
Marshal's 4th vs Silverblade's 4th) was *"the one to quote"* because the land cancels and no constant is
imported. That is right about **apparatus** bias and **wrong about MODELLING bias** — and the matched pair
is the *worst* row in the round M table on that axis, because it differences two cards whose biases point
in OPPOSITE directions:

| card | direction of the goldfish bias | why |
|---|---|---|
| **Silverblade Paladin** | **OVER**-stated, three ways at once | the 2/2 body never dies (no removal, no blockers), so the pairing never breaks, and the double strike is never absorbed by a blocker |
| **Benalish Marshal** | **UNDER**-stated | the `+1/+1` toughness half has nothing here to pay off against |

So the −0.0100 gap is inflated at **both ends**, and stacking the two into one number hid that. Note this is
**not a modelling bug** — `Permanent::paired_with` tracks soulbond correctly and is folded into the keys;
it is that a goldfish never presents the conditions that break the pairing. [[bracket-notes-are-the-judgement-call]]
says an under-modelled card flatters the arm that cuts it; the symmetric case is that an OVER-modelled card
flatters the arm that keeps it, and here both were live in one comparison.

**THE USER'S SHARPER FORM — SOULBOND IS A 2-FOR-1 LIABILITY, AND IT IS RULES-CORRECT.** *"In a real game,
the opponent can remove the silverblade or soulbonded creature and then block and kill the other. This makes
it a liability against removal. Whereas a lord is a much smaller risk in that situation."*

CR 702.94 grants double strike only *"as long as this creature is paired"*, so **the pair breaks when EITHER
half leaves the battlefield** — one removal spell strips double strike from **two** creatures, and the
survivor, now unpumped, loses the block it would otherwise have won. The difference from a lord is one of
SHAPE, not magnitude:

| event | Silverblade pair | Benalish Marshal |
|---|---|---|
| the pump source is removed | both halves lose double strike; survivor is a vanilla body that now dies to a block | others lose `+1/+1` but keep their bodies and keep attacking |
| a partner creature is removed | pairing breaks — same cliff, either direction | Marshal keeps pumping everything else |
| cost to the opponent | answers TWO creatures with one card, and the target is a **2/2** | one card for a **3/3** body, and the rest of the board survives |

**Marshal degrades gracefully; Silverblade falls off a cliff.** And this apparatus can represent **none** of
it: with a passive opponent the pairing never breaks, so **100% of Silverblade's modelled value is its best
case**, and no measurement available here can ever price the liability. That is the quarantine class in its
purest form — a price, never a verdict — so the user's judgement is the terminal authority rather than a
tiebreak against a number.

**It also makes the decision WELL-FORMED rather than a coin flip.** +0.0019 (held-out +0.0005) *is* a
measured null, and [[a-null-must-not-break-a-tie]] requires a null to be broken by a card-level dominance
argument rather than a t-statistic. The removal-liability asymmetry is exactly such an argument.

**THE RULING COSTS NOTHING EVEN AT FACE VALUE, and its arm is already on disk.** Marshal 4 / Silverblade 3 /
24 lands is exactly round M's control arm **`m_sp3_p20`**: measured **+0.0019**, held-out **+0.0005** — dead
inside the ~0.0020 noise floor. Its keep table already exists at `logs/deckcmp/WhiteKnights/m_sp3_p20/`, so
adopting this list owes **no new measurement**. The settled 60 under all rulings:

```
4 Dauntless Bodyguard   4 Knight Exemplar     3 Silverblade Paladin   1 Sol Ring
4 Venerable Knight      4 Accorder Paladin    3 Hero of Bladehold     1 Swords to Plowshares
4 Worthy Knight         4 Benalish Marshal    3 Adeline               1 Unexpectedly Absent
20 Plains   4 Remote Farm                                             = 60
```

#### TWO CONSEQUENCES THE RULING IMPLIES BUT DOES NOT ITSELF DECIDE

1. **The objection is about the CARD, so it extends down the whole ladder — and the shipped list ran
   Silverblade 1.** The campaign walked it **1 → 4** entirely on measured value (2nd copy −0.0255, 3rd
   −0.0071, 4th ≈ −0.007). If the card is systematically over-valued, that whole walk is suspect, not just
   the top copy. It is the MIRROR of the Marshal story: Marshal drifted up as an **unmeasured sink**,
   Silverblade drifted up on **measurements sharing one common bias**. How far down to go is the user's
   call; nothing below the 4th copy has been ruled.

   **WHICH COPIES ARE ACTUALLY FRAGILE — the argument most plausibly lands on Silverblade 2, not 3.** Set
   each measured margin against the ~0.0020 noise floor and ask which could be erased by a
   removal-liability correction:

   | copy | measured | vs floor | robust to the user's objection? |
   |---|---|---|---|
   | 2nd | **−0.0255** | 12x | **yes** — would need a huge haircut to flip |
   | 3rd | −0.0071 | 3.5x | **no** — exactly the magnitude at risk |
   | 4th | ≈ −0.007 | 3.5x | **no** — and this one is now cut by ruling |

   So the 2nd copy is safe on almost any view, while the 3rd and 4th sit precisely in the band a
   liability correction could erase or reverse. **Silverblade 2 is the natural extension of this ruling**
   and is un-measured; it needs no new measurement to be *chosen* (it is a judgement on unmodellable
   text), but it does change the list and therefore the keep table.
2. **THE LAND LADDER WAS PAID FOR IN SILVERBLADE COPIES, so it is contaminated in the direction that hurt
   lands.** Round G traded lands against Silverblades — `g_l22` carried Silverblade **4**, `g_l25` carried
   Silverblade **1** — so the LOW-land arms were flattered by the inflated card. The round G section already
   read the 22/23 rungs honestly on that basis, but the same logic bites hardest at the top: **`g_l25` was
   rejected at +0.0144 having given up THREE Silverblade copies.** At ~0.005 of inflation per copy that is
   ~0.015 — the entire penalty. **So 25 lands may have been rejected on the Silverblade bias alone.** This
   is a hypothesis with arithmetic, not a finding; it is cheap to settle and it should be settled BEFORE
   adoption freezes a land count. Re-test the land ladder against a payer the user trusts (Haytham-style
   measured null, or a straight Plains-for-spell swap), not against Silverblade copies.

### Round N — BASRI, TOMORROW'S CHAMPION, and the exert mechanic it forced

User request: *"Please add Basri, Tomorrow's Champion (a new 2/1) to your tests."* Card data from **Scryfall,
not recall**: `{W}` Legendary Creature — Human Knight **2/1** (Aetherdrift), Modern/Legacy/Vintage legal, so
it clears the standing legality gate (*"a card can only be used in a way that it is legal in some format"*
— which also puts a 2nd Sol Ring permanently out of scope).

    {W}, {T}, Exert Basri: Create a 1/1 white Cat creature token with lifelink.
    Cycling {2}{W}
    When you cycle this card, Cats you control gain hexproof and indestructible until end of turn.

**Why it is a clean test:** Basri is a `{W}` 2/1 **Human Knight** — the same printed body *and subtypes* as
Venerable Knight and Dauntless Bodyguard, both provably blank in this engine. Swapping one for Basri holds
every modelled interaction fixed (Exemplar's Knight-only anthem still pumps it, Worthy Knight still
triggers, Adeline still counts it) and isolates three things: the token ability, cycling, and being legendary.

**EXERT WAS NOT MODELLED, AND MEASUREMENT REFUSED THE SHORTCUT.** I predicted the ability would never be
worth using — tapping a 2/1 to make a 1/1 in a deck whose modal win is turn 4 — and committed in advance to
stopping if a probe disagreed. It disagreed, decisively: shipped without exert, **Cat tokens appeared in 54%
of 600 games, up to FOUR at once.** The reason my reasoning failed is that Basri is a **one-drop** that lands
turn 1 and sits idle, and with the `{T}` repaid at every untap the ability is repeatable **every** turn —
a Cat engine for `{W}`, not a one-off trade, i.e. roughly twice the printed card. Nothing else in the deck
makes Cats, so the Cat count is an exact activation count (`logs/wk_screen/basri_probe.py`).

**Implemented, following the Rimescale Dragon ice lock** — the same shape, a per-permanent reason not to
untap:

| piece | where |
|---|---|
| `Permanent::skip_next_untap` | one-shot flag; *"your NEXT untap step"* is exactly one skip |
| `CardParams::tap_token_exerts` | default **false**, so Sliver Hive is untouched |
| set on activation | `ActivateTapTokensShared` — the **single shared path**, so both worlds get it by construction instead of at ~19 call sites |
| honoured + consumed | `GameEngine::UntapStep` **and** `TurnSolver::SimulateEndAndStartNextTurn`, the documented lockstep pair |

**Verified in both directions, because equality alone can mean a dead feature**
([[digest-equality-can-mean-broken]]):

* **Neutrality** — six blocks (WhiteKnights and Knights × 3 seeds) **byte-identical** before and after, so
  every keep table generated tonight remains valid and `HEAD:src` drift costs nothing.
* **Liveness** — the multi-Cat signature collapsed exactly as *"every other turn"* predicts: 4-at-once
  **9 → 0 games**, 3-at-once **18 → 6**, while single-Cat games held at **231 → 233**.

**A MEASURED CAVEAT THAT MAKES ROUND N A FLOOR.** `ActivateTapTokensShared` is a **greedy spare-mana pass**,
not a searched choice: it activates whenever mana allows, and now pays a real attack for each Cat. Basri
*with* the ability measures **worse** than Basri with the ability stripped — **+0.0190 / +0.0095 / +0.0150**
over three 2,000-game seeds (mean ≈ **+0.0145**). So round N measures a Basri the engine **misplays**, and
its value is a floor ~0.015 below correct play. Fixing that is `heuristic-optimization.md` work (gate the
greedy activation on whether the body would rather attack) and it would also touch Sliver Hive — recorded,
not bundled.

**Two remaining gaps, both inert:** lifelink on the Cat (no `tap_token_keywords` param; the passive opponent
deals no damage so life gain never matters) and the cycling trigger granting Cats hexproof + indestructible
(unmodellable protection, and it needs Cats at all).

**Round N arms** (`mkspec_n.py`, seeds 11.4M / 11.8M, 3 arm tables + one R=10 **pool** table because the
shipped table cannot bucket Basri — that affects only the *shared* column, never the `own` column that is
the result): reference `j2_ad3_sp4`, then Venerable Knight → Basri at **1 / 2 / 3** copies. The generator
asserts both user rulings as invariants (Exemplar 4, Adeline 3) so a later edit cannot reopen them.
**Prediction on record:** one copy neutral-to-slightly-better (the legend rule cannot bite and cycling is a
free option), degrading with copies as duplicates strand.

### USER RULING 2026-09-27 — THE CAT ABILITY IS DISABLED BY HEURISTIC, not removed from the card

The user, on being shown that Basri's `{W}, {T}, Exert` was being activated in 54% of games: *"Yeah, cats
are definitely not worth considering."* Then, on how to act on it: *"For the cats I recommend you just
disable that ability in a heuristic"*, and immediately clarifying the boundary: *"We shouldn't disable it
entirely, since that doesn't make sense, but heuristically it does make sense."*

**That boundary is the whole ruling, and it maps onto an existing precedent exactly.** The ability stays
fully implemented and fully legal — a VALUE gate, not a legality one, which is the Ajani-0 / Serra-(−6)
class already in this engine. The gate sits in `ActivateTapTokensShared` (`src/ai/ManaPayment.cpp`), which
**already stands down under `HumanPlayActive()`** because human play reaches the ability by a different
route (`Action::Kind::TapForTokenPay`). So a person playing the deck is still offered it whenever the rules
allow; only the autonomous greedy spare-mana pass declines. Nothing about the card is narrowed.

**Predicate: the cost EXERTS the source** (`tap_token_exerts`), not "the source is a creature that could
attack". Exert costs the body its *next* untap, so the attack is forgone on a future turn regardless of
which phase the greedy pass runs in — no combat or phase reasoning needed, which is what makes the gate
impossible to get subtly wrong. The two predicates select the **same card today**: the whole database holds
exactly two tap-token cards, Basri (a Creature that exerts) and Sliver Hive (an exert-free Land), so
**Sliver Hive is provably untouched and `slivers_vial` stays byte-identical**. The broader attacker-aware
heuristic is deliberately left unwritten until a second such card exists. `MTG_GREEDY_EXERT_TOKEN=1`
restores the old always-activate behaviour for an A/B (default OFF, per the `EnvOn` convention).

**Two real gaps in the ORIGINAL exert implementation, both found while wiring this and both now fixed.**
Worth recording because each was invisible in the direction that mattered:

1. **`skip_next_untap` was folded into NO state key** — not `FungibilityKey`, not `BuildSimKey`, not
   `dominance::Build()`, not the two-world mismatch checker in `ManaPayment.cpp`. Execution was correct
   (both untap sites honoured the flag, so games really did miss the untap), but the transposition table
   could merge an exerted state with an un-exerted one, leaving **the search blind to the cost of the very
   ability it was choosing to activate**. Now folded in all four, each nonzero-gated so every deck without
   an exert source is byte-identical. Classified off the `paired_with` precedent: it survives cleanup
   deliberately (it is consumed at the *next* untap step), so it must be COMPARED, not refused at
   `AtCleanBoundary`.
2. **The human-play route never set it at all.** `TurnSolver.cpp`'s `TapForTokenPay` branch created the
   token and stopped. The original comment claimed `ActivateTapTokensShared` was "the one shared activation
   path" — it is not; it is one of **two** routes, and it is the one that stands down for humans. Harmless
   while the greedy pass was doing the activating; **now the only route the ability is normally reached
   by**, i.e. precisely where an unmodelled exert would have been a free untap. Fixed there too.

**Consequence for round N: it is SUPERSEDED, not merely floored.** It measured the old greedy policy — and
measured it while the search could not see the exert cost. Basri under the ruling reduces to *a {W} 2/1
Human Knight with cycling {2}{W} that is Legendary*, i.e. a Venerable Knight (provably blank in this
engine) plus a cycling option minus the legend rule. That is a clean, cheap test and it is the honest one;
the Cat ability is out of scope by ruling, not by approximation.

### Round O — GIDEON, ALLY OF ZENDIKAR (user request: "double check")

**The card-data check paid off on the first line.** From Scryfall, not recall: **{2}{W}{W}**, loyalty 4 —
not the `{3}{W}` first assumed. That is *exactly* Hero of Bladehold's cost, which is what makes the
head-to-head below the natural test rather than an arbitrary one. Modern/Legacy/Vintage/Pioneer legal, so
it clears the standing legality gate.

**Strategic read, stated before the run.** Gideon costs 4 and this deck's modal win is turn 4. He enters
summoning-sick, so on the turn he lands the `+1` cannot attack and the `0`'s token cannot either — **`−4`
is the only ability that does anything immediately**. So the prior is that Gideon is an *emblem card* here,
not a token engine: a 4-mana Glorious Anthem on the board we already have, which then dies. Against the
marginal Hero of Bladehold — whose attacking turns were **lethal 79% of the time** (round L) — the prior is
that he loses.

| ability | status | note |
|---|---|---|
| `0`: 2/2 white Knight Ally token | **implemented** | the **Knight** subtype is the payload, not the body |
| `−4`: emblem, creatures you control +1/+1 | **implemented** | new `Player::emblem_team_pump` |
| `+1`: becomes a 5/5 until EOT | **implemented** | `animated_printed_types` + a ~34-site refactor |

**The emblem needed new state, and it landed cleanly.** `ComputeLordBonus` already takes
`const GameState&` and has **29 call sites** — real combat damage, every attack projection, the SBA
toughness recheck, both worlds — so one term inside that function is picked up everywhere in lockstep,
with no caller-side plumbing and no fake battlefield permanent. It deliberately mirrors an
`affects_all_creatures` lord (Benalish Marshal) *including* being unconditional within the function, so it
inherits the callers' existing creature filtering rather than introducing a second, divergent copy of it:
"the emblem behaves like an anthem" is then true by construction rather than by inspection. Held on
`Player` because CR 114 makes an emblem **not a permanent** — a fake anthem permanent would be miscounted
by every "permanents you control" reader and would render in the viewer as a card that does not exist. It
takes `has_city_blessing`'s classification verbatim (monotone, never reset, never removable) and is folded
gated-on-nonzero into the sim key and dominance, so every deck with no emblem source is byte-identical.
**Unlike Serra the Benevolent's emblem it is NOT inert and could not be a no-op** — it is an anthem on a
board we are racing with.

### USER DIRECTIVE 2026-09-27 — "Gideon's +1 needs to be done to evaluate him"

It was first built without the `+1`, as a disclosed floor. The user overruled that, correctly: the `+1` is
the button a *race* wants, so a Gideon without it is not the card being evaluated. Implemented in full.

**The engine had conflated two different questions, and that is the whole difficulty.**
`Permanent::is_animated` answered *both* "is this non-creature currently a creature?" and "does it have
EVERY creature type?" — because the only animation in the pool was Mutavault, whose own wording is "with
all creature types" (CR 205.3b). Gideon's +1 breaks the equivalence: he becomes a Human Soldier **Ally**,
so a Knight lord must not reach him. Animating him through the old machinery would have handed him **+4/+4
from four Knight Exemplars he is not entitled to** — every point of it flattering the arm that plays him,
which is the [[bracket-notes-are-the-judgement-call]] failure mode twice caught in this campaign already.

The split is `Permanent::animated_printed_types` (default **false**, set only by a typed animation) behind
one predicate, **`Permanent::AnimatedAllTypes()`**, now passed as the `all_creature_types` argument at every
lord / haste / subtype-count site. `is_animated` alone still answers the first question. So Benalish
Marshal's `affects_all_creatures` anthem **does** reach an animated Gideon while Knight Exemplar's
Knight-only anthem **does not**. Chosen with that polarity deliberately: the two pre-existing animation
sites needed no change, so Mutavault and every deck that animates nothing are byte-identical by
construction rather than by audit.

**Scope: 34 sites, classified one at a time rather than sed-ed.** Three classes had to be told apart, and
two of them look identical on the page:
* **A — "is it a creature?"** (`IsCreature() || is_animated`): unchanged, ~60 sites. Gideon belongs here.
* **B — "does it have all creature types?"** (the `all_creature_types` argument; direct reads pairing
  `is_animated` with `subtypes_affected` or `CardHasSubtype`): switched to `AnimatedAllTypes()`, 34 sites.
* **C — the P/T add** (`if (animated) pw += animate_power`): unchanged. Gideon must keep this or he is a 0/0.

**Five sites used ONE local for both B and C**, which is the trap: `const bool animated = p.is_animated;`
feeding both `ComputeLordBonus(...)` and `+= animate_power`. Using the wrong one either makes Gideon a 0/0
that dies, or hands him the Knight anthem. An automated pass also produced exactly one **false positive** —
`DecisionProviders.cpp:853`, a P/T add caught because a `ComputeLordBonus(` sat three lines above it in the
lookback window — which is why every change was printed and reviewed rather than trusted.

**NOT a haste concern**, worth recording because it was the first suspicion and it was wrong:
`CanAttackFull` already applies summoning sickness to animated permanents correctly (CR 302.6 tracks
control duration), so a Gideon animated the turn he lands cannot attack — which is why `+1` is deliberately
absent from the cast-turn activation table and priced at zero there. `Permanent::CanAttack`'s
unconditional-haste shortcut is **unreachable** for animated permanents: its only two callers are a
keep-model feature counter and a site already guarded by `card.IsCreature()`. There is no Mutavault haste
bug to write up.

**His animated TOUGHNESS is unmodelled, and that is inert AND safe** — nothing in the engine reads
`animate_toughness`. It cannot kill him because the zero-toughness state-based action gates on
`p.card.IsCreature()`, the PRINTED type, so an animated Planeswalker is exempt exactly as an animated Land
is. Nothing in this game can damage him either way, so the indestructible and damage-prevention riders are
inert by construction too (the Knight Exemplar argument).

### Round O — the VERIFICATION, because byte-identity alone would have proved nothing

Smoke came back **97 passed / 0 failed / 0 play-changed** — every existing deck, including `slivers_vial`
(the only Mutavault deck) and WhiteKnights itself, provably untouched by the 34-site refactor, the emblem,
the exert folds and the exert heuristic. That is the [[digest-equality-beats-a-sign-test]] gate, and it also
keeps round J′'s `j2_ad3_sp4` keep table valid for round O.

But identical digests are exactly what [[digest-equality-can-mean-broken]] warns about: they would look the
same if Gideon's new paths never fired at all. Each ability was therefore proved to FIRE, separately:

| ability | evidence |
|---|---|
| `0` token | `--log-dir` board census: **2/2 Knight Token on turn 4**, the turn he lands |
| `+1` animation | a Gideon-only deck (24 Plains + 36 Gideon, **no other creature in the deck**) wins at avg turn **8.13**, and every ATTACK reads **damage: 7** = 2 (token) + 5 (animated Gideon). Nothing else in that deck can produce 7 |
| `+1` is NOT a Knight | add Knight Exemplar: over 20 games, **23 attacks match the correct model and 0 match the buggy one** (`ex=1, tok=1, dmg=10` = 3 pumped token + 2 Exemplar + **5 unpumped Gideon**; the buggy model predicts 11). The token IS pumped, the planeswalker is not — the refactor's whole purpose, measured |
| `−4` emblem | committed fixture `test/scenarios/whiteknights_gideon_emblem_anthem.json`: six blank 2/1s attack for exactly **18** instead of 12 |

The emblem fixture was then checked for DISCRIMINATING POWER rather than assumed to have it — opponent at
18 wins turn 6 at life 0, at **19** the win slips to turn 7 (so the damage is exactly 18, not 19+), and at
13 it wins turn 6 with −5 overkill. Without the emblem the six Knights deal 12 and the 18-life case fails.
Full scenario suite: **104 passed, 0 failed**.

**So round O is NOT a floor.** All three abilities are live and measured, and the number it returns is the
card.

**Round O arms** (`mkspec_o.py`, seeds 12.4M / 12.8M): reference `j2_ad3_sp4`, then Gideon at 1 and 2
copies against **two payers** — `_hob` cuts Hero of Bladehold (a 4-drop for a 4-drop, so **the curve is
held fixed** and the number is a clean "Gideon vs the marginal Hero"), and `_vk` cuts Venerable Knight (the
cheapest payer the campaign has measured, since round J′'s DBG-vs-VK null came back **−0.0014** pooled —
but it is a 1-drop, so this arm also shifts the curve). Round J′'s method applies: three payers agreeing
within 0.0014 is what licensed "the payer is irrelevant and the gain belongs to the card". If the two
payers agree here, the curve shift is not what is being measured. Both user rulings are asserted as
invariants in the generator.

**Struct-size tripwires did NOT fire, and that is logged rather than relied on.** `sizeof(Player)` stayed
200 and `sizeof(Permanent)` stayed 328 — both new ints landed in existing padding. `Dominance.h`'s own
maintenance log documents this exact blind spot ("size is a proxy, not a proof; a same-slot addition is
exactly the case it cannot catch"), so both entries were written into the log anyway. **Measured** from a
scratch TU, not reasoned about.

### Round O RESULT — GIDEON DOES NOT EARN A SLOT, and the reason is not the one predicted

Completed 05:06Z, 2h16m, all six format-blocks rc=0. Read on the **own R=40** column; every arm cleared
its apparatus floor by **3.8x**, so the shared apparatus is not producing the effect. Positive = WORSE.

| arm | long (.5) | 2hg (.3) | std (.2) | **pooled** | meas | conf |
|---|---|---|---|---|---|---|
| `j2_ad3_sp4` (ref) | −0.2503 | −0.3415 | −0.2284 | **−0.2733** | ref | ref |
| `o_gid1_vk` | +0.0019 | +0.0001 | +0.0085 | **+0.0027** | +0.0021 | +0.0033 |
| `o_gid1_hob` | +0.0105 | +0.0044 | +0.0003 | **+0.0067** | +0.0041 | +0.0092 |
| `o_gid2_vk` | +0.0077 | +0.0056 | +0.0239 | **+0.0103** | +0.0071 | +0.0134 |
| `o_gid2_hob` | +0.0256 | +0.0115 | +0.0026 | **+0.0168** | +0.0137 | +0.0199 |

**All four arms are worse, the ladder is monotone in copies, and every one replicates** — `meas` (seed
12.4M) and `conf` (held-out 12.8M) agree in sign on all four, which is the [[replicate-trades-before-ruling]]
bar. Against round J′'s measured null control (~0.0020), one copy is 1.4x the floor on the cheap payer and
3x on the expensive one; two copies are unambiguous. **Gideon is not played.**

**THE PAYER MATTERS HERE, and that is itself a result.** Round J′ found three payers agreeing within
0.0014, which licensed "the payer is irrelevant and the gain belongs to the card". Not so this time — the
`hob − vk` gap is **+0.0040 at one copy and +0.0065 at two**, far outside that agreement. So the two arms
are answering two different questions, and both answers are no. It is also the expected direction: Hero of
Bladehold is a real card (round L: **79% of his attacking turns were lethal**) while a Venerable Knight is
the campaign's measured null, so cutting the Hero should cost more, and does.

**The per-format split CROSSES, which is the interesting part and it refutes the stated prior.** The
prediction on record was "Gideon is an emblem card in a turn-4 deck, too slow, he loses." He does lose —
but not for that reason:

* **vs Hero** (`_hob`): a dead null in std (+0.0003) and clearly worse in long (+0.0105).
* **vs a 1-drop** (`_vk`): a near-null in long (+0.0019) and clearly worse in std (+0.0085).

So he is squeezed from both sides rather than being simply slow: in the FAST format he is fine against a
Hero but loses the tempo of a one-drop on curve; in the LONG format he is fine against a one-drop but is
badly beaten by the Hero. Because the ranking is late-weighted (long 0.5 / 2hg 0.3 / std 0.2), the long
column is what sinks the `_hob` arms pooled.

A mechanism that fits both halves — Hero makes **two** bodies per attack plus battle cry, so his output
compounds with the number of attack steps, whereas Gideon gives one 2/2 per turn or a one-shot +1/+1 — is
consistent with round L's Hero data, but it is an explanation arrived at AFTER the numbers and so explains
them rather than extending them ([[dont-rationalize-a-measured-cut]]). It is not evidence for any further
cut.

**Not a floor, and not an apparatus artifact.** All three abilities were implemented and each was
separately proved to fire (above); smoke was 97/0 with zero play changes, so the base arm's round-J′ table
was legitimately reused; and the `card_scores` marginal the driver printed for Gideon (**−0.0427**, i.e.
"good") pointed the WRONG WAY, which is the fourth time that statistic has inverted against a measured
screen — [[card-scores-invert-against-play]] holds.

**WhiteKnights remains the settled 60 of round M.** Gideon joins Valiant Knight, Aether Vial, Acclaimed
Contender and Lightning Greaves as measured-out.

### Round N2 — BASRI RE-LAUNCHED UNDER THE RULING, and there is still NO prior Basri number

**Round N was killed with the user's approval and produced nothing** — one 0-byte log. It was stopped
because the cat ruling had superseded what it was measuring (the old greedy always-activate policy, and
measured while the search could not see the exert cost at all), and because the heuristic could not be
built while it ran: formats 2–6 would have executed on a different binary than format 1, making the pooled
late-weighted ranking a chimera across two engines. **So no Basri measurement exists yet. N2 is the
first.** Fresh tags (`n2_*`) and fresh seeds (13.4M / 13.8M) so nothing from round N can be reached by the
`(counts, R)` table cache.

**What N2 measures is a much smaller card than round N intended.** Under the ruling Basri reduces to *a
`{W}` 2/1 Human Knight with cycling `{2}{W}` that is Legendary* — a Venerable Knight (provably blank here)
plus a cycling option, minus the legend rule. So the estimand is **cycling's option value against the
legend rule**, and those two partly **cancel**: cycling `{2}{W}` is precisely the out for a stranded
second copy, which Adeline never had. **The copy ladder is therefore the informative axis, not the
one-copy number** — and if the ladder comes back FLAT, that cancellation is the finding rather than a null.

**Arms** (`mkspec_n2.py`; reference `j2_ad3_sp4`, then Venerable Knight → Basri at 1 / 2 / 3), all three
formats, `bracket: generate` + `floor_R: 40`, 20,000 games per block. The generator asserts both user
rulings (Exemplar 4, Adeline 3) as invariants so a later edit cannot silently reopen them. One R=10 pool
table over the union is needed because the shipped table cannot bucket Basri — that affects only the
*shared* column, never the `own` column that is the result.

**Prediction on record** (carried from round N, narrowed to what the ruling leaves measurable): one copy
neutral-to-slightly-better; degrading with copies as duplicates strand, but on a **shallower slope than
Adeline's**, because cycling is the out Adeline lacked. If `n2_basri1` comes back clearly better, the only
modelled thing it can be is cycling — so confirm the Cat count really is zero (`basri_probe.py`) before
crediting it, since a nonzero count means the heuristic gate is not firing.

**THE GATE IS VERIFIED LIVE, and it needed its own check because smoke could not provide one.** The
Gideon commit's smoke was 97 passed / 0 play-changed — but **no shipped deck contains Basri**, so that
result says nothing at all about whether the cat gate fires. Checked directly instead: 300 games of the
3-copy arm (`n2_basri3`, the arm with the most chances to activate) at the screens' searched `depth=5`,
**0 Cat tokens in 300 games**, with Basri cast in 52% of them. The gate is firing on the searched path
the measurement actually uses, so N2's estimand really is the cycling-vs-legend-rule card described above.

> **A reading artifact nearly condemned this run, and the trap is worth keeping.** `basri_probe.py`
> counted Cats with `"Cat" in cardName`, which also matches **Adeline, Resplendent CAThar** — three
> copies in every arm. The clean run first read as *"Cat tokens in 117/300 games (39%)"* and looked like
> a failed gate. The tell was in the same output: *"max Cats at once: 1"* is the **legend rule**, not a
> repeatable token engine. Probe now requires the literal token form and `Cat` as a whole word. Tokens
> are genuinely visible in a `--log-dir` battlefield (`1/1 Human Token`, `1/1 Soldier Token`), so the
> zero is informative rather than blind. See [[recorded-events-are-not-a-control-signal]].
>
> Same census incidentally confirms **Worthy Knight creates HUMAN tokens, not Knight** (`cards.json`,
> not recall) — so the absence of Knight tokens is correct, not a missing trigger.

**Launched 05:28:14Z** (`run_n2.sh`, pid 295271); round O's comparable shape took **2h16m**. Read with
`python3 logs/wk_screen/pool_h.py n2_wk n2c_wk j2_ad3_sp4`.

### Round N2 RESULT — BASRI IS A NULL AT ONE COPY, and NOTHING REPLICATES

Completed **07:28:11Z** (2h00m), all six format-blocks rc=0. Positive = WORSE.

| arm | long (.5) | 2hg (.3) | std (.2) | **pooled** | step vs ref | meas (13.4M) | conf (13.8M) |
|---|---|---|---|---|---|---|---|
| `j2_ad3_sp4` (ref) | −0.2437 | −0.3493 | −0.2264 | **−0.2719** | — ref | | |
| `n2_basri1` | −0.2455 | −0.3447 | −0.2265 | **−0.2715** | **+0.0004** | +0.0031 | **−0.0022** |
| `n2_basri2` | −0.2443 | −0.3427 | −0.2235 | **−0.2697** | **+0.0022** | +0.0059 | **−0.0014** |
| `n2_basri3` | −0.2407 | −0.3425 | −0.2215 | **−0.2674** | **+0.0045** | +0.0096 | **−0.0006** |

**THE PREDICTION ON RECORD WAS RIGHT ON BOTH COUNTS.** It said *"one copy neutral-to-slightly-better …
degrading with copies as duplicates strand, but on a SHALLOWER slope than Adeline's, because cycling is the
out Adeline lacked. If the ladder is FLAT, that cancellation is the finding, not a null."*

* **One copy is a dead null: +0.0004 pooled**, a fifth of the measured ~0.0020 noise floor.
* **The slope is real but shallow** — +0.0004 → +0.0022 → +0.0045 across three copies. Compare Adeline,
  whose legend-rule stranding showed up as **+4.2pp of games with a dead duplicate** (round L) on a single
  extra copy. Three Basri strand less than one extra Adeline did, which is exactly what a `{2}{W}` cycling
  outlet predicts. **The cancellation is the finding.**

**BUT THE HEADLINE IS A REPLICATION FAILURE, AND IT MUST NOT BE BURIED.** The measurement and held-out
blocks **disagree in sign on ALL THREE ARMS** — meas +0.0031 / +0.0059 / +0.0096 against conf −0.0022 /
−0.0014 / −0.0006. The pooled step is the mean of two contradictory blocks, so by
[[replicate-trades-before-ruling]] **none of these three numbers is an established effect**, including the
ladder's slope. What IS established is the bound: every arm sits at or under **|0.0096| on its worst
block**, so Basri is not a meaningful gain *or* loss at any count from 1 to 3.

**VERDICT: Basri does not earn a slot, and this is the campaign's cleanest "no effect" rather than a
measured loss.** The settled 60 is unchanged. Note this is a *weaker* rejection than Gideon's (round O:
monotone, all four arms replicating in sign) and should not be quoted as if it were the same thing — the
honest statement is *"indistinguishable from the blank 2/1 it replaces."*

**And note WHAT was measured, because the ruling shrank the card:** under the cat ruling Basri is a `{W}`
2/1 Human Knight with cycling `{2}{W}` that is Legendary, with the Cat ability gated out of autonomous play
(verified 0 Cats in 300 games). So this round priced **cycling's option value against the legend rule**, and
the answer is that they cancel to within noise. The Cat ability — the half that measured 54% activation
before the ruling — is out of scope by the user's decision, not by approximation.

### Round P (2026-09-28) — the three open items, on the ruled base, under the hand-entry adoption

**Launched 2026-09-28 ~08:20 UTC** after the user cancelled the Snow value leaf (*"go back to analysis of
WhiteKnights"*). Spec generator `logs/wk_screen/mkspec_p.py`, runner `run_p.sh`, logs `out/p_wk_*.log`
(screen, seed 14.4M) and `out/pc_wk_*.log` (held-out, 14.8M), pooled by `pool_h.py p_wk pc_wk m_sp3_p20`.
Base = **`m_sp3_p20`** (every ruling: Exemplar 4, Marshal 4, Adeline 3, Valiant 0, Greaves 0, Vial 0,
Silverblade 3, 24 lands), R=40 per-arm tables, `--floor` on every tag, three formats late-weighted.

| arm | edit vs base | what it answers |
|---|---|---|
| `p_ct2_vk2` | Acclaimed Contender 2, Venerable Knight 2 | **the Contender re-open** — first measurement with the same-turn deploy SEARCHED (ETB_DIG + hand-entry, fixture at d5); payer = the measured null |
| `p_ct3_vk1` | Contender 3, Venerable 1 | the shipped count, same payer |
| `p_sp2_hob4` | Silverblade 2, Hero of Bladehold 4 | **Silverblade 2** (the soulbond-liability extension), paid by the cheapest measured sink (+0.0037) |
| `p_sp2_p21` | Silverblade 2, Plains 21 (25 lands) | Silverblade 2 paid by a land — the payer's bias runs AGAINST the land here |
| `p_l25_hob2` | Plains 21, Hero 2 (25 lands) | **land ladder up**, trusted payer (Hero 3→2 measured null) |
| `p_l23_hob4` | Plains 19, Hero 4 (23 lands) | **land ladder down**, same payer |

Engine caveat, stated before the numbers: the base's and the reference tables predate the adoption
(`6b9f0cb2` moved WhiteKnights' play: overnight 0 slower / 2 faster, 2HG 0 / 1); every new arm's table is
generated at HEAD. The per-arm `--floor` nulls bracket that asymmetry. Predictions on record: Contender
does not earn its slot back (user: *"I somewhat doubt it will save the card"*; the affected frequency is
1.8% of casts); Silverblade 2 costs ≈ +0.007 at face value and is then a judgement call; the land count
stays 24 unless `p_l25_*` clears the floor with both payers agreeing.

### Round P RESULT (landed 09:45 UTC, 1h47m for 6 tables + 6 blocks) — all three questions answered, none in the candidate's favour

Pooled, late-weighted, each arm on its OWN R=40 table, step vs the ruled base `m_sp3_p20`; `meas` = the
screen block (seed 14.4M), `conf` = the held-out block (14.8M):

| arm | long | 2hg | std | step vs ref | meas | conf | reading |
|---|---|---|---|---|---|---|---|
| `p_l23_hob4` (23 lands, Hero 4) | −0.2363 | −0.3223 | −0.2127 | **−0.0011** | −0.0002 | −0.0020 | null — 23 = 24 |
| **`m_sp3_p20`** (ref) | −0.2291 | −0.3269 | −0.2185 | — | | | |
| `p_l25_hob2` (25 lands, Hero 2) | −0.2200 | −0.3279 | −0.2240 | **+0.0031** | +0.0025 | +0.0037 | 25 lands slightly worse, replicated |
| `p_sp2_hob4` (Silverblade 2, Hero 4) | −0.2281 | −0.3212 | −0.2091 | **+0.0041** | +0.0046 | +0.0036 | the 3rd Silverblade is worth ~0.004 at face value |
| `p_sp2_p21` (Silverblade 2, 25 lands) | −0.2175 | −0.3199 | −0.2153 | **+0.0085** | +0.0073 | +0.0098 | = Silverblade cut + the 25th land, additive |
| `p_ct2_vk2` (Contender 2) | −0.2244 | −0.3180 | −0.1884 | **+0.0110** | +0.0122 | +0.0099 | worse in every format; std +0.030 |
| `p_ct3_vk1` (Contender 3) | −0.2202 | −0.3125 | −0.1714 | **+0.0182** | +0.0192 | +0.0171 | monotone worse |

Every step replicates in sign and magnitude across the two seed blocks (the largest disagreement is
0.0025 on `p_sp2_p21`), and every per-arm `--floor` bracket clears 3x except `p_ct3_vk1` (2.42x, an
arm-vs-shipped figure that is not the round's question).

**1. The Acclaimed Contender re-open is CLOSED — the card does not earn its slot back with the deploy
searched.** The precondition was verified from play, not assumed: `logs/wk_screen/dug_probe.py` on 300
logged games of `p_ct3_vk1` at the screen's own settings (d5/20 ms, own table, own sidecar) finds **9
casts of a card acquired mid-phase, 8 of them immediately after Acclaimed Contender** — the line that was
0 of 3,000 before `MTG_BP_ETB_DIG` + the hand-entry adoption. With that line live, two Contenders for two
Venerable Knights cost **+0.0110** (std +0.030, 2hg +0.009, long +0.005) and three cost +0.0182. The
deploy fires at about the frequency round L bounded (a few percent of games) and it is not enough: a
{2}{W} 3/1 that digs is still a worse turn than a {W} 2/1 in a deck whose modal kill is turn 4, and the
penalty is largest at 20 life, where speed matters most. The user's prediction (*"I somewhat doubt it
will save the card"*) held. The Contender is out on a fair hearing.

**2. Silverblade: the 3rd copy is worth ~+0.004 at face value** (Hero's 4th as payer; +0.0085 when a
land pays, i.e. the two costs add). That is the measured price of the soulbond-liability extension — twice
the noise floor, replicated — and it is a *price*, since a goldfish never breaks the pair. Whether to pay
0.004 t of modelled speed for removal-resilience is the user's judgement, exactly as the 4th copy was.
Recommendation: **stay at Silverblade 3**; the ruling's own argument (the 2nd copy at −0.0255 is robust,
the 3rd/4th at ≈−0.007 are the fragile band) already priced the 4th and this measurement puts the 3rd
at only 0.004, inside what the ruling accepted for the 4th.

**3. The land count is settled at 24 with a trusted payer.** 23 lands is a null (−0.0011, the two blocks
straddle zero) and 25 is slightly worse (+0.0031, replicated) with Hero of Bladehold as the payer in both
directions — so round G's rejection of 25 was NOT the Silverblade bias alone; with a null payer the 25th
land still costs ~0.003. 23 = 24 means the 24th land is free rather than valuable; keep 24 (the ruling
paid for it, and a null does not overturn a ruling).

**THE FINAL WhiteKnights LIST IS THEREFORE `m_sp3_p20`, unchanged** — every remaining axis is measured
flat or adverse, under the adopted engine, on the ruled base:

```
4 Dauntless Bodyguard   4 Knight Exemplar     3 Silverblade Paladin   1 Sol Ring
4 Venerable Knight      4 Accorder Paladin    3 Hero of Bladehold     1 Swords to Plowshares
4 Worthy Knight         4 Benalish Marshal    3 Adeline               1 Unexpectedly Absent
20 Plains   4 Remote Farm                                             = 60
```

**Table-era control (10:11 UTC):** the reference's round-M table (pre-adoption) was moved aside
(`m_sp3_p20.preadopt`), regenerated at HEAD (R=40, 18 min), and the three screen blocks re-run
(`run_p2.sh` → `out/p2_wk_*.log`). **Every cell of the pooled table reproduces to the digit** — the
adoption did not move this deck's keep table, so the engine caveat stated before the round is void and
the steps above are table-symmetric.

Its keep table is on disk (`logs/deckcmp/WhiteKnights/m_sp3_p20/`, regenerated at HEAD by that
control). What it still owes at adoption is the runbook: archive the shipped list as
`decks/WhiteKnights/v1-…/` with its references moved, then profile → value leaf → mulligan → suite/GT.
**Adoption is the user's call** (it discards the shipped keep table, −0.1985 t on its own, and the fitted
leaf); nothing has been created under `decks/`.

## RESUME HERE — the road to a FINAL Knights list

**State at 2026-09-27 05:50Z.** *(This block supersedes an earlier one dated 2026-09-26 21:15Z that
described Knights' round H as in flight — it landed; its result is the "Round H on Knights" section
above.)*

**WhiteKnights' copy-count SEARCH is essentially done** — five cards are measured out (Valiant Knight,
Aether Vial, Acclaimed Contender, Lightning Greaves, Gideon) and every remaining ladder is flat or
adverse. The list is **not** signed off, though: the reference arm every round is measured against is still
`j2_ad3_sp4` (**Marshal 4**), because round M's Marshal 3 is a *price*, not a verdict. **One measurement is
still open:**

* **Round N2 (Basri) LANDED 07:28:11Z — a NULL, and nothing replicates.** One copy +0.0004 pooled (a fifth
  of the noise floor); the ladder is +0.0004 / +0.0022 / +0.0045 across 1/2/3 copies, but the measurement
  and held-out blocks **disagree in sign on all three arms**, so no effect is established — only the bound
  that Basri is within |0.0096| of the blank 2/1 it replaces at every count. **Basri does not earn a slot**,
  as the campaign's cleanest no-effect rather than a measured loss. Full table in the round N2 section.

**Two things block calling WhiteKnights DONE, and neither is a copy count:**

1. **The Acclaimed Contender cut must be RE-OPENED once the engine is fixed.** The same-turn deploy of a
   card acquired mid-phase is **depth-0-only by construction**, so every Contender measurement in this
   campaign (d3 in the suite, d5 in the screens) ran with the card handicapped — and it was cut on
   **−0.0095**. Full diagnosis, deterministic repro, depth ladder and fix directions are in
   `docs/design/contender-same-turn-deploy-searched.md`. The fix moves GT for every `etb_dig` deck and
   needs the regression accept flow, so it is real work, not a rebaseline.
2. **LIGHTNING GREAVES 1→0 IS RULED OUT (user, 2026-09-27): *"Dropping Lightning Greaves is okay if the
   haste is not very good."*** The condition is met, and the per-format split is what establishes it
   rather than the headline: the cut is **−0.0131** late-weighted but **−0.0184 in long against −0.0073 in
   std**, i.e. it gains MOST at 40 life where haste matters least and LEAST at 20 life where haste should
   shine. That is the shape a genuinely weak haste produces; the reverse would have suggested a
   mismeasurement. Its sink was the **3rd Benalish Marshal** (screen A's common sink), worth −0.0037 alone,
   so Greaves itself prices at ≈ **+0.009 — actively harmful**, not merely marginal. Mechanism: `{2}` with
   equip `{0}` that adds no power, so in a deck whose modal win is turn 4 it spends a whole turn changing
   nothing on board — and the hand-played reference bench had already shown the engine correctly declining
   to equip it. Shroud (unmodelled removal protection) was the only half the sign-off was waiting on.
   **Operationally this changes nothing: Greaves has been 0 in the candidate base since round C/D**, so
   every round since ran on a Greaves-0 list. No re-measurement is owed.
3. **BENALISH MARSHAL 4 STAYS — RULED (user, 2026-09-27). The 4th SILVERBLADE PALADIN pays for the 24th
   land instead.** Round M priced the Marshal cut at −0.0081, but the user overrode it on an unmodellable
   structural argument: soulbond breaks when **either** half leaves (CR 702.94), so one removal spell
   strips double strike from two creatures and the survivor loses the block it would have won, whereas a
   lord degrades gracefully. The passive opponent cannot represent any of that, so 100% of Silverblade's
   modelled value is its best case. **The resulting list is round M's own control arm `m_sp3_p20`
   (+0.0019, held-out +0.0005 — a null), whose keep table is already on disk, so it owes NO new
   measurement.** Full reasoning and the two consequences it implies are in the round M section above.
4. **Still open from that ruling, neither decided:** how far down the Silverblade ladder to go (the
   margin-vs-floor table puts the 2nd copy out of reach of the objection and the 3rd squarely inside it,
   so **Silverblade 2** is the natural extension), and a **land-ladder re-test against a trusted payer** —
   round G paid for lands in Silverblade copies, and `g_l25`'s +0.0144 rejection is roughly exactly the
   inflation on the three copies it shed. Settle the land count before adoption freezes it.

**Then adoption**, which is unstarted and is the expensive part — see the ADOPTION RUNBOOK section above.
Note it throws away the shipped keep table (worth −0.1985 on its own) and the value leaf, so the list it
adopts must be the final one.

**What is STILL MISSING for a final Knights list, in priority order:**

1. **Knight Exemplar in Knights is the big untested axis.** Screen 7 measured 4→3 at **−0.0368** on the
   shared apparatus — the second-best cut in that deck after Acclaimed Contender — but it has **never been
   combined** with the Contender/Adeline/land changes, and never measured on a real table. WhiteKnights'
   round H makes this urgent: that deck's Exemplar ladder is still sloping at 3→2, and Knights runs the
   same four copies with the same passive-opponent inertness. **This is almost certainly the largest
   remaining gain in Knights and it should be round I.**
2. **Extend whichever ladder round H left sloping.** Round H landed: Adeline 4 / 21 lands is endorsed and
   **the 23rd land is negative**, which is what mis-specified round I (below).
3. **Knights' Exemplar and land axes compete for the same slots**, exactly as Contender and Adeline did,
   so round I has to be a combination round rather than a set of independent rungs.
4. **Not blocking, but unresolved:** the land ladder's payer confound (Acclaimed Contender) is only
   partly broken by `h_kn_moz3_l23`; and Knights' sideboard promotions remain out of scope by the user's
   *"optimize on the set we have for now"*.

**Round I is written but MUST NOT BE RUN AS WRITTEN** — see *"Round I as written is now MIS-SPECIFIED"*
above. `mkspec_i.py` predates round H and every one of its arms pays for an Exemplar cut **with a land**,
walking the count to 22/23/24; round H then showed the 23rd land is negative, so two arms would buy a good
cut with a bad sink and report the sum. **Rebuild it on the round J structure first:** drop the `L`
parameter entirely and fix the cheapest sink — **Haytham Kenway 3→4, a measured null from round G** — on
every arm, cutting one card per arm so the sink price is common and cancels in every pair. That is also the
only shape that can rank four lord types at once (one arm each cutting Exemplar, Inspiring Veteran, Marshal
of Zhalfir, Haytham). The generator's existing **slot-ledger assertion** (frees = uses) is worth keeping —
it is the check that would have caught round F's 57-card arm.

```
python3 logs/wk_screen/mkspec_i.py <L>        # DO NOT USE until rebuilt: bakes in the land sink
python3 scripts/deck_compare.py logs/wk_screen/i_knights_long.json --floor <tags> --dry-run
```

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

### Round Q (2026-09-28) — KINSBAILE CAVALIER, an introduced card

User: *"Let's try out Kinsbaile Cavalier."* This is the screen-5 class that open question 3 below files
under `Knights`; the user asked for it on **WhiteKnights**, so the base is round P's settled
`m_sp3_p20`. Generator `logs/wk_screen/mkspec_q.py`, runner `run_q.sh`, seeds **15.2M / 15.6M**,
`floor_R: 40`, `max_fallback: 0.30`, six arms, five new tables, read with
`pool_h.py q_wk qc_wk m_sp3_p20`.

**The card, and why it needed no engine work.** `{3}{W}` 2/2 Kithkin Knight, *"Knight creatures you
control have double strike."* Added to `cards.json` as a pure data entry: the engine already had the
static subtype double-strike lord (`grants_double_strike` + `subtypes_affected`, Thrumming Hivepool's
shape), resolved inside `CreatureHasDoubleStrike` — the one oracle `ResolveCombatDamage` and both of
TurnSolver's attack projections consult — so executor, rollout and search are lockstep for free.
`vanilla_creature`, deliberately NOT `lord_effect`: with 0/0 P/T bonuses it is not an
`IsLordPermanent`, so it enters `BoardSources::ds` only and `ComputeLordBonus` is untouched. The grant
says "Knight creatures", **not** "Other", and `HasDoubleStrikeFromLords` has no self-exclusion, so it
doubles its own power too — the same self-inclusive shape Cloudshredder Sliver's haste grant has.
Validation on the adding commit: **unit 318/318** (2,640,057 assertions), **scenarios 118/118**,
**smoke byte-identical** (0 changed / 101 unchanged). New fixture
`test/scenarios/whiteknights_kinsbaile_double_strike.json` casts it off exactly four Plains and swings
three Venerable Knights plus the Cavalier doubled: **win T5 / opponent −8**, against **T7 / −4** on the
identical board without it, so it guards the cast decision and the grant in one assertion.

| arm | edit vs `m_sp3_p20` | what it asks |
|---|---|---|
| `q_kc1_vk3` | Cavalier 1, Venerable Knight 4→3 | the card at one copy, isolated |
| `q_kc2_vk2` | Cavalier 2, Venerable Knight 4→2 | two copies |
| `q_kc3_vk1` | Cavalier 3, Venerable Knight 4→1 | three copies |
| `q_kc2_hob1` | Cavalier 2, Hero of Bladehold 3→1 | the curve-top trade against the deck's best card |
| `q_kc2_vk1_p21` | Cavalier 2, Venerable Knight 4→1, Plains 21 | = `q_kc2_vk2` + exactly (VK −1, Plains +1) |

Venerable Knight is the payer on the ladder for round P's reason: a vanilla 2/1 for `{W}` whose only
printed text is a death trigger that is provably inert here, so the trade is body-for-body. The last
arm differs from `q_kc2_vk2` by one land and one blank 2/1, so the **pair difference** reads the 25th
land conditioned on two Cavaliers rather than confounding a third axis.

**THE APPARATUS MISTAKE THIS ROUND CAUGHT, and it is a driver defect, not a spec one.** The shipped
table does not bucket Kinsbaile Cavalier, and `deck_compare.py --floor` answered that by generating a
**pool table over a 73-card union decklist** — the object Rule 0a bans outright — because that code
path never consulted `"pool_table": false`. The screen path honours the setting; the bracket path did
not. It was killed by captured PID at 4 minutes. **`scripts/deck_compare.py` now REFUSES there** and
prints the alias recipe instead of silently choosing a different apparatus.
*Lesson worth keeping: load average was 32.29 on 32 cores while it built the banned thing.* CLAUDE.md's
first-ten-minutes utilisation check proves the box is BUSY, not that it is doing the right work — the
tell was a `pooltable/` directory appearing when the spec said none should exist.

**The fix, which is the approved route.** Kinsbaile Cavalier is **aliased into the ACCLAIMED CONTENDER
bucket** of the round-P table (`logs/wk_screen/alias_q_WhiteKnights/`, K unchanged at 14, with
`profile.json` and `value.json` copied in beside it because the engine resolves sibling models
directory-relative). Contender is the right host **mechanically**: it is 0 in every arm of this round,
so the bucket holds nothing but Cavaliers, and its shipped cap of 3 covers the whole 1..3 ladder with
no composition overflow. Hero of Bladehold's bucket would overflow at `q_kc3_vk1` (3 + 3 > 3) and
Valiant Knight's (cap 1) at two copies. Post-alias fall-through is **7.03% on every arm including the
base** — perfectly symmetric, driven by the base's own raises (Adeline 1→3, Marshal 2→4, Silverblade
1→3) against the shipped table, not by the introduced card. The alias's standing assumption applies:
the shared table treats a Cavalier **as** a Contender for the keep decision, which holds the keep
policy fixed across arms by construction, but cannot see that the Cavalier might want a different keep
policy. The per-arm R=40 **own** tables, which is what `pool_h.py` reports, are unaffected and give the
card a real bucket.

**What the number will not say.** Nothing in the card is unmodelled — double strike is the one combat
keyword that is not blocker-dependent, so unlike Knight Exemplar's first strike or Cloudshredder's
flying it is fully live in goldfishing. But goldfishing **flatters** it: a 2/2 for four that does
nothing on its own is never killed, never removed, never a bad topdeck, and its doubled damage is never
traded against a blocker. Its ceiling is also capped in this shell because **tokens are not Knights** —
Worthy Knight's Soldiers, Adeline's Humans and Hero of Bladehold's Soldiers all stay single strike.
State both with the result. [[goldfish-bias-has-two-readouts]], [[bracket-notes-are-the-judgement-call]].

### Round Q RESULT (landed 23:42:28Z, 1h27m for 5 tables + 6 blocks) — the Cavalier EARNS A SLOT, but only over Hero of Bladehold

Pooled, late-weighted, each arm on its OWN R=40 table, step vs the ruled base `m_sp3_p20`.
Negative = FASTER. `meas` = screen block (15.2M), `conf` = held-out (15.6M):

| arm | long (.5) | 2hg (.3) | std (.2) | pooled | step vs ref | meas | conf |
|---|---|---|---|---|---|---|---|
| `q_kc3_vk1` (Cavalier 3) | −0.2603 | −0.3739 | −0.1962 | −0.2816 | **−0.0196** | −0.0160 | −0.0232 |
| `q_kc2_hob1` (Cavalier 2, **Hero 1**) | −0.2448 | −0.3723 | −0.2329 | −0.2807 | **−0.0187** | −0.0165 | −0.0208 |
| `q_kc2_vk2` (Cavalier 2) | −0.2571 | −0.3654 | −0.2114 | −0.2804 | **−0.0184** | −0.0184 | −0.0185 |
| `q_kc2_vk1_p21` (Cavalier 2, 25 lands) | −0.2554 | −0.3698 | −0.2004 | −0.2787 | **−0.0167** | −0.0113 | −0.0221 |
| `q_kc1_vk3` (Cavalier 1) | −0.2501 | −0.3539 | −0.2198 | −0.2752 | **−0.0132** | −0.0124 | −0.0140 |
| **`m_sp3_p20`** (ref) | −0.2347 | −0.3322 | −0.2249 | −0.2620 | — ref | | |

**Every arm gains and every arm REPLICATES in sign and magnitude** — the direct opposite of round N2,
where all three arms disagreed in sign. `q_kc2_vk2` reproduces to the fourth decimal (−0.0184 / −0.0185).
So sampling noise is not the issue here. The apparatus is.

**1. THE FLOOR SPLITS THE ARMS IN TWO, and it inverts the naive ranking.** The bracket's bias for a STEP
is `null(arm) − null(ref)`, not the arm-vs-shipped figure the per-arm blocks print:

| arm | step | bias vs ref | margin | reading |
|---|---|---|---|---|
| `q_kc2_hob1` | −0.0187 | **−0.0027** | **6.8x** | **ESTABLISHED** |
| `q_kc1_vk3` | −0.0132 | −0.0094 | 1.4x | unresolved |
| `q_kc2_vk2` | −0.0184 | −0.0181 | 1.0x | unresolved |
| `q_kc2_vk1_p21` | −0.0167 | −0.0177 | 0.9x | unresolved |
| `q_kc3_vk1` | −0.0196 | −0.0284 | 0.7x | unresolved |

Read the nulls, not just the bias, as the skill instructs: every null is large and negative (each arm's
own table plays it FASTER than the shared one, −0.051 to −0.080). The ref's is −0.0511 and
`q_kc2_hob1`'s is −0.0539 — **near-equal, so they cancel**, which is exactly the "two large equal nulls
are a level difference" case. The Venerable-Knight-paid arms sit at −0.060 to −0.080 and do NOT cancel.
Per the skill this is **unresolved, not refuted**, and re-running the bracket bigger will not settle it;
only a high-R regeneration will. No mechanism is offered for why the ladder arms' tables flatter them
more ([[dont-rationalize-a-measured-cut]]).

**2. THE PER-FORMAT SPLIT SAYS WHERE THE CARD BELONGS, and it agrees with the floor independently.**

| arm | long step | 2hg step | std step |
|---|---|---|---|
| `q_kc1_vk3` | −0.0154 | −0.0217 | **+0.0050** |
| `q_kc2_vk2` | −0.0224 | −0.0331 | **+0.0134** |
| `q_kc3_vk1` | −0.0256 | −0.0417 | **+0.0287** |
| `q_kc2_hob1` | −0.0101 | −0.0401 | **−0.0080** |
| `q_kc2_vk1_p21` | −0.0207 | −0.0376 | **+0.0245** |

Paying with Venerable Knight **loses at 20 life, monotonically worse with copies**; paying with Hero of
Bladehold **gains in all three formats**. The mechanism is the curve, and it is in the counts rather
than invented: the ladder arms keep Hero at 3 and so run **3 + N four-drops** (five of them at
`q_kc2_vk2`) while cutting one-drops to pay, in a deck whose modal kill is turn 4. `q_kc2_hob1` swaps
**four-drop for four-drop** and holds the count at three. So the two independent readings — the only arm
whose floor clears is also the only arm that respects the curve — point the same way.

The gain is largest in **2hg every time**, which is the expected shape for a damage multiplier: 30 life
× 2 heads is 60 total life, so doubling damage scales with how much of it you need.

**3. 24 LANDS STILL STANDS.** `q_kc2_vk1_p21` − `q_kc2_vk2` isolates the 25th land conditioned on two
Cavaliers: **+0.0017**, inside the ~0.0020 noise floor, and the two blocks disagree in sign
(meas +0.0071, conf −0.0036). Adding a fourth four-drop slot does **not** flip round P's land verdict.
That sub-question is answered.

**4. WHAT THE NUMBER WILL NOT SAY, and here it runs AGAINST the card.** The established arm says a
Cavalier is a better four-drop than **Hero of Bladehold**, and that is precisely where this model is
least trustworthy. Hero makes two Soldier tokens per attack and carries battle cry; the Cavalier is a
2/2 whose entire contribution is a multiplier. In a goldfish there are no blockers for Hero's tokens to
absorb and no removal for them to survive, so extra bodies buy almost nothing while a multiplier on an
unopposed board buys everything. Against a real opponent that asymmetry reverses: Hero's tokens and
battle cry survive any single removal spell, whereas killing the Cavalier erases its whole value.
Note also that the model already prices two REAL disadvantages of the Cavalier and still prefers it —
**tokens are not Knights** (Worthy Knight's Soldiers, Adeline's Humans, Hero's Soldiers all stay single
strike) and fewer Hero tokens means a smaller Adeline, whose power is the creature count. So the
in-model signal is honest; it is the out-of-model comparison that is unfavourable.
[[goldfish-bias-has-two-readouts]], [[bracket-notes-are-the-judgement-call]].

**VERDICT: Kinsbaile Cavalier earns a slot on this evidence, in place of Hero of Bladehold rather than
the one-drops, at a count that is NOT yet settled.** `q_kc2_hob1` (Cavalier 2 / Hero 1) is the one
established arm. It is NOT a recommendation to adopt: the copy count and the Cavalier-vs-Hero mix within
a three-slot four-drop budget are unmeasured, and the two obvious follow-up arms (Cavalier 3 / Hero 0,
Cavalier 1 / Hero 2) were not in this round. **Adoption is the user's call** and, as with round P's list,
would discard the shipped keep table and value leaf.

### Round S (2026-09-29) — the Cavalier at the USER'S CAP, and WHICH card it replaces

Round R was launched and **killed ~15 min in**: three of its five arms tested 3 Cavaliers or a four-drop
count of four, both ruled out by the user mid-round. `r_kc1_hob2`'s partial table resumed from its
journal into round S. Generator `mkspec_s.py`, runner `run_s.sh`, seeds **16.8M / 17.2M**, seven arms,
four tables (one resumed, three cached), apparatus unchanged from round Q.

**The user's specification, verbatim (2026-09-29):** *"Kinsbaile is potentially a 1 or 2 of. That would
be the cap from my perspective."* / *"But it duplicates what Silverblade Paladin does to a degree."* /
*"So a 2-of might look like -1 Silverblade and -1 Hero of Bladehold."* / *"1-of you would remove one or
the other."*

| tag | Cav | Silverblade | Hero | four-drops | role |
|---|---|---|---|---|---|
| `m_sp3_p20` | 0 | 3 | 3 | 3 | reference (cached) |
| `r_kc1_hob2` | 1 | 3 | 2 | 3 | 1-of, remove the HERO (resumed) |
| `s_kc1_sp2` | 1 | 2 | 3 | 4 | 1-of, remove the SILVERBLADE |
| `s_kc2_sp2_hob2` | 2 | 2 | 2 | 4 | **the user's stated 2-of shape** |
| `q_kc2_hob1` | 2 | 3 | 1 | 3 | 2-of from Hero — a THIRD block on round Q's one floor-clearing arm (cached) |
| `s_kc2_sp1` | 2 | 1 | 3 | 5 | 2-of from Silverblade |
| `p_sp2_hob4` | 0 | 2 | 4 | 4 | **CONTROL**: the Silverblade cut with NO Cavalier (cached, round P +0.0041) |

The control is the piece every earlier round lacked: it separates *"the Cavalier is good"* from *"the
3rd Silverblade is bad"*. **Prediction on record:** the user's −1/−1 shape beats both pure payers,
because taking two from Silverblade alone leaves Hero at 3 and pushes the deck to **five** four-drops,
the configuration round Q showed losing at 20 life. If `s_kc2_sp1` wins anyway, the four-drop count was
not what drove that.

Note the shared-table fall-through now **varies by arm** (3.74%–7.03%) rather than being uniform as in
round Q: cutting Silverblade removes one of the three raises (Silverblade 1→3) that caused it, so the
Silverblade arms get *better* coverage. That is an asymmetry in the SHARED column only; the reported OWN
column is unaffected and the floor bracket quantifies it.

#### THE REDUNDANCY IS REAL IN THIS ENGINE, not only on paper

`CreatureHasDoubleStrike` returns true if ANY source grants it, so with a Cavalier on the battlefield
every Knight already has double strike and Silverblade's soulbond payload adds **nothing** to a Knight
partner. And the partner is essentially always a Knight: `DecisionProvider::SoulbondPartner` picks the
highest effective power through `DynamicBasePower`, and its own comment notes Adeline — a Knight, power
= creature count — is *"routinely this deck's largest creature and therefore its best pair"*. Tokens do
escape the Cavalier's grant (Soldiers and Humans, not Knights) but are never the highest-power body, so
they are never chosen. The overlap bites whenever a Cavalier is actually out.

#### THE OUT-OF-MODEL RANKING — state this with any result from rounds Q, R or S

**User, 2026-09-29:** *"The tests here may overstate Kinsbaile because there is no removal. Like
Silverblade, it is quite weak to removal."* … *"Hero is also better than both in surviving Lightning
Bolt which is important against a number of decks."* … *"So, overall, it will be the card most
understated by our analysis."* **This is correct, and it is a design principle here rather than a
footnote.**

| card | cost | body | Lightning Bolt | value left behind if it dies |
|---|---|---|---|---|
| Hero of Bladehold | `{2}{W}{W}` | **3/4** | **survives** | Soldier tokens already made + battle cry already applied |
| Silverblade Paladin | `{1}{W}{W}` | 2/2 | dies | none — the pair breaks (CR 702.94, either half leaving) |
| Kinsbaile Cavalier | `{3}{W}` | 2/2 | dies | none — the grant is a static that evaporates |

Two consequences, and the first is uncomfortable:

1. **Every HERO-paid arm is an UPPER BOUND on the Cavalier** — including round Q's `q_kc2_hob1`
   (−0.0187), which is the *only* arm in the campaign that cleared its floor. So the cleanest
   measurement is also the most biased trade. Do not quote it as the adoption case on its own.
2. **The SILVERBLADE-paid arms are the honest comparison.** Both cards are 2/2, both die to the
   format's most common removal spell, and both lose their entire contribution when they do. The
   no-removal bias therefore largely **cancels** in that swap, which is why round S exists.

**One qualification that runs the other way, also unmodelled.** Knight Exemplar grants other Knights
**+1/+1 and indestructible**, and cards.json declares that grant inert ("no blockers, no opponent
removal"). Under an Exemplar all three cards are 3/3-or-better AND indestructible, so the Bolt gap
closes completely, and the deck runs four Exemplars. But the Exemplar is itself a 2/2 that dies to Bolt,
so the umbrella is what gets removed first. It narrows the point rather than overturning it.

**And Hero is not the only understated card on this list** — the standing caveat at the top of this
document already names Swords to Plowshares (−0.257) and Unexpectedly Absent (−0.229) as the
worst-*scoring* cards purely because the passive opponent presents nothing to target, and Knight
Exemplar's first strike is inert alongside its indestructible. Hero is the most understated card
*within the trade being measured*, which is the claim that matters for this decision.

### Round S RESULT (landed 01:20:22Z, 1h09m) — FOUR arms clear their floors; the redundancy argument gets NO measured support, and the reason is the CURVE

Pooled, late-weighted, own R=40 table per arm, step vs `m_sp3_p20`. Negative = FASTER.
`meas` = 16.8M, `conf` = 17.2M. Bias = `null(arm) − null(ref)`:

| arm | Cav | SP | Hero | pooled | step | meas | conf | bias | margin | verdict |
|---|---|---|---|---|---|---|---|---|---|---|
| `q_kc2_hob1` | 2 | 3 | 1 | −0.2751 | **−0.0161** | −0.0139 | −0.0182 | −0.0007 | **23.0x** | **ESTABLISHED** |
| `s_kc2_sp2_hob2` | 2 | 2 | 2 | −0.2727 | **−0.0137** | −0.0162 | −0.0111 | −0.0037 | **3.66x** | **ESTABLISHED** |
| `r_kc1_hob2` | 1 | 3 | 2 | −0.2724 | **−0.0134** | −0.0128 | −0.0141 | −0.0021 | **6.35x** | **ESTABLISHED** |
| `s_kc2_sp1` | 2 | 1 | 3 | −0.2690 | −0.0100 | −0.0112 | −0.0088 | −0.0092 | 1.08x | unresolved |
| `s_kc1_sp2` | 1 | 2 | 3 | −0.2645 | −0.0055 | −0.0110 | **−0.0001** | −0.0037 | 1.49x | unresolved |
| **`m_sp3_p20`** (ref) | 0 | 3 | 3 | −0.2590 | — | | | | | |
| `p_sp2_hob4` (CONTROL) | 0 | 2 | 4 | −0.2515 | **+0.0075** | +0.0026 | +0.0125 | −0.0021 | **3.58x** | **ESTABLISHED** |

**1. `q_kc2_hob1` IS NOW THE BEST-ESTABLISHED EDIT IN THIS CAMPAIGN.** Its apparatus bias is −0.0007 —
essentially zero, a 23x margin — and it has replicated across **three independent seed blocks**
(round Q −0.0187, round S −0.0161; mean ≈ −0.017). Nothing else on this deck has that.

**2. THE CONTROL IS THE KEY, AND IT REFUTES THE REDUNDANCY HYPOTHESIS.** `p_sp2_hob4` — cut one
Silverblade, add a 4th Hero, **no Cavalier** — comes in at **+0.0075 WORSE**, clearing its floor at
3.58x and replicating round P's +0.0041 on the same arm with fresh seeds. So the 3rd Silverblade is a
**real card in this model, not a null**, and paying for the Cavalier with it costs something.
The prediction that the user's −1/−1 shape would beat both pure payers was **WRONG**: the ordering is
Hero-paid (−0.0161) > split (−0.0137) > Silverblade-paid (−0.0100). *The more you pay from Hero, the
better* — the exact opposite of what the double-strike overlap predicted.

**3. WHY, AND IT IS THE MANA CURVE RATHER THAN CARD QUALITY.** Silverblade Paladin is `{1}{W}{W}` = **3
mana**; Kinsbaile Cavalier `{3}{W}` and Hero of Bladehold `{2}{W}{W}` are both **4**. The std column
separates perfectly on that one fact:

| arm | payer moves | long | 2hg | **std (20 life)** |
|---|---|---|---|---|
| `r_kc1_hob2` | 4-drop → 4-drop | −0.0122 | −0.0213 | **−0.0047** |
| `q_kc2_hob1` | 4-drop → 4-drop | −0.0096 | −0.0353 | **−0.0036** |
| `s_kc2_sp2_hob2` | one of each | −0.0109 | −0.0303 | **+0.0044** |
| `s_kc1_sp2` | 3-drop → 4-drop | −0.0074 | −0.0106 | **+0.0066** |
| `s_kc2_sp1` | 3-drop → 4-drop | −0.0114 | −0.0224 | **+0.0121** |
| `p_sp2_hob4` | 3-drop → 4-drop (no Cav) | +0.0041 | +0.0095 | **+0.0129** |

**Every arm that pays from the 3-drop tier is worse at 20 life; every arm that pays from the 4-drop tier
is better.** So the model is not saying "Hero is a worse card than Silverblade" — it is saying *do not
trade a 3-drop for a 4-drop in a deck whose modal kill is turn 4*. The double-strike overlap is real in
the engine (see the round S design section) but it is **not the binding constraint**; the curve is.

**4. THE 2nd COPY IS WORTH LITTLE, which is what double strike being BINARY predicts.** Marginal value
of the second Cavalier: **−0.0027** paying from Hero (−0.0134 → −0.0161) and −0.0045 from Silverblade
(−0.0055 → −0.0100). Both sit at or just above the ~0.0020 noise floor. A second Cavalier adds nothing
to the first one's grant — it buys only the chance of *having* one — so this is the expected shape and it
argues for the **low end** of the user's 1–2 cap.

**5. THE TENSION IS NOW SHARP AND NO AMOUNT OF GAMES RESOLVES IT.** The model's three established arms
all pay from Hero. The user's out-of-model argument (2026-09-29) is that Hero is *"the card most
understated by our analysis"* — 3/4 survives Lightning Bolt where both 2/2s die, and its tokens and
battle cry are already banked if it does die. **So the best-measured edit is the most out-of-model-biased
trade.** That is not a defect in either argument; it is the goldfish boundary.

**WHERE THEY CONVERGE, and this is the practical read.** Two established options give up only ONE Hero:

* **`r_kc1_hob2`** — 1 Cavalier for 1 Hero: **−0.0134**, floor **6.35x**, better in all three formats.
* **`s_kc2_sp2_hob2`** — 2 Cavaliers for 1 Silverblade + 1 Hero (**the user's own shape**): **−0.0137**,
  floor **3.66x**, but +0.0044 worse at 20 life.

They are **statistically identical** (−0.0134 vs −0.0137, well inside noise). So the 1-of is the more
conservative of the two on every axis that matters: it is the smaller change, it clears a higher floor,
it keeps all three Silverblades, it gives up only one copy of the card the user judges most understated,
and it does not slow the deck at 20 life. If the Cavalier goes in, **1-of in place of 1 Hero of Bladehold
is the option the measurement and the out-of-model reasoning both support.**

**ADOPTION IS THE USER'S CALL** and still discards the shipped keep table and value leaf.

### Round T RESULT (landed 04:41:09Z) — the two deferred cases, on v2's OWN apparatus. BASRI IS LITERALLY VENERABLE KNIGHT; VIAL COSTS

Run on **v2's own regenerated profile + value leaf + R=40 keep table** — the thing waiting for the
finalised list bought. Basri and Aether Vial aliased into the Dauntless/Venerable bucket (cap 8), every
arm paying 1:1 from Venerable Knight so the bucket total stays at exactly 8: **zero composition
fall-through on every arm**, against 3.74–7.03% in rounds Q and S. Seeds 17.6M / 18.0M. Positive = WORSE.

| arm | long | 2hg | std | pooled | step | meas | conf | own-table floor |
|---|---|---|---|---|---|---|---|---|
| `m_v2` (ref) | 0 | 0 | 0 | 0 | — | | | |
| `t_basri1_vk3` | +0.0042 | +0.0051 | +0.0010 | +0.0038 | **+0.0038** | +0.0040 | +0.0036 | 0.99x |
| `t_av1_vk3` | +0.0219 | +0.0189 | +0.0216 | +0.0209 | **+0.0209** | +0.0190 | +0.0228 | 1.10x |
| `t_av2_vk2` | +0.0445 | +0.0349 | +0.0449 | +0.0417 | **+0.0417** | +0.0401 | +0.0434 | 0.96x |

#### 1. BASRI IS THE SAME CARD AS VENERABLE KNIGHT IN THIS DECK — 100.0% IDENTICAL over 120,000 games

Under the shared table, which holds the keep policy fixed, `t_basri1_vk3` vs the reference is
**100.0% identical play in all SIX blocks** (3 formats × 2 seed blocks × 20,000 games), delta ±0.0001
(rounding). Swapping a Venerable Knight for a Basri changes *nothing whatsoever*. Three reasons, and
together they are exhaustive:

* **The cycling is STRUCTURALLY UNREACHABLE.** `GenericProvider::HasAnyDigSource` returns **false**, and
  **none** of `DeckProvider` → `VialProvider` → `KnightsProvider` → `WhiteKnightsProvider` overrides it.
  `DecisionProviders.h` states the consequence outright: a deck routed this way *"would never cycle a
  single card, i.e. would never play its own game. That is a capability-narrowing DEFAULT."* Only
  Fluctuator, Auras and TreasureHunt switch it back on. The two cycling sites in `TurnSolver.cpp` are
  both labelled human-play (`P3`/`P6`), and `FluctuatorProvider::SelectDigSource` is the only
  deck-aware cycler. So the autonomous search never even offers the ability.
* **At ONE copy the legend rule cannot bite** — there is never a second instance to kill.
* **The printed body is identical**: `{W}` 2/1 Human Knight, exactly Venerable Knight, whose own death
  trigger is inert here (creatures never die).

So the **+0.0038 is not the card. It is a keep-table artifact**: only the arm's OWN table gives Basri a
14th bucket, and that re-partition costs 0.0038. The bracket says so precisely — shared −0.0001 at
100.0% ident, own +0.0037 at 93.6%, bias +0.0038 flagged *"the shared table flatters the VARIANT --
unexpected"*.

> **THIS CORRECTS THE ROUND N2 RECORD.** That section concludes *"this round priced cycling's option
> value against the legend rule, and the answer is that they cancel to within noise."* **That is wrong:
> neither quantity was in the measurement.** Cycling was unreachable and one copy cannot trip the legend
> rule, so N2's +0.0004 was a null because Basri *is* the card it replaced — and its notorious
> sign-disagreement across blocks was pure noise around a true zero, exactly what you get measuring an
> identity. The N2 verdict ("does not earn a slot") stands; its *mechanism* did not.
> This is the [[flag-default-is-not-the-arming-condition]] class: the param is set, the code exists, and
> a gate predicate makes it unreachable for this deck.

**Basri is therefore a strictly WORSE Venerable Knight in this list** — same body, plus a legend-rule
liability at 2+ copies, minus an ability the engine will never use. **CLOSED.** The only thing that
could reopen it is a CAPABILITY change (giving the Knights provider chain a dig source), which is a
reserved decision for the user, not a copy count. Round N2's premise only becomes testable after that.

#### 2. AETHER VIAL COSTS, and the apparatus is MASKING the effect rather than creating it

`t_av1_vk3` = **+0.0209**, replicating in sign AND magnitude (+0.0190 / +0.0228), and the ladder is
linear: two copies cost **+0.0417**, almost exactly twice. Consistent across all three formats.

The floor ratio is ~1.1x, which the tool calls unresolved — **but read the direction, because it
inverts the usual worry.** For `t_av1_vk3`: shared table **+0.0393**, own table **+0.0190**, bias
−0.0203. *Both apparatuses agree Vial costs*, and the arm's OWN better-fitted table gives the SMALLER
cost. The floor test exists to catch a shared apparatus MANUFACTURING an effect; here it is masking half
of one. The alias is why, and it was predicted: the shared table treats an Aether Vial as a vanilla 2/1
for keep purposes, which is simply wrong, so the Vial arms play worse under it. **The sign is settled;
only the magnitude is loose, bracketed +0.019 (conservative) to +0.039.**

**The chassis argument I raised for Vial did NOT survive contact.** I noted that `vial_target_mv` is 3
and the three-drop creature count grew 11 → 14 from v1 to v2, so Vial's deploy pool widened by a quarter
on its own axis, and predicted that would help it. It did not: 1 Vial costs **+0.021**, *worse* than the
~+0.014/copy interpolated from the old 3→2 (−0.0154) and 3→0 (−0.0423) measurements. Recorded because a
mechanism invented before the numbers is still only a hypothesis ([[dont-rationalize-a-measured-cut]]).

**THE USER'S QUESTION WAS WHETHER IT IS "TOO TERRIBLE", AND THIS IS THE HONEST ANSWER.** In-model, one
Vial costs +0.021 — about 10x the ~0.0020 noise floor, and not marginal. Against that stands the
standing caveat, which is the whole reason the question was asked: the engine models Vial's charge and
deploy heuristic but **not** flashing a creature in to dodge sorcery-speed removal or countermagic. All
three stated reasons — *uncounterable, instant-speed deploy, dodging removal* — are structurally
invisible to a goldfish that has nothing to be countered by, nothing to dodge, and no opponent turn to
act during. So **+0.021 is a floor on the cost and the real value is higher by an unmeasured amount**;
whether an unmodellable instant-speed deploy is worth 0.021 turns of measured speed is a judgement the
simulator cannot make. **Not adopted; surfaced with both halves.**

### DEFERRED — re-measure the close cases on the finalized list's OWN regenerated table

**User, 2026-09-28**, across one exchange: *"if we have cases where it is extremely close we might want
to measure those with regeneration"*; *"Maybe even 1 Basri is worth consideration this way"*; *"But
let's wait until we have a finalized list first. Then we can regenerate that and work from it."*

**The sequence is therefore fixed: land round Q → finalize the list → regenerate THAT list's mulligan
table as a real artifact → re-measure the close cases on it.** Not before, because every one of these
re-measurements is only worth doing against an apparatus fit to the list being shipped.

**Why regeneration is the right instrument here and was not before.** The shipped WhiteKnights table
has **exactly one version** (`a4b01fb8`, built at commit `083c4108`, `effective_R` 40, 42,271 entries,
14 buckets) and is fit to the **shipped** 60, not the settled one. Across the other decks a second
table is usually a *new list* rather than a regeneration of the same one; `Knights` is the one clear
same-list regeneration, at `max_mull=6`. Meanwhile the screening loop has been generating per-arm
tables since round G, at `floor_R: 40` — the **same** `effective_R` as the shipped artifact. So the
2026-09-02 *"we don't need to regenerate any mulligan profiles"* directive no longer describes what
this campaign does, and the remaining gap on a close case is games and chassis fit, not R.

| case | what is on record | why it is unresolved |
|---|---|---|
| **The Kinsbaile Cavalier COPY COUNT** (round Q) | 1 / 2 / 3 copies at `−0.0132 / −0.0184 / −0.0196`, all replicating | the Venerable-Knight-paid arms' apparatus bias (−0.0094 to −0.0284) is the same size as the effect, so margins are 0.7–1.4x. This is now the MOST urgent of the three: the card is established as good, only the count is open. Add the two arms round Q lacked — **Cavalier 3 / Hero 0** and **Cavalier 1 / Hero 2** — so the four-drop budget is held at three and the payer stops being a confound |
| **1 Basri, Tomorrow's Champion** | round N2: `+0.0004` pooled at one copy, ladder `+0.0004 / +0.0022 / +0.0045` | the measurement and held-out blocks **disagree in sign on all three arms**, so nothing is established beyond the bound `|0.0096|`. And the reference was `j2_ad3_sp4` — **Silverblade 4, 23 lands** — not today's Silverblade 3 / 24 lands |
| **1 Aether Vial** | **never measured at 1.** Only 3→2 (`−0.0154`, two payers agreeing) and 3→0 (`−0.0423`); the gradient is ~0.014/copy, so 0→1 interpolates to about `+0.014` | an interpolation across a chassis change, not a measurement. Every arm in rounds G–J holds Vial 0, so no snapshot isolates it |

**The Vial case has a second argument that was never made, and it points the other way from the cut.**
Vial charges toward the most common creature mana value in hand (`vial_target_mv` = 3 on this deck).
The shipped list held **eleven** three-drop creatures; the settled list holds **fourteen** (Silverblade
1→3, Adeline 1→3, Marshal 2→4, Contender out). Vial's deploy pool therefore grew by about a quarter on
exactly the axis Vial keys on, *after* the −0.0423 was measured. Add the standing caveat that a Vial
cut is **flattered** here — the engine models the charge/deploy heuristic but not flashing a creature
in to dodge sorcery-speed removal or countermagic, and the user's three stated reasons (uncounterable,
instant-speed deploy, dodging removal) are all structurally invisible to a goldfish, since there is
nothing to be countered by, nothing to dodge, and no opponent turn to act during.

### Round U (2026-09-29) — THE PAYMENT AXIS: a 9th one-drop, and Vial paid by a LAND

**User, immediately after round T:** *"is there no spot remaining for a 9th one-drop? How does a vial
look when replacing a land?"*

**Why round T did not answer either question, despite measuring both cards.** Round T paid for Basri and
for Vial 1:1 out of **Venerable Knight** — and then discovered that Basri *is* Venerable Knight, to
100.0% identical play over 120,000 games. So `t_basri1_vk3` was a **null arm by construction**: it
swapped a one-drop for the same one-drop and the deck held eight one-drops before and after. It never
tested nine. And `t_av1_vk3` priced Vial against a *creature*, not against the land the user is
proposing to cut.

**The round T identity is now the instrument, not a disappointment.** Basri is the legal way to play a
5th copy of a vanilla {W} 2/1 Knight, so every Basri arm below reads as *"what does a 9th one-drop cost,
paid by X"*, and the answer generalises to any 2/1 Knight one-drop rather than being about Basri.

**Better still, round T calibrated the apparatus bias for exactly this case.** `t_basri1_vk3` measured
`+0.0038` on play that was *provably identical* — so `+0.0038` is a pure measurement of the "the arm's
own table gains a 14th bucket" artifact for this card on this apparatus. Round U's Basri arms carry the
same artifact, so their real effect is approximately **measured − 0.0038**, and Vial's round T cost
restates as `0.0209 − 0.0038 ≈ +0.017` against a one-drop at 24 lands. A null arm is worth running.

**The land-paid Vial arm cannot be read alone, and that dictates the design.** Cut a Plains and the mana
base gets worse whatever fills the slot, so a positive number would be unattributable between *"Vial is
bad"* and *"23 lands is bad"*. The Basri arm **at the same land count** is that control, because it
fills the slot with a body of measured-null value:

```
u_av1_pl19 − u_b1_pl19  =  Vial vs a 2/1 one-drop, at 23 lands
t_av1_vk3               =  Vial vs a 2/1 one-drop, at 24 lands   (+0.0209, round T)
```

If the gap **narrows** at 23 lands, Vial is genuinely substituting for the land it replaced. If it holds
near 0.021, Vial is not paying for the mana it costs and the land-cut penalty is simply additive.

| tag | added | paid by | lands | 1-drop creatures | asks |
|---|---|---|---|---|---|
| `m_v2` | — | — | 24 | 8 | reference = the shipped v2 60 |
| `u_b1_pl19` | Basri 1 | Plains 20→19 | 23 | 9 | Q1, land pays |
| `u_b1_rf3` | Basri 1 | Remote Farm 4→3 | 23 | 9 | Q1, the TAPPED land pays |
| `u_b1_ap3` | Basri 1 | Accorder Paladin 4→3 | 24 | 9 | Q1, curve only, mana base untouched |
| `u_av1_pl19` | Aether Vial 1 | Plains 20→19 | 23 | 8 | **Q2 — exactly the question** |
| `u_av1_rf3` | Aether Vial 1 | Remote Farm 4→3 | 23 | 8 | Q2, the TAPPED land pays |
| `u_av2_pl18` | Aether Vial 2 | Plains 20→18 | 22 | 8 | ladder — a lone Vial is rarely drawn |

**Why these payers and not others.** The user's standing rulings pin Knight Exemplar 4, Benalish Marshal
4, Adeline 3, and cap Silverblade + Kinsbaile at ≤ 4 squishy double-strike providers, so the genuinely
free slots are the lands, Accorder Paladin, Worthy Knight, Hero of Bladehold and the three 1-ofs.

* **Worthy Knight is deliberately EXCLUDED as a payer**, and this is a modelling fact rather than a
  preference. Its trigger is fully modelled (`cast_trigger_subtype: "Knight"`,
  `cast_trigger_creates_tokens: 1`), so cutting a Worthy Knight to add a Knight one-drop *fights
  itself* — it removes a token maker in order to add a token trigger, confounding the question with the
  deck's own synergy. The round U trace probe measured **6,693 Human tokens across 500 games (~13 per
  game)**, so this is the deck's largest single source of board presence, not a rounding error.
* **Accorder Paladin is the non-land payer** ({1}{W} 3/1 battle cry) — it answers Q1 with the mana base
  untouched, which is the reading of "a 9th one-drop" that is about the *curve*. v2's curve is 8
  one-drops / 8 two-drops / **14** three-drops / 3 four-drops, so the two-drop slot is the thinnest
  place above one mana and the honest place to take a slot from.
* **Hero of Bladehold is NOT a payer here.** It just went 3→2 for the Cavalier, and the user has said
  plainly it is the card this analysis understates most (bolt-resilient where Silverblade and Kinsbaile
  are not). Cutting it again inside a screen would produce a number we have already been told not to
  trust in that direction.
* **Both lands are tested separately** because they are not the same card. Remote Farm enters **tapped**
  with two depletion counters and sacrifices itself after producing {W}{W} twice, so it is the worse
  land for a deck that wants turn-1 and turn-2 plays — but it is also the deck's only double-white
  source for 4 Benalish Marshal ({W}{W}{W}). Which one is the right cut is not obvious a priori.

**PRE-CHECK RUN BEFORE LAUNCH — and it was a GATE-LIVENESS check, not an open risk.** I initially
framed this as "round T's zero-Cat result might not transfer to nine one-drops, because
`ActivateTapTokensShared` is a greedy spare-mana pass and `u_b1_ap3` reaches nine one-drops on a cheaper
curve at the same 24 lands." **That framing was wrong, and the user caught it: the Cat ability is
already gated off by their own 2026-09-27 ruling** (see the USER RULING section above — *"For the cats I
recommend you just disable that ability in a heuristic"*). The gate is
[`src/ai/ManaPayment.cpp:3818`](../../src/ai/ManaPayment.cpp): `if (def->params.tap_token_exerts &&
!s_greedy_exert) { continue; }`, with `MTG_GREEDY_EXERT_TOKEN` defaulting OFF. It is **unconditional for
autonomous play** — it does not consult mana, curve, board or life total — so no amount of spare {W}
can reach the ability and zero Cats was **guaranteed** rather than discovered. Round T's zero was the
same gate, not an empirical coincidence that might have failed to transfer.

**What the probe was therefore actually worth, which is not nothing but is much less than advertised:**
it verified the gate is live *in the binary round U is running on* — the [[verify-done-claims-in-tree]]
check — and it produced the Worthy Knight token census below, which is load-bearing for two separate
decisions in this round. **The lesson to carry: a user ruling that was implemented as a gate is a fact
about the binary. Read this document's USER RULING sections before treating a gated behaviour as an
open risk** — the answer was already written down, one section up, and re-deriving it empirically cost
two minutes and produced two wrong intermediate answers.

Probed with `--game-trace-dir` over 500 full decision logs (250 games each on `u_b1_ap3` and
`u_b1_pl19`) at **40 life**, the most permissive case for late spare mana. Result: **zero Cat tokens**,
as the gate requires. The only tokens created anywhere were 6,693 Human (Worthy Knight) and 674 Soldier
(Hero of Bladehold). Two traps worth recording, both of which produced a wrong answer first:

* **`--game-log-dir` is the wrong flag.** In `--batch` it writes per-game `<job>.wins` *outcome*
  records, not decision logs, so grepping it for a token name matches nothing and looks like a clean
  negative. `--game-trace-dir` is the one that writes a real `GameLogger` JSON per game
  (`src/runner/BatchRunner.h:90`).
* **Grepping for `Cat` matches `Adeline, Resplendent Cathar`** — 2,522 times, in 145 of 250 traces,
  which reads exactly like a strong positive. Every hit was the Cathar. Match the token, not the
  substring.

**Apparatus.** v2's own profile + value leaf, with the **round T alias directory reused verbatim**
(`logs/wk_screen/alias_t_WhiteKnights/`, K=13, Basri *and* Aether Vial both aliased into the Dauntless
Bodyguard / Venerable Knight bucket) so round U's shared column stays directly comparable to round T's.
`pool_table: false`, `max_fallback: 0.30`, `bracket: generate`, `floor_R: 40` (v2's **shipped**
`effective_R`, not the default 10), three formats pooled late-weighted (long .5 / 2hg .3 / std .2), read
on the OWN column. Seeds 18.4M / 18.8M, disjoint from P (14.4/14.8), Q (15.2/15.6), the abandoned R
(16.0/16.4), S (16.8/17.2) and T (17.6/18.0).

**The one place round U is weaker than round T, stated up front.** Round T could hold the alias host
bucket at *exactly* 8 in every arm because every arm paid out of Venerable Knight — zero composition
fall-through by construction. Round U pays out of lands and Accorder Paladin, so that bucket goes 8→9
(10 for `u_av2_pl18`) and the **shared** table's cell probabilities no longer match those arms. The
driver's own pre-flight reports **"all cards covered"**, so this is a probability mismatch rather than a
coverage hole, and it lands only on the bracket's shared column — i.e. on the floor. The OWN tables that
`pool_h.py` reports are per-arm and unaffected.

**Two out-of-model caveats for Vial, and they point in opposite directions.**

1. **Cutting the same way as always:** the goldfish cannot see *any* of the user's stated reasons for
   wanting Vial — uncounterable, instant-speed deploy, dodging sorcery-speed removal. There is nothing
   to be countered by, nothing to dodge, and no opponent turn to act during. Whatever round U measures
   for Vial is a **floor** on its real value.
2. **Cutting the other way, and this one IS modelled:** a creature put onto the battlefield by Vial is
   not **cast**, so it triggers none of the deck's 4 Worthy Knights, and Vial itself is not a Knight
   spell. At ~13 Human tokens per game that is a real cost round U *will* see — and a cost that gets
   *bigger*, not smaller, in a real game where those tokens block.

Scripts: `logs/wk_screen/mkspec_u.py`, `logs/wk_screen/run_u.sh`, `logs/wk_screen/catprobe.sh`
(gitignored). Launched 05:48Z; 6 new R=40 tables, so ~1h45m by round P's precedent.

### Round U RESULT (landed 08:05:41Z, 2h17m for 6 tables + 6 blocks) — NOTHING BEATS THE SHIPPED 60; but the 9th one-drop is nearly free and the LAND TO CUT IS A PLAINS

**Every one of the six arms is positive (= slower than shipped v2), and every one replicates across the
two disjoint seed sets.** Pooled late-weighted, long .5 / 2hg .3 / std .2:

| arm | long | 2hg | std | **pooled** | meas | conf | own-table effect / floor |
|---|---|---|---|---|---|---|---|
| `m_v2` (shipped) | — | — | — | **0** | ref | ref | clears 3x+ |
| `u_b1_pl19` 9th one-drop, −1 Plains | +0.0089 | +0.0095 | **−0.0009** | **+0.0072** | +0.0069 | +0.0075 | 0.83x |
| `u_b1_rf3` 9th one-drop, −1 Remote Farm | +0.0136 | +0.0260 | +0.0117 | **+0.0169** | +0.0173 | +0.0165 | 1.49x |
| `u_b1_ap3` 9th one-drop, −1 Accorder | +0.0215 | +0.0209 | +0.0097 | **+0.0189** | +0.0181 | +0.0198 | 2.46x |
| `u_av1_pl19` Vial 1, −1 Plains | +0.0200 | +0.0208 | +0.0155 | **+0.0193** | +0.0194 | +0.0193 | 1.39x |
| `u_av1_rf3` Vial 1, −1 Remote Farm | +0.0441 | +0.0498 | +0.0348 | **+0.0439** | +0.0439 | +0.0440 | 2.30x |
| `u_av2_pl18` Vial 2, −2 Plains | +0.0533 | +0.0499 | +0.0384 | **+0.0493** | +0.0515 | +0.0471 | 2.03x |

**The replication is unusually tight** — `u_av1_pl19` at +0.0194 / +0.0193 and `u_av1_rf3` at +0.0439 /
+0.0440 are essentially exact across 240,000 independent games. No arm clears the driver's 3x floor
rule, but for the Basri arms that is because **the effects are genuinely small**, not because the
apparatus is suspect: `u_b1_pl19`'s bias is `+0.0018` at `t = +0.67`, i.e. no measurable bias at all.

**THE FLOOR CONFIRMS THE DESIGN'S PREDICTION, QUANTITATIVELY.** The design said the alias is "fine for
Basri and a real stretch for Vial — read the Vial arm's FLOOR with extra suspicion, not its point
estimate." The per-arm nulls sort exactly that way, and the base null is `+0.0005`, so all of it is the
arm's own table rather than the reference:

| arm | null (own vs shared) | why |
|---|---|---|
| `u_b1_pl19` / `u_b1_ap3` / `u_b1_rf3` | +0.0023 / −0.0043 / −0.0098 | the alias is nearly **literal** — Basri *is* a {W} 2/1 Knight |
| `u_av1_pl19` / `u_av1_rf3` / `u_av2_pl18` | −0.0273 / −0.0221 / −0.0387 | aliasing a **Vial** as a 2/1 Knight is a bad model of the keep; the own table, which gives Vial a real bucket, plays 6–17x better |

So the Vial arms' large floors are the **shared** column being wrong, not evidence the effect is
apparatus-driven. The OWN column — what `pool_h.py` reports — is the read.

#### Q1 — "is there no spot remaining for a 9th one-drop?" NO FREE SPOT, BUT A PLAINS IS NEARLY FREE

The payer ordering is the finding, and it is clean: **Plains (+0.0072) < Remote Farm (+0.0169) <
Accorder Paladin (+0.0189)**. Subtracting round T's `+0.0038` bucket artifact (measured on provably
identical play, so it is the same artifact these arms carry) puts the true cost of a 9th one-drop at
roughly **+0.003 paid by a Plains**, against ~+0.013 paid by a Remote Farm and ~+0.015 paid by a body.

**And at 20 life it is a wash or better: `−0.0009`.** That is the only negative cell anywhere in round
U, and it is the mechanism in one number — a 9th one-drop buys a marginally faster clock, and pays for
it in long games where the 24th land is what keeps you hitting drops. Under the user's late-weighted
ranking (long .5) that nets out positive; under a 20-life-first weighting it would not.

**A 9th one-drop is therefore affordable but not free, and it is affordable only out of the mana base.**
Taking the slot from a two-drop body costs 2.6x as much.

#### THE INCIDENTAL FINDING, which is useful independently of Basri: IF YOU EVER CUT A LAND, CUT A PLAINS

Cutting a **Remote Farm** costs more than cutting a **Plains** in both pairs, with the direction
consistent and the magnitude large: `+0.0169` vs `+0.0072` on the Basri arms (+0.0097) and `+0.0439` vs
`+0.0193` on the Vial arms (+0.0246). This is counter-intuitive — Remote Farm enters **tapped**, which
is the worst property a land can have in a turn-4 aggro deck, and the Plains-cut arm keeps *fewer*
untapped sources.

**Mechanism, and it is a mana-units argument rather than a tempo one:** Remote Farm taps for `{W}{W}`
and does it twice before sacrificing, so cutting one removes **two** mana-units per activation where
cutting a Plains removes one — and this deck needs `{W}{W}{W}` for 4 Benalish Marshal, which is its
only triple-pip card. Remote Farm is the double-white engine, and that outweighs entering tapped. The
super-additivity on the Vial pair (0.0246 vs 0.0097) says Vial and a Farm-cut compound: Vial costs a
card that produces no mana at all, so it is worst exactly when the double-white is already thinnest.

#### Q2 — "how does a vial look when replacing a land?" IT LOOKS ALMOST EXACTLY LIKE VIAL REPLACING A CREATURE

**+0.0193 paid by a Plains, against +0.0209 paid by a Venerable Knight in round T. Cutting a land
instead of a body saves 0.0016 — the cost is intrinsic to the card, not to what it displaces.**

The control decomposition the design was built for (both terms measured on own tables, so the bucket
artifact cancels in each difference):

```
u_av1_pl19 − u_b1_pl19  =  0.0193 − 0.0072  =  +0.0121   Vial vs a 2/1 one-drop, at 23 lands
t_av1_vk3  − t_basri1_vk3 = 0.0209 − 0.0038 =  +0.0171   Vial vs a 2/1 one-drop, at 24 lands
```

**The gap narrows by 29%, and it narrows in all three formats independently** (long 0.0177→0.0111, 2hg
0.0138→0.0113, std 0.0206→0.0164). So Vial *is* genuinely substituting for part of the land it
replaced — the effect is real and replicates — but it recovers only about a third of its cost, and the
remaining +0.0121 is what Vial gives up against simply playing another body.

**The second copy refutes the "a lone Vial is rarely drawn" hypothesis for the third time in this
campaign, and does so harder when lands pay.** Creature-paid the gradient is linear (+0.0209 → +0.0417,
i.e. +0.0208 per copy); land-paid it *accelerates* (+0.0193 → +0.0493, so the second copy costs
+0.0300). At 24→23 lands Vial partially substitutes; at 23→22 the mana base breaks. More Vials is
worse, not better, on every chassis this campaign has measured.

**Ranking every Vial configuration ever measured on this deck**, best to worst: 1 Vial for a Plains
(+0.0193) < 1 Vial for a one-drop (+0.0209) < 2 Vial for 2 Plains (+0.0493) < 1 Vial for a Remote Farm
(+0.0439, and 2-for-2 is worse still). **The best available Vial configuration costs 2.7x what the best
9th-one-drop configuration costs, and both cost more than doing nothing.**

#### WHAT THE MEASUREMENT CANNOT SEE, stated for both cards, because they differ

* **Vial — the unmodelled effects point in OPPOSITE directions, so the magnitude is bracketed, not
  floored.** *Understating the cost:* Vial deploys do not **cast**, so they trigger none of the deck's 4
  Worthy Knights, and a goldfish only values those Human tokens as attackers — in a real game they also
  block and trade, so the ~13-tokens-per-game engine is worth more than the sim credits and Vial's
  skipping of it costs more. *Overstating the cost:* the user's three stated reasons — uncounterable,
  instant-speed deploy, dodging sorcery-speed removal — are all structurally invisible to a goldfish
  that has nothing to be countered by, nothing to dodge and no opponent turn to act during. **The sign
  is settled (six independent blocks, +0.0193 replicating to four decimal places); the magnitude is
  bracketed by two unmodelled effects pulling opposite ways.** Whether an instant-speed deploy is worth
  ~0.019 turns of measured speed is a judgement the simulator cannot make.
* **Basri — the unmodelled effect points ONE way, and it is the favourable one.** Cycling `{2}{W}` is a
  perfectly real ability that a human player *would* use to turn a dead late one-drop into a card; the
  engine never offers it only because `GenericProvider::HasAnyDigSource` returns false for this provider
  chain (round T). That is an engine limitation, not a rules one. So **+0.0072 is an upper bound on the
  cost of 1 Basri**, and the true figure is lower by an unmeasured amount — plausibly enough to make it
  free in real play. Adopting on that basis would be a judgement call, not a measurement, and list
  adoption is the user's call.

#### VERDICT — NO CHANGE. The shipped v2 60 wins on every arm tested.

Nothing is adopted and nothing is staged. Both of the user's questions are answered in the negative,
but with two things worth keeping: **a 9th one-drop is affordable (~+0.003) if and only if a Plains
pays**, and **Remote Farm is not the land to cut** — a finding that will apply to any future land-count
question on this deck.

## Round V (2026-09-29) — price the slot against the deck's OWN blank, not against a land

### LANDED 12:33Z. VERDICT: **Unexpectedly Absent is a real cost, and cutting it is the deck's
### cheapest available gain.** Two of three arms established; the Vial arm is void (see the defect below).

Step vs the shipped v2, **negative = faster = better**; pooled late-weighted (long .5 / 2hg .3 /
std .2) over 40,000 games per arm per format, across two disjoint seed families (19.2M / 19.6M).

| arm | edit | long | 2hg | std | **pooled** | meas / conf | bias | margin |
|---|---|---|---|---|---|---|---|---|
| `v_ua0_pl21` | UA → 21st Plains | −0.0237 | −0.0201 | −0.0174 | **−0.0214** | −0.0204 / −0.0223 | −0.0002 | **4.38x** ESTABLISHED |
| `v_b1_ua0` | UA → 1 Basri | −0.0187 | −0.0134 | −0.0195 | **−0.0173** | −0.0182 / −0.0164 | −0.0014 | **3.12x** ESTABLISHED |
| `v_av1_ua0` | UA → 1 Aether Vial | +0.0016 | +0.0016 | +0.0005 | +0.0014 | −0.0000 / +0.0029 | −0.0192 | 0.81x **VOID** |

**Every sign replicates in all three formats and across both seed families**, and the two established
arms have near-zero apparatus bias (their own tables and the shared one agree to 0.0002 and 0.0014),
which is the cleanest bracket this campaign has produced.

**The finding: the blank costs about 0.02, and what replaces it barely matters — as long as it does
something.** A 21st Plains buys −0.0214 and a Basri buys −0.0173, a gap of 0.0041 that sits at the
edge of the floor. This is the answer to the user's actual question. It also **retrospectively
justifies the whole round**: round U hunted for a free payer among cards it was allowed to cut and
found none (Plains +0.0072 was the best), because cutting interaction had been ruled out of scope.
The deck's worst card was never on the table.

**The third arm is the one that broke the case open.** Replacing a dead card with a Vial measured
+0.0014 — *worse than replacing it with a land by 0.0228*. A card that cannot beat a Plains in the
slot vacated by the deck's acknowledged blank is not a card that is merely overpriced; it is a card
doing nothing at all. That is exactly what it turned out to be — see the defect section below. Its
0.81x floor margin and its −0.0192 bias (by far the round's largest) were the apparatus reporting
the same thing in its own language: the arm's own table could not find anything to fit.

**Consequence for the final list.** Unexpectedly Absent is the payer for whatever the deck adds next.
Note the out-of-model caveat cuts the *normal* way here, not both ways: UA's value (answering a
resolved permanent) is invisible to a goldfish, so −0.0214 is an **upper bound on the gain** from
cutting it, and a real opponent is the reason to keep it. That is the user's judgement, not the
model's. The model's contribution is narrow and solid: *within the goldfish, the slot is free.*

**The question.** User, on Aether Vial after round U: *"it seems kind of nice to have, but the
measurements don't look too good for it. How does the number compare vs a dead card in goldfish like
Unexpectedly Absent?"*

**The honest answer is that it has never been measured, and the number that looks like an answer is
the wrong KIND of number.** The repo carries `card_scores["Unexpectedly Absent"] = −0.2814` on v2's
own profile — the worst card in the deck, with Swords second at −0.1981 and the fifteen-card spread
running −0.28 to +0.21. That is not comparable to Vial's +0.0193 for two independent reasons, and
both are recorded elsewhere in this document:

* **Different estimand.** A card score is fitted to the *keep decision* — how much worse an opening
  hand is for holding the card — not to the race. The screen numbers are pooled, late-weighted
  win-turn steps over 40,000 games in three formats.
* **Different reliability.** See "THE CARD SCORES INVERTED THREE TIMES OUT OF THREE" above. They are
  hypothesis generators on this deck, never findings.

**Why Unexpectedly Absent is the right yardstick, which is the whole design.** Rounds T and U priced
Vial against cards the deck *wants* — a Venerable Knight (+0.0209) and a Plains (+0.0193) — so it was
always going to look expensive. But the deck already ships a card the goldfish cannot use **at all**:
Unexpectedly Absent needs an opponent permanent to target and the passive opponent presents none. We
keep it anyway, for value the simulation cannot see. So if Vial costs *less* than the blank we already
accept, "too expensive in-model" stops being an argument against it, and the slot becomes a choice
between two kinds of unmodelled value — a judgement, not a measurement.

| tag | added | paid by | asks |
|---|---|---|---|
| `m_v2` | — | — | reference: the shipped v2 60 |
| `v_av1_ua0` | Aether Vial 1 | Unexpectedly Absent | **the question** |
| `v_ua0_pl21` | Plains 21 | Unexpectedly Absent | what the blank costs, against a land |
| `v_b1_ua0` | Basri 1 | Unexpectedly Absent | the 9th one-drop, paid by the blank |

**Every arm pays with the same card**, so the payer cannot confound the comparison *between* arms —
and that buys three independent paths to one quantity. Writing `c(X)` for a card's cost in the slot:

```
step(v_av1_ua0)  = c(Vial)   − c(UA)        step(v_ua0_pl21) = c(Plains) − c(UA)
step(v_b1_ua0)   = c(Basri)  − c(UA)
round U, same apparatus:  u_av1_pl19 = +0.0193 = c(Vial) − c(Plains)
                          u_b1_pl19  = +0.0072 = c(Basri) − c(Plains)
```

So `step(v_av1_ua0) − step(v_ua0_pl21)` must reproduce **+0.0193**, and
`step(v_b1_ua0) − step(v_ua0_pl21)` must reproduce **+0.0072**. If they do, `c(UA)` is established
three ways and the answer is not resting on one arm. If they do not, the apparatus is lying somewhere
and the round says so before anything is adopted.

**`v_b1_ua0` is also a real deckbuilding question, not only a control.** Round U found no free payer
for a 9th one-drop among the cards it was allowed to cut (Plains +0.0072, Remote Farm +0.0169,
Accorder Paladin +0.0189). It never tried the deck's *worst* card, because cutting interaction was
out of scope. If the blank is the payer, the 9th one-drop may be free or better than free.

**Artifact correction — MEASURED at generation time, and it lands on ONE arm, not the two predicted.**
The design above expected `v_av1_ua0` and `v_b1_ua0` both to inherit round T's +0.0038 (a 14th bucket
from an added card). The generated tables say otherwise, and the reason is a fact about this deck that
had not been noticed: **the shipped table's bucket 0 is `[Swords to Plowshares, Unexpectedly Absent]`
— the two interaction spells already share a bucket.** So cutting UA does not free a bucket, it
shrinks that one; and what the added card does depends on whether discovery will have it.

| arm | K | what happened | correction |
|---|---|---|---|
| `m_v2` (ref) | 13 | bucket 0 = `[Swords, Unexpectedly Absent]` | — |
| `v_av1_ua0` | **13** | bucket 0 = **`[Aether Vial, Swords to Plowshares]`** — Vial landed in the bucket UA vacated | **none** |
| `v_ua0_pl21` | **13** | bucket 0 collapses to `[Swords]`; the 21st Plains adds nothing | **none** |
| `v_b1_ua0` | **14** | Basri gets his OWN bucket, reproducing round T exactly | **subtract +0.0038** |

Two things follow, and the first is a result in its own right.

**~~Equivalence discovery cannot distinguish Aether Vial from Swords to Plowshares for the keep
decision.~~ RETRACTED — it was a symptom of the defect below, not a finding.** The claim as
committed was that discovery clusters on mean |Δ win-turn| per probe, and that on that metric a
1-mana artifact deploying creatures a turn early and a 1-mana exile spell with nothing to exile come
out as the same card — offered as independent mechanical corroboration of the user's premise,
reached before a game was played.

It corroborated nothing. **The Vial in `v_av1_ua0` never gained a charge counter**, so it could
deploy only mana-value-0 creatures, of which this deck has none. Discovery clustered two cards that
both did literally *nothing*, and that is the only sense in which they were indistinguishable. The
right reading is the opposite of the one committed: the co-bucketing is a **detector** for the
defect, not evidence about the card. See the next section.

**And the merge ruling is demonstrably needed rather than merely tidy.** `v_b1_ua0` is the second
independent run to give a lone Basri his own bucket (round T was the first). Discovery's metric is
integer win-turn granularity over an 8-turn horizon, so a 1-of simply cannot generate enough
whole-turn observations to be placed — it is not a close call the threshold could fix. The
`PENDING-ADOPTION` ruling is what removes it.

**Out-of-model, and here it cuts BOTH ways — which is why this round cannot settle the slot alone.**
Vial's stated value (uncounterable, instant-speed deploy, dodging sorcery-speed removal) is invisible
to a goldfish, so its measured cost is a floor. But Unexpectedly Absent's value is invisible for the
*same reason* — no opponent permanent to target — so its cost is a floor too. The round compares two
floors, which is fair, and it still cannot say whose real-game upside is larger. That is the user's
judgement. The one thing that *is* modelled and runs against Vial: a creature put onto the battlefield
by Vial is not **cast**, so it triggers none of the deck's four Worthy Knights.

**Engine note.** This is the first WhiteKnights round on the generic dig gate
(`docs/design/generic-dig-gate.md`). WhiteKnights is byte-identical under it — the shipped 60 holds no
dig source — and **Basri stays a clean null even though his cycling is now reachable**, because the
generic heuristic digs only when the hand holds nothing castable and a {W} 2/1 Knight is always
castable. User: *"A 2/1 Knight is always useful in goldfishing… It would be most helpful for stuck
boards in real games. In those situations the 2/1 may not be useful, but something else in the deck
could be."* A goldfish has no stuck boards, so round T's 100.0%-identical-play finding survives the
capability change and Basri's cycling joins Vial's instant-speed deploy in the out-of-model column.

### THE AETHER VIAL MEASUREMENTS WERE ALL PRICING A BLANK (found 2026-09-29, fixed `c4da77cd`)

The user asked a question that looked like paperwork: *"On Aether Vial what heuristic are we using?
We should be using the same rule as other decks. (if we aren't using a rule at all that would pollute
the results somewhat)"*. The answer was worse than the question feared.

**The rule was never the problem, and it is shared.** `GenericProvider::WantVialCharge` delegates to
`::WantVialCharge` in `SpellEffects.h` — the hand-aware root policy every Vial deck uses, and
WhiteKnights reaches it through `VialProvider → KnightsProvider → WhiteKnightsProvider`. Nor is it
searched into oblivion: `MTG_VIAL_AXIS` has been default-OFF since the user's own 2026-08-30 ruling
(*"in general we don't want to fully search vial decisions, because that is a waste of effort"*) and
`MTG_SEARCHED_VIAL` defaults false. No bespoke heuristic, none missing, no branching tax.

**The INPUT was the problem.** `WantVialCharge` opened with `if (target <= 0) return false;` where
`target = state.vial_target_mv`. That field is not a tuning constant — it is a pure **function of the
decklist** (the deck's dominant creature mana value, 0 for a deck holding no Vial). But it was carried
on `MulliganProfile`, and `deck_compare.py` hands every arm the **base deck's** profile. The base is
v2, which holds no Vial, so the field was absent → 0 → the charge policy declined on line one,
forever. A Vial on zero counters can only deploy creatures of mana value zero, and this deck has none.
**The card was a blank that cost a card**, and nothing errored or warned — a Vial at zero counters is
a legal permanent that simply never does anything.

Measured, not inferred — the same 300 games at d5, same seeds, one field changed:

| `vial_target_mv` | avg win turn | play digest |
|---|---|---|
| absent (what every arm ran) | 4.2500 | `42020a6059026bfd` |
| `= 3` (what the decklist says) | **4.2400** | `8e66ec98908e5e3f` |

**What it invalidates, and what it does not.** Invalid: every arm that ADDED Vial to a base without
one — `t_av1_vk3` +0.0209, `t_av2_vk2` +0.0417, `u_av1_pl19` +0.0193, `u_av1_rf3` +0.0439,
`u_av2_pl18` +0.0493, `v_av1_ua0` +0.0014. All are upper bounds on a card never allowed to act. Still
valid: the v1-era **cut** ladders (3→2 −0.0154, 3→0 −0.0423, 4th copy −0.364), because those base
decks *contained* the Vial and so their profiles carried the field. Also demoted: the round V
"discovery buckets Vial with Swords" finding, retracted above.

**Fixed as a class, because this was the third instance.** The same shape had already cost
`slivers_vial` a 2.6x error in 2026-08 (a screen on a default profile read the cut of two Vials as
−0.0953 instead of −0.0246) and left Pirates with a Vial that *"never charges, because there is no
`.profile.json` yet"*. So the fix is not a default change:

* `GoldFishRunner::DeckVialTargetMv` derives the ceiling from the decklist (keyed on the
  `upkeep_adds_charge` param, not the card's name), and `StampDeckTraits` stamps it beside every
  other deck trait. **One writer.**
* The field is **deleted from `MulliganProfile`**, its JSON is no longer written, and the read is
  replaced by a comment saying not to restore it. Nothing is left to omit, to copy from the wrong
  deck, or to keep in sync.
* All **sixteen** `state.vial_target_mv = profile.vial_target_mv` overwrites are gone — including
  `BatchRunner`'s per-job scalar, which is the path every screen and every regression run uses.
* The duplicate derivations in `AnalyzerEngine::ComputeVialTargetMv` and `analyze_deck.py` are
  deleted. There were **three** copies of one function; now there is one.
* `WantVialCharge`'s early return is gone. The hand-driven half needs no target at all; only the
  speculative pre-charge does, and it is guarded there.
* `test/unit/test_vial_target_from_decklist.cpp` pins the derivation (including the higher-MV
  tie-break Goblins relies on), the stamp through `SetupGame` with no profile present, and that a
  0-counter Vial still charges toward a creature in hand.

**Suite byte-identical** — 0 configs changed / 101 unchanged, 0 searched slower / 0 faster / 0
play-changed — with slivers, knights, goblins, minotaur and pirates all in that suite. That is the
proof the fix reaches only decks whose Vial was already broken: all six shipped profiles already
agreed with what their decklists derive.

**And verified POSITIVELY, on the real path, because byte-identity alone can mean the new code never
fires** (`digest-equality-can-mean-broken`). Two checks on round V's own `v_av1_ua0` arm directory —
the exact decklist and the exact profile that produced the void +0.0014:

1. `MTG_BATCH_STATE_DUMP` through `mtg --batch` prints **`vial=3`**. That is the BatchRunner path
   every screen and every regression run uses, reading a profile that does not contain the key.
2. 250 traced games at 40 life: the Vial resolved in 31 of them and **deployed 20 creatures for
   free** — Worthy Knight ×4, Dauntless Bodyguard ×4, Knight Exemplar ×2, Venerable Knight ×2,
   Benalish Marshal ×2, Accorder Paladin ×2, Silverblade Paladin ×2, Adeline ×1, Kinsbaile
   Cavalier ×1. Under the old binary this number was necessarily **zero**: with the target at 0 the
   counter never advanced, and the deploy requires `charge_counters == mv` exactly, which no card in
   the deck satisfies at 0.

The spread of deployed mana values is worth noting on its own — the Vial reached **4** counters
(Kinsbaile Cavalier) as well as 1, 2 and 3. That is the hand-aware `count_above` clause climbing past
the deck's dominant MV toward a bigger creature actually in hand, which is the behaviour the policy
was written for and which had never once executed on this deck.

A caution the probe also produced, and it is a real limit rather than a bug: at 20 life with an
8-turn horizon the same 250 games yielded **no** deploys at all. A Vial cast on turn 2 reaches three
counters on turn 5, and this deck's mean kill is turn 4.1 — so in the fast format the card runs out
of clock before it can act. Expect the corrected cost to be format-dependent, and expect the `std`
column to remain the worst of the three.

**The generalisable lesson.** A field that is a *function of the decklist* must be derived from the
decklist at the point of use, never carried alongside it in a file that can describe a different
decklist. Everything else in `StampDeckTraits` already worked this way; this one field did not, and
it took three separate mismeasurements before anyone looked at it. The user's reaction sets the bar:
*"If it never increments the counter by default that is a disaster. (and should NOT be our default).
I would rather search it than do that."* — and, the next morning, *"We need to make what happened
with vial here impossible."*

**Swept the rest of the profile for the same shape; there was exactly one.** After the fix, the only
things a `MulliganProfile` puts onto a `GameState` are `m_required_pieces` and `m_card_scores` —
both pointers to *fitted* quantities used for discard ranking and hand scoring. Every other profile
field (`min_color_sources`, `min_playable`, `curve_check`, `stop_at`, `bottom_eval_*`,
`search_leaf_depth`, `card_scores`, the keep/eval/value models) is a **fitted or learned** parameter,
not a decklist function. That distinction is the one that matters, and it is why sharing an
apparatus across screen arms is still correct: a fitted parameter imported from a neighbouring
decklist makes a *decision* slightly worse, which the floor bracket is built to measure. A
decklist-derived field imported from a neighbouring decklist makes a **card inert**, which no
bracket can see, because the arm's own table dutifully fits itself to a deck playing 59 cards.

### The merge ruling and the list change are ONE decision

User, 2026-09-29: *"We can merge Basri."* Writing `decks/WhiteKnights/WhiteKnights.buckets.json`
immediately failed: `mtg-analyze` refuses a policy naming a card the deck does not contain — *"a ruling
that matches nothing is silently no ruling at all."* The shipped 60 has Dauntless Bodyguard and
Venerable Knight but no Basri, so the file made **every** analyzer run on this deck fail, which would
have broken `analyze_deck.py`, `valueleaf.sh` and `mullgen.sh`. Round V's first block caught it in 90
seconds. The guard is right and the ordering error was mine: **a merge ruling presupposes its cards
are in the deck.** The ruling is parked untracked at
`logs/wk_screen/WhiteKnights.buckets.json.PENDING-ADOPTION` and installs the moment Basri is adopted;
adoption also owes the serial pipeline (profile → value leaf → mulligan), which the merge makes cheap
(bucket stays at 8 cards, K stays 13) but not free.

Kept, and independently correct: `deck_compare.py` now copies `<stem>.buckets.json` into each arm's
scratch directory beside the profile and value sidecar. `mtg-analyze` resolves the ruling off the
**decklist** it is handed, and an arm's own bracket table is generated from a scratch decklist — so
without it every generated table is fit to a bucketing we do not ship, exactly the defect the profile
copy already guards against.

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
