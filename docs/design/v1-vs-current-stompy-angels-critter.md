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

# V1 vs the current list — Stompy, Angels, Critter Lifegain

**Every current list wins faster than the list it replaced, in both formats, on every one of 10 paired
blocks — six cells, no exceptions.** Depth 7, budget 200 ms, `max_turns` 8.

| deck | format | v1 | current | change | se | t | blocks | games/arm |
|---|---|---|---|---|---|---|---|---|
| **Stompy** | standard | 4.5136 | **4.1006** | **−0.4130t** | 0.0029 | −143.7 | 10/10 | 100,000 |
| | 2HG | 4.9359 | **4.3750** | **−0.5609t** | 0.0031 | −183.0 | 10/10 | 100,000 |
| **Angels** | standard | 5.2790 | **4.6317** | **−0.6473t** | 0.0015 | −434.3 | 10/10 | 500,000 |
| | 2HG | 5.7095 | **5.1278** | **−0.5817t** | 0.0016 | −362.9 | 10/10 | 500,000 |
| **Critter Lifegain** | standard | 4.7700 | **4.3695** | **−0.4006t** | 0.0009 | −446.8 | 10/10 | 500,000 |
| | 2HG | 5.1158 | **4.6795** | **−0.4363t** | 0.0010 | −451.3 | 10/10 | 500,000 |

Average turn the goldfish wins on, scoring a non-win as 9; lower is better. Standard and 2HG are
co-equal, not headline-plus-footnote.

## But the mean is not the headline — the win-turn distribution is

A goldfish win on turn 7 or 8 is a loss in a real game, and turn 5–6 is marginal against a fast
opponent. So the value of a win decays steeply with turn number, while the mean prices a T6→T5 shift
exactly like a T4→T3 one. **The share of games won by turn 4** is the number that tracks what a list is
actually worth:

| deck | standard ≤T4 | 2HG ≤T4 | standard ≥T6 | 2HG ≥T6 |
|---|---|---|---|---|
| **Stompy** | 55.34% → **83.46%** | 25.45% → **64.08%** | 5.83% → 1.92% | 15.86% → 4.54% |
| **Angels** | 7.31% → **44.93%** | 1.58% → **12.96%** | 27.02% → 6.61% | 54.70% → 20.38% |
| **Critter Lifegain** | 34.91% → **65.91%** | 15.96% → **39.17%** | 9.69% → 2.50% | 22.98% → 6.09% |

`≥T6` counts non-wins, i.e. it is the share of games that are too slow against a fast deck. Angels v1
needed turn 6 or later in 27% of its standard games and 55% of its 2HG games, and the current list cuts
that to 6.6% and 20.4%. No mean conveys that.

**What a goldfish cannot show (USER, 2026-10-18).** All three decks also carry ways to slow the opponent
down — lifegain, removal and other answers — and the speed gains here were made without giving up much
of that. Angels in particular was already a good deck in real games: its lifegain and removal push the
game long, so its late goldfish turns overstate how often v1 actually lost. Its weakness was fast combo,
which it lost to unless it had the right removal at the right time; winning sooner while keeping that
interaction should improve that matchup and others.

**The format reorders the gains, and it nearly changes the ranking.** **Stompy gains 36% more at 2HG**
(−0.4130 → −0.5609) and Critter Lifegain slightly more (−0.4006 → −0.4363), while **Angels is the only
deck here whose edge narrows** (−0.6473 → −0.5817) — which cuts Angels' lead over Stompy from 0.23t to
0.02t. None of these three decks holds a card with a per-opponent parameter, so the 2HG differences
come from the 30-life requirement alone, not from a card that scales with heads.

## Method

* **2,400,000 games** — 500,000 × 2 arms × 2 formats for Angels and Critter Lifegain, 100,000 × 2 × 2
  for Stompy — pooled into one batch per format, never a loop per deck or per arm.
* **d7 / b200 for every job**, deliberately deeper and far more generous than any suite case for these
  decks (d0/d3/d5 at 0/10/20 ms), so this compares the *lists* rather than which one copes with a tight
  budget.
* **Stompy was run with `--ignore-play-profile` on BOTH arms.** It is the one deck here whose
  `value_play` drives play (`target_depth 6`, `enabled true`), so the engine refuses a pinned `--depth`
  outright. Both arms drive at the same depth and budget, so the paired delta stays fair; its absolute
  numbers are off-policy, because its `vp_here`-gated budget-renewal and escalation-cap levers do not
  fire at a pinned depth. The two other decks set no `target_depth`, so `drives()` is false for them and
  the pinned depth is simply deeper, not off-policy.
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
  from its own directory. Correct for "did the deck get better"; a deliberate confound, see the appendix.
* **`provider=` verified identical across arms** on all three decks in both formats (`Stompy`, `Angels`,
  `CritterLifegain`). An edited list can cross an archetype signature and silently swap one arm's decision
  heuristics; nothing else catches it.
* **Averages count non-wins at 9**, the `ref_bench` convention — verified by recomputing both forms from
  the per-game `.wins` files, not assumed from the source, which reads as if it averaged won games only.
* Engine `build/PGO` (PGO+LTO). **Angels and Critter Lifegain at src tree `55191e63`; Stompy at
  `b6985a8a`** — the two runs are a month apart and the branch moved between them. The engine difference
  is not corrected for: it applies identically to both arms of each deck, so each deck's paired delta is
  internally clean, and only a cross-deck comparison of absolute turn numbers carries it.
* The 500,000-game standard run was executed twice, with and without per-game logging, and all 60 job
  digests were byte-identical — both a determinism check and proof that the logging is passive.

<div style="page-break-after: always;"></div>

# Stompy — −0.4130t standard, −0.5609t 2HG, and 2.3–2.6× cheaper

Adopted 2026-09-17. Predecessor `decks/StompySurprise/v1-arborelf-worldspine4/`. **The only deck of the
three that got both faster and dramatically cheaper**, and the only one that now wins on turn 3 at all.

## Current list — 60 cards, 13 lands

| n | card | n | card |
|---|---|---|---|
| 13 | Forest | 2 | Call of the Wild |
| 4 | Elvish Archdruid | 2 | Craterhoof Behemoth |
| 4 | Elvish Mystic | 2 | Terastodon |
| 4 | Fyndhorn Elves | 2 | World War Hulk |
| 4 | Llanowar Elves | 1 | Apex Altisaur |
| 4 | Natural Order | 1 | Elderscale Wurm |
| 4 | Priest of Titania | 1 | Ghalta, Stampede Tyrant |
| 4 | Turntimber Symbiosis | 1 | Hornet Queen |
| 4 | Worldly Tutor | 1 | Sol Ring |
| | | 1 | Vaultborn Tyrant |
| | | 1 | Worldspine Wurm |

Only 13 lands, because 14 one- and two-mana mana sources do the work the lands would.

## Cards added

| | card | v1 → now | |
|---|---|---|---|
| **+4** | Fyndhorn Elves | 0 → 4 | **new card** — a third Llanowar Elves |
| **+2** | Natural Order | 2 → 4 | up to a 4-of |
| **+2** | World War Hulk | 0 → 2 | **new card** (Saga; chapter 1 free-casts an R/G creature) |
| **+1** | Apex Altisaur | 0 → 1 | **new card**, 10/10 |
| **+1** | Ghalta, Stampede Tyrant | 0 → 1 | **new card**, 12/12 |
| **+1** | Craterhoof Behemoth | 1 → 2 | increased |

## Cards removed

| | card | v1 → now | |
|---|---|---|---|
| **−3** | Worldspine Wurm | 4 → 1 | down to a 1-of |
| **−2** | Arbor Elf | 2 → 0 | **cut** |
| **−2** | Call of the Wild | 4 → 2 | reduced |
| **−1** | Forest | 14 → 13 | reduced |
| **−1** | Mirri's Guile | 1 → 0 | **cut** |
| **−1** | Vaultborn Tyrant | 2 → 1 | down to a 1-of |
| **−1** | Wirewood Lodge | 1 → 0 | **cut** |

**The edit is one coherent move: more Elves, more Natural Order, fewer copies of the thing it fetches.**
Natural Order is `tutor_to_battlefield_single` for a **green creature** at the additional cost of
**sacrificing a green creature**, so every mana dork is simultaneously the ramp, the fodder and the
enabler. Taking dorks from 10 to 14 also feeds Priest of Titania, which taps for mana *per Elf on the
battlefield* (`mana_per_creature_count_all: true`) — so each added Elf is worth more than one mana. Arbor
Elf is the one dork cut: it needs a Forest to untap and has no upside over Fyndhorn Elves (USER,
2026-10-18). Meanwhile Worldspine Wurm at 4 copies was mostly a dead 11-mana draw
when the point is to cheat it in: 4 → 1, with the fetch targets diversified instead (Craterhoof 1 → 2,
plus Ghalta and Apex Altisaur).

## Win-turn distribution — share of all games

| | | T2 | T3 | T4 | T5 | T6 | T7 | T8 | none | **≤T4** | ≥T6 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **std** | v1 | 0.00% | 0.50% | 54.85% | 38.83% | 4.78% | 0.81% | 0.17% | 0.07% | **55.34%** | 5.83% |
| | current | 0.03% | 8.84% | 74.59% | 14.62% | 1.54% | 0.28% | 0.07% | 0.03% | **83.46%** | 1.92% |
| | change | +0.03 | **+8.34** | +19.75 | −24.20 | −3.23 | −0.52 | −0.11 | −0.05 | **+28.12** | −3.91 |
| **2HG** | v1 | 0.00% | 0.09% | 25.37% | 58.69% | 13.25% | 2.05% | 0.44% | 0.12% | **25.45%** | 15.86% |
| | current | 0.01% | 3.84% | 60.23% | 31.38% | 3.81% | 0.59% | 0.10% | 0.03% | **64.08%** | 4.54% |
| | change | +0.01 | **+3.75** | +34.86 | −27.31 | −9.44 | −1.45 | −0.33 | −0.09 | **+38.62** | −11.32 |

**Turn 3 is the headline**: 0.50% → 8.84% of standard games, a seventeen-fold increase, and it is the
Fyndhorn/Natural Order curve showing up directly. The current list wins by turn 4 in five games in six.

## How the averages changed

| format | | win% | non-wins | avg(won) | **avg(pen=9)** |
|---|---|---|---|---|---|
| **standard** | v1 | 99.9270 | 73 | 4.5103 | **4.5136** |
| | current | 99.9740 | 26 | 4.0993 | **4.1006** |
| | change | **+0.0470** | **−47** | −0.4110 | **−0.4130** |
| **2HG** | v1 | 99.8830 | 117 | 4.9311 | **4.9359** |
| | current | 99.9690 | 31 | 4.3736 | **4.3750** |
| | change | **+0.0860** | **−86** | −0.5576 | **−0.5609** |

* Block deltas **−0.4254 to −0.3996** standard, **−0.5731 to −0.5471** 2HG. A spread of ~0.026t across
  100,000 games, so the effect is flat rather than carried by a few blocks.
* Both v1 and the current list essentially always close by turn 8 (99.93% → 99.97%); the gain here is
  pure speed, not reliability.
* **Cost collapses: 157.44 → 69.10 ms/game standard (2.3× cheaper) and 445.14 → 169.83 at 2HG (2.6×)**,
  with units falling in step (10,394 → 4,600 and 28,242 → 11,248). Cutting 3 Worldspine Wurm and 2 Call
  of the Wild removed the deck's most expensive decisions — a 15/15 whose death makes three 5/5 tokens
  and reshuffles the library is a large amount of board state to search over. Dearest deck in this
  document even so, by a factor of three.

<div style="page-break-after: always;"></div>

# Angels — −0.6473t standard, −0.5817t 2HG

Adopted 2026-09-19 after an 18-screen settling campaign. Predecessor
`decks/Angels/v1-thune4-chancery/`. **The largest gain in this document in both formats.**

## Current list — 60 cards, 23 lands

| n | card | n | card |
|---|---|---|---|
| 15 | Plains | 3 | Unexpectedly Absent |
| 4 | Bishop of Wings | 2 | Archangel of Thune |
| 4 | Giada, Font of Hope | 2 | Serra the Benevolent |
| 4 | Lightstall Inquisitor | 2 | Swords to Plowshares |
| 4 | Lyra, Archangel of Dawn | 1 | Lightning Greaves |
| 4 | Remote Farm | 1 | Lyra Dawnbringer |
| 4 | Resplendent Angel | 1 | Sol Ring |
| 4 | Righteous Valkyrie | 1 | Youthful Valkyrie |
| 4 | Seraph Sanctuary | | |

## Cards added

| | card | v1 → now | |
|---|---|---|---|
| **+4** | Lightstall Inquisitor | 0 → 4 | **new card** |
| **+4** | Lyra, Archangel of Dawn | 0 → 4 | **new card** |
| **+4** | Remote Farm | 0 → 4 | **new card** |
| **+2** | Giada, Font of Hope | 2 → 4 | up to a 4-of |

## Cards removed

| | card | v1 → now | |
|---|---|---|---|
| **−4** | Plains | 19 → 15 | land restructure |
| **−3** | Youthful Valkyrie | 4 → 1 | down to a 1-of |
| **−2** | Archangel of Thune | 4 → 2 | reduced |
| **−2** | Lyra Dawnbringer | 3 → 1 | down to a 1-of |
| **−1** | Azorius Chancery | 1 → 0 | **cut** |
| **−1** | Legion Angel | 1 → 0 | **cut**, with the 3 copies it wished for |
| **−1** | Swords to Plowshares | 3 → 2 | reduced |

Lyra, Archangel of Dawn is legend-capped at one on the battlefield, so it is a genuinely different card
from "more Lyra Dawnbringer" — which is why both Lyras appear, at 4 and 1.

## Win-turn distribution — share of all games

| | | T2 | T3 | T4 | T5 | T6 | T7 | T8 | none | **≤T4** | ≥T6 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **std** | v1 | 0.00% | 0.02% | 7.29% | 65.66% | 21.19% | 4.06% | 1.16% | 0.61% | **7.31%** | 27.02% |
| | current | 0.00% | 0.16% | 44.76% | 48.46% | 5.40% | 0.89% | 0.21% | 0.11% | **44.93%** | 6.61% |
| | change | +0.00 | +0.14 | **+37.47** | −17.20 | −15.79 | −3.17 | −0.95 | −0.50 | **+37.61** | −20.41 |
| **2HG** | v1 | 0.00% | 0.00% | 1.58% | 43.72% | 41.84% | 9.16% | 2.42% | 1.28% | **1.58%** | 54.70% |
| | current | 0.00% | 0.00% | 12.96% | 66.66% | 16.41% | 2.88% | 0.76% | 0.32% | **12.96%** | 20.38% |
| | change | +0.00 | +0.00 | **+11.38** | +22.94 | −25.43 | −6.28 | −1.66 | −0.96 | **+11.38** | −34.32 |

**The largest speed gain measured here.** v1 put 27% of standard games and **54.70% of 2HG games** at
turn 6 or later; the current list takes that to 6.61% and 20.38%. A goldfish undersells v1: its lifegain
and removal push real games long, and its weak matchup was fast combo, where it needed the right removal
at the right time. At 2HG the mass has not yet reached the fast region (12.96% by turn 4);
the whole move is out of T6+ and into T5, which the mean records faithfully but understates in value.

## How the averages changed

| format | | win% | non-wins | avg(won) | **avg(pen=9)** |
|---|---|---|---|---|---|
| **standard** | v1 | 99.3886 | 3,057 | 5.2561 | **5.2790** |
| | current | 99.8886 | 557 | 4.6269 | **4.6317** |
| | change | **+0.5000** | **−2,500** | −0.6293 | **−0.6473** |
| **2HG** | v1 | 98.7236 | 6,382 | 5.6669 | **5.7095** |
| | current | 99.6804 | 1,598 | 5.1153 | **5.1278** |
| | change | **+0.9568** | **−4,784** | −0.5516 | **−0.5817** |

* Block deltas **−0.6424 to −0.6579** standard, **−0.5753 to −0.5931** 2HG — a spread of ~0.016t across
  500,000 games.
* **It closes games v1 lost outright**: non-wins fall 5.5× standard, 4.0× at 2HG. In both formats the
  penalised delta exceeds the won-only delta, the signature of "wins at all" and not only "wins sooner".
* **The one deck here whose edge narrows at 2HG.** Both numbers are large and the ordering is never in
  doubt; recorded as an observation, since nothing measured identifies a cause and no Angels card carries
  a per-opponent parameter.
* **Cost is flat** — 13.70 ms/game vs v1's 14.28 standard, 49.07 vs 48.37 at 2HG, so ~3.6× its own
  standard cost in the longer format. Cheap in absolute terms: a fortieth of Stompy per game.

<div style="page-break-after: always;"></div>

# Critter Lifegain — −0.4006t standard, −0.4363t 2HG

Predecessor `decks/CritterLifegain/v1-thune4-basilica/`. **The most consistent result in this
document** — the tightest standard errors anywhere, with `t` beyond 440 in both formats.

## Current list — 60 cards, 23 lands

| n | card | n | card |
|---|---|---|---|
| 19 | Plains | 3 | Unexpectedly Absent |
| 4 | Ajani's Pridemate | 2 | Archangel of Thune |
| 4 | Ocelot Pride | 2 | Heliod, Sun-Crowned |
| 4 | Remote Farm | 1 | Ajani, Strength of the Pride |
| 4 | Serra Ascendant | 1 | Daxos, Blessed by the Sun |
| 4 | Soul Warden | 1 | Ranger-Captain of Eos |
| 4 | Soul's Attendant | | |
| 4 | Voice of the Blessed | | |
| 3 | Auriok Champion | | |

## Cards added

| | card | v1 → now | |
|---|---|---|---|
| **+4** | Ocelot Pride | 0 → 4 | **new card** |
| **+4** | Remote Farm | 0 → 4 | **new card** |
| **+2** | Serra Ascendant | 2 → 4 | up to a 4-of |

## Cards removed

| | card | v1 → now | |
|---|---|---|---|
| **−3** | Orzhov Basilica | 3 → 0 | **cut** |
| **−2** | Ajani, Strength of the Pride | 3 → 1 | down to a 1-of |
| **−2** | Archangel of Thune | 4 → 2 | reduced |
| **−2** | Plains | 21 → 19 | reduced |
| **−1** | Auriok Champion | 4 → 3 | reduced |

Only three cards added and the land base rebuilt: 3 Orzhov Basilica and 2 Plains out for 4 Remote Farm —
the same Remote Farm addition Angels made, and the only edit shared across this document's three decks.

## Win-turn distribution — share of all games

| | | T2 | T3 | T4 | T5 | T6 | T7 | T8 | none | **≤T4** | ≥T6 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **std** | v1 | 0.00% | 0.00% | 34.91% | 55.41% | 7.99% | 1.28% | 0.30% | 0.11% | **34.91%** | 9.69% |
| | current | 0.00% | 0.00% | 65.91% | 31.59% | 2.20% | 0.24% | 0.04% | 0.01% | **65.91%** | 2.50% |
| | change | +0.00 | +0.00 | **+31.01** | −23.82 | −5.79 | −1.04 | −0.26 | −0.10 | **+31.01** | −7.19 |
| **2HG** | v1 | 0.00% | 0.00% | 15.96% | 61.06% | 19.52% | 2.60% | 0.62% | 0.24% | **15.96%** | 22.98% |
| | current | 0.00% | 0.00% | 39.17% | 54.74% | 5.22% | 0.72% | 0.11% | 0.03% | **39.17%** | 6.09% |
| | change | +0.00 | +0.00 | **+23.22** | −6.32 | −14.30 | −1.88 | −0.51 | −0.21 | **+23.22** | −16.89 |

Turn 4 is this deck's floor in both lists — it never wins on turn 3 — so the whole gain is the T5→T4
conversion, and the current list makes it in **two games in three** at standard. The 2HG `≥T6` share
falls from 22.98% to 6.09%, a 3.8× reduction in games too slow against a fast deck.

## How the averages changed

| format | | win% | non-wins | avg(won) | **avg(pen=9)** |
|---|---|---|---|---|---|
| **standard** | v1 | 99.8874 | 563 | 4.7653 | **4.7700** |
| | current | 99.9900 | 50 | 4.3690 | **4.3695** |
| | change | **+0.1026** | **−513** | −0.3963 | **−0.4006** |
| **2HG** | v1 | 99.7616 | 1,192 | 5.1065 | **5.1158** |
| | current | 99.9668 | 166 | 4.6781 | **4.6795** |
| | change | **+0.2052** | **−1,026** | −0.4284 | **−0.4363** |

* Block deltas **−0.3944 to −0.4047** standard, **−0.4313 to −0.4403** 2HG — a spread of ~0.010t, the
  flattest effect in this document.
* Non-wins fall **11×** standard (563 → 50) and **7×** at 2HG (1,192 → 166). At 99.99% / 99.97% the
  current list essentially always closes by turn 8.
* **This deck costs more to play than its predecessor, in both formats**: 17.99 ms/game vs 13.33
  standard (**+35%**, units 1,269 vs 913) and 42.56 vs 32.07 at 2HG (**+33%**). Four Ocelot Pride and
  four Serra Ascendant generate more board state and more decisions than the lands and Ajani copies they
  replaced. Worth knowing when sizing this deck's tier rows; it buys ~0.40–0.44 turns.

<div style="page-break-after: always;"></div>

# Appendix — reproducing this

The run apparatus lived under `logs/v1_vs_current/` (gitignored), so the manifest generator is here in
full. It *is* the experiment definition, and the pairing depends entirely on `seed`/`game_index`:

```python
import json, sys
DECKS = {
 "stompy":  ("decks/StompySurprise",  "StompySurprise",  "v1-arborelf-worldspine4"),
 "angels":  ("decks/Angels",          "Angels",          "v1-thune4-chancery"),
 "critter": ("decks/CritterLifegain", "CritterLifegain", "v1-thune4-basilica"),
}
# Decks whose value_play DRIVES play (target_depth > 0 AND enabled), so the engine REFUSES a pinned
# --depth. Only StompySurprise, and it drives on BOTH arms at the SAME target_depth -- so forcing
# d7/b200 treats the arms identically and the paired delta stays fair; only the ABSOLUTE numbers
# become off-policy. Do NOT set this globally: that would change the treatment of the other decks.
NEEDS_IGNORE = {"stompy"}
BASES = {"std": 700000, "2hg": 800000}       # one base per format

def build(fmt, blocks, per, slugs):
    jobs = []
    for slug in slugs:
        root, stem, v1 = DECKS[slug]
        for arm, d in (("cur", root), ("v1", f"{root}/{v1}")):
            for b in range(blocks):
                off = b * per
                j = {"name": f"{slug}-{arm}_b{b}",
                     "deck": f"{d}/{stem}.cod", "profile": f"{d}/{stem}.profile.json",
                     "games": per, "seed": BASES[fmt] + off, "game_index": off,
                     "depth": 7, "budget_ms": 200}
                if slug in NEEDS_IGNORE:
                    j["ignore_play_profile"] = True
                if fmt == "2hg":
                    j["starting_life"], j["opponent_heads"] = 30, 2
                jobs.append(j)
    return jobs
```

A chunk covering global games `[off, off+n)` sets `seed = BASE + off` **and** `game_index = off` — the
contract in `BatchRunner.cpp`'s `Job::game_index` comment, and what makes game *i* of block *b* the same
shuffle on both arms. Set one and not the other and the arms silently stop being paired: the run still
completes and the numbers still look plausible.

Per-deck game counts go into **one** manifest (`slug:blocks:per` triples), so a cheap deck can keep a
large *n* while an expensive one is trimmed without splitting the run into two batches — two batches
means two load-imbalance tails.

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
different and cheaper experiment. The six gains are far too large for apparatus bias to explain: the
smallest, Critter Lifegain at −0.4006t standard, is ~445× its own standard error and 10/10 blocks
consistent.

**Capability scans read the mainboard and the sideboard together**, so a sideboard is never fully inert
even when nothing in the deck can fetch from it. None of these three lists carries a sideboard, in either
version, so the question does not arise here — but it is why the card diffs above check both zones
rather than the main alone.

*Process codified as `.claude/skills/deck-update-summary.md`.*
