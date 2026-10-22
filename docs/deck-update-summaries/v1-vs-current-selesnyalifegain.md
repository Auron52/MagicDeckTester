# V1 vs the current list — Selesnya Lifegain

**The current list wins over half a turn sooner than the list it replaced, in both formats, on all 10
paired blocks, at a third of the cost per game.**

| deck | format | v1 | current | change | blocks better |
|---|---|---|---|---|---|
| **Selesnya Lifegain** | standard | 5.1341 | **4.5908** | **−0.5433t** | 10/10 |
| | 2HG | 5.4255 | **4.8892** | **−0.5363t** | 10/10 |

Average turn the goldfish wins on (a non-win counts as turn 9); lower is better.

## The share of games won by turn 4 is the number that matters

| deck | standard ≤T4 | 2HG ≤T4 | standard ≥T6 | 2HG ≥T6 |
|---|---|---|---|---|
| **Selesnya Lifegain** | 18.59% → **53.74%** | 9.02% → **35.00%** | 24.57% → 9.46% | 37.78% → 17.50% |

A goldfish win on turn 6 or later (`≥T6`, including non-wins) is too slow against a fast deck.

* **Over half of standard games now end by turn 4**, up from fewer than one in five. Neither list ever
  wins before turn 4 (0 of 160,000 games).
* **Almost all of the speed is the cards.** With both lists on v1's keep table and value sidecar, the
  screen measured −0.5086t standard and −0.5330t 2HG.
* **Most of the cost drop is not the cards.** The new list plays leafless at depth 6 where v1 used a
  learned value table at depth 5; the card changes alone cut cost to about 0.7×.
* **Goldfish measures speed only.** The lifegain package stays (more Feed the Clan, Heroes' Reunion,
  more Blighted Steppe) and now feeds Bogbeast's damage.

**Method.** 160,000 games (40,000 per list per format), each list at its own shipped setting (v1 d5,
current d6; 20 ms), `max_turns` 8. 2HG = 30 life, 2 opponent heads. 10 paired blocks per list: both lists
see the same shuffles. Each list plays with its own shipped profile, keep table and value sidecar.

<div style="page-break-after: always;"></div>

# Selesnya Lifegain — −0.5433t standard, −0.5363t 2HG, 3× cheaper

**Bogbeast becomes the payoff, with more mana Elves and a faster mana base. Gone: the big lifegain
creatures and the cards that make mana from lifegain.**

## Shared core — 42 of 60 cards in both lists

| n | card | n | card | n | card |
|---|---|---|---|---|---|
| 4 | Ageless Entity | 4 | Priest of Titania | 2 | Forest |
| 4 | Branchloft Pathway | 3 | Elvish Archdruid | 2 | Genesis Wave |
| 4 | Brushland | 3 | Elvish Mystic | 1 | Blossoming Bogbeast |
| 4 | Llanowar Elves | 3 | Feed the Clan | 1 | Craterhoof Behemoth |
| 4 | Nykthos Paragon | 2 | Blighted Steppe | 1 | Wirewood Lodge |

## Cards added

| | card | v1 → now |
|---|---|---|
| **+4** | Razorverge Thicket | 0 → 4 (new) |
| **+3** | Blossoming Bogbeast | 1 → 4 |
| **+2** | Blighted Steppe | 2 → 4 |
| **+2** | Fyndhorn Elves | 0 → 2 (new) |
| **+2** | Heroes' Reunion | 0 → 2 (new) |
| **+2** | Horizon Canopy | 0 → 2 (new) |
| **+1** | Elvish Archdruid | 3 → 4 |
| **+1** | Elvish Mystic | 3 → 4 |
| **+1** | Feed the Clan | 3 → 4 |

## Cards removed

| | card | v1 → now |
|---|---|---|
| **−4** | Verdant Sun's Avatar | 4 → 0 (cut) |
| **−3** | Accomplished Alchemist | 3 → 0 (cut) |
| **−3** | Forest | 5 → 2 |
| **−2** | Blossoming Sands | 2 → 0 (cut) |
| **−2** | Genesis Wave | 4 → 2 |
| **−2** | Selesnya Sanctuary | 2 → 0 (cut) |
| **−2** | Wellwisher | 2 → 0 (cut) |

18 Elves and 21 lands. Neither list has a sideboard.

## Win-turn distribution — share of games

| | | T3 | T4 | T5 | ≥T6 | **≤T4** |
|---|---|---|---|---|---|---|
| **standard** | v1 | 0.00% | 18.59% | 56.84% | 24.57% | **18.59%** |
| | current | 0.00% | 53.74% | 36.80% | 9.46% | **53.74%** |
| | change | 0 | **+35.15** | −20.04 | −15.11 | **+35.15** |
| **2HG** | v1 | 0.00% | 9.02% | 53.20% | 37.78% | **9.02%** |
| | current | 0.00% | 35.00% | 47.50% | 17.50% | **35.00%** |
| | change | 0 | **+25.98** | −5.70 | −20.28 | **+25.98** |

## Averages and cost

| format | avg turn v1 → current | change | win% | ms/game |
|---|---|---|---|---|
| **standard** | 5.1341 → **4.5908** | **−0.5433** | 99.52 → 99.80 | 123.4 → **40.6** |
| **2HG** | 5.4255 → **4.8892** | **−0.5363** | 99.01 → 99.52 | 216.3 → **75.4** |

* **Bogbeast is the engine:** its attack trigger gains 2 life, then gives the team +X/+X and trample for
  the life gained that turn. In the screen, the three extra copies were worth −0.155t at standard on their own, still
  climbing at the 4-copy limit.
* **The cuts went to cheaper cards:** each Accomplished Alchemist → Fyndhorn Elves step measured about
  −0.06t. Elves were fixed at 18 by your ruling, keeping all 4 Priest of Titania. Horizon Canopy is 2,
  not 4, because the goldfish barely charges for its life payment.
* **Non-wins halve in both formats** (192 → 79 and 395 → 192 of 40,000).
