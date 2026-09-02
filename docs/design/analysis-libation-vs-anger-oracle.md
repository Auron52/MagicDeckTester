# Why a singleton Luxurious Libation falls short of Ancestral Anger and Oracle's Restoration

**Question (user, 2026-09-02):** adding one Luxurious Libation over one Ancestral Anger, or over one
Oracle's Restoration, measures worse. Why?

**Answer in one line:** Mirrorwing's combo turn is **card-limited, not body-limited**. Anger and
Oracle's are cantrips whose draw is *copied by the magnet* — one cast with N other creatures draws
**N+1** cards and refuels the chain. Libation instead makes **N+1 1/1 Citizens**, which the deck does
not need, and costs `{X}{G}` for a pump the deck usually cannot afford (**39–45% of casts pay X=0**,
i.e. a +0/+0 that buys only the body). The arm ends its combo turn with *more creatures* and *two
fewer cards*, and the chain stops early.

Both screens are fully accounted for: the per-game turn deltas below sum to exactly the measured
aggregate.

| swap | delta (20,000 paired games) | se | t | games identical | net turns attributed |
|---|---|---|---|---|---|
| Libation over the 4th **Ancestral Anger** | **+0.0257** | 0.0021 | +12.5 | 95.0% | +515 / 20000 = +0.0257 |
| Libation over the 3rd **Oracle's Restoration** | **+0.0291** | 0.0021 | +14.2 | 94.8% | +582 / 20000 = +0.0291 |

Positive = slower = worse. Cutting Oracle's costs about 13% more than cutting Anger.

---

## 1. How the games were obtained

Run under the **bucket-substitution route** (`.claude/skills/deck-screening.md`, "THE APPROVED ROUTE
for a card the shipped table does not bucket"), because the user's constraint was explicit: *"We
don't need to regenerate any mulligan profiles… we just do the best we can with the existing
profile, substituting our card(s) into an existing slot(s). That is currently the only approved
route."* Libation's name is aliased into the bucket of the card it replaces, so the shipped
155,978-cell keep table covers every arm, K stays at 16, and nothing is generated.

* the two screens were measured on engine commit **`664efac3`** (recorded in each
  `.results.json`). The 60,000-game logging run behind §2 and §2a was made *after* the branch pulled
  opponent-deck work (`45d4e6a0`, touches `src/`) and a rebuild — and it reproduces **every one of
  the 40,000 win turns** the screens recorded (`mw_libation_setups.py` checks this and prints
  `MATCH`, it is not assumed). So that engine change does not touch this deck's play and the two
  halves of the analysis are directly comparable
* apparatus: shipped keep table (K=16, R=40), pooled card scores, deck's value sidecar, d5 / 20 ms
* seeds 2,600,000 + 0…19,999; the arm's Libation **inherits the vacated library slot number**, so
  both arms deal the identical library order and exactly one physical card differs, in place
* divergent games: **1,008** (Anger) and **1,039** (Oracle's) — about 5% of games
* every divergent game was re-run in **both** arms with `--log-dir`, giving **4,094 paired JSON game
  logs**, and each re-run was checked against the screen's own recorded win turn before use

The alias held: after normalising the swapped name, **not one game** in either screen had a
different mulligan/bottom decision. The keep policy is genuinely identical across arms, which is
exactly what the route promises and what makes the in-play comparison clean.

Reproducing a batch game as a single run needs **both** `--seed base+gi` **and** `--game-index gi`.
Omitting the latter reproduced 89% of games and silently mis-played 11%, deterministically — it
looks like a clean repro and repeats identically. A `--games 600` single-process run matching the
batch 600/600 is what localised it. See `scripts/mw_libation_logs.sh`.

## 2. Every game, filed under its mechanism

Over **all 20,000 games per arm**, not just the divergent ones — the ~95% that came out identical
are the games where the swap did nothing and they belong in every denominator below.
`scripts/mw_libation_setups.py <anger|oracle>`.

| class | what it means | Anger: games / net turns | Oracle's: games / net turns |
|---|---|---|---|
| **SEARCH_ONLY** | the swapped library slot never reached hand | 15,771 (78.9%) / **+5** | 15,555 (77.8%) / **+2** |
| **BOTH_CAST** | each arm cast its own card — the difference is what the card *did* | 2,704 (13.5%) / **+422 (82%)** | 2,939 (14.7%) / **+523 (90%)** |
| **NEITHER_CAST** | held, cast by neither | 883 (4.4%) / +2 | 936 (4.7%) / +1 |
| **REPLACED_ONLY** | base cast its card; the arm never cast Libation | 465 (2.3%) / +66 (13%) | 476 (2.4%) / +32 (5%) |
| **LIBATION_ONLY** | only Libation was cast | 177 (0.9%) / +20 | 94 (0.5%) / +24 |
| **TOTAL** | | 20,000 / **+515** | 20,000 / **+582** |

**The swap is inert in four games out of five and costs ~0.16 turns in the one where the card is
actually cast.** `SEARCH_ONLY` is worth **+5 turns out of 515** — the lookahead seeing a different
library composition is not a mechanism here, it is noise.

> **Correction.** An earlier version of this table ran on the ~5% divergent games only and decided
> "did this card reach hand" from the log's `DRAW` actions. Those record the **draw step only** — a
> card put in hand by a cantrip rider leaves no `DRAW` action, and in this deck the magnet copies
> the cantrip, so most cards arrive that way. That undercounted "seen" and inflated `SEARCH_ONLY`
> to 84 games / 10% of the loss, when the true figure is 2 games / 1%. The `boardAfter` hand
> snapshot is authoritative and is what the numbers above use. The mechanism conclusions in §3 are
> unaffected — they were computed on `BOTH_CAST` games, whose membership does not depend on that
> test — and `BOTH_CAST`'s share of the loss rises from 75% to 82%.

The swap is not a coin-flip: among divergent games the arm loses ~3.4 for every 1 it wins.

## 2a. Which setups — the answer to "is it when we have no other pump spells?"

**Yes, and it is nearly the whole story.** Crossing the two axes that separate the strata — is there
anything in play to **copy** the trick (a magnet, or Frontline Heroism), and is there **another pump
available that turn** — partitions all 20,000 games into six groups that sum to the measured
aggregate:

| setup (arm-read) | Anger: games | mean Δ | net | Oracle's: games | mean Δ | net |
|---|---|---|---|---|---|---|
| 1. never drew it | 15,771 (78.9%) | +0.0003 | +5 | 15,555 (77.8%) | +0.0001 | +2 |
| 2. held it, never cast it | 1,348 (6.7%) | +0.0504 | +68 | 1,412 (7.1%) | +0.0234 | +33 |
| **3. cast with NO copier, no other pump** | **869 (4.3%)** | **+0.3280** | **+285** | **914 (4.6%)** | **+0.2681** | **+245** |
| 4. cast with NO copier, other pumps up | 344 (1.7%) | +0.0523 | +18 | 393 (2.0%) | +0.2214 | +87 |
| 5. cast with a copier, no other pump | 959 (4.8%) | +0.1564 | +150 | 1,032 (5.2%) | +0.1831 | +189 |
| **6. cast with a copier, other pumps up** | **709 (3.5%)** | **−0.0155** | **−11** | 694 (3.5%) | +0.0375 | +26 |

("no other pump" = at most one other trick available that turn.)

**Group 3 is 4.3% of games and 55% of the entire loss** (42% on the Oracle's swap). A Libation cast
with nothing to copy it and nothing else to chain into costs a **third of a turn**. Group 6 — a
copier out and other pumps in hand — is the one setup where Libation is **not worse than the 4th
Ancestral Anger** (−0.0155), and is only mildly worse than the 3rd Oracle's.

The same gradient shows up in the strictly **exogenous** cut, where the stratum label is a property
of the deal and is identical in both arms by construction (the kept opening hand, swapped slot
excluded), so it cannot be an artefact of how either arm played:

| other pumps in the kept opening hand | Anger mean Δ | Oracle's mean Δ |
|---|---|---|
| swapped slot in hand, **0** other pumps | **+0.2494** (n=389) | **+0.3039** (n=408) |
| swapped slot in hand, 1 other pump | +0.1975 (n=866) | +0.1526 (n=832) |
| swapped slot in hand, 2 other pumps | +0.0793 (n=429) | +0.1834 (n=469) |
| swapped slot in hand, 3 other pumps | +0.0638 (n=47) | +0.0405 (n=74) |
| not in the opening hand | +0.0115 (n=18,269) | +0.0133 (n=18,217) |

The gradient is clean and monotone on the Anger swap; on the Oracle's swap the 1-vs-2 rows invert
(+0.1526 vs +0.1834) while the endpoints hold. The **0-other-pumps and not-in-hand rows are the load
bearing ones**, and they are unambiguous in both.

The other exogenous cuts are consistent but **much weaker, and only the magnet cut holds on both
swaps**:

| exogenous stratum | Anger mean Δ | Oracle's mean Δ |
|---|---|---|
| 0 magnets in the opening hand | +0.0328 | +0.0428 |
| 1 / 2 magnets in the opening hand | +0.0207 / +0.0161 | +0.0179 / +0.0208 |
| 1 land in the opening hand | +0.0403 | +0.0351 |
| 4 lands in the opening hand | +0.0147 | +0.0239 |
| kept on 7 (mull 0) | +0.0183 | +0.0267 |
| mull 3 / mull 4 | +0.0326 / +0.0775 | +0.0299 / +0.0704 |

The magnet cut is the one to trust: **no magnet in the opener is ~1.6× worse for Libation** on the
Anger swap and ~2.4× on the Oracle's swap, which is the same story as group 3 above — with nothing
to copy the trick, a cantrip that digs toward a magnet beats a token that does not. The land and
mulligan cuts lean the same way on the Anger swap and are essentially flat on the Oracle's swap
(mull 0→2 even rises there), so they should not be quoted as evidence on their own.

## 3. The mechanism, from the logs

### 3a. The cantrip is copied. The token is too — and the deck wants the cards.

Verified in `src/core/SpellEffects.h`'s `ApplyTrickPayload`, which runs **per resolution instance**
(the original plus every magnet copy):

* `Ancestral Anger` — `cast_draw: 1` per instance → with N other creatures, **draws N+1 cards**
* `Oracle's Restoration` — `cast_draw: 1` **and** `cast_lifegain: 1` per instance → **N+1 cards and
  N+1 life**, and the life feeds `Fortifying Draught`'s `pump_per_life_gained`
* `Luxurious Libation` — no draw, no life; `trick_token_*` → **N+1 1/1 Citizens**

Measured on the combo turn (BOTH_CAST, games the arm loses; the games it wins are reported alongside
in `logs/mw_libation/*_report.txt` and mirror this):

| on the turn the swapped card was cast | Anger base → arm | Oracle's base → arm |
|---|---|---|
| spells cast that turn | 2.80 → **1.90** | 2.88 → **1.94** |
| spells cast *after* the swapped card | 1.41 → **0.58** | 1.44 → **0.61** |
| creatures at end of that turn | 4.39 → **5.11** | 4.56 → **5.06** |

**Libation delivers exactly what it promises and it does not help.** The arm ends the turn with
*more* creatures (+0.7, +0.5) and casts *half* as many further spells. Bodies are not the constraint.

The card ledger says the same thing even more plainly. On the last turn both games reached:

| | Anger base → arm | Oracle's base → arm |
|---|---|---|
| spells cast that turn | 2.98 → 1.36 | 2.98 → 1.37 |
| **cards left in hand afterwards** | 4.74 → **2.48** | 4.68 → **2.53** |
| damage dealt that turn | 24.0 → 4.6 | 25.5 → 4.7 |

Base casts ~1.6 **more** spells and still finishes holding ~2.2 **more** cards. That is the cantrip
fan-out refuelling the chain, and it compounds: one fewer cantrip → fewer cards → fewer spells →
fewer cantrips.

### 3b. `{X}{G}` is the wrong cost on the turn that matters

Distribution of the X actually paid, over every BOTH_CAST game:

| X paid | 0 | 1 | 2 | 3 | 4 | 5+ |
|---|---|---|---|---|---|---|
| Anger screen (n=831) | **326 (39%)** | 178 | 111 | 99 | 82 | 35 |
| Oracle's screen (n=901) | **394 (44%)** | 186 | 93 | 99 | 75 | 54 |

Mean X ≈ 1.4. **Two in five casts pay X=0** — a `{G}` spell that pumps +0/+0 and makes one 1/1 per
creature. Even at the median X=1, the rate is `{1}{G}` for +1/+1 per creature, against `{R}` for
Anger's +X/+0 *plus a card* per creature: half the damage per mana **and** no refuel. And Libation's
X is fixed across the whole fan-out (CR 707.10, modelled), so it cannot escalate copy-by-copy the way
the drawn-count, Treasure-count and lifegain counters all do.

Libation is also cast *early and small* rather than as part of the kill: only **142/621** (Anger) and
**156/694** (Oracle's) of the losing games cast it on the arm's own kill turn; the modal cast turn is
**2**, where X can be at most 1.

### 3c. Cutting Oracle's costs more, and the life curve says why

Oracle's is the bigger loss (+0.0291 vs +0.0257) because its rider feeds a second payoff. Base's
life total after the shared last turn, BOTH_CAST games the arm loses:

* Oracle's screen — base **25.8** vs arm **21.1** (**−4.7 life**)
* Anger screen — base 24.2 vs arm 21.0 (−3.2)

`Fortifying Draught` pumps `+X/+X` where X is *the life gained this turn*, so those ~4.7 points are
Draught pump the arm never got. Consistently, base casts Draught on the same turn as Oracle's in
**39%** of these games (vs 32% for Anger). Cutting Oracle's costs a cantrip **and** a Draught
enabler; cutting Anger costs a cantrip and one point of graveyard escalation for the remaining
copies.

## 4. Where Libation does win (the 23% that go the other way)

232–239 games per screen go to Libation, and they are not noise — they are a real, smaller effect
running the other way. In those games the arm has **+2.2 creatures** and casts **+0.35 spells** on
the shared last turn, and the mean X paid is higher (1.66–1.77 vs 1.30–1.39). The pattern is a turn
with mana to spare and a thin board: Libation buys width the magnet then multiplies. It is simply
outnumbered ~3.4 : 1 by the turns where the chain wanted a card instead.

## 5. Limitations — what this measurement cannot say

* **The apparatus bias floor is unmeasured.** `--with-floor` brackets it by generating a table per
  arm, which is regeneration and therefore out of scope under the standing constraint. Both effects
  are large (t = +12.5 and +14.2) and both agree in sign and mechanism, but `t` alone is not a
  verdict in this harness — see `deck-screening.md`.
* **The alias holds the mulligan policy fixed by construction.** Libation is bucketed *as* the card
  it replaces, which is what isolates the in-play difference — and it means this route can never
  detect that Libation might want a *different* keep policy. If the real thesis for Libation were
  "it changes which hands you keep", this measurement would be blind to it.
* **Libation is modelled with one disclosed gap** — token colour (green and white) is not modelled.
  No card in this deck reads a permanent's colour, so it cannot matter here, but the comparison is
  formally between a fully-modelled card and a partly-modelled one.
* Both screens ran on **train seeds only** (2,600,000 block). A confirm on disjoint seeds was not
  run; with 20,000 paired games per screen and a single pre-specified arm each, there is no
  selection over arms to correct for.

## Artefacts

| what | where |
|---|---|
| screens (per-game `[win]` lines) | `logs/mw_libation/{anger,oracle}_screen.err` |
| divergence sets | `logs/mw_libation/{anger,oracle}_div.json` |
| 4,094 paired game logs | `logs/mw_libation/glogs/{anger,oracle}/{base,lib_over_*}/` |
| per-game filing | `logs/mw_libation/{anger,oracle}_games.txt` |
| full reports | `logs/mw_libation/{anger,oracle}_report.txt` |
| setup stratification (all 20,000) | `logs/mw_libation/{anger,oracle}_setups.txt` |
| all 60,000 game logs (3 arms × 20,000) | `logs/mw_libation/full/{base,anger,oracle}/` |
| log collector (divergent games, paired) | `scripts/mw_libation_logs.sh` |
| mechanism classifier (divergent sample) | `scripts/mw_libation_classify.py` |
| setup stratifier (all games) | `scripts/mw_libation_setups.py` |
| alias tool | `scripts/alias_card_into_bucket.py` |
