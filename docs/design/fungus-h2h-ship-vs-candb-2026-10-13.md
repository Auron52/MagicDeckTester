# Fungus head-to-head: shipped list vs candidate-b (2026-10-13)

**Question (USER):** *"Let's test their performance first. I believe candidate-b is the one we want
to modify, but that will depend on what we find."*

**Answer: candidate-b is the better deck in SEARCHED play, and the edge is deck quality rather than
a goldfish artifact.** It costs ~1.85–2.0x the units. The shipped list is better only at d0.

## Method

ONE pooled `mtg --batch` (`logs/fungus_h2h/manifest.json`), 18 jobs, 100,000 games, 17 min wall at
**32/32 workers (100%)**, PGO+LTO binary on `src 0b33d259`.

* **NOT `deck_compare.py`.** These are two different decklists, each with its own shipped apparatus
  (profile + value leaf + keep table). deck_compare's Rule 0 deliberately shares ONE apparatus
  across arms — correct for combinations of a single card pool, wrong here: it would make one list
  play on the other's keep table. Each job names its own deck AND profile, so every sibling model
  resolves directory-relative to the list that owns it.
* **Job shape mirrors `test/regression_cases.sh`** so these numbers are comparable to committed GT:
  d0/d3 pinned with `ignore_play_profile`, d5 sets budget only and lets `value_play` drive the depth
  (verified identical `[play]` lines to the suite's, including `source=default(depth)+cli(budget)`).
* **Formats:** `std` + `2hg` are the USER's co-equal primaries (2026-10-08); `long`/40 is a
  tiebreaker **and** the bias instrument. 2HG = `starting_life 30` + `opponent_heads 2`, set
  explicitly, never inferred from life.
* **Held-out seed base 925000** — clear of every tier seed (1001/2002/3003/4004–10010) and of the
  PGO training seed (7001). The hypothesis came from the suite cells; this is not a re-read of them.
* `~se` scales the paired-diff sd (1.06) measured from the committed `gt_logs`; at n ≥ 2000 the
  deltas are 9–35 se, so nothing here turns on the exact sd.

## Result — delta = candb − ship, NEGATIVE = candidate-b faster

| format | depth | n/arm | ship | candb | delta | ~se | ~sig | units x | wall x |
|---|---|---|---|---|---|---|---|---|---|
| std | d0 | 10000 | 5.6838 | 6.2076 | **+0.5238** | 0.015 | +34.9 | — | 1.47 |
| std | d3 | 6000 | 5.3753 | **5.1967** | **−0.1786** | 0.019 | −9.2 | 1.52 | 2.21 |
| std | d5 | 5000 | 5.3720 | **5.1740** | **−0.1980** | 0.021 | −9.3 | 1.85 | 2.81 |
| 2hg | d0 | 10000 | 6.2715 | 6.7497 | **+0.4782** | 0.015 | +31.9 | — | 1.24 |
| 2hg | d3 | 6000 | 5.8562 | **5.6033** | **−0.2529** | 0.019 | −13.1 | 2.00 | 3.08 |
| 2hg | d5 | 5000 | 5.8612 | **5.6068** | **−0.2544** | 0.021 | −12.0 | 2.01 | 3.22 |
| long | d0 | 4000 | 6.7073 | 7.1665 | +0.4592 | 0.024 | +19.4 | — | 1.17 |
| long | d3 | 2000 | 6.2920 | **5.9905** | −0.3015 | 0.034 | −9.0 | 2.41 | 3.80 |
| long | d5 | 2000 | 6.3170 | **6.0115** | −0.3055 | 0.034 | −9.1 | 2.08 | 3.90 |

Consistent with the committed GT, which was the origin of the hypothesis: a paired read over
`gt_logs` (pairing on game index across the overlapping prefix, because candidate-b's suite rows are
deliberately half-sized) gave d0 **+0.568** (t +13.6), d3 **−0.244** (t −3.9), d5 **−0.300** (t −3.4).

### The bias instrument says this is NOT a goldfish exploit

`long/std` delta ratio — a gain from real deck quality keeps some of itself when the game must go
long; a gain from pure speed collapses toward 0 (Angels' suspect arms scored 0.02–0.18):

| depth | std | long | ratio |
|---|---|---|---|
| d0 | +0.5238 | +0.4592 | +0.88 |
| d3 | −0.1786 | −0.3015 | **+1.69** |
| d5 | −0.1980 | −0.3055 | **+1.54** |

Candidate-b's advantage **grows** as the game lengthens (0.198 → 0.306). That is the opposite of the
speed-exploit signature.

### The d0 reversal is a finding, not noise

d0 is the greedy policy — and greedy is also the **horizon rollout policy inside the search**. So
candidate-b wins at d3/d5 *despite* a materially worse rollout policy (+0.48–0.52). Two consequences:

1. **Headroom.** Candidate-b's searched edge is achieved with weaker rollouts; improving its greedy
   policy or its value leaf should widen the lead rather than being redundant with it.
2. **Budget sensitivity.** Its lines need search to be found, so it degrades faster as the budget
   drops. Relevant to any depth/budget retuning, and to the d0 suite rows, where it will look bad
   by construction.

The mechanism is visible in the curve: candidate-b runs **12 four/five-drops** (Mycoloth 4,
Doubling Season 4, Saproling Burst 4) and greedy sequences a top-heavy curve badly.

## Cast census — no inert slots on either side

`--games 300 --seed 931000 --depth 3 --budget-ms 10 --log-dir`, counting `CAST_SPELL` by name
(one process, not per-game invocations). **Every nonland in both lists is cast at least sometimes**,
so neither arm is flattered by a dead card, and no card in either list carries a `[bracket note]`.

Candidate-b, casts per game (300 games, 1,516 casts):

| card | copies | casts/game |
|---|---|---|
| Sporecrown Thallid | 4 | 0.84 |
| Undercellar Myconid | 4 | 0.56 |
| Utopia Mycon | 4 | 0.50 |
| Saproling Burst | 4 | 0.46 |
| Tukatongue Thallid | 2 | 0.34 |
| Beastmaster Ascension | 2 | 0.33 |
| Wild Growth | 2 | 0.31 |
| **Doubling Season** | **4** | **0.25** |
| Sol Ring | 1 | 0.24 |
| Vitaspore / Deathspore Thallid | 2 each | 0.23 each |
| Concordant Crossroads | 1 | 0.17 |
| Brightcap Badger | 1 | 0.17 |
| Psychotrope Thallid / Shroofus | 1 each | 0.13 each |
| **Mycoloth** | **4** | **0.09** |
| Slimefoot, the Stowaway | 1 | 0.07 |

Shipped, for contrast: Sporecrown 1.00, Thallid 0.82, Tukatongue 0.74, Utopia Mycon 0.66,
Beastmaster 0.63, Sporesower 0.45, Shell-Dweller 0.27, Doubling Season 0.23, **Mycoloth 0.02** (2
copies).

**The standout: Mycoloth is a 4-of that resolves in under 1 game in 10**, in a deck whose mean win
is turn 5.17. Doubling Season is a 4-of at 0.25. These are the first places to test cuts — but a cut
ladder never asks what to ADD, so any such screen must also price the replacement and test a
3rd/4th copy of a card that is already pulling (Sporecrown, Undercellar Myconid, Utopia Mycon), and
must price each sink against the CUT rather than against zero.

## Corrections to older records

* The `fungusb` suite comment claims **~14x** the incumbent's per-game cost ("measured 4.0 s/game at
  d5/b20, 2026-10-01"). On the current engine it is **1.85–2.0x in units** and 2.8–3.2x in wall.
  That comment predates a lot of perf work and should not be used for sizing.
* `fungusb` carries **no 2HG rows and no overnight rows**, and before this run the 2HG axis was
  unmeasured for BOTH lists — one of the two co-equal primary formats was simply absent. Worth
  fixing in `regression_cases.sh` if candidate-b is going to be modified and re-measured.

## Status

Nothing adopted; this is a measurement. The shipped list remains `decks/Fungus/Fungus.cod` and
candidate-b remains the adoption candidate. On this evidence **candidate-b is the list to modify**.
