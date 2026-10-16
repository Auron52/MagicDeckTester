# V1 vs the current list — Angels, Critter Lifegain, White Knights

**500,000 games per arm at depth 7, budget 200 ms. All three current lists win faster than the list
they replaced, on every one of 10 paired blocks.**

| deck | v1 | current | change | win% v1 → current |
|---|---|---|---|---|
| Angels | 5.2790 | **4.6317** | **−0.6473t** | 99.3886 → 99.8886 |
| Critter Lifegain | 4.7700 | **4.3695** | **−0.4006t** | 99.8874 → 99.9900 |
| White Knights | 4.3727 | **4.1543** | **−0.2184t** | 99.9848 → 99.9950 |

Average turn the goldfish wins on; lower is better. Every change is 10/10 blocks better with
|t| > 180, so none of this is close.

## How it was measured

* **3,000,000 games total** — 500,000 per arm × 2 arms × 3 decks — as ONE pooled batch
  (`logs/v1_vs_current/run.manifest.json`), 19m40s wall on 32 cores.
* **depth 7, budget 200 ms**, `max_turns` 8. This is deliberately deeper and far more generous than
  anything the suite runs these decks at (their cases are d0/d3/d5 at budgets 0/10/20 ms), so the
  comparison is about the *lists*, not about which one copes better with a tight budget. None of the
  three sidecars sets `value_play.target_depth`, so `drives()` is false and no per-deck play-lever is
  being run off-policy — d7/b200 is simply a deeper setting, not a disabled one.
* **Paired blocks.** Each arm is 10 blocks of 50,000 games. Block *b* of both arms covers the same
  global games `[b·50000, (b+1)·50000)` with the same base seed 700000 and `game_index`, so game *i*
  gets shuffle seed `700000+off+i` and opponent-spawn index `off+i` on **both** arms. The 10 blocks
  are 10 paired observations and the quoted `se` is over those blocks, which is what absorbs shuffle
  variance.
* **Each arm plays with its own shipped apparatus** — the profile, value sidecar and exhaustive keep
  table out of its own directory (`decks/<Deck>/` for current, `decks/<Deck>/v1-*/` for v1). That is
  the right pairing for "is the deck we ship now better than the deck we shipped then": the lists and
  the artifacts fitted to them moved together. It is explicitly *not* the apparatus-free single-card
  screen, which shares one apparatus across arms on purpose.
* **Decision provider verified identical across arms** on all three decks (`provider=Angels`,
  `CritterLifegain`, `WhiteKnights`). This is the one asymmetry a shared apparatus cannot absorb — an
  edited decklist can cross an archetype signature and silently give the two arms different
  heuristics — so it was checked rather than assumed.
* **Averages count non-wins.** The runner's `avg` penalises a game that fails to win by turn 8 as 9,
  the same convention `ref_bench` prints. Both forms are given per deck: `avg(pen=9)` is the headline
  number because it cannot be gamed by turning a slow win into a non-win; `avg(won)` is the mean over
  won games only.
* Engine `build/PGO` (PGO+LTO), src tree `55191e63`, repo at `cb69d73ec`. The whole 3M-game run was
  executed **twice** — once without and once with per-game logging — and all 60 job digests were
  byte-identical, which is both a determinism check and proof the logging is passive.

<div style="page-break-after: always;"></div>

---

# Angels — **−0.6473t**, the largest of the three

Adopted 2026-09-19 after an 18-screen settling campaign. Predecessor archived at
`decks/Angels/v1-thune4-chancery/`.

## The current list (60 cards, 23 lands, no sideboard)

| n | card | | n | card |
|---|---|---|---|---|
| 15 | Plains | | 3 | Unexpectedly Absent |
| 4 | Bishop of Wings | | 2 | Archangel of Thune |
| 4 | Giada, Font of Hope | | 2 | Serra the Benevolent |
| 4 | Lightstall Inquisitor | | 2 | Swords to Plowshares |
| 4 | Lyra, Archangel of Dawn | | 1 | Lightning Greaves |
| 4 | Remote Farm | | 1 | Lyra Dawnbringer |
| 4 | Resplendent Angel | | 1 | Sol Ring |
| 4 | Righteous Valkyrie | | 1 | Youthful Valkyrie |
| 4 | Seraph Sanctuary | | | |

## How the averages changed

| | win% | non-wins | avg(won) | **avg(pen=9)** |
|---|---|---|---|---|
| v1 | 99.3886 | 3,057 | 5.2561 | **5.2790** |
| current | 99.8886 | 557 | 4.6269 | **4.6317** |
| change | **+0.5000** | **−2,500** | −0.6293 | **−0.6473** |

* **−0.6473t**, se 0.0015, **t −434.3**, **10/10 blocks better**. Block deltas run −0.6424 to −0.6579
  — a spread of 0.016t across 500,000 games, i.e. the effect is flat, not driven by a few blocks.
* It also **closes games it used to lose outright**: non-wins fall from 3,057 to 557, a 5.5× cut. Note
  the penalised delta (−0.6473) is *larger* than the won-games-only delta (−0.6293); the improvement is
  not only "wins sooner" but "wins at all" more often.
* **No cost penalty** — 13.70 ms/game against v1's 14.28, so the stronger list is marginally *cheaper*
  to play (units 1208 vs 1128, i.e. slightly more search charged but less real time).

## What changed from v1

| | card | v1 → current |
|---|---|---|
| **+4** | Lightstall Inquisitor | 0 → 4 |
| **+4** | Lyra, Archangel of Dawn | 0 → 4 |
| **+4** | Remote Farm | 0 → 4 |
| **+2** | Giada, Font of Hope | 2 → 4 |
| −1 | Azorius Chancery | 1 → 0 |
| −1 | Legion Angel | 1 → 0 |
| −1 | Swords to Plowshares | 3 → 2 |
| −2 | Archangel of Thune | 4 → 2 |
| −2 | Lyra Dawnbringer | 3 → 1 |
| −3 | Youthful Valkyrie | 4 → 1 |
| −4 | Plains | 19 → 15 |

The counts above are **main deck**. v1 also had a 5-card sideboard — 3 Legion Angel, 1 Lyra, Archangel
of Dawn, 1 Lightstall Inquisitor — which is now empty, and that is the more interesting part of the
story: **the two cards v1 kept as sideboard singletons are the two the current list promoted to
4-ofs.** Lightstall Inquisitor and Lyra, Archangel of Dawn went from 1 in the side to 4 in the main
deck each, while Legion Angel left entirely (its 1 main copy and all 3 side copies).

Lyra, Archangel of Dawn is capped at one instance on the battlefield by the legend rule, which is why
it is a genuinely different card from "more Lyra Dawnbringer" rather than a redundant copy — and why
both Lyras appear in the current list at 4 and 1.

<div style="page-break-after: always;"></div>

---

# Critter Lifegain — **−0.4006t**

Predecessor archived at `decks/CritterLifegain/v1-thune4-basilica/`.

## The current list (60 cards, 23 lands, no sideboard)

| n | card | | n | card |
|---|---|---|---|---|
| 19 | Plains | | 3 | Unexpectedly Absent |
| 4 | Ajani's Pridemate | | 2 | Archangel of Thune |
| 4 | Ocelot Pride | | 2 | Heliod, Sun-Crowned |
| 4 | Remote Farm | | 1 | Ajani, Strength of the Pride |
| 4 | Serra Ascendant | | 1 | Daxos, Blessed by the Sun |
| 4 | Soul Warden | | 1 | Ranger-Captain of Eos |
| 4 | Soul's Attendant | | | |
| 4 | Voice of the Blessed | | | |
| 3 | Auriok Champion | | | |

## How the averages changed

| | win% | non-wins | avg(won) | **avg(pen=9)** |
|---|---|---|---|---|
| v1 | 99.8874 | 563 | 4.7653 | **4.7700** |
| current | 99.9900 | 50 | 4.3690 | **4.3695** |
| change | **+0.1026** | **−513** | −0.3963 | **−0.4006** |

* **−0.4006t**, se 0.0009, **t −446.8**, **10/10 blocks better**. Block deltas −0.3944 to −0.4047.
* Non-wins fall **11×**, 563 → 50. At 99.99% the current list essentially always closes by turn 8.
* **This one costs more to play**: 17.99 ms/game against v1's 13.33, **+35%**, and units 1269 vs 913
  (+39%). The current list gives the search more to do — four Ocelot Pride and four Serra Ascendant
  generate more board state and more decisions than the lands and Ajani copies they replaced. Worth
  knowing when sizing this deck's tier rows, but it buys 0.40 turns.

## What changed from v1

| | card | v1 → current |
|---|---|---|
| **+4** | Ocelot Pride | 0 → 4 |
| **+4** | Remote Farm | 0 → 4 |
| **+2** | Serra Ascendant | 2 → 4 |
| −1 | Auriok Champion | 4 → 3 |
| −2 | Ajani, Strength of the Pride | 3 → 1 |
| −2 | Archangel of Thune | 4 → 2 |
| −2 | Plains | 21 → 19 |
| −3 | Orzhov Basilica | 3 → 0 |

The land base swapped 3 Orzhov Basilica and 2 Plains for 4 Remote Farm — the same Remote Farm
addition all three decks made.

<div style="page-break-after: always;"></div>

---

# White Knights — **−0.2184t**, and 33% cheaper

Predecessor archived at `decks/WhiteKnights/v1-contender-vial/`. **Note this spans two adoptions:**
the deck also passed through `v2-kinsbaile-cavalier`, so "v1 → current" is the cumulative effect of
both, not a single screen.

## The current list (60 cards, 23 lands, no sideboard)

| n | card | | n | card |
|---|---|---|---|---|
| 19 | Plains | | 2 | Hero of Bladehold |
| 4 | Accorder Paladin | | 1 | Aether Vial |
| 4 | Benalish Marshal | | 1 | Basri, Tomorrow's Champion |
| 4 | Dauntless Bodyguard | | 1 | Kinsbaile Cavalier |
| 4 | Knight Exemplar | | 1 | Sol Ring |
| 4 | Remote Farm | | 1 | Swords to Plowshares |
| 4 | Worthy Knight | | 1 | Unexpectedly Absent |
| 3 | Adeline, Resplendent Cathar | | | |
| 3 | Silverblade Paladin | | | |
| 3 | Venerable Knight | | | |

## How the averages changed

| | win% | non-wins | avg(won) | **avg(pen=9)** |
|---|---|---|---|---|
| v1 | 99.9848 | 76 | 4.3720 | **4.3727** |
| current | 99.9950 | 25 | 4.1541 | **4.1543** |
| change | **+0.0102** | **−51** | −0.2180 | **−0.2184** |

* **−0.2184t**, se 0.0012, **t −187.2**, **10/10 blocks better**. Block deltas −0.2116 to −0.2234.
* The smallest gain of the three, but from the strongest starting point — v1 White Knights was already
  the fastest of the three v1 lists at 4.3727, quicker than the *current* Critter Lifegain.
* **And it is substantially cheaper**: 5.15 ms/game against v1's 7.69, a **33% reduction**, with units
  379 vs 572 (−34%). Cutting 3 Acclaimed Contender and 2 Aether Vial removed real branching — Vial
  especially, since each charge level is a decision — so this is the one deck where the better list is
  also clearly the cheaper one.

## What changed from v1

| | card | v1 → current |
|---|---|---|
| **+4** | Remote Farm | 0 → 4 |
| **+2** | Adeline, Resplendent Cathar | 1 → 3 |
| **+2** | Benalish Marshal | 2 → 4 |
| **+2** | Silverblade Paladin | 1 → 3 |
| **+1** | Basri, Tomorrow's Champion | 0 → 1 |
| **+1** | Kinsbaile Cavalier | 0 → 1 |
| −1 | Hero of Bladehold | 3 → 2 |
| −1 | Lightning Greaves | 1 → 0 |
| −1 | Valiant Knight | 1 → 0 |
| −1 | Venerable Knight | 4 → 3 |
| −2 | Aether Vial | 3 → 1 |
| −3 | Acclaimed Contender | 3 → 0 |
| −3 | Plains | 22 → 19 |

Thirteen changed slots, the most of the three decks — consistent with this being two adoptions stacked
rather than one.

<div style="page-break-after: always;"></div>

---

# Appendix — reproducing this

The run apparatus lived under `logs/v1_vs_current/` (gitignored), so the manifest generator is
reproduced here in full — it *is* the experiment definition, and the pairing depends on getting
`seed`/`game_index` right:

```python
import json
decks = {
 "angels":       ("decks/Angels",          "Angels",          "v1-thune4-chancery"),
 "critter":      ("decks/CritterLifegain", "CritterLifegain", "v1-thune4-basilica"),
 "whiteknights": ("decks/WhiteKnights",    "WhiteKnights",    "v1-contender-vial"),
}
BASE, BLOCKS, PER = 700000, 10, 50000
jobs = []
for slug, (root, stem, v1) in decks.items():
    for arm, d in (("cur", root), ("v1", f"{root}/{v1}")):
        for b in range(BLOCKS):
            off = b * PER
            jobs.append({"name": f"{slug}-{arm}_b{b}",
                         "deck": f"{d}/{stem}.cod", "profile": f"{d}/{stem}.profile.json",
                         "games": PER, "seed": BASE + off, "game_index": off,
                         "depth": 7, "budget_ms": 200})
json.dump({"jobs": jobs}, open("run.manifest.json", "w"), indent=1)
```

A chunk covering global games `[off, off+n)` sets `seed = BASE + off` **and** `game_index = off` —
that is the contract in `BatchRunner.cpp`'s `Job::game_index` comment, and it is what makes game *i*
of block *b* the same shuffle on both arms. Get it wrong and the arms silently stop being paired.

```
# one pooled batch, 60 jobs, 3,000,000 games (~20 min on 32 cores)
build/PGO/mtg --batch run.manifest.json

# ... plus per-game win turns, for the win rates (play is byte-identical either way)
build/PGO/mtg --batch run.manifest.json --game-log-dir build/scratch/v1cmp
```

The job lines give `played=/avg=/digest=/ms=/units=` per block, which is enough for the paired block
deltas. The win rates need the `.wins` files, whose format is `<game_index> <win_turn> <digest>` with
`win_turn <= 0` meaning no win inside `max_turns`. Per-game logs go under `build/scratch/` (the
container build volume) rather than `logs/`, because 3M per-game records are regenerable churn and
`logs/` is on the 9p host mount.

## One caveat worth stating plainly

Each arm carries **its own** keep table and value sidecar, so these numbers measure the shipped
package, not the decklist in isolation. A list change and the artifacts refitted to it are
deliberately confounded here. That is the correct answer to "did the deck get better", and the wrong
answer to "how much of the gain came from the cards alone" — the second question needs both lists run
against one shared apparatus, which is a different (and cheaper) experiment.

The three gains are far too large for apparatus bias to explain them: the smallest, White Knights at
−0.2184t, is ~180× its own standard error and 10/10 blocks consistent.
