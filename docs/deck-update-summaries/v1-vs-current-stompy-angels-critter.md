# V1 vs the current list — Stompy, Angels, Critter Lifegain

**Every current list wins faster than the list it replaced, in both formats, on all 10 paired blocks.**

| deck | format | v1 | current | change | blocks better |
|---|---|---|---|---|---|
| **Stompy** | standard | 4.5136 | **4.1006** | **−0.4130t** | 10/10 |
| | 2HG | 4.9359 | **4.3750** | **−0.5609t** | 10/10 |
| **Angels** | standard | 5.2790 | **4.6317** | **−0.6473t** | 10/10 |
| | 2HG | 5.7095 | **5.1278** | **−0.5817t** | 10/10 |
| **Critter Lifegain** | standard | 4.7700 | **4.3695** | **−0.4006t** | 10/10 |
| | 2HG | 5.1158 | **4.6795** | **−0.4363t** | 10/10 |

Average turn the goldfish wins on (a non-win counts as turn 9); lower is better.

## The share of games won by turn 4 is the number that matters

| deck | standard ≤T4 | 2HG ≤T4 | standard ≥T6 | 2HG ≥T6 |
|---|---|---|---|---|
| **Stompy** | 55.34% → **83.46%** | 25.45% → **64.08%** | 5.83% → 1.92% | 15.86% → 4.54% |
| **Angels** | 7.31% → **44.93%** | 1.58% → **12.96%** | 27.02% → 6.61% | 54.70% → 20.38% |
| **Critter Lifegain** | 34.91% → **65.91%** | 15.96% → **39.17%** | 9.69% → 2.50% | 22.98% → 6.09% |

A goldfish win on turn 6 or later (`≥T6`, including non-wins) is too slow against a fast deck.

* **Goldfish measures speed only.** All three decks also have ways to slow the opponent down (lifegain,
  removal and other answers), and the speed gains came without giving up much of that.
* **Angels is the largest gain**: games running to turn 6 or later fall from 27% to 6.6% at standard and
  from 55% to 20.4% at 2HG. v1 was already a good deck in real games, because its lifegain and removal
  push the game long; the new list mainly helps against fast combo, where v1 needed the right removal at
  the right time.
* **Stompy now wins on turn 3** (0.50% → 8.84% of standard games) and is 2.3–2.6× cheaper to play.
* **The format reorders the gains**: Stompy gains 36% more at 2HG, Critter slightly more, and Angels is
  the only deck whose edge narrows.

**Method.** 2.4M games (500k per arm per format for Angels and Critter, 100k for Stompy), depth 7,
budget 200 ms, `max_turns` 8. 2HG = 30 life, 2 opponent heads. 10 paired blocks per arm: both lists
see the same shuffles. Each list plays with its own shipped profile and keep table.

<div style="page-break-after: always;"></div>

# Stompy — −0.4130t standard, −0.5609t 2HG, 2.3–2.6× cheaper

**More Elves, more Natural Order, fewer copies of what it fetches** — and the only deck of the three that
now wins on turn 3.

## Shared core — 49 of 60 cards in both lists

| n | card | n | card | n | card |
|---|---|---|---|---|---|
| 13 | Forest | 4 | Worldly Tutor | 1 | Hornet Queen |
| 4 | Elvish Archdruid | 2 | Call of the Wild | 1 | Sol Ring |
| 4 | Elvish Mystic | 2 | Natural Order | 1 | Vaultborn Tyrant |
| 4 | Llanowar Elves | 2 | Terastodon | 1 | Worldspine Wurm |
| 4 | Priest of Titania | 1 | Craterhoof Behemoth | | |
| 4 | Turntimber Symbiosis | 1 | Elderscale Wurm | | |

## Cards added

| | card | v1 → now |
|---|---|---|
| **+4** | Fyndhorn Elves | 0 → 4 (new) |
| **+2** | Natural Order | 2 → 4 |
| **+2** | World War Hulk | 0 → 2 (new) |
| **+1** | Apex Altisaur | 0 → 1 (new) |
| **+1** | Ghalta, Stampede Tyrant | 0 → 1 (new) |
| **+1** | Craterhoof Behemoth | 1 → 2 |

## Cards removed

| | card | v1 → now |
|---|---|---|
| **−3** | Worldspine Wurm | 4 → 1 |
| **−2** | Arbor Elf | 2 → 0 |
| **−2** | Call of the Wild | 4 → 2 |
| **−1** | Forest | 14 → 13 |
| **−1** | Mirri's Guile | 1 → 0 |
| **−1** | Vaultborn Tyrant | 2 → 1 |
| **−1** | Wirewood Lodge | 1 → 0 |

## Win-turn distribution — share of games

| | | T3 | T4 | T5 | ≥T6 | **≤T4** |
|---|---|---|---|---|---|---|
| **standard** | v1 | 0.50% | 54.85% | 38.83% | 5.83% | **55.34%** |
| | current | 8.84% | 74.59% | 14.62% | 1.92% | **83.46%** |
| | change | **+8.34** | +19.75 | −24.20 | −3.91 | **+28.12** |
| **2HG** | v1 | 0.09% | 25.37% | 58.69% | 15.86% | **25.45%** |
| | current | 3.84% | 60.23% | 31.38% | 4.54% | **64.08%** |
| | change | **+3.75** | +34.86 | −27.31 | −11.32 | **+38.62** |

## Averages and cost

| format | avg turn | change | win% | ms/game |
|---|---|---|---|---|
| **standard** | 4.5136 → **4.1006** | **−0.4130** | 99.93 → 99.97 | 157.44 → **69.10** |
| **2HG** | 4.9359 → **4.3750** | **−0.5609** | 99.88 → 99.97 | 445.14 → **169.83** |

* **Why it works:** Natural Order sacrifices a green creature, so every mana Elf is ramp, fodder and
  enabler at once, and Priest of Titania taps for one mana per Elf. **Arbor Elf is cut** because it needs
  a Forest to untap and has no upside over Fyndhorn Elves.
* **Worldspine Wurm 4 → 1:** at four copies it was mostly a dead 11-mana draw; the fetch targets are
  diversified instead (Craterhoof, Ghalta, Apex Altisaur).
* **Turn 3 is the headline:** 0.50% → 8.84% of standard games. It wins by turn 4 in five games in six.

<div style="page-break-after: always;"></div>

# Angels — −0.6473t standard, −0.5817t 2HG

**The largest goldfish gain of the three in both formats.** Cost is flat. v1 was already a good deck in
real games, since its lifegain and removal push the game long; goldfish cannot show that.

## Shared core — 46 of 60 cards in both lists

| n | card | n | card | n | card |
|---|---|---|---|---|---|
| 15 | Plains | 3 | Unexpectedly Absent | 1 | Lightning Greaves |
| 4 | Bishop of Wings | 2 | Archangel of Thune | 1 | Lyra Dawnbringer |
| 4 | Resplendent Angel | 2 | Giada, Font of Hope | 1 | Sol Ring |
| 4 | Righteous Valkyrie | 2 | Serra the Benevolent | 1 | Youthful Valkyrie |
| 4 | Seraph Sanctuary | 2 | Swords to Plowshares | | |

## Cards added

| | card | v1 → now |
|---|---|---|
| **+4** | Lightstall Inquisitor | 0 → 4 (new) |
| **+4** | Lyra, Archangel of Dawn | 0 → 4 (new) |
| **+4** | Remote Farm | 0 → 4 (new) |
| **+2** | Giada, Font of Hope | 2 → 4 |

## Cards removed

| | card | v1 → now |
|---|---|---|
| **−4** | Plains | 19 → 15 |
| **−3** | Youthful Valkyrie | 4 → 1 |
| **−2** | Archangel of Thune | 4 → 2 |
| **−2** | Lyra Dawnbringer | 3 → 1 |
| **−1** | Azorius Chancery | 1 → 0 |
| **−1** | Legion Angel | 1 → 0 |
| **−1** | Swords to Plowshares | 3 → 2 |

## Win-turn distribution — share of games

| | | T3 | T4 | T5 | ≥T6 | **≤T4** |
|---|---|---|---|---|---|---|
| **standard** | v1 | 0.02% | 7.29% | 65.66% | 27.02% | **7.31%** |
| | current | 0.16% | 44.76% | 48.46% | 6.61% | **44.93%** |
| | change | +0.14 | **+37.47** | −17.20 | **−20.41** | **+37.61** |
| **2HG** | v1 | 0.00% | 1.58% | 43.72% | 54.70% | **1.58%** |
| | current | 0.00% | 12.96% | 66.66% | 20.38% | **12.96%** |
| | change | +0.00 | **+11.38** | +22.94 | **−34.32** | **+11.38** |

## Averages and cost

| format | avg turn | change | win% | ms/game |
|---|---|---|---|---|
| **standard** | 5.2790 → **4.6317** | **−0.6473** | 99.39 → 99.89 | 14.28 → 13.70 |
| **2HG** | 5.7095 → **5.1278** | **−0.5817** | 98.72 → 99.68 | 48.37 → 49.07 |

* **Much faster on its own:** v1 needed turn 6 or later in 27% of standard games and **55% of 2HG
  games**; the current list takes that to 6.6% and 20.4%.
* **Where it matters:** v1 tended to lose to fast combo unless it drew the right removal at the right time.
  Winning sooner without cutting its lifegain and removal should improve that matchup and others.
* **Goldfish non-wins fall 5.5× at standard and 4.0× at 2HG.**
* **The only deck here whose edge narrows at 2HG**, though both gains are large.

<div style="page-break-after: always;"></div>

# Critter Lifegain — −0.4006t standard, −0.4363t 2HG

**The most consistent result of the three** (every block within 0.01t of the others). Three cards added
and the land base rebuilt; it costs about a third more to play.

## Shared core — 50 of 60 cards in both lists

| n | card | n | card | n | card |
|---|---|---|---|---|---|
| 19 | Plains | 3 | Auriok Champion | 1 | Ajani, Strength of the Pride |
| 4 | Ajani's Pridemate | 3 | Unexpectedly Absent | 1 | Daxos, Blessed by the Sun |
| 4 | Soul Warden | 2 | Archangel of Thune | 1 | Ranger-Captain of Eos |
| 4 | Soul's Attendant | 2 | Heliod, Sun-Crowned | | |
| 4 | Voice of the Blessed | 2 | Serra Ascendant | | |

## Cards added

| | card | v1 → now |
|---|---|---|
| **+4** | Ocelot Pride | 0 → 4 (new) |
| **+4** | Remote Farm | 0 → 4 (new) |
| **+2** | Serra Ascendant | 2 → 4 |

## Cards removed

| | card | v1 → now |
|---|---|---|
| **−3** | Orzhov Basilica | 3 → 0 |
| **−2** | Ajani, Strength of the Pride | 3 → 1 |
| **−2** | Archangel of Thune | 4 → 2 |
| **−2** | Plains | 21 → 19 |
| **−1** | Auriok Champion | 4 → 3 |

## Win-turn distribution — share of games

| | | T3 | T4 | T5 | ≥T6 | **≤T4** |
|---|---|---|---|---|---|---|
| **standard** | v1 | 0.00% | 34.91% | 55.41% | 9.69% | **34.91%** |
| | current | 0.00% | 65.91% | 31.59% | 2.50% | **65.91%** |
| | change | +0.00 | **+31.01** | −23.82 | −7.19 | **+31.01** |
| **2HG** | v1 | 0.00% | 15.96% | 61.06% | 22.98% | **15.96%** |
| | current | 0.00% | 39.17% | 54.74% | 6.09% | **39.17%** |
| | change | +0.00 | **+23.22** | −6.32 | **−16.89** | **+23.22** |

## Averages and cost

| format | avg turn | change | win% | ms/game |
|---|---|---|---|---|
| **standard** | 4.7700 → **4.3695** | **−0.4006** | 99.89 → 99.99 | 13.33 → 17.99 (+35%) |
| **2HG** | 5.1158 → **4.6795** | **−0.4363** | 99.76 → 99.97 | 32.07 → 42.56 (+33%) |

* **Turn 4 is this deck's floor** in both lists, so the whole gain is converting turn-5 wins into turn-4
  wins: two games in three now win by turn 4 at standard.
* **Non-wins fall 11× at standard and 7× at 2HG**; games needing turn 6 or later at 2HG fall 3.8×.
* **Remote Farm** replaces 3 Orzhov Basilica and 2 Plains — the same land Angels added.
