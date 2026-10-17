<style>
/* PRINT LAYOUT: one deck per page. Pagination is done with explicit page-break
   divs (they survive renderers that strip <style>); this block only handles fit
   -- tighter type and margins, and never splitting a table or orphaning a
   heading. Do NOT also add `h1 { page-break-before: always }`: combined with the
   divs it inserts a blank page between every section. */
@media print {
  @page { size: A4 portrait; margin: 13mm 13mm 11mm 13mm; }
  body { font-size: 9.5pt; line-height: 1.26; }
  h1 { font-size: 15pt; margin: 0 0 2mm 0; }
  h2 { font-size: 11pt; margin: 3.5mm 0 1.5mm 0; }
  table { border-collapse: collapse; font-size: 9pt; width: 100%; }
  th, td { padding: 1px 5px; }
  table, tr, td, th, pre { page-break-inside: avoid; }
  h1, h2, h3 { page-break-after: avoid; }
  ul { margin: 1.5mm 0; padding-left: 5mm; }
  li { margin: 0.6mm 0; }
}
</style>

# V1 vs the current list — Fungus, Kitty Equipment, White Knights

**Every current list wins faster than the list it replaced, in both formats, on every paired block.**
Depth 7, budget 200 ms, `max_turns` 8. **These are the three most heavily rebuilt decks** — Fungus and
Kitty Equipment each changed more than half their slots, and two of the three spans cover two successive
adoptions rather than one.

| deck | format | v1 | current | change | se | t | blocks | games/arm |
|---|---|---|---|---|---|---|---|---|
| **Fungus** | standard | 5.3604 | **4.5552** | **−0.8052t** | 0.0039 | −206.0 | 10/10 | 100,000 |
| | 2HG | 5.8469 | **4.8482** | **−0.9987t** | 0.0052 | −190.8 | 10/10 | 50,000 |
| **Kitty Equipment** | standard | 4.2519 | **3.8096** | **−0.4423t** | 0.0056 | −78.4 | 10/10 | 100,000 |
| | 2HG | 4.7337 | **4.1645** | **−0.5692t** | 0.0108 | −52.5 | 10/10 | 50,000 |
| **White Knights** | standard | 4.3727 | **4.1543** | **−0.2184t** | 0.0012 | −187.2 | 10/10 | 500,000 |
| | 2HG | 4.9599 | **4.6107** | **−0.3492t** | 0.0012 | −288.0 | 10/10 | 500,000 |

Average turn the goldfish wins on, scoring a non-win as 9; lower is better. Standard and 2HG are
co-equal, not headline-plus-footnote. **Fungus at 2HG is the largest gain measured on any deck: nearly
a full turn.**

## But the mean is not the headline — the win-turn distribution is

A goldfish win on turn 7 or 8 is a loss in a real game, and turn 5–6 is marginal against a fast
opponent. So the value of a win decays steeply with turn number, while the mean prices a T6→T5 shift
exactly like a T4→T3 one. **The share of games won by turn 4** is the number that tracks what a list is
actually worth:

| deck | standard ≤T4 | 2HG ≤T4 | standard ≥T6 | 2HG ≥T6 |
|---|---|---|---|---|
| **Fungus** | 3.58% → **49.38%** | 0.00% → **32.17%** | 31.08% → 3.95% | 60.93% → 14.19% |
| **Kitty Equipment** | 72.30% → **82.26%** | 43.47% → **76.56%** | 7.30% → 7.21% | 16.13% → 11.98% |
| **White Knights** | 65.55% → **85.61%** | 19.85% → **44.20%** | 2.70% → 0.91% | 13.40% → 4.43% |

`≥T6` counts non-wins, i.e. it is the share of games that are losses in practice. **Fungus v1 was
barely a functional deck at 2HG** — it won by turn 4 in **2 games out of 50,000**, and 60.93% of its
games were losses in practice. **This document is also where the mean is most misleading**: Kitty
Equipment's −0.4423t is the middling gain of the three, yet it is the deck whose play changed most, and
its distribution says so where its mean does not — its standard turn-3 kills go from 13.26% to 50.56%
of all games. See its page.

**All three decks gain MORE at 2HG**, and for two different kinds of reason. White Knights' larger 2HG
gain is a specific card that scales with the number of opponents — the only such card in either
document. Neither Fungus nor Kitty Equipment holds a per-opponent parameter at all, so their 2HG
differences come from the 30-life requirement alone: go-wide damage and free-equip damage both scale
into a larger life total better than the lists they replaced did.

## Method

* **1,800,000 games** — 100,000 standard and 50,000 2HG per arm for Fungus and Kitty Equipment, 500,000
  per arm per format for White Knights — pooled into one batch per format, never a loop per deck or per
  arm. Per-deck game counts go into a single manifest so the expensive decks could be trimmed without
  splitting the run into two batches and paying two load-imbalance tails.
* **d7 / b200 for every job**, deliberately deeper and far more generous than any suite case for these
  decks (d0/d3/d5 at 0/10/20 ms), so this compares the *lists* rather than which one copes with a tight
  budget. No deck here drives play from its value sidecar — Kitty v1 carries a `target_depth 5` but with
  `enabled: false`, so `drives()` is false — which makes the pinned depth simply deeper, not off-policy.
* **2HG = `starting_life 30` + `opponent_heads 2`** on the same deck and profile — the suite's canonical
  definition (`test/regression_cases.sh`, "2HG gate" block).
* **`max_turns 8` was validated for 2HG before spending the run.** 30 team life could have pushed games
  past the horizon and quietly turned the average into a non-win-rate measure; a 12,000-game probe
  returned win rates of 98.25%–100%, so the horizon keeps its resolution and the formats stay
  comparable.
* **Paired blocks.** 10 blocks per arm. Block *b* of both arms covers the same global games, with the
  same base seed **and** the same `game_index`, so game *i* gets shuffle seed `base+off+i` and spawn
  index `off+i` on **both** arms. Quoted `se` is over the 10 block deltas, which is what absorbs shuffle
  variance. Base seed 700000 standard, 800000 2HG.
* **Each arm plays with its own shipped apparatus** — profile, value sidecar and exhaustive keep table
  from its own directory. Correct for "did the deck get better"; a deliberate confound, and on this
  document's decks an unusually visible one, so it is named on the Fungus and Kitty pages as well as in
  the appendix.
* **`provider=` verified identical across arms** on all three decks in both formats (`Fungus`,
  `Equipment`, `WhiteKnights`). An edited list can cross an archetype signature and silently swap one arm's
  decision heuristics; nothing else catches it. Kitty's rebuild is exactly the scale of edit that could
  have done it — 15 cards in, 16 out — so the check mattered here.
* **Averages count non-wins at 9**, the `ref_bench` convention — verified by recomputing both forms from
  the per-game `.wins` files, not assumed from the source, which reads as if it averaged won games only.
* Engine `build/PGO` (PGO+LTO). **White Knights at src tree `55191e63`; Fungus and Kitty Equipment at
  `b6985a8a`** — the runs are a month apart and the branch moved between them. The engine difference is
  not corrected for: it applies identically to both arms of each deck, so each deck's paired delta is
  internally clean, and only a cross-deck comparison of absolute turn numbers carries it.
* **Cost is quoted as wall-clock ms per game, not units.** A `units` count is not comparable across
  search shapes — a rollout turn-step costs ~5× an interior plan-loop unit and both are charged one — so
  on a deck whose search shape changed, units and wall can disagree in *sign*. Kitty does exactly that.

<div style="page-break-after: always;"></div>

# Fungus — −0.8052t standard, −0.9987t 2HG, and 5.7–7.0× cheaper

Adopted 2026-10-16 after a 39-round screening campaign. Predecessor
`decks/Fungus/v1-sporesower-shelldweller/`. **The largest gain measured on any deck in either document,
and simultaneously the largest cost reduction.** A near-total rebuild: 10 cards in, 11 out.

## Current list — 60 cards, 24 lands

| n | card | n | card |
|---|---|---|---|
| 16 | Forest | 4 | Sporecrown Thallid |
| 4 | Ancient Tomb | 4 | Undercellar Myconid |
| 4 | Beastmaster Ascension | 3 | Doubling Season |
| 4 | Brightcap Badger | 3 | Wild Growth |
| 4 | Hickory Woodlot | 2 | Thallid |
| 4 | Saproling Burst | 1 | Concordant Crossroads |
| 4 | Saproling Migration | 1 | Shroofus Sproutsire |
| | | 1 | Sol Ring |
| | | 1 | Utopia Mycon |

## Cards added

| | card | v1 → now | |
|---|---|---|---|
| **+4** | Ancient Tomb | 0 → 4 | **new card** — a land that taps for two |
| **+4** | Brightcap Badger | 0 → 4 | **new card** — a free Saproling every end step |
| **+4** | Hickory Woodlot | 0 → 4 | **new card** — a land that taps for two |
| **+4** | Saproling Burst | 0 → 4 | **new card** — 7 fading counters, 1 mana per token |
| **+4** | Saproling Migration | 0 → 4 | **new card** — two tokens for {1}{G}, four kicked |
| **+4** | Undercellar Myconid | 0 → 4 | **new card** — a token on ETB *and* on death |
| **+1** | Concordant Crossroads | 0 → 1 | **new card** — haste for every creature |
| **+1** | Shroofus Sproutsire | 0 → 1 | **new card** |
| **+1** | Sol Ring | 0 → 1 | **new card** |
| **+1** | Wild Growth | 2 → 3 | increased |

## Cards removed

| | card | v1 → now | | | card | v1 → now | |
|---|---|---|---|---|---|---|---|
| **−4** | Sporesower Thallid | 4 → 0 | **cut** | **−3** | Utopia Mycon | 4 → 1 | reduced |
| **−4** | Thallid Shell-Dweller | 4 → 0 | **cut** | **−2** | Mycoloth | 2 → 0 | **cut** |
| **−4** | Tukatongue Thallid | 4 → 0 | **cut** | **−2** | Thallid | 4 → 2 | reduced |
| **−3** | Forest | 19 → 16 | reduced | **−1** | Doubling Season | 4 → 3 | reduced |
| **−3** | Simic Growth Chamber | 3 → 0 | **cut** | **−1** | Psychotrope Thallid | 1 → 0 | **cut** |
| | | | | **−1** | Essence Warden | 1 → 0 | **cut** |

v1 also carried a four-card sideboard (3 Essence Warden, 1 Spore Flower) that the current list drops
entirely. **The edit replaces an attrition deck with a go-wide deck.** v1's spore-counter Thallids
accumulate slowly; every added card makes tokens *immediately* — Saproling Migration on cast, Undercellar
Myconid on entry and again on death, Brightcap Badger unconditionally each end step, Saproling Burst on
demand for a mana. Those tokens feed the two 4-ofs both lists share: Sporecrown Thallid (+1/+1 to every
Fungus and Saproling) and Beastmaster Ascension, which needs **seven** attackers' worth of quest counters
before granting +5/+5 to the team. Concordant Crossroads grants haste to everything, so a token wave
attacks the turn it arrives — counting toward Ascension immediately. Ancient Tomb, Hickory Woodlot and
Sol Ring are there to land Doubling Season at {4}{G}{G} on schedule.

## Win-turn distribution — share of all games

| | | T2 | T3 | T4 | T5 | T6 | T7 | T8 | none | **≤T4** | ≥T6 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **std** | v1 | 0.00% | 0.00% | 3.58% | 65.34% | 24.41% | 5.13% | 1.21% | 0.33% | **3.58%** | 31.08% |
| | current | 0.00% | 0.09% | 49.29% | 46.67% | 3.20% | 0.53% | 0.14% | 0.08% | **49.38%** | 3.95% |
| | change | +0.00 | +0.09 | **+45.71** | −18.67 | −21.21 | −4.60 | −1.07 | −0.25 | **+45.80** | −27.13 |
| **2HG** | v1 | 0.00% | 0.00% | 0.00% | 39.07% | 42.87% | 13.52% | 3.38% | 1.16% | **0.00%** | 60.93% |
| | current | 0.00% | 0.05% | 32.12% | 53.64% | 11.99% | 1.69% | 0.35% | 0.15% | **32.17%** | 14.19% |
| | change | +0.00 | +0.05 | **+32.12** | +14.58 | −30.87 | −11.83 | −3.03 | −1.01 | **+32.17** | −46.74 |

**The ≤T4 column is the whole story**: 3.58% → 49.38% of standard games and 0.00% → 32.17% at 2HG. The
2HG v1 figure is not rounded away — it is literally **2 games out of 50,000** that won by turn 4. The
`≥T6` share, i.e. games that are losses in practice, drops **7.9× standard and 4.3× at 2HG**.

## How the averages changed

| format | | win% | non-wins | avg(won) | **avg(pen=9)** |
|---|---|---|---|---|---|
| **standard** | v1 | 99.6700 | 330 | 5.3483 | **5.3604** |
| | current | 99.9190 | 81 | 4.5516 | **4.5552** |
| | change | **+0.2490** | **−249** | −0.7967 | **−0.8052** |
| **2HG** | v1 | 98.8360 | 582 | 5.8098 | **5.8469** |
| | current | 99.8460 | 77 | 4.8418 | **4.8482** |
| | change | **+1.0100** | **−505** | −0.9680 | **−0.9987** |

* Block deltas **−0.8328 to −0.7902** standard, **−1.0290 to −0.9844** 2HG. **Every one of the ten 2HG
  blocks is worse than −0.98t**, so "nearly a full turn" is the flat effect, not an average over a
  lopsided few.
* Non-wins fall 4.1× standard and 7.6× at 2HG, and the penalised delta exceeds the won-only delta in
  both formats — the signature of "wins at all" and not only "wins sooner".
* **Cost collapses: 434.62 → 76.40 ms/game standard (5.7×) and 1564.44 → 223.09 at 2HG (7.0×)**, with
  units agreeing in direction (42,100 → 7,057 and 149,438 → 19,889). Part of this is cards and part is
  apparatus, and **this run does not separate them**: v1 ships a 136 KB learned value-leaf table, while
  the current list's own v2 generation concluded it was better off **leafless** (`value_play.leaf:
  "none"`, which substitutes a constant mid-game evaluator at the horizon and sets `value_trust_depth`
  to 0). That was the deck's own measured adoption decision, so it belongs inside "did the deck get
  better" — but it is not a cards-only comparison.

<div style="page-break-after: always;"></div>

# Kitty Equipment — −0.4423t standard, −0.5692t 2HG, and the deck the mean gets wrong

v3 promoted 2026-10-15. Predecessor `decks/KittyEquipment/v1-kemba-balan/`. **This span covers two
adoptions** — the deck also passed through `v2-puresteel-hammer` — so v1 → current is cumulative. 15
cards in, 16 out: **more than half the deck, and a different deck in kind.**

## Current list — 60 cards, 16 lands, 37 artifacts

| n | card | n | card |
|---|---|---|---|
| 8 | Plains | 4 | Puresteel Paladin |
| 4 | Accorder's Shield | 4 | Remote Farm |
| 4 | Ancient Den | 4 | Spidersilk Net |
| 4 | Bone Saw | 4 | Sram, Senior Edificer |
| 4 | Cathar's Shield | 2 | Shadowspear |
| 4 | Colossus Hammer | 1 | Adelbert Steiner |
| 4 | Golem-Skin Gauntlets | 1 | Deconstruction Hammer |
| 4 | Kite Shield | 1 | Dragonfire Blade |
| | | 1 | Dwalin, Weaponmaster |
| | | 1 | Sigarda's Aid |
| | | 1 | Skateboard |

**37 artifacts against v1's 17**, and four of the 16 lands (Ancient Den) are themselves artifacts.

## Cards added

| | card | v1 → now | | | card | v1 → now | |
|---|---|---|---|---|---|---|---|
| **+4** | Accorder's Shield | 0 → 4 | **new** | **+4** | Spidersilk Net | 0 → 4 | **new** |
| **+4** | Ancient Den | 0 → 4 | **new** | **+4** | Sram, Senior Edificer | 0 → 4 | **new** |
| **+4** | Bone Saw | 0 → 4 | **new** | **+1** | Adelbert Steiner | 0 → 1 | **new** |
| **+4** | Cathar's Shield | 0 → 4 | **new** | **+1** | Deconstruction Hammer | 0 → 1 | **new** |
| **+4** | Golem-Skin Gauntlets | 0 → 4 | **new** | **+1** | Dragonfire Blade | 0 → 1 | **new** |
| **+4** | Kite Shield | 0 → 4 | **new** | **+1** | Dwalin, Weaponmaster | 0 → 1 | **new** |
| **+4** | Remote Farm | 0 → 4 | **new** | **+1** | Sigarda's Aid | 0 → 1 | **new** |
| | | | | **+1** | Skateboard | 0 → 1 | **new** |

**Every single addition is a card v1 did not play at all** — there is not one re-count among the fifteen.

## Cards removed

| | card | v1 → now | | | card | v1 → now | |
|---|---|---|---|---|---|---|---|
| **−14** | Plains | 22 → 8 | reduced | **−2** | Unexpectedly Absent | 2 → 0 | **cut** |
| **−4** | Armored Skyhunter | 4 → 0 | **cut** | **−1** | Boros Garrison | 1 → 0 | **cut** |
| **−4** | Bonesplitter | 4 → 0 | **cut** | **−1** | Grafted Wargear | 1 → 0 | **cut** |
| **−4** | Kor Duelist | 4 → 0 | **cut** | **−1** | Lightning Greaves | 1 → 0 | **cut** |
| **−2** | Balan, Wandering Knight | 2 → 0 | **cut** | **−1** | Loxodon Warhammer | 1 → 0 | **cut** |
| **−2** | Kemba, Kha Regent | 2 → 0 | **cut** | **−1** | O-Naginata | 1 → 0 | **cut** |
| **−2** | Umezawa's Jitte | 2 → 0 | **cut** | **−1** | Sol Ring | 1 → 0 | **cut** |
| | | | | **−1** | Stoneforge Mystic | 1 → 0 | **cut** |
| | | | | **−1** | Swords to Plowshares | 1 → 0 | **cut** |

**Six of those "cuts" are demotions, not deletions.** The current list carries a 23-card sideboard, and
Bonesplitter, Umezawa's Jitte, O-Naginata, Sol Ring, Armored Skyhunter and Kor Duelist all appear in it
as singletons — so a main-only diff overstates what left the deck.

**The mechanism is metalcraft.** Both lists keep 4 Puresteel Paladin and 4 Colossus Hammer; what changed
is whether that pairing works. Puresteel Paladin makes equip cost **{0} while you control three or more
artifacts** (`metalcraft_equip_zero_artifacts: 3`), and Colossus Hammer's own equip cost is **{8}** for
+10/+10. Going from 17 to 37 artifacts is what turns a prohibitive equip into a free one, and the nine
new cheap artifacts do it while also drawing cards — Puresteel draws on each equipment entering, Sram
draws on each equipment cast. Golem-Skin Gauntlets then scales with the equipment count itself
(`equip_scale_power_per_equipment: 1`). Plains could fall 22 → 8 because the curve tops out around two
mana and Ancient Den counts for metalcraft while still being a land. v1, by contrast, was a
Kemba/Balan voltron deck that wanted few, large equipment.

## Win-turn distribution — share of all games

| | | T2 | T3 | T4 | T5 | T6 | T7 | T8 | none | **≤T4** | ≥T6 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **std** | v1 | 0.04% | 13.26% | 59.00% | 20.40% | 5.02% | 1.39% | 0.52% | 0.37% | **72.30%** | 7.30% |
| | current | 0.00% | **50.56%** | 31.70% | 10.53% | 3.82% | 1.44% | 0.70% | 1.25% | **82.26%** | 7.21% |
| | change | −0.04 | **+37.30** | −27.29 | −9.87 | −1.20 | +0.06 | +0.18 | +0.88 | **+9.96** | −0.09 |
| **2HG** | v1 | 0.00% | 6.51% | 36.96% | 40.40% | 11.31% | 3.09% | 1.06% | 0.67% | **43.47%** | 16.13% |
| | current | 0.00% | **29.33%** | 47.23% | 11.46% | 6.20% | 2.87% | 1.26% | 1.66% | **76.56%** | 11.98% |
| | change | +0.00 | **+22.83** | +10.27 | −28.95 | −5.11 | −0.23 | +0.20 | +0.99 | **+33.09** | −4.15 |

**This is the deck the mean gets wrong.** −0.4423t is the smallest gain in this document, but the
distribution says something a mean cannot: **turn-3 kills go from 13.26% to 50.56% of all games** — from
one game in eight to one in two — while the `≥T6` tail is flat (7.30% → 7.21%). The whole improvement
lands in the region where a win is actually worth something, and the mean prices it as if a T6→T5 shift
were the same thing.

## How the averages changed

| format | | win% | non-wins | avg(won) | **avg(pen=9)** |
|---|---|---|---|---|---|
| **standard** | v1 | 99.6320 | 368 | 4.2344 | **4.2519** |
| | current | 98.7540 | 1,246 | 3.7441 | **3.8096** |
| | change | **−0.8780** | **+878** | −0.4903 | **−0.4423** |
| **2HG** | v1 | 99.3320 | 334 | 4.7050 | **4.7337** |
| | current | 98.3400 | 830 | 4.0829 | **4.1645** |
| | change | **−0.9920** | **+496** | −0.6222 | **−0.5692** |

* Block deltas **−0.4858 to −0.4192** standard, **−0.6136 to −0.5198** 2HG, 10/10 blocks better in both.
  The widest spreads in either document (~0.067t and ~0.094t), consistent with the largest change in
  deck identity — and the only cells here whose `t` is in the tens rather than the hundreds.
* **The current list fails to close by turn 8 more often** — 1,246 games against 368 at standard, 830
  against 334 at 2HG — and this is the one deck here where the won-only delta is *larger* than the
  penalised one in both formats (−0.4903 vs −0.4423, −0.6222 vs −0.5692), because scoring a non-win as
  9 drags the faster list's mean up. Both are reported; `avg(won)` is the better read of how fast the
  wins are, `avg(pen=9)` the conservative one. **Every cell of the distribution that grew sits at turn 7
  or later**, in both formats — which is where a goldfish win would not have survived a real opponent
  anyway.
* **Cost moves in opposite directions in the two formats.** At standard it costs **more** wall-clock and
  **fewer** units — 418.22 → 566.70 ms/game (**+36%**) while units *fell* 26,944 → 23,610 (**−12%**). At
  2HG both fall: 1850.82 → 1537.17 ms/game (**−17%**) and 120,199 → 57,093 units (**−53%**). The
  standard sign disagreement is why wall is quoted throughout: the search shape changed, and a unit is
  not a fixed amount of work — a rollout turn-step costs about five times an interior plan-loop unit and
  both are charged one, so a list that does less rollout and more interior planning can spend more time
  on fewer units. Size tier rows from wall. **Dearest deck in either document**, by an order of
  magnitude at 2HG.

<div style="page-break-after: always;"></div>

# White Knights — −0.2184t standard, −0.3492t 2HG, and much cheaper

Predecessor `decks/WhiteKnights/v1-contender-vial/`. **This span covers two adoptions** — the deck also
passed through `v2-kinsbaile-cavalier` — so v1 → current is cumulative. The smallest gain in this
document, from by far the strongest starting point: **v1 here was already faster than the *current*
Fungus list.**

## Current list — 60 cards, 23 lands

| n | card | n | card |
|---|---|---|---|
| 19 | Plains | 2 | Hero of Bladehold |
| 4 | Accorder Paladin | 1 | Aether Vial |
| 4 | Benalish Marshal | 1 | Basri, Tomorrow's Champion |
| 4 | Dauntless Bodyguard | 1 | Kinsbaile Cavalier |
| 4 | Knight Exemplar | 1 | Sol Ring |
| 4 | Remote Farm | 1 | Swords to Plowshares |
| 4 | Worthy Knight | 1 | Unexpectedly Absent |
| 3 | Adeline, Resplendent Cathar | | |
| 3 | Silverblade Paladin | | |
| 3 | Venerable Knight | | |

## Cards added

| | card | v1 → now | |
|---|---|---|---|
| **+4** | Remote Farm | 0 → 4 | **new card** |
| **+2** | Adeline, Resplendent Cathar | 1 → 3 | **the 2HG story, below** |
| **+2** | Benalish Marshal | 2 → 4 | up to a 4-of |
| **+2** | Silverblade Paladin | 1 → 3 | increased |
| **+1** | Basri, Tomorrow's Champion | 0 → 1 | **new card** |
| **+1** | Kinsbaile Cavalier | 0 → 1 | **new card** (v2's namesake) |

## Cards removed

| | card | v1 → now | |
|---|---|---|---|
| **−3** | Acclaimed Contender | 3 → 0 | **cut** |
| **−3** | Plains | 22 → 19 | reduced |
| **−2** | Aether Vial | 3 → 1 | down to a 1-of |
| **−1** | Hero of Bladehold | 3 → 2 | reduced |
| **−1** | Lightning Greaves | 1 → 0 | **cut** |
| **−1** | Valiant Knight | 1 → 0 | **cut** |
| **−1** | Venerable Knight | 4 → 3 | reduced |

## Win-turn distribution — share of all games

| | | T2 | T3 | T4 | T5 | T6 | T7 | T8 | none | **≤T4** | ≥T6 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **std** | v1 | 0.00% | 0.33% | 65.22% | 31.75% | 2.32% | 0.31% | 0.05% | 0.02% | **65.55%** | 2.70% |
| | current | 0.00% | 0.00% | 85.61% | 13.48% | 0.79% | 0.10% | 0.02% | 0.01% | **85.61%** | 0.91% |
| | change | +0.00 | −0.33 | **+20.39** | −18.27 | −1.54 | −0.22 | −0.03 | −0.01 | **+20.06** | −1.80 |
| **2HG** | v1 | 0.00% | 0.04% | 19.81% | 66.75% | 11.34% | 1.70% | 0.28% | 0.08% | **19.85%** | 13.40% |
| | current | 0.00% | 0.00% | 44.20% | 51.38% | 3.73% | 0.58% | 0.09% | 0.03% | **44.20%** | 4.43% |
| | change | +0.00 | −0.04 | **+24.39** | −15.38 | −7.61 | −1.12 | −0.19 | −0.05 | **+24.35** | −8.97 |

The current list wins by turn 4 in **six games in seven** at standard — the highest `≤T4` share anywhere
in either document — and its `≥T6` share of 0.91% is the lowest. Note the small negative T3 entries: both
formats trade away a fraction of a percent of turn-3 wins to make turn 4 near-certain, which is the right
trade when T5 is the alternative.

## How the averages changed

| format | | win% | non-wins | avg(won) | **avg(pen=9)** |
|---|---|---|---|---|---|
| **standard** | v1 | 99.9848 | 76 | 4.3720 | **4.3727** |
| | current | 99.9950 | 25 | 4.1541 | **4.1543** |
| | change | **+0.0102** | **−51** | −0.2180 | **−0.2184** |
| **2HG** | v1 | 99.9248 | 376 | 4.9568 | **4.9599** |
| | current | 99.9742 | 129 | 4.6095 | **4.6107** |
| | change | **+0.0494** | **−247** | −0.3473 | **−0.3492** |

* Block deltas **−0.2116 to −0.2234** standard, **−0.3431 to −0.3563** 2HG, over 500,000 games per arm.
* **The 60%-larger 2HG gain is Adeline, Resplendent Cathar** — *"whenever you attack, for each opponent,
  create a 1/1 Human"*, i.e. `attack_creates_tokens: 1` with **no** `attack_tokens_per_opponent` key, so
  it takes the database default (**true**) and makes **two** tokens per attack at `opponent_heads 2`.
  Current plays **3 against v1's 1**. (The suite's 2HG gate names it: *"knights (Adeline token per head)"*.)
* **The card traded against her does NOT scale:** Hero of Bladehold (3 → 2) sets
  `attack_tokens_per_opponent: False`. So a **head-neutral** token maker was swapped for **head-scaling**
  ones — exactly the shape that shows up as a larger 2HG gain. Both read alike from their oracle text;
  only `cards.json` distinguishes them. **This is the only per-opponent scaling card in either
  document**, which is why it is the only deck whose format gap has a named cause rather than an
  observation.
* **Much cheaper too** — 5.15 ms/game vs 7.69 standard (**−33%**), **14.04 vs 28.99 at 2HG, less than
  half**. Cutting 3 Acclaimed Contender and 2 Aether Vial removed real branching (each Vial charge level
  is a decision), which pays more in longer games. **The cheapest deck in either document by a wide
  margin** — a hundredth of Kitty Equipment per game.

<div style="page-break-after: always;"></div>

# Appendix — reproducing this

The run apparatus lived under `logs/v1_vs_current/` (gitignored), so the manifest generator is here in
full. It *is* the experiment definition, and the pairing depends entirely on `seed`/`game_index`:

```python
import json, sys
DECKS = {
 "fungus":       ("decks/Fungus",          "Fungus",          "v1-sporesower-shelldweller"),
 "kitty":        ("decks/KittyEquipment",  "KittyEquipment",  "v1-kemba-balan"),
 "whiteknights": ("decks/WhiteKnights",    "WhiteKnights",    "v1-contender-vial"),
}
BASES = {"std": 700000, "2hg": 800000}       # one base per format

def build(fmt, blocks, per, slugs):
    jobs = []
    for slug in slugs:
        root, stem, v1 = DECKS[slug]
        for arm, d in (("cur", root), ("v1", f"{root}/{v1}")):
            for b in range(blocks):
                off = b * per
                jobs.append({"name": f"{slug}-{arm}_b{b}",
                             "deck": f"{d}/{stem}.cod", "profile": f"{d}/{stem}.profile.json",
                             "games": per, "seed": BASES[fmt] + off, "game_index": off,
                             "depth": 7, "budget_ms": 200,
                             **({"starting_life": 30, "opponent_heads": 2} if fmt == "2hg" else {})})
    return jobs

# Per-deck sizes in ONE pooled manifest (slug:blocks:per), so an expensive deck can be trimmed
# without splitting the run into two batches -- two batches means two load-imbalance tails.
#   2hg:  fungus:10:5000  kitty:10:5000  whiteknights:10:50000
```

A chunk covering global games `[off, off+n)` sets `seed = BASE + off` **and** `game_index = off` — the
contract in `BatchRunner.cpp`'s `Job::game_index` comment, and what makes game *i* of block *b* the same
shuffle on both arms. Set one and not the other and the arms silently stop being paired: the run still
completes and the numbers still look plausible.

```
build/PGO/mtg --batch run.manifest.json --game-log-dir build/scratch/<name>
```

Job lines give `played=/avg=/digest=/ms=/units=` per block, enough for the paired block deltas. Win rates
and distributions need the `.wins` files: `<game_index> <win_turn> <digest>`, with `win_turn <= 0`
meaning no win inside `max_turns`. Per-game logs go under `build/scratch/` (the container build volume)
rather than `logs/`, because millions of per-game records are regenerable churn and `logs/` is on the 9p
host mount.

**A pooled batch is resumable at job granularity**, which this run needed: a power cut killed the 2HG
batch partway through. Because the seed and `game_index` live in the manifest rather than in run order,
the surviving jobs were validated (`.wins` line count == reported `played` == expected, zero truncated
orphans) and only the missing jobs were re-queued.

## Two caveats worth stating plainly

**Each arm carries its own keep table and value sidecar**, so these numbers measure the shipped package,
not the decklist in isolation. A list change and the artifacts refitted to it are deliberately
confounded. That is the correct answer to "did the deck get better", and the wrong answer to "how much of
the gain came from the cards alone" — the second needs both lists against one shared apparatus, a
different and cheaper experiment. On this document's decks the confound is unusually visible: Fungus v1
ships a learned value-leaf table and the current list deliberately ships none, which moves cost as well
as play. The gains are nonetheless far too large for apparatus bias to explain them — the smallest,
White Knights at −0.2184t standard, is ~180× its own standard error and 10/10 blocks consistent.

**Capability scans read the mainboard and the sideboard together**, so a sideboard is never fully inert
even when nothing in the deck can fetch from it. It matters here: Kitty Equipment's current list carries
23 sideboard cards against v1's none, and Fungus v1 carried four against the current list's none. No card
in any of these lists can wish from the sideboard — only Living Wish, Legion Angel, Glittering Wish and
Reborn Hope carry wish parameters in the database, and none appears here — so the sideboards never enter
play. They are still read when the engine decides what a deck is *capable* of, which is why the card
diffs above check both zones.

*Process codified as `.claude/skills/deck-update-summary.md`.*
