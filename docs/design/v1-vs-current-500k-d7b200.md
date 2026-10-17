<style>
/* PRINT LAYOUT: one deck per page. Pagination is done with explicit
   page-break divs (they survive renderers that strip <style>); this block only
   handles fit -- tighter type and margins, and never splitting a table or
   orphaning a heading. Do NOT also add `h1 { page-break-before: always }`:
   combined with the divs it inserts a blank page between every section. */
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

# V1 vs the current list — Angels, Critter Lifegain, White Knights

**500,000 games per arm per format at depth 7, budget 200 ms. All three current lists win faster than
the list they replaced — in both formats, on every one of 10 paired blocks, in all six cells.**

| deck | format | v1 | current | change | se | t | blocks |
|---|---|---|---|---|---|---|---|
| **Angels** | standard | 5.2790 | **4.6317** | **−0.6473t** | 0.0015 | −434.3 | 10/10 |
| | 2HG | 5.7095 | **5.1278** | **−0.5817t** | 0.0016 | −362.9 | 10/10 |
| **Critter Lifegain** | standard | 4.7700 | **4.3695** | **−0.4006t** | 0.0009 | −446.8 | 10/10 |
| | 2HG | 5.1158 | **4.6795** | **−0.4363t** | 0.0010 | −451.3 | 10/10 |
| **White Knights** | standard | 4.3727 | **4.1543** | **−0.2184t** | 0.0012 | −187.2 | 10/10 |
| | 2HG | 4.9599 | **4.6107** | **−0.3492t** | 0.0012 | −288.0 | 10/10 |

Average turn the goldfish wins on; lower is better. Standard and 2HG are co-equal, not
headline-plus-footnote.

**The format reorders the gains.** Angels is the only deck whose edge *narrows* at 2HG
(−0.6473 → −0.5817); Critter Lifegain's is slightly larger (−0.4006 → −0.4363); **White Knights' grows
by 60%** (−0.2184 → −0.3492), which has a specific and verified cause — see its page.

## Method

* **6,000,000 games** — 500,000 × 2 arms × 3 decks × 2 formats — as **two** pooled batches, one per
  format. Standard 19m40s, 2HG 56m56s (2HG is ~3× dearer per game: 30 team life, longer games, more
  board state). 0 abandoned games; 75 of 3,000,000 exceeded 30 s at 2HG.
* **d7 / b200, `max_turns` 8**, both formats — deliberately deeper and far more generous than any suite
  case for these decks (d0/d3/d5 at 0/10/20 ms), so this compares the *lists*, not which one copes with
  a tight budget. No deck sets `value_play.target_depth`, so `drives()` is false and the pinned depth is
  simply deeper, not off-policy.
* **2HG = `starting_life 30` + `opponent_heads 2`** on the same deck and profile — the suite's canonical
  definition (`test/regression_cases.sh`, "2HG gate" block).
* **`max_turns 8` was validated for 2HG before spending the run.** 30 team life could have pushed games
  past the horizon and quietly turned the average into a non-win-rate measure; a 12,000-game probe
  returned win rates of 98.25%–100%, so the horizon does not bind and the formats stay comparable.
* **Paired blocks.** 10 blocks of 50,000 per arm. Block *b* of both arms covers the same global games
  `[b·50000, (b+1)·50000)` with the same base seed and `game_index`, so game *i* gets shuffle seed
  `base+off+i` and spawn index `off+i` on **both** arms. Quoted `se` is over the 10 block deltas. Base
  seed 700000 standard, 800000 2HG.
* **Each arm plays with its own shipped apparatus** — profile, value sidecar and exhaustive keep table
  from its own directory. Correct for "did the deck get better"; a deliberate confound — see the
  appendix.
* **`provider=` verified identical across arms** on all three decks. An edited list can cross an
  archetype signature and silently swap one arm's heuristics; nothing else catches it.
* **Averages count non-wins at 9**, the `ref_bench` convention — verified against the per-game `.wins`
  files, not assumed from the source. `avg(pen=9)` is the headline because it cannot be gamed by turning
  a slow win into a non-win; `avg(won)` is tabulated beside it.
* Engine `build/PGO` (PGO+LTO), **src tree `55191e63`**. The standard run was executed twice, with and
  without per-game logging, and all 60 job digests were byte-identical — a determinism check and proof
  the logging is passive.

<div style="page-break-after: always;"></div>

# Angels — −0.6473t standard, −0.5817t 2HG

Adopted 2026-09-19 after an 18-screen settling campaign. Predecessor `decks/Angels/v1-thune4-chancery/`.
The largest gain of the three decks in both formats.

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
  500,000 games, so the effect is flat rather than carried by a few blocks.
* **It closes games v1 lost outright**: non-wins fall 5.5× standard, 4.0× at 2HG. In both formats the
  penalised delta exceeds the won-only delta, the signature of "wins at all" and not only "wins sooner".
* **The one deck whose edge narrows at 2HG.** Both numbers are large and the ordering is never in doubt;
  recorded as an observation, since nothing measured identifies a cause.
* **Cost is flat** — 13.70 ms/game vs v1's 14.28 standard, 49.07 vs 48.37 at 2HG. Dearest of the three
  decks at 2HG, ~3.6× its own standard cost.

<div style="page-break-after: always;"></div>

# Critter Lifegain — −0.4006t standard, −0.4363t 2HG

Predecessor `decks/CritterLifegain/v1-thune4-basilica/`.

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

The land base swapped 3 Orzhov Basilica and 2 Plains for 4 Remote Farm — the same Remote Farm addition
all three decks made.

## How the averages changed

| format | | win% | non-wins | avg(won) | **avg(pen=9)** |
|---|---|---|---|---|---|
| **standard** | v1 | 99.8874 | 563 | 4.7653 | **4.7700** |
| | current | 99.9900 | 50 | 4.3690 | **4.3695** |
| | change | **+0.1026** | **−513** | −0.3963 | **−0.4006** |
| **2HG** | v1 | 99.7616 | 1,192 | 5.1065 | **5.1158** |
| | current | 99.9668 | 166 | 4.6781 | **4.6795** |
| | change | **+0.2052** | **−1,026** | −0.4284 | **−0.4363** |

* Block deltas **−0.3944 to −0.4047** standard, **−0.4313 to −0.4403** 2HG. The 2HG gain is slightly the
  larger of the two.
* Non-wins fall **11×** standard (563 → 50) and **7×** at 2HG (1,192 → 166). At 99.99% / 99.97% the
  current list essentially always closes by turn 8.
* **This deck costs more to play than its predecessor, in both formats**: 17.99 ms/game vs 13.33
  standard (**+35%**, units 1269 vs 913) and 42.56 vs 32.07 at 2HG (**+33%**). Four Ocelot Pride and four
  Serra Ascendant generate more board state and more decisions than the lands and Ajani copies they
  replaced. Worth knowing when sizing this deck's tier rows; it buys ~0.40–0.44 turns.

<div style="page-break-after: always;"></div>

# White Knights — −0.2184t standard, −0.3492t 2HG, and much cheaper

Predecessor `decks/WhiteKnights/v1-contender-vial/`. **This span covers two adoptions** — the deck also
passed through `v2-kinsbaile-cavalier` — so v1 → current is cumulative, consistent with it having the
most changed slots of the three.

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

## How the averages changed

| format | | win% | non-wins | avg(won) | **avg(pen=9)** |
|---|---|---|---|---|---|
| **standard** | v1 | 99.9848 | 76 | 4.3720 | **4.3727** |
| | current | 99.9950 | 25 | 4.1541 | **4.1543** |
| | change | **+0.0102** | **−51** | −0.2180 | **−0.2184** |
| **2HG** | v1 | 99.9248 | 376 | 4.9568 | **4.9599** |
| | current | 99.9742 | 129 | 4.6095 | **4.6107** |
| | change | **+0.0494** | **−247** | −0.3473 | **−0.3492** |

* Block deltas **−0.2116 to −0.2234** standard, **−0.3431 to −0.3563** 2HG.
* **The 60%-larger 2HG gain is Adeline, Resplendent Cathar** — *"whenever you attack, for each opponent,
  create a 1/1 Human"*, i.e. `attack_creates_tokens: 1` with **no** `attack_tokens_per_opponent` key, so
  it takes the database default (**true**) and makes **two** tokens per attack at `opponent_heads 2`.
  Current plays **3 against v1's 1**. (The suite's 2HG gate names it: *"knights (Adeline token per head)"*.)
* **The card traded against her does NOT scale:** Hero of Bladehold (3 → 2) sets
  `attack_tokens_per_opponent: False`. So a **head-neutral** token maker was swapped for **head-scaling**
  ones — exactly the shape that shows up as a larger 2HG gain. Both read alike from their oracle text;
  only `cards.json` distinguishes them.
* Smallest standard gain, from the strongest base: **v1 here was already the fastest v1 list** at 4.3727,
  quicker than the *current* Critter Lifegain.
* **Much cheaper too** — 5.15 ms/game vs 7.69 standard (**−33%**), **14.04 vs 28.99 at 2HG, less than
  half**. Cutting 3 Acclaimed Contender and 2 Aether Vial removed real branching (each Vial charge level
  is a decision), which pays more in longer games. The only unambiguous win on both axes here.

<div style="page-break-after: always;"></div>

# Appendix — reproducing this

The run apparatus lived under `logs/v1_vs_current/` (gitignored), so the manifest generator is here in
full — it *is* the experiment definition, and the pairing depends on `seed`/`game_index`:

```python
import json
decks = {
 "angels":       ("decks/Angels",          "Angels",          "v1-thune4-chancery"),
 "critter":      ("decks/CritterLifegain", "CritterLifegain", "v1-thune4-basilica"),
 "whiteknights": ("decks/WhiteKnights",    "WhiteKnights",    "v1-contender-vial"),
}
TWOHG  = False                       # True -> the 2HG arm
BASE   = 800000 if TWOHG else 700000
BLOCKS, PER = 10, 50000
jobs = []
for slug, (root, stem, v1) in decks.items():
    for arm, d in (("cur", root), ("v1", f"{root}/{v1}")):
        for b in range(BLOCKS):
            off = b * PER
            j = {"name": f"{slug}-{arm}_b{b}",
                 "deck": f"{d}/{stem}.cod", "profile": f"{d}/{stem}.profile.json",
                 "games": PER, "seed": BASE + off, "game_index": off,
                 "depth": 7, "budget_ms": 200}
            if TWOHG:
                j["starting_life"], j["opponent_heads"] = 30, 2
            jobs.append(j)
json.dump({"jobs": jobs}, open("run.manifest.json", "w"), indent=1)
```

A chunk covering global games `[off, off+n)` sets `seed = BASE + off` **and** `game_index = off` — the
contract in `BatchRunner.cpp`'s `Job::game_index` comment, and what makes game *i* of block *b* the same
shuffle on both arms. Get it wrong and the arms silently stop being paired.

```
# one pooled batch per format; 60 jobs, 3,000,000 games each
# standard ~20 min, 2HG ~57 min on 32 cores
build/PGO/mtg --batch run.manifest.json --game-log-dir build/scratch/v1cmp
```

Job lines give `played=/avg=/digest=/ms=/units=` per block, enough for the paired block deltas. Win rates
need the `.wins` files: `<game_index> <win_turn> <digest>`, with `win_turn <= 0` meaning no win inside
`max_turns`. Per-game logs go under `build/scratch/` (the container build volume) rather than `logs/`,
because 3M per-game records are regenerable churn and `logs/` is on the 9p host mount.

## One caveat worth stating plainly

Each arm carries **its own** keep table and value sidecar, so these numbers measure the shipped package,
not the decklist in isolation. A list change and the artifacts refitted to it are deliberately
confounded. That is the correct answer to "did the deck get better", and the wrong answer to "how much of
the gain came from the cards alone" — the second needs both lists against one shared apparatus, a
different and cheaper experiment.

The six gains are far too large for apparatus bias to explain: the smallest, White Knights at −0.2184t
standard, is ~180× its own standard error and 10/10 blocks consistent.

*Process codified as `.claude/skills/deck-update-summary.md`.*
