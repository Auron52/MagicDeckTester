<style>
/* PRINT LAYOUT: one section per page. Pagination is done with explicit page-break
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

# V1 vs the current list — Selesnya Lifegain

**The current list (v2-bogbeast-heroes, adopted 2026-10-22) wins over half a turn sooner than the list it
replaced (v1-avatar-alchemist), in both formats, on all 10 paired blocks, at a third of the cost per
game.** Each list plays at its own shipped setting (v1 d5, current d6; both 20 ms), `max_turns` 8.

| deck | format | v1 | current | change | se | t | blocks | games/arm |
|---|---|---|---|---|---|---|---|---|
| **Selesnya Lifegain** | standard | 5.1341 | **4.5908** | **−0.5433t** | 0.0043 | −127.2 | 10/10 | 40,000 |
| | 2HG | 5.4255 | **4.8892** | **−0.5363t** | 0.0074 | −72.5 | 10/10 | 40,000 |

Average turn the goldfish wins on, scoring a non-win as 9; lower is better. Standard and 2HG are
co-equal, not headline-plus-footnote.

## But the mean is not the headline — the win-turn distribution is

A goldfish win on turn 6 or later is too slow against a fast deck, so the share of games won by
turn 4 is the number that tracks what a list's speed is worth:

| deck | standard ≤T4 | 2HG ≤T4 | standard ≥T6 | 2HG ≥T6 |
|---|---|---|---|---|
| **Selesnya Lifegain** | 18.59% → **53.74%** | 9.02% → **35.00%** | 24.57% → 9.46% | 37.78% → 17.50% |

`≥T6` includes non-wins. **The current list wins by turn 4 in more than half of its standard games, up
from fewer than one in five**, and at 2HG nearly four times as often (9.02% → 35.00%). **Neither list
ever wins before turn 4**: 0 of the 160,000 games played, counted from the per-game files rather than
read off a rounded table. So `≤T4` here is exactly the turn-4 column.

**What a goldfish cannot show.** This is a lifegain deck: in a real game its life total buys turns that a
goldfish never needs. The goldfish measures speed only. The new list keeps the lifegain package (Feed the
Clan 3 → 4, Heroes' Reunion added, Blighted Steppe 2 → 4, Bogbeast's own trigger) and turns it into
damage, rather than trading it away for speed.

**The format barely moves the gain** (−0.5433 standard, −0.5363 2HG), unlike the three reference decks
of the earlier documents, where 2HG reordered the gains by up to 36%. The 2HG games start further back
(v1 had 37.78% of 2HG games at turn 6 or later), and the share the new list pulls forward into turn 4 is
smaller there (+25.98 points against +35.15 at standard).

## How much of this is the cards alone

These numbers carry each list's own keep table and value sidecar, so they measure the shipped package.
The list screen that chose the current list (`logs/selesnya_screen/`, round 15) already answers the
cards-only question: there both lists played with **v1's** keep table and value sidecar, 40,000 games
each per format, d5 / b20:

| format | v1 | current list on v1's apparatus | change (cards only) | change (this run, own apparatus) |
|---|---|---|---|---|
| standard | 5.1313 | 4.6227 | −0.5086 | −0.5433 |
| 2HG | 5.4354 | 4.9024 | −0.5330 | −0.5363 |

So **almost all of the speed comes from the card changes**: the refitted keep table, value sidecar and
d6 setting add about 0.03t at standard and nothing measurable at 2HG. The screen's arms were on a
different seed block and were not slot-paired, so read the last column's small differences as
approximate. **Cost is the opposite story** — see the deck page.

## Method

* **160,000 games** — 40,000 × 2 arms × 2 formats — in ONE pooled batch, 10 paired blocks of 4,000 per
  arm per format.
* **Each list at its own shipped play setting**, not a common pinned depth: v1 has no enabled
  `value_play` block, so it plays at the built-in d5; the current list's `value_play` drives play at
  `target_depth 6` (adopted at its value-leaf stage, E.7). Budget 20 ms on both (both lists' play budget).
  This differs from the earlier v1-vs-current documents, which pinned d7 / b200: the numbers here are
  "as shipped", and their **absolute** turn values are not comparable with those documents'.
* **2HG = `starting_life 30` + `opponent_heads 2`** on the same deck and profile, the suite's definition
  (`test/regression_cases.sh`).
* **`max_turns 8` validated first.** A 1,200-game probe returned win rates of 99.33–100% in every cell;
  the full run finished at 99.01–99.80%, so the horizon does not bind and the average is not a
  non-win-rate in disguise.
* **Paired blocks.** Block *b* of both arms covers the same global games: `seed = BASE + off` **and**
  `game_index = off`, so game *i* gets the same shuffle and spawn index on both arms. Quoted `se` is over
  the 10 block deltas. Base seed 51,000,000 standard, 52,000,000 2HG.
* **Each arm plays with its own shipped apparatus** — profile, value sidecar and exhaustive keep table
  from its own directory. The two sidecars differ in search SHAPE, not only in fit: see the deck page.
* **`provider=SelesnyaLifegain` on all 40 jobs, both arms, both formats**, and no `provider_detected=`
  line, so the decision heuristics are the same on both arms.
* **Averages count non-wins at 9**, and the batch's own `avg` was cross-checked against a recompute from
  the `.wins` files for every block (agreement to 6e-5).
* Engine `build/PGO` (PGO+LTO), **src tree `ec2de5145`**. The arms were the two lists' directories on the
  day: v1 at `decks/SelesnyaLifegain/` (since archived as `v1-avatar-alchemist/`) and the current list at
  `decks/SelesnyaLifegain/v2-bogbeast-heroes/` (since promoted to `decks/SelesnyaLifegain/`). The
  promotion was verified inert for both lists (every suite key reproduced its ground truth
  byte-identically), so the numbers apply to the files at their present paths.

<div style="page-break-after: always;"></div>

# Selesnya Lifegain — −0.5433t standard, −0.5363t 2HG, 3× cheaper

**Bogbeast as the payoff, more mana Elves and a faster mana base; the big lifegain creatures and the
mana-from-lifegain cards are gone.** Turn-4 wins: 18.59% → 53.74% standard, 9.02% → 35.00% 2HG.

## Current list — 60 cards, 21 lands

| n | card | n | card | n | card |
|---|---|---|---|---|---|
| 4 | Llanowar Elves | 4 | Ageless Entity | 4 | Branchloft Pathway |
| 4 | Elvish Mystic | 1 | Craterhoof Behemoth | 4 | Razorverge Thicket |
| 2 | Fyndhorn Elves | 4 | Feed the Clan | 2 | Horizon Canopy |
| 4 | Priest of Titania | 2 | Heroes' Reunion | 2 | Forest |
| 4 | Elvish Archdruid | 2 | Genesis Wave | 4 | Blighted Steppe |
| 4 | Blossoming Bogbeast | 4 | Brushland | 1 | Wirewood Lodge |
| 4 | Nykthos Paragon | | | | |

18 Elves, 21 lands. Neither list has a sideboard, so the main-zone diff below is the whole change.

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

## Win-turn distribution — share of all games

| | | T4 | T5 | T6 | T7 | T8 | non-win | ≥T6 | **≤T4** |
|---|---|---|---|---|---|---|---|---|---|
| **standard** | v1 | 18.59% | 56.84% | 19.10% | 4.00% | 0.99% | 0.48% | 24.57% | **18.59%** |
| | current | 53.74% | 36.80% | 7.01% | 1.74% | 0.52% | 0.20% | 9.46% | **53.74%** |
| | change | **+35.15** | −20.04 | −12.09 | −2.26 | −0.47 | −0.28 | −15.11 | **+35.15** |
| **2HG** | v1 | 9.02% | 53.20% | 27.91% | 6.95% | 1.94% | 0.99% | 37.78% | **9.02%** |
| | current | 35.00% | 47.50% | 12.96% | 3.15% | 0.91% | 0.48% | 17.50% | **35.00%** |
| | change | **+25.98** | −5.70 | −14.95 | −3.80 | −1.03 | −0.51 | −20.28 | **+25.98** |

No game won on turns 1–3 in either list or format (0 of 160,000). Raw counts per 40,000 games — standard
v1 / current: T4 7,437 / 21,496, T5 22,734 / 14,720, T6 7,641 / 2,805, T7 1,598 / 694, T8 398 / 206,
non-win 192 / 79; 2HG: T4 3,606 / 14,000, T5 21,282 / 18,999, T6 11,162 / 5,184, T7 2,780 / 1,260,
T8 775 / 365, non-win 395 / 192.

## How the averages changed

| format | | win% | non-wins | avg(won) | **avg(pen=9)** | ms/game | units/game |
|---|---|---|---|---|---|---|---|
| **standard** | v1 | 99.52 | 192 | 5.1154 | **5.1341** | 123.4 | 8,699 |
| | current | 99.80 | 79 | 4.5820 | **4.5908** | 40.6 | 3,466 |
| | change | **+0.28** | **−113** | −0.5334 | **−0.5433** | **0.33×** | 0.40× |
| **2HG** | v1 | 99.01 | 395 | 5.3899 | **5.4255** | 216.3 | 14,833 |
| | current | 99.52 | 192 | 4.8693 | **4.8892** | 75.4 | 6,052 |
| | change | **+0.51** | **−203** | −0.5206 | **−0.5363** | **0.35×** | 0.41× |

* **Block deltas −0.5593 to −0.5132 standard, −0.5625 to −0.4780 2HG**, 10/10 blocks better in both. The
  effect is flat across the sample rather than carried by a few blocks.
* **The current list also closes more games**: non-wins halve in both formats (192 → 79, 395 → 192), which
  is why the penalised delta is larger than the won-only one.
* **The two arms run different search shapes, so the cost drop is NOT the cards'.** v1 ships a learned
  value-leaf table (129 KB sidecar, with an `escalation_skip_r` ladder) at the built-in d5. The current
  list's value-leaf generation chose to be **leafless** (`value_play.leaf: "none"`, `ladder: "single"`, a
  400-byte sidecar, no table) and to play at `target_depth 6`. That was the deck's own measured adoption
  (0.51× units, quality delta −0.00025), so it is inside "did the deck get better", but it moves cost as
  much as the cards do. In the screen, where both lists shared v1's apparatus, the card changes alone cut
  cost to about **0.7×** (units 8,671 → 6,130 standard, 14,824 → 10,226 2HG); the rest of the drop to
  0.33–0.35× comes from the new keep table and the leafless shape.
* **The keep tables differ in buckets as well**: v1's has 19 discovered buckets, the current list's 16
  (R = 30 in both). The current table measured −0.2978t against static keeps at adoption.

## What the screen measured for each change

Each line is the measured effect of a one-copy step in the list screen (2026-10-22, 10,000 paired games
per cell, standard / 2HG, negative = faster). These are the screen's numbers at the step where the change
was made, not a re-decomposition of the −0.54t above; the steps were taken in sequence, so they do not
sum to it exactly.

* **Blossoming Bogbeast 1 → 4.** Its attack trigger gains 2 life, then gives the team +X/+X and trample
  where X is the life gained that turn, so every lifegain source becomes damage. Verdant Sun's Avatar →
  Bogbeast measured −0.064 / −0.072 for the 2nd copy and −0.155 / −0.174 for all three extra copies,
  still climbing at the 4-copy limit.
* **Verdant Sun's Avatar 4 → 0** (7 mana): the cheapest donor for the Bogbeast copies and Feed the Clan's
  4th.
* **Accomplished Alchemist 3 → 0, Wellwisher 2 → 0** (both into one-mana Elves): each Alchemist →
  Fyndhorn Elves step measured −0.071 to −0.055 standard, each Wellwisher → Elf step −0.030 / −0.026. The Elf count was then set at 18 by the USER, keeping all
  4 Priest of Titania (cards already owned break ties) and cutting Fyndhorn Elves to 2.
* **Genesis Wave 4 → 2**: Wave → Elvish Archdruid #4 −0.036 / −0.029, Wave → a Blighted Steppe −0.025 / −0.017.
* **Heroes' Reunion 0 → 2**: the USER chose 2 Heroes' Reunion over a 1 Heroes' Reunion + 1 Rest for the
  Weary split. Separated at 40,000 slot-paired games: Heroes' Reunion is ahead by 0.0011 ± 0.0004 standard
  and 0.0087 ± 0.0008 2HG. The difference is mostly timing: Rest for the Weary gains 8
  only if a land entered that turn, so the 8 was often spent early where it was wasted, and on the payoff
  turn it was held back when it would gain only 4.
* **Mana base**: both Selesnya Sanctuary cut (a bounce land measured worse than an untapped dual and about
  equal to a Forest), Blossoming Sands → Razorverge Thicket −0.010 / −0.009 (Sands always enters tapped),
  Razorverge #3 and #4 additive, Blighted Steppe 2 → 4. **Horizon Canopy is 2, not 4, by USER ruling**:
  the goldfish prices its life payment at almost nothing, so the measured −0.014 / −0.017 per copy is an
  upper bound.

<div style="page-break-after: always;"></div>

# Appendix — reproducing this

The run apparatus lived under `logs/selv2_vs_shipped/` (gitignored), so the manifest generator is here
in full, exactly as run. It *is* the experiment definition, and the pairing depends entirely on
`seed`/`game_index`. To reproduce at today's paths, point `ship` at
`decks/SelesnyaLifegain/v1-avatar-alchemist/SelesnyaLifegain` and `v2` at
`decks/SelesnyaLifegain/SelesnyaLifegain`.

```python
"""Each arm plays with its OWN artifacts out of its own directory (profile + keep table + value
sidecar), and its OWN adopted play setting: no "depth" key, so each deck's value_play block decides
the depth (shipped: no enabled block -> built-in d5; v2: target_depth 6), budget 20 ms on both.
Pairing contract: block b of BOTH arms covers the same global games [off, off+B):
seed = BASE + off AND game_index = off. One BASE per format (disjoint seed blocks).
usage: gen_manifest.py <blocks> <games_per_block> <out.json>     (run as: 10 4000)
"""
import json, sys
NB, B, out = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
ARMS = {"ship": "decks/SelesnyaLifegain/SelesnyaLifegain",
        "v2":   "decks/SelesnyaLifegain/v2-bogbeast-heroes/SelesnyaLifegain"}
FMTS = {"std": (51_000_000, {}),
        "2hg": (52_000_000, {"starting_life": 30, "opponent_heads": 2})}
assert B < 1_000_000 and NB * B < 1_000_000   # blocks never overlap the other format's base
jobs = []
for fmt, (base, extra) in FMTS.items():
    for b in range(NB):
        off = b * B
        for arm, stem in ARMS.items():   # arms innermost: a block's two arms are scheduled together
            jobs.append(dict(name=f"{arm}_{fmt}_b{b}", deck=stem + ".cod", profile=stem + ".profile.json",
                             games=B, seed=base + off, game_index=off, budget_ms=20, **extra))
json.dump({"jobs": jobs}, open(out, "w"), indent=1)
```

```
build/PGO/mtg --batch run.json --game-log-dir build/scratch/selv2/run_wins
```

Job lines give `played=/avg=/digest=/ms=/units=` per block, enough for the paired block deltas. Win rates
and distributions come from the `.wins` files (`<game_index> <win_turn> <digest>`, `win_turn <= 0` = no
win inside `max_turns`). Both formats went into one manifest, so the run had a single load-imbalance tail.

The "cards only" comparison is the screen's round 15 (`logs/deckcmp/SelesnyaLifegain/spec_round15.*`):
arms `base` (v1) and `FINAL_hh` (the current list), both on the staged apparatus
`logs/deckcmp/SelesnyaLifegain/aliased_r8` (v1's keep table with the new cards aliased into its buckets,
and v1's value sidecar).

## Caveats worth stating plainly

**Each arm carries its own keep table and value sidecar, and the two sidecars are different search
shapes**, so the headline measures the shipped package, not the decklist in isolation. That is the right
answer to "did the deck get better". The cards-only screen comparison above shows the speed gain is
almost entirely the cards, and the gain is ~127× its standard error at standard and ~73× at 2HG, 10/10
blocks consistent, so apparatus bias is not a live explanation for it. **The cost gain is not the
cards'**: about 0.7× of it is, the rest is the leafless shape and the new keep table.

**The goldfish underprices life payments and never blocks.** Horizon Canopy and Brushland cost life, which
the goldfish barely charges for, and an opponent who blocks would take something off an unpumped Elf
attack (a screen diagnostic put the share of damage dealt by creatures with no pump beyond Elf lords at
about 21% for an 18-Elf list like the current one and 26% for v1, standard, won games). Both biases apply to both lists.

*Process: `.claude/skills/deck-update-summary.md`.*
