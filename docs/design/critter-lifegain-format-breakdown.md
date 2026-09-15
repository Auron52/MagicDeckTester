# CritterLifegain — a card-by-card breakdown of both formats

**Date:** 2026-09-15. **Engine:** `065fec8a` (+ the `deck_compare.py` format-axis change this work
needed). **Companion to** `docs/design/analysis-CritterLifegain.md`, which is the deck's running
ledger; this file is the format comparison it did not have.

Everything here comes from **two pooled batches** and one small probe, all on one frozen binary:

| run | spec | seed block | cells | games |
|---|---|---|---|---|
| the breakdown | `logs/deckcmp/critter_breakdown.json` | 1,100,000 | 19 arms x 2 formats = 38 | 1,520,000 |
| the Serra funding screen | `logs/deckcmp/critter_serra_2hg.json` | 1,800,000 | 8 arms x 2 formats = 16 | 640,000 |
| the second-head probe | `logs/deckcmp/critter_heads_inert.json` | 1,700,000 | 2 arms x 2 formats = 4 | 40,000 |

---

## 0. How to read every number in this file

- **The metric is average turn-to-win, and lower is better.** A delta of `-0.03` means *three
  hundredths of a turn faster*. "Worth" always means "worth in turns of speed".
- **Every marginal is measured against one more Plains**, because a 60-card deck has no empty slots:
  cutting a card means playing something else, and a Plains is the least-opinionated something else.
  So "the 4th Soul Warden is worth +0.031" means *this copy beats a 25th land by 0.031 turns*, not
  that it is worth 0.031 in the abstract.
- **Standard error is ~0.002 on every marginal** (40,000 paired games, ~87% of games identical
  between arm and reference). Treat **0.006 as the resolution limit** and anything under it as "not
  distinguishable from a Plains".
- **Deltas are comparable across the two formats; LEVELS are not.** A 2HG base wins on turn 5.11 and
  a 20-life base on turn 4.77 — that gap is the format, not a card.
- **`t` is not a verdict here.** The apparatus bias floor is unmeasured on this route (see §10), so a
  statistically overwhelming 0.004 is still unresolved. The numbers that matter in this document are
  the ones in the 0.02–0.20 range, which are 3–100x any floor this repo has ever measured.

---

## 1. What "2HG" actually is in this harness — and it is not what the name suggests

**Measured, not argued: the second head does nothing to this deck.** Holding `starting_life` at 30
and moving *only* `opponent_heads` from 1 to 2 produces **byte-identical play**:

```
h1::base   played=10000  avg=5.1137  digest=e45e9d8eeef2c9e5
h2::base   played=10000  avg=5.1137  digest=e45e9d8eeef2c9e5     <- identical
h1::final  played=10000  avg=4.8413  digest=2acffdf3fe0f4672
h2::final  played=10000  avg=4.8413  digest=2acffdf3fe0f4672     <- identical
```

That is a proof, not a sign test — the per-block digest is a hash of the actual play, so equality
means *not one decision differed in 10,000 games*.

**Why it is structural, and would hold at any sample size.** `opponent_heads` does **not** split the
life pool: both heads share `players[1]` (`src/core/GameSetup.h`). What a second head changes is (a)
**targeting** — there is a second face a spell can point at — and (b) **"each opponent" arithmetic.
CritterLifegain has neither.** Its only targeting is Heliod's counter trigger (targets *our own*
creature) and Unexpectedly Absent (a nonland permanent); it has no burn, no drain, and no "each
opponent" effect anywhere. So the extra face is unreachable.

### Therefore: in this harness, "2HG" means exactly one thing — **both players start at 30 instead of 20**

That single change does two things, and it is worth separating them because they pull in opposite
directions:

| | effect |
|---|---|
| **The opponent has 30 life.** | 50% more damage to deal. This is why every 2HG number is slower, and why the format rewards raw board growth over a fast start. |
| **WE start at 30 life.** | `life_threshold_pump_life` is a **literal 30** (`SpellEffects.h` reads `players[c].life >= 30`, it does **not** scale with starting life). So **Serra Ascendant is a 6/6 lifelink for `{W}` from turn one**, instead of a 1/1 that has to gain +10 first. |

**The whole format divergence in this deck traces to that second row.** Section 5 shows it is
literally the only card that changes rank.

---

## 2. The two formats at a glance

The settled list (`final`, 60 cards / 24 lands — the one in the ledger) against the shipped list:

| | 20 life | 2HG (30 life) |
|---|---|---|
| shipped list (`base`) | 4.7700 | 5.1169 |
| settled list (`final`) | **4.4697** | **4.8471** |
| the settled list's gain | **-0.3003** +-0.0031 | **-0.2699** +-0.0033 |

### Win-turn distribution — where the speed actually comes from

```
                   20 life                        2HG
turn        base        final            base        final
  4       34.98%       57.95%          16.02%       28.28%
  5       55.40%       37.85%          61.01%       60.97%
  6        7.84%        3.62%          19.54%        9.00%
  7        1.36%        0.47%           2.42%        1.38%
  8        0.29%        0.08%           0.74%        0.28%
unwon      0.14%        0.03%           0.28%        0.09%
```

Three things to take from this table:

1. **The deck is a turn-4/turn-5 goldfish kill in both formats.** The settled list's whole gain at 20
   life is converting turn-5 kills into turn-4 kills (+23 points of turn-4 rate).
2. **2HG shifts the same curve one turn right**, as the 50% extra damage requires. The settled list's
   2HG gain is mostly draining turn 6 (19.5% -> 9.0%).
3. **Games essentially never go long — 0.03% and 0.09% unwon by turn 8.** Hold onto this: it is the
   single biggest thing the harness cannot see, and it is exactly what §8 is about.

---

## 3. The centrepiece — every card's marginal value, in both formats

Each row is one arm that changed **one slot** of the settled list, re-centred on that list
(`scripts/screen_marginals.py --ref final`). **Positive = this copy beats one more Plains.**

| copy | 20 life | 2HG | agree? |
|---|---:|---:|:--:|
| **Serra Ascendant, 2nd** | +0.0118 | **+0.1099** | ✗ **9.3x** |
| **Serra Ascendant, 3rd** | +0.0100 | **+0.0977** | ✗ **9.8x** |
| Soul's Attendant, 4th | **+0.0340** | +0.0158 | ~ |
| Soul Warden, 4th | **+0.0315** | +0.0175 | ~ |
| Ocelot Pride, 4th | +0.0285 | **+0.0334** | ✓ |
| Voice of the Blessed, 4th | +0.0260 | +0.0212 | ✓ |
| Ajani's Pridemate, 4th | +0.0246 | +0.0186 | ✓ |
| Daxos, the only one | +0.0042 | −0.0016 | ✓ (both ~0) |
| Remote Farm, 4th *(vs a Plains — land for land)* | +0.0035 | +0.0016 | ✓ (both ~0) |
| Heliod, 2nd | +0.0011 | +0.0059 | ✓ (both ~0) |
| Ranger-Captain, the only one | −0.0044 | −0.0028 | ✓ (both ~0) |
| Auriok Champion, 3rd | −0.0056 | −0.0071 | ✓ |
| **Ajani, Strength of the Pride, 2nd** | **−0.0121** | **−0.0086** | ✓ |
| **Archangel of Thune, 2nd** | **−0.0160** | **−0.0119** | ✓ |
| **Archangel of Thune, 3rd** | **−0.0179** | **−0.0149** | ✓ |
| **Unexpectedly Absent, 3rd** | **−0.0249** | **−0.0264** | ✓ |
| **Unexpectedly Absent, 4th** | **−0.0251** | **−0.0250** | ✓ |

*(se ~0.002 throughout; `~` marks a pair that keeps its sign but changes size by more than 1.5x.)*

### The shape of the deck this reveals

The list divides cleanly into three bands, and **the band boundaries are the same in both formats**:

- **Clearly better than a land (+0.02 to +0.11):** the 1-drop lifegain enablers, the 2-drop counter
  payoffs, and Ocelot Pride. These are the deck.
- **Indistinguishable from a land (±0.006):** Daxos, the 2nd Heliod, the Ranger-Captain, the 4th
  Remote Farm. Four slots the simulator has no opinion about.
- **Worse than a land (−0.005 to −0.027):** the 3rd Auriok, the 2nd Ajani walker, the Archangels, and
  every Unexpectedly Absent. **Three of these four are protected by a user ruling or by known
  un-modelled upside — see §8 before reading anything into them.**

---

## 4. Where the formats agree — which is nearly everywhere

The cross-format difference-of-differences (paired per game index: both formats run the same seeds
and the same `deck_numbering`, so a game opens on the *same seven cards* in both worlds):

```
every card except Serra Ascendant:   |2HG − 20 life|  <=  0.018
Serra Ascendant, 2nd copy:                             =  0.098   (t = +42.8)
Serra Ascendant, 3rd copy:                             =  0.088   (t = −34.2)
```

**Not one card other than Serra Ascendant changes sign, and not one changes rank band.** The
practical consequence is large and worth stating plainly: **you do not need two decks.** Fifty-eight
of the 60 slots want the same thing in both formats.

The largest non-Serra movements, for completeness:

| | 20 life | 2HG | reading |
|---|---:|---:|---|
| Soul's Attendant 4th | +0.0340 | +0.0158 | the 4th enabler matters ~2x more when 20 damage ends it |
| Soul Warden 4th | +0.0315 | +0.0175 | same |
| Ocelot Pride 4th | +0.0285 | +0.0334 | the only *non-Serra* card that gains in 2HG — longer games give the end-step Cat more turns to compound |

---

## 5. Where they disagree — Serra Ascendant, and nothing else

At 20 life Serra Ascendant is a `{W}` 1/1 that becomes a 6/6 *once you have gained 10 life*. The deck
does gain 10 life routinely, so the card is not bad — just **slow, and late**. At 30 starting life it
is a **6/6 lifelink for one mana on turn one**, with no condition to meet.

The mechanism is visible directly in the win-turn distribution. Number of Serras vs turn-4 kill rate:

```
              1 Serra   2 Serra   3 Serra
 20 life       57.32%    57.95%    58.43%        +0.5 points per copy
 2HG           20.39%    28.28%    35.31%        +7.5 points per copy    <- 15x
```

**Each additional Serra Ascendant converts ~7.5% of all 2HG games into a turn-four kill.** At 20 life
the same copy converts half a percent.

### The marginal is still almost FLAT at four copies

| copy | 2HG worth | 20-life worth |
|---|---:|---:|
| 2nd | +0.1099 | +0.0118 |
| 3rd | +0.0977 | +0.0100 |
| 4th | +0.0825 | +0.0109 |

A card whose 4th copy is still worth 75% of its 2nd copy is **nowhere near its optimum count** in
that format. (Diminishing returns are real but mild: 0.110 -> 0.098 -> 0.083.)

---

## 6. The Serra count — and the finding that changes the 20-life answer too

The ledger currently pins Serra at 2, on this ruling:

> *"I don't want 4 Serra Ascendant for this list **unless we have easy cuts** (and I don't think we
> do)... that doesn't mean I want to reduce it below 2 either."*

**The condition in that sentence is now satisfied, and by measurement rather than by argument.** Two
slots in the settled list are not merely cheap, they are *negative*: cutting them for a **Plains**
already gains speed.

- the **2nd Ajani, Strength of the Pride** — cutting it gains 0.0121 (20 life) / 0.0086 (2HG)
- the **3rd Auriok Champion** — cutting it gains 0.0056 (20 life) / 0.0071 (2HG)

Every earlier screen funded extra Serras out of **Ajani's Pridemate and Voice of the Blessed**, which
are +0.025 cards — which is why 4 Serra always looked like it cost ~0.05 at 20 life. It was paying
the wrong price. Funded from the two negative slots instead (`critter_serra_2hg.json`, 40,000 paired
games per cell, both formats, one batch):

| arm | funding | 20 life vs settled | 2HG vs settled |
|---|---|---:|---:|
| `serra3_plains` | Serra 3, −1 Plains | −0.0054 | −0.0941 |
| `serra4_plains` | Serra 4, −2 Plains | −0.0163 | −0.1766 |
| `serra3_ajani1` | Serra 3, walker 2→1 | −0.0216 | −0.1062 |
| `serra4_ajani1_daxos0` | Serra 4, walker 2→1, −Daxos | −0.0260 | −0.1930 |
| `serra4_ajani1_rc0` | Serra 4, walker 2→1, −Ranger-Captain | −0.0355 | −0.1973 |
| **`serra4_ajani1_auriok2`** | **Serra 4, walker 2→1, Auriok 3→2** | **−0.0371** | **−0.2018** |

**`serra4_ajani1_auriok2` is the best arm in BOTH formats** — and it is an improvement at 20 life, not
a concession to 2HG. Against the shipped list it is **−0.3385 at 20 life and −0.4752 in 2HG**.

Two supporting facts:

- **Held-out replication.** `serra3_plains` repeats the breakdown screen's Serra-3 arm on a disjoint
  seed block: 2HG +0.0977 -> +0.0941 (shrinkage 0.0036 ± 0.0037) and 20 life +0.0100 -> +0.0054
  (shrinkage 0.0046 ± 0.0033). **The 2HG effect reproduces cleanly; the 20-life Serra-3-for-a-Plains
  effect is small enough that it is not separable from the unmeasured floor.** The 20-life case for
  four Serras rests on the *funding*, not on Serra beating a Plains.
- **Held-out confirmation of the winner** (required by `deck-screening.md`, since this is the max of
  seven noisy arms). Re-measured against `base` on a disjoint seed block (2,300,000) under the same
  apparatus, **it reproduces in both formats**:

  | | screen (seed 1.8M) | held out (seed 2.3M) | shrinkage | pooled, 80,000 games |
  |---|---:|---:|---:|---:|
  | 20 life | −0.3385 ±0.0032 | −0.3327 ±0.0032 | +0.0058 ±0.0046 (t=+1.28) | **−0.3356** |
  | 2HG | −0.4752 ±0.0037 | −0.4726 ±0.0037 | +0.0026 ±0.0053 (t=+0.50) | **−0.4739** |

  Both shrinkages are inside noise, so the screen was not merely selecting on its own luck.

### The list this implies

```
4 Soul Warden          4 Voice of the Blessed          2 Heliod, Sun-Crowned
4 Soul's Attendant     1 Daxos, Blessed by the Sun     2 Auriok Champion
4 Serra Ascendant      1 Ajani, Strength of the Pride  1 Ranger-Captain of Eos
4 Ajani's Pridemate    2 Archangel of Thune            3 Unexpectedly Absent
4 Ocelot Pride        20 Plains                        4 Remote Farm
```

60 cards, 24 lands. It respects both standing rulings (2 Archangels kept; Unexpectedly Absent
untouched) and it does **not** cut a single card from the "+0.02 or better" band.

**This is a proposal, not an adoption.** Read §8 first — one of the two funding cuts is a slot whose
un-modelled upside points up.

---

## 7. The mana side

- **Remote Farm vs Plains is a wash at the 4th copy**: +0.0035 (20 life) / +0.0016 (2HG), both inside
  the resolution limit. The big Remote Farm win was the *Orzhov Basilica* swap (−0.090, in the
  ledger); the 4th copy is free either way.
- **Land count cannot be separated from card value in this design, by construction.** Every marginal
  arm trades a spell for a land, so the number it reports is `card − land`, and the sums confirm the
  local linearity: `m_absent + p_absent = +0.0002` and `m_thune + p_thune = +0.0020`, i.e. cutting the
  Nth copy and adding the (N+1)th are equal and opposite to within noise. What the table *does* say is
  where the 25th land ranks: **better than a 3rd Unexpectedly Absent or a 2nd Archangel, far worse
  than any 1- or 2-drop.**
- **The curve is why.** 29 of the 36 spells cost two mana or less:

  ```
  1 mana  14   Soul Warden 4, Soul's Attendant 4, Serra Ascendant 2, Ocelot Pride 4
  2 mana  15   Ajani's Pridemate 4, Voice 4, Auriok 3, Absent 3 (X=0), Daxos 1
  3 mana   3   Heliod 2, Ranger-Captain 1
  4 mana   2   Ajani, Strength of the Pride
  5 mana   2   Archangel of Thune
  ```

  A deck that wants three lands and never wants a sixth is exactly the deck where the 25th land is
  near-worthless and the top of the curve is the first thing to cut.

---

## 8. What the harness cannot see — read this before acting on §3

This is the part no measurement can supply, and it points **against** four of the rows above.

| card | measured | what the sim cannot model | direction |
|---|---|---|---|
| **Archangel of Thune** | −0.0160 / −0.0119 (worse than a land) | **0.03% / 0.09% of games are unwon by turn 8.** The stalled, grindy board where a 5-drop that pumps the team every lifegain event takes over is ~1-in-1000 here and common in reality. Your ruling — *"in games that drag out and the board is stuck the Archangels are key"* — is about a game state this harness produces roughly never. | **strongly up** |
| **Unexpectedly Absent** | −0.025 (the worst card in the deck) | The passive opponent has no permanent worth answering. Its whole real-game function — removing a blocker, a hatebear, an enchantment — is unreachable against a goldfish. This is the standing ruling not to re-propose cutting it; the measurement is exactly the artefact that ruling exists to override. | **strongly up** |
| **Auriok Champion** *(a §6 funding cut)* | −0.0056 / −0.0071 | Protection from black and red is inert on all four DEBT axes against this opponent (nothing deals damage, nothing enchants/equips, nothing blocks, nothing targets). Against a real red or black deck it is a house. | **up** |
| **Ajani, Strength of the Pride** *(the other §6 funding cut)* | −0.0121 / −0.0086 | Its `0` ability (exile Ajani and each artifact and creature your opponents control, at +15 life) is a one-sided wrath in reality and is **deliberately not enumerated** here, because against a passive opponent whose only permanents are inert spawn tokens it is strictly negative. | **up** |
| **Serra Ascendant** | +0.11 in 2HG | The granted **flying** is inert — this opponent never blocks. A 6/6 flier for `{W}` is better in reality than a 6/6 ground creature. | **up** (so 2HG is *under*-stated) |
| **Voice of the Blessed** | +0.021 to +0.026 | Flying/vigilance at 4 counters and indestructible at 10 are modelled but outcome-inert (no blockers, nothing that destroys). | **up** |
| **Ocelot Pride** | +0.029 / +0.033 | Nothing significant — the token engine is the card and it is fully modelled. A 200-game probe found 71.5% of games reach the 10 permanents ascend needs, with the copy half (a ≥2-token single-phase jump) firing in 22.5%. | neutral |

**The net of this table:** §6's proposal spends two slots whose true value is higher than measured,
to buy Serras whose true value is also higher than measured. In 2HG the gain (+0.20) is an order of
magnitude past any plausible correction. At 20 life (+0.037) the correction is a real fraction of the
effect — so **the 20-life case for the fourth Serra is good but not overwhelming**, and a reasonable
person could keep the 3rd Auriok and take `serra4_ajani1_rc0` (+0.0355) or `serra3_ajani1` (+0.0216)
instead. All three are improvements.

---

## 9. Recommendations

**If one list has to serve both formats** — take `serra4_ajani1_auriok2` (§6). It is the best measured
arm in each format independently, so there is no trade to make. Gain over the settled list: −0.037
(20 life), −0.202 (2HG); over the shipped list, **−0.3356 and −0.4739, both held-out confirmed over
80,000 games**.

**If you would rather not lean on the two flattered cuts** — `serra3_ajani1` (Serra 3, walker to 1,
nothing else touched) gets −0.0216 / −0.1062 while cutting only the slot that measures negative in
*both* formats and has the smaller real-game footprint of the two. This is the conservative version of
the same finding.

**If the list is for 20 life only** — the settled list is already good, and the honest statement is
that the remaining gains are small and partly inside the floor. The one clean, ruling-free improvement
is **the 2nd Ajani walker → anything**, worth +0.012.

**What I would not do:** act on the Archangel or Unexpectedly Absent rows. They are the two places the
harness is most clearly blind, and both are already covered by your rulings.

**Nothing here is adopted.** The decklist on disk is unchanged. An adopted list owes its own value
leaf, then its own mulligan table (strictly that order, alone on the box), then its own regression GT
— a screen's number is a *ranking*, not that deck's measured strength.

---

## 10. Apparatus, and what it does not bound

- **One shared apparatus on every arm and every format**: the shipped R=40 / K=13 keep table with the
  two standing aliases (Ocelot Pride into Ajani's Pridemate's bucket, Remote Farm into Orzhov
  Basilica's), plus the deck's play profile and value leaf. Composition fall-through 0.13% (breakdown)
  and 0.41% (funding screen), both inside the 1% limit, so the table was live on every cell.
- **The apparatus is fitted at 20 life / 1 head.** It is symmetric across arms, so every delta in this
  document holds; the 2HG *levels* are not comparable to 20-life ones, and a format-native table and
  leaf would be a separate build. This is the same accepted approximation the regression suite's
  `critter2hg` cases already use.
- **The bias floor is UNMEASURED.** `--floor` generates a table, and generation is prohibited on the
  approved alias route. The mitigation worth stating: with the alias every arm runs the *same table
  over the same composition space*, so the asymmetry the floor exists to bracket is far smaller by
  construction — but that is an argument, not a measurement. **Treat anything under ~0.006 as
  unresolved.**
- **Utilisation:** 32/32 workers busy (100%) throughout both batches.

### Reproducing

```bash
python3 scripts/deck_compare.py logs/deckcmp/critter_breakdown.json      # 38 cells, 1.52M games
python3 scripts/deck_compare.py logs/deckcmp/critter_serra_2hg.json      # 16 cells, 640k games
python3 scripts/deck_compare.py logs/deckcmp/critter_heads_inert.json    # the digest probe
python3 scripts/screen_marginals.py logs/deckcmp/critter_breakdown.json --ref final --dist base,final
```

**`logs/` is gitignored scratch, so those three specs are not in the repo** — the tables above are the
durable record. Each is reconstructible without them: take the settled list from
`analysis-CritterLifegain.md`, express it as counts relative to the shipped `.cod`
(`Archangel of Thune 2, Ajani Strength of the Pride 2, Auriok Champion 3, Ocelot Pride 4,
Orzhov Basilica 0, Plains 20, Remote Farm 4`), and make each arm that list with **one** count moved —
`m_*` takes a copy out and adds a Plains, `p_*` puts a copy in and removes one. Every arm needs
`"replace": {"Archangel of Thune": "Ocelot Pride", "Orzhov Basilica": "Remote Farm"}` so the new
copies inherit the departing cards' numbering, `"pool_table": false` so nothing is generated, and the
`profile`/`value_profile` pair pointed at `logs/deckcmp/critter_op_apparatus/` (the aliased table plus
copies of the deck's profile and value leaf — the engine resolves siblings directory-relative off the
profile, so pointing at `decks/` instead would silently detach the value leaf).

### Tooling this required

1. **A per-arm FORMAT axis in `deck_compare.py`** (`"formats": {...}`). The format used to be a
   property of the whole spec, which meant one invocation per format — two queues, two load-imbalance
   tails and no shared apparatus fingerprint, i.e. exactly the "one batch per variant" shape
   CLAUDE.md's pooling rule forbids. Now every combination runs under every format in **one** batch,
   each compared to its own format's base, and the cross-format comparison is **paired on the game
   index** because both formats open on the same hand. `--with-floor` refuses a multi-format spec (a
   floor is measured at one format), and single-format specs are byte-identical to before.
2. **`scripts/screen_marginals.py`** — re-centres a finished screen on any reference arm from its
   existing `.err` log, with error bars the pairwise matrix never printed, plus the paired
   cross-format difference and win-turn histograms. It runs no games.
