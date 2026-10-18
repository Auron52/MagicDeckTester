# V1 vs the current list — Fungus, Kitty Equipment, White Knights

**Every current list wins faster than the list it replaced, in both formats, on all 10 paired blocks.**
These are the three most heavily rebuilt decks.

| deck | format | v1 | current | change | blocks better |
|---|---|---|---|---|---|
| **Fungus** | standard | 5.3604 | **4.5552** | **−0.8052t** | 10/10 |
| | 2HG | 5.8469 | **4.8482** | **−0.9987t** | 10/10 |
| **Kitty Equipment** | standard | 4.2519 | **3.8096** | **−0.4423t** | 10/10 |
| | 2HG | 4.7337 | **4.1645** | **−0.5692t** | 10/10 |
| **White Knights** | standard | 4.3727 | **4.1543** | **−0.2184t** | 10/10 |
| | 2HG | 4.9599 | **4.6107** | **−0.3492t** | 10/10 |

Average turn the goldfish wins on (a non-win counts as turn 9); lower is better.

## The share of games won by turn 4 is the number that matters

| deck | standard ≤T4 | 2HG ≤T4 | standard ≥T6 | 2HG ≥T6 |
|---|---|---|---|---|
| **Fungus** | 3.58% → **49.38%** | 0.00% → **32.17%** | 31.08% → 3.95% | 60.93% → 14.19% |
| **Kitty Equipment** | 72.30% → **82.26%** | 43.47% → **76.56%** | 7.30% → 7.21% | 16.13% → 11.98% |
| **White Knights** | 65.55% → **85.61%** | 19.85% → **44.20%** | 2.70% → 0.91% | 13.40% → 4.43% |

A goldfish win on turn 6 or later (`≥T6`, including non-wins) is too slow against a fast deck.

* **Fungus at 2HG is the largest gain measured on any deck — nearly a full turn.** v1 won by turn 4 in
  2 games out of 50,000 there; the current list does it in a third of games, at a seventh of the cost.
* **Kitty Equipment is where the mean misleads**: its −0.44t looks middling, but turn-3 kills go from
  13% to 51% of standard games.
* **All three gain more at 2HG.** For White Knights that is one card, Adeline, which makes a token per
  opponent.

**Method.** 1.8M games (100k standard / 50k 2HG per arm for Fungus and Kitty, 500k per arm for White
Knights), depth 7, budget 200 ms, `max_turns` 8. 2HG = 30 life, 2 opponent heads. 10 paired blocks per
arm: both lists see the same shuffles. Each list plays with its own shipped profile and keep table.

<div style="page-break-after: always;"></div>

# Fungus — −0.8052t standard, −0.9987t 2HG, 5.7–7.0× cheaper

**The largest gain on any deck, and the largest cost reduction.** A near-total rebuild (10 cards in, 11
out) that replaces a slow attrition deck with a go-wide token deck.

## Shared core — 32 of 60 cards in both lists

| n | card | n | card | n | card |
|---|---|---|---|---|---|
| 16 | Forest | 3 | Doubling Season | 1 | Utopia Mycon |
| 4 | Beastmaster Ascension | 2 | Thallid | | |
| 4 | Sporecrown Thallid | 2 | Wild Growth | | |

## Cards added

| | card | v1 → now |
|---|---|---|
| **+4** | Ancient Tomb | new |
| **+4** | Brightcap Badger | new |
| **+4** | Hickory Woodlot | new |
| **+4** | Saproling Burst | new |
| **+4** | Saproling Migration | new |
| **+4** | Undercellar Myconid | new |
| **+1** | Concordant Crossroads | new |
| **+1** | Shroofus Sproutsire | new |
| **+1** | Sol Ring | new |
| **+1** | Wild Growth | 2 → 3 |

## Cards removed

| | card | v1 → now |
|---|---|---|
| **−4** | Sporesower Thallid | 4 → 0 |
| **−4** | Thallid Shell-Dweller | 4 → 0 |
| **−4** | Tukatongue Thallid | 4 → 0 |
| **−3** | Forest | 19 → 16 |
| **−3** | Simic Growth Chamber | 3 → 0 |
| **−3** | Utopia Mycon | 4 → 1 |
| **−2** | Mycoloth | 2 → 0 |
| **−2** | Thallid | 4 → 2 |
| **−1** | Doubling Season | 4 → 3 |
| **−1** | Psychotrope Thallid | 1 → 0 |
| **−1** | Essence Warden | 1 → 0 |

## Win-turn distribution — share of games

| | | T3 | T4 | T5 | ≥T6 | **≤T4** |
|---|---|---|---|---|---|---|
| **standard** | v1 | 0.00% | 3.58% | 65.34% | 31.08% | **3.58%** |
| | current | 0.09% | 49.29% | 46.67% | 3.95% | **49.38%** |
| | change | +0.09 | **+45.71** | −18.67 | **−27.13** | **+45.80** |
| **2HG** | v1 | 0.00% | 0.00% | 39.07% | 60.93% | **0.00%** |
| | current | 0.05% | 32.12% | 53.64% | 14.19% | **32.17%** |
| | change | +0.05 | **+32.12** | +14.58 | **−46.74** | **+32.17** |

## Averages and cost

| format | avg turn | change | win% | ms/game |
|---|---|---|---|---|
| **standard** | 5.3604 → **4.5552** | **−0.8052** | 99.67 → 99.92 | 434.62 → **76.40** |
| **2HG** | 5.8469 → **4.8482** | **−0.9987** | 98.84 → 99.85 | 1564.44 → **223.09** |

* **Every new card makes tokens immediately** (Migration on cast, Myconid on entry and death, Badger each
  end step, Burst on demand), feeding Sporecrown Thallid and Beastmaster Ascension. Crossroads gives them
  haste; Ancient Tomb, Hickory Woodlot and Sol Ring land Doubling Season on time.
* **All ten 2HG blocks are better than −0.98t**, so "nearly a full turn" is the flat effect.
* **The cost drop is partly the cards and partly the apparatus:** the current list ships without a
  learned value leaf, which v1 had. This run does not separate the two.

<div style="page-break-after: always;"></div>

# Kitty Equipment — −0.4423t standard, −0.5692t 2HG

**The deck the mean gets wrong: turn-3 kills go from 13% to 51% of standard games.** 15 cards in, 16 out
(across two adoptions): a few-big-equipment voltron deck became a 37-artifact metalcraft deck.

## Shared core — 18 of 60 cards in both lists

| n | card | n | card | n | card |
|---|---|---|---|---|---|
| 8 | Plains | 4 | Puresteel Paladin | | |
| 4 | Colossus Hammer | 2 | Shadowspear | | |

## Cards added

| | card | | | card |
|---|---|---|---|---|
| **+4** | Accorder's Shield | | **+4** | Spidersilk Net |
| **+4** | Ancient Den | | **+4** | Sram, Senior Edificer |
| **+4** | Bone Saw | | **+1** | Adelbert Steiner |
| **+4** | Cathar's Shield | | **+1** | Deconstruction Hammer |
| **+4** | Golem-Skin Gauntlets | | **+1** | Dragonfire Blade |
| **+4** | Kite Shield | | **+1** | Dwalin, Weaponmaster |
| **+4** | Remote Farm | | **+1** | Sigarda's Aid |
| | | | **+1** | Skateboard |

All fifteen are cards v1 did not play at all.

## Cards removed

| | card | | | card |
|---|---|---|---|---|
| **−14** | Plains | | **−2** | Unexpectedly Absent |
| **−4** | Armored Skyhunter† | | **−1** | Boros Garrison |
| **−4** | Bonesplitter† | | **−1** | Grafted Wargear |
| **−4** | Kor Duelist† | | **−1** | Lightning Greaves |
| **−2** | Balan, Wandering Knight | | **−1** | Loxodon Warhammer |
| **−2** | Kemba, Kha Regent | | **−1** | O-Naginata† |
| **−2** | Umezawa's Jitte† | | **−1** | Sol Ring† |
| | | | **−1** | Stoneforge Mystic |
| | | | **−1** | Swords to Plowshares |

† now in the 23-card sideboard as a single copy, so not fully gone.

## Win-turn distribution — share of games

| | | T3 | T4 | T5 | ≥T6 | **≤T4** |
|---|---|---|---|---|---|---|
| **standard** | v1 | 13.26% | 59.00% | 20.40% | 7.30% | **72.30%** |
| | current | **50.56%** | 31.70% | 10.53% | 7.21% | **82.26%** |
| | change | **+37.30** | −27.29 | −9.87 | −0.09 | **+9.96** |
| **2HG** | v1 | 6.51% | 36.96% | 40.40% | 16.13% | **43.47%** |
| | current | **29.33%** | 47.23% | 11.46% | 11.98% | **76.56%** |
| | change | **+22.83** | +10.27 | −28.95 | −4.15 | **+33.09** |

## Averages and cost

| format | avg turn | change | win% | ms/game |
|---|---|---|---|---|
| **standard** | 4.2519 → **3.8096** | **−0.4423** | 99.63 → 98.75 | 418.22 → 566.70 (+36%) |
| **2HG** | 4.7337 → **4.1645** | **−0.5692** | 99.33 → 98.34 | 1850.82 → 1537.17 (−17%) |

* **The mechanism is metalcraft:** with three or more artifacts Puresteel Paladin makes equip cost {0},
  which turns Colossus Hammer's {8} equip free. 17 → 37 artifacts is what makes that reliable.
* **It fails to close by turn 8 slightly more often** (win% down ~1 point), but the only cells of the
  distribution that grew are turn 7, turn 8 and non-wins — games a real opponent would have won anyway.
* **The most expensive deck here**, by an order of magnitude at 2HG.

<div style="page-break-after: always;"></div>

# White Knights — −0.2184t standard, −0.3492t 2HG, much cheaper

**The smallest gain, from by far the strongest start** — and now the highest turn-4 rate anywhere: six
games in seven at standard. It also costs half as much to play at 2HG.

## Shared core — 48 of 60 cards in both lists

| n | card | n | card | n | card |
|---|---|---|---|---|---|
| 19 | Plains | 3 | Venerable Knight | 1 | Silverblade Paladin |
| 4 | Accorder Paladin | 2 | Benalish Marshal | 1 | Sol Ring |
| 4 | Dauntless Bodyguard | 2 | Hero of Bladehold | 1 | Swords to Plowshares |
| 4 | Knight Exemplar | 1 | Adeline, Resplendent Cathar | 1 | Unexpectedly Absent |
| 4 | Worthy Knight | 1 | Aether Vial | | |

## Cards added

| | card | v1 → now |
|---|---|---|
| **+4** | Remote Farm | 0 → 4 (new) |
| **+2** | Adeline, Resplendent Cathar | 1 → 3 |
| **+2** | Benalish Marshal | 2 → 4 |
| **+2** | Silverblade Paladin | 1 → 3 |
| **+1** | Basri, Tomorrow's Champion | 0 → 1 (new) |
| **+1** | Kinsbaile Cavalier | 0 → 1 (new) |

## Cards removed

| | card | v1 → now |
|---|---|---|
| **−3** | Acclaimed Contender | 3 → 0 |
| **−3** | Plains | 22 → 19 |
| **−2** | Aether Vial | 3 → 1 |
| **−1** | Hero of Bladehold | 3 → 2 |
| **−1** | Lightning Greaves | 1 → 0 |
| **−1** | Valiant Knight | 1 → 0 |
| **−1** | Venerable Knight | 4 → 3 |

## Win-turn distribution — share of games

| | | T3 | T4 | T5 | ≥T6 | **≤T4** |
|---|---|---|---|---|---|---|
| **standard** | v1 | 0.33% | 65.22% | 31.75% | 2.70% | **65.55%** |
| | current | 0.00% | 85.61% | 13.48% | 0.91% | **85.61%** |
| | change | −0.33 | **+20.39** | −18.27 | −1.80 | **+20.06** |
| **2HG** | v1 | 0.04% | 19.81% | 66.75% | 13.40% | **19.85%** |
| | current | 0.00% | 44.20% | 51.38% | 4.43% | **44.20%** |
| | change | −0.04 | **+24.39** | −15.38 | **−8.97** | **+24.35** |

## Averages and cost

| format | avg turn | change | win% | ms/game |
|---|---|---|---|---|
| **standard** | 4.3727 → **4.1543** | **−0.2184** | 99.985 → 99.995 | 7.69 → **5.15** (−33%) |
| **2HG** | 4.9599 → **4.6107** | **−0.3492** | 99.92 → 99.97 | 28.99 → **14.04** (−52%) |

* **The bigger 2HG gain is Adeline:** she makes a 1/1 per opponent on every attack, so two at 2HG, and
  the list plays three instead of one. Hero of Bladehold, which does not scale with opponents, went
  3 → 2.
* **It gives up a fraction of a percent of turn-3 wins** to make turn 4 near-certain — the right trade
  when turn 5 is the alternative.
* **The cheapest deck in either document:** cutting Acclaimed Contender and two Aether Vials removed real
  branching.
