# V1 vs the current list — Angels, Critter Lifegain, White Knights

**500,000 games per arm per format at depth 7, budget 200 ms. All three current lists win faster than
the list they replaced — in BOTH formats, on every one of 10 paired blocks, in all six cells.**

| deck | format | v1 | current | change | t |
|---|---|---|---|---|---|
| **Angels** | standard | 5.2790 | **4.6317** | **−0.6473t** | −434.3 |
| | 2HG | 5.7095 | **5.1278** | **−0.5817t** | −362.9 |
| **Critter Lifegain** | standard | 4.7700 | **4.3695** | **−0.4006t** | −446.8 |
| | 2HG | 5.1158 | **4.6795** | **−0.4363t** | −451.3 |
| **White Knights** | standard | 4.3727 | **4.1543** | **−0.2184t** | −187.2 |
| | 2HG | 4.9599 | **4.6107** | **−0.3492t** | −288.0 |

Average turn the goldfish wins on; lower is better. 10/10 blocks better in every cell, |t| > 180
throughout — none of this is close. Standard and 2HG are treated as co-equal, not headline-plus-footnote.

**The format changes the ranking of the gains.** Angels is the only deck whose edge *narrows* at 2HG
(−0.6473 → −0.5817); Critter Lifegain's is slightly larger (−0.4006 → −0.4363); and **White Knights'
gain grows by 60%** (−0.2184 → −0.3492), which has a specific cause — see its page.

## How it was measured

* **6,000,000 games total** — 500,000 per arm × 2 arms × 3 decks × 2 formats — as **two** pooled
  batches, one per format. Standard: 19m40s. 2HG: 56m56s (2HG is ~3× dearer per game, because 30 team
  life means longer games and more board state).
* **depth 7, budget 200 ms**, `max_turns` 8, both formats. This is deliberately deeper and far more
  generous than anything the suite runs these decks at (their cases are d0/d3/d5 at budgets 0/10/20 ms),
  so the comparison is about the *lists*, not about which one copes better with a tight budget. None of
  the three sidecars sets `value_play.target_depth`, so `drives()` is false and no per-deck play-lever
  is being run off-policy — d7/b200 is simply a deeper setting, not a disabled one.
* **2HG is `starting_life 30` + `opponent_heads 2`** on the same deck and profile — the suite's own
  canonical definition (`test/regression_cases.sh`, the "2HG gate" block), not an invented one.
* **`max_turns 8` was validated for 2HG before committing the run.** 30 team life could have pushed
  games past the horizon and quietly turned the average into a non-win-rate measure. A 12,000-game
  probe returned win rates of 98.25%–100%, so the horizon is not binding and the 2HG numbers stay
  directly comparable to the standard ones.
* **Paired blocks.** Each arm is 10 blocks of 50,000 games. Block *b* of both arms covers the same
  global games `[b·50000, (b+1)·50000)` with the same base seed and `game_index`, so game *i* gets
  shuffle seed `base+off+i` and opponent-spawn index `off+i` on **both** arms. The 10 blocks are 10
  paired observations and the quoted `se` is over those blocks, which is what absorbs shuffle variance.
  Base seed 700000 for standard, 800000 for 2HG.
* **Each arm plays with its own shipped apparatus** — the profile, value sidecar and exhaustive keep
  table out of its own directory (`decks/<Deck>/` for current, `decks/<Deck>/v1-*/` for v1). That is
  the right pairing for "is the deck we ship now better than the deck we shipped then": the lists and
  the artifacts fitted to them moved together. It is explicitly *not* the apparatus-free single-card
  screen, which shares one apparatus across arms on purpose. See the appendix caveat.
* **Decision provider verified identical across arms** on all three decks (`provider=Angels`,
  `CritterLifegain`, `WhiteKnights`). This is the one asymmetry a shared apparatus cannot absorb — an
  edited decklist can cross an archetype signature and silently give the two arms different
  heuristics — so it was checked rather than assumed.
* **Averages count non-wins.** The runner's `avg` penalises a game that fails to win by turn 8 as 9,
  the same convention `ref_bench` prints — verified against the per-game `.wins` files rather than
  assumed. `avg(pen=9)` is the headline because it cannot be gamed by converting a slow win into a
  non-win; `avg(won)` (mean over won games only) is tabulated alongside it on each page.
* Engine `build/PGO` (PGO+LTO), src tree `55191e63`. The standard run was executed **twice** — once
  without and once with per-game logging — and all 60 job digests were byte-identical, which is both a
  determinism check and proof that the logging is passive. 0 abandoned games in either format; 75
  games out of 3,000,000 exceeded 30 s at 2HG.

<div style="page-break-after: always;"></div>

---

# Angels — **−0.6473t** standard, **−0.5817t** 2HG

Adopted 2026-09-19 after an 18-screen settling campaign. Predecessor archived at
`decks/Angels/v1-thune4-chancery/`. The largest gain of the three decks in both formats.

## The current list — 60 cards, 23 lands, no sideboard

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

## Cards ADDED since v1

| change | card | v1 → current | |
|---|---|---|---|
| **+4** | Lightstall Inquisitor | 0 → 4 | **new to the main deck** (was 1 in the sideboard) |
| **+4** | Lyra, Archangel of Dawn | 0 → 4 | **new to the main deck** (was 1 in the sideboard) |
| **+4** | Remote Farm | 0 → 4 | **new card** |
| **+2** | Giada, Font of Hope | 2 → 4 | increased to a 4-of |

## Cards REMOVED since v1

| change | card | v1 → current | |
|---|---|---|---|
| **−4** | Plains | 19 → 15 | reduced (land restructure) |
| **−3** | Youthful Valkyrie | 4 → 1 | reduced to a 1-of |
| **−2** | Archangel of Thune | 4 → 2 | reduced |
| **−2** | Lyra Dawnbringer | 3 → 1 | reduced to a 1-of |
| **−1** | Azorius Chancery | 1 → 0 | **cut entirely** |
| **−1** | Legion Angel | 1 → 0 | **cut entirely** (plus all 3 sideboard copies) |
| **−1** | Swords to Plowshares | 3 → 2 | reduced |

The counts above are main deck. v1 also had a **5-card sideboard** — 3 Legion Angel, 1 Lyra, Archangel
of Dawn, 1 Lightstall Inquisitor — now empty, and that is the more interesting part of the story: **the
two cards v1 kept as sideboard singletons are the two the current list promoted to 4-ofs.** Legion
Angel left entirely.

Lyra, Archangel of Dawn is capped at one instance on the battlefield by the legend rule, which is why
it is a genuinely different card from "more Lyra Dawnbringer" rather than a redundant copy — and why
both Lyras appear in the current list, at 4 and 1.

## How the averages changed

| format | | win% | non-wins | avg(won) | **avg(pen=9)** |
|---|---|---|---|---|---|
| **standard** | v1 | 99.3886 | 3,057 | 5.2561 | **5.2790** |
| | current | 99.8886 | 557 | 4.6269 | **4.6317** |
| | change | **+0.5000** | **−2,500** | −0.6293 | **−0.6473** |
| **2HG** | v1 | 98.7236 | 6,382 | 5.6669 | **5.7095** |
| | current | 99.6804 | 1,598 | 5.1153 | **5.1278** |
| | change | **+0.9568** | **−4,784** | −0.5516 | **−0.5817** |

* **Standard −0.6473t**, se 0.0015, **t −434.3**, 10/10 blocks better (block deltas −0.6424 to −0.6579,
  a spread of 0.016t across 500,000 games — the effect is flat, not driven by a few blocks).
* **2HG −0.5817t**, se 0.0016, **t −362.9**, 10/10 blocks better (−0.5753 to −0.5931).
* **It closes games v1 lost outright.** Non-wins fall 5.5× at standard (3,057 → 557) and **4.0× at 2HG**
  (6,382 → 1,598). In both formats the penalised delta is *larger* than the won-games-only delta
  (−0.6473 vs −0.6293; −0.5817 vs −0.5516), so the improvement is partly "wins at all", not only
  "wins sooner".
* **This is the one deck whose edge narrows at 2HG** (−0.6473 → −0.5817). Both numbers are large and
  the ordering of the two lists is never in doubt; stated as an observation, since nothing in the
  measurement identifies the cause.
* **Cost is flat**: 13.70 ms/game vs v1's 14.28 at standard, 49.07 vs 48.37 at 2HG. Angels is the
  dearest of the three decks at 2HG, ~3.6× its own standard cost.

<div style="page-break-after: always;"></div>

---

# Critter Lifegain — **−0.4006t** standard, **−0.4363t** 2HG

Predecessor archived at `decks/CritterLifegain/v1-thune4-basilica/`.

## The current list — 60 cards, 23 lands, no sideboard

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

## Cards ADDED since v1

| change | card | v1 → current | |
|---|---|---|---|
| **+4** | Ocelot Pride | 0 → 4 | **new card** |
| **+4** | Remote Farm | 0 → 4 | **new card** |
| **+2** | Serra Ascendant | 2 → 4 | increased to a 4-of |

## Cards REMOVED since v1

| change | card | v1 → current | |
|---|---|---|---|
| **−3** | Orzhov Basilica | 3 → 0 | **cut entirely** |
| **−2** | Ajani, Strength of the Pride | 3 → 1 | reduced to a 1-of |
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

* **Standard −0.4006t**, se 0.0009, **t −446.8**, 10/10 blocks better (−0.3944 to −0.4047).
* **2HG −0.4363t**, se 0.0010, **t −451.3**, 10/10 blocks better (−0.4313 to −0.4403). Slightly the
  larger gain of the two formats.
* Non-wins fall **11×** at standard (563 → 50) and **7×** at 2HG (1,192 → 166). At 99.99% / 99.97% the
  current list essentially always closes by turn 8.
* **This deck costs more to play than its predecessor, in both formats**: 17.99 ms/game vs 13.33 at
  standard (**+35%**, units 1269 vs 913) and 42.56 vs 32.07 at 2HG (**+33%**). The current list gives
  the search more to do — four Ocelot Pride and four Serra Ascendant generate more board state and more
  decisions than the lands and Ajani copies they replaced. Worth knowing when sizing this deck's tier
  rows; it buys ~0.40–0.44 turns.

<div style="page-break-after: always;"></div>

---

# White Knights — **−0.2184t** standard, **−0.3492t** 2HG, and much cheaper

Predecessor archived at `decks/WhiteKnights/v1-contender-vial/`. **This span covers two adoptions:** the
deck also passed through `v2-kinsbaile-cavalier`, so "v1 → current" is the cumulative effect of both,
not a single screen — which is consistent with it having the most changed slots of the three.

## The current list — 60 cards, 23 lands, no sideboard

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

## Cards ADDED since v1

| change | card | v1 → current | |
|---|---|---|---|
| **+4** | Remote Farm | 0 → 4 | **new card** |
| **+2** | Adeline, Resplendent Cathar | 1 → 3 | increased — **the 2HG story, below** |
| **+2** | Benalish Marshal | 2 → 4 | increased to a 4-of |
| **+2** | Silverblade Paladin | 1 → 3 | increased |
| **+1** | Basri, Tomorrow's Champion | 0 → 1 | **new card** |
| **+1** | Kinsbaile Cavalier | 0 → 1 | **new card** (the v2 adoption's namesake) |

## Cards REMOVED since v1

| change | card | v1 → current | |
|---|---|---|---|
| **−3** | Acclaimed Contender | 3 → 0 | **cut entirely** |
| **−3** | Plains | 22 → 19 | reduced |
| **−2** | Aether Vial | 3 → 1 | reduced to a 1-of |
| **−1** | Hero of Bladehold | 3 → 2 | reduced |
| **−1** | Lightning Greaves | 1 → 0 | **cut entirely** |
| **−1** | Valiant Knight | 1 → 0 | **cut entirely** |
| **−1** | Venerable Knight | 4 → 3 | reduced |

Thirteen changed slots, the most of the three decks — consistent with this being two adoptions stacked.

## How the averages changed

| format | | win% | non-wins | avg(won) | **avg(pen=9)** |
|---|---|---|---|---|---|
| **standard** | v1 | 99.9848 | 76 | 4.3720 | **4.3727** |
| | current | 99.9950 | 25 | 4.1541 | **4.1543** |
| | change | **+0.0102** | **−51** | −0.2180 | **−0.2184** |
| **2HG** | v1 | 99.9248 | 376 | 4.9568 | **4.9599** |
| | current | 99.9742 | 129 | 4.6095 | **4.6107** |
| | change | **+0.0494** | **−247** | −0.3473 | **−0.3492** |

* **Standard −0.2184t**, se 0.0012, **t −187.2**, 10/10 blocks better (−0.2116 to −0.2234).
* **2HG −0.3492t**, se 0.0012, **t −288.0**, 10/10 blocks better (−0.3431 to −0.3563).
* **The gain is 60% larger at 2HG, and there is a concrete reason: Adeline, Resplendent Cathar.** Her
  trigger is *"whenever you attack, **for each opponent**, create a 1/1 white Human token"*, implemented
  as `attack_creates_tokens` with the card database's opponent-count multiply left at its default
  (true). So at `opponent_heads 2` **she makes two tokens per attack instead of one**. The current list
  plays **3 Adeline against v1's 1** — tripling a card whose output doubles at 2HG. (The suite's own
  2HG gate block names exactly this: *"knights (Adeline token per head)"*.)
* **And the card it was traded against does NOT scale.** Hero of Bladehold went 3 → 2, but its
  parameters set `attack_tokens_per_opponent: False` — its oracle says "create two 1/1 ... tokens" with
  no "for each opponent" clause, so it makes two tokens at 2HG just as at standard. Adeline omits that
  param and so takes the database default (true). The current list therefore traded a **head-neutral**
  token maker for **head-scaling** ones, which is exactly the shape that shows up as a bigger 2HG gain.
* The smallest standard gain of the three, but from the strongest starting point: **v1 White Knights was
  already the fastest of the three v1 lists** at 4.3727, quicker than the *current* Critter Lifegain.
* **And it is much cheaper**: 5.15 ms/game vs v1's 7.69 at standard (**−33%**, units 379 vs 572) and
  **14.04 vs 28.99 at 2HG — less than half the cost**. Cutting 3 Acclaimed Contender and 2 Aether Vial
  removed real branching (Vial especially, since each charge level is a decision), and the effect is far
  more pronounced in the longer 2HG games. This is the one deck where the better list is also decisively
  the cheaper one — the only unambiguous win on both axes in this document.

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

A chunk covering global games `[off, off+n)` sets `seed = BASE + off` **and** `game_index = off` — that
is the contract in `BatchRunner.cpp`'s `Job::game_index` comment, and it is what makes game *i* of block
*b* the same shuffle on both arms. Get it wrong and the arms silently stop being paired.

```
# one pooled batch per format; 60 jobs, 3,000,000 games each
# standard ~20 min, 2HG ~57 min on 32 cores
build/PGO/mtg --batch run.manifest.json --game-log-dir build/scratch/v1cmp
```

The job lines give `played=/avg=/digest=/ms=/units=` per block, which is enough for the paired block
deltas. The win rates need the `.wins` files, whose format is `<game_index> <win_turn> <digest>` with
`win_turn <= 0` meaning no win inside `max_turns`. Per-game logs go under `build/scratch/` (the
container build volume) rather than `logs/`, because 3M per-game records are regenerable churn and
`logs/` is on the 9p host mount.

## One caveat worth stating plainly

Each arm carries **its own** keep table and value sidecar, so these numbers measure the shipped
package, not the decklist in isolation. A list change and the artifacts refitted to it are deliberately
confounded here. That is the correct answer to "did the deck get better", and the wrong answer to "how
much of the gain came from the cards alone" — the second question needs both lists run against one
shared apparatus, which is a different (and cheaper) experiment.

The six gains are far too large for apparatus bias to explain them: the smallest, White Knights at
−0.2184t standard, is ~180× its own standard error and 10/10 blocks consistent.
