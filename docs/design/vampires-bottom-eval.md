# Vampires: the bottom-evaluation setting -- the route under the 3x suite gate (2026-10-21)

**Status: MEASURED, NOT ADOPTED.** The bottom-eval setting is mulligan policy, so adopting one is the USER's
call (NivMizzetCombo question 15 is the same question for Niv).

## Why this is the lever

The suite's 3x cost gate is hard (USER 2026-10-20), and Vampires failed it at 5.80x Hinata (worst row
vampires2hg d5, 8,517 ms/game; ledger `docs/design/analysis-Vampire.md` question 45). The deck is out of the
suite, so it cannot get a keep table. With no keep table, every mulligan's bottom is chosen by playing out
every legal bottom subset as a full clairvoyant trial game at play settings (AIEngine.cpp, the subset
rollouts). Those trial games are most of the cost. The profile's `mulligan.bottom_eval_*` keys make them
cheap. The precedents are Prevent Damage (`bottom_eval_budget_ms 1`), Melira Pod (`bottom_eval_depth 0` +
`bottom_eval_topk 5`) and KittyEquipment (`bottom_eval_units 600`).

## The A/B (one pooled batch, 17,080 games; scratch `build/scratch/vampires/opt/be1/`)

Tree `1a1cf9b9e` (the landing branch). Held-out blocks A = 16,100,001, B = 16,400,001. Per arm per block:
std d3 b10 x600, std d5 b20 x300, 2HG d3 b10 x200, 2HG d5 b20 x120. Arms are paired per game against `cur`
(the current setting). Net = sum of per-game win-turn differences (positive = slower).

| arm | setting | std d3 net (units x) | std d5 net (units x) | 2HG d3 net (units x) | 2HG d5 net (units x) | total net |
|---|---|---|---|---|---|---|
| `b1` | budget_ms 1 | +1 (0.57) | +1 (0.44) | 0 (0.57) | +2 (0.46) | **+4** |
| `u600` | units 600 | +1 (0.56) | +1 (0.43) | 0 (0.54) | +2 (0.44) | **+4** |
| `d0k5` | depth 0 + top-5 refine | +3 (0.66) | +1 (0.59) | +1 (0.64) | 0 (0.66) | +5 |
| `b1k3` | budget 1 + top-3 refine | +2 (0.81) | +1 (0.61) | +1 (0.88) | +1 (0.72) | +5 |
| `d3b10` | depth 3, b10 | 0 (1.00) | 0 (0.71) | 0 (1.00) | +2 (0.67) | +2 |
| `leaf0` | search_leaf_depth 0 (not a bottoming setting) | +4 (0.81) | 0 (0.85) | +10 (0.86) | +7 (1.13) | +21 |

- Not one arm is faster in a single game: a cheaper evaluation of the bottoms can only lose against the
  full-fidelity one. `d3b10` at the d3 cells is byte-identical to `cur` (the plumbing control).
- **The same three deals account for nearly every slower game** across the cheap arms: std block B gi 161
  (seed 16400162, T4 -> T5 at d3 and d5) and 2HG d5 block A gi 50 / gi 76 (seeds 16100051 / 16100077, T4 -> T5).
  They are being root-caused.
- `leaf0` is out: it plays worse and is dearer at 2HG d5.

## What is left once bottoming is cheap

Units per game, `MTG_BOTTOM_ROLLOUTS=0` (the heuristic bottom, no trial games) vs the arms, same seeds:

| row | cur | u600 | no trials |
|---|---|---|---|
| 2HG d5 (block A) | 152,669 | 70,429 | 47,049 |
| std d5 (block B) | 86,046 | 34,359 | 21,829 |
| 2HG d3 (block A) | 92,348 | 52,666 | 32,310 |
| std d3 (block B) | 49,384 | 26,405 | 14,615 |

- With `u600`, the trial games are still about a third of the cost (2HG d5: 23k of 70k units).
- Dropping the trial games altogether costs real quality (std d5 4.383 -> 4.427 avg), so a cheap
  clairvoyant bottom is the right shape, not none.

## Against the gate (estimate; the gate itself must be re-measured on a quiet box)

Scaling the 10-19 quiet refill's worst row (vampires2hg d5 s2002, 8,517 ms/game) by `u600`'s 0.43-0.44 gives
~3,700-3,750 ms/game, ~2.5x Hinata (1,468): inside 3x with ~15% margin. `b1` scales to ~3,900-4,800 ms/game
(its ms ratio, 0.56, runs above its units ratio), which is too close to call.

## The three slower deals, root-caused (scratch `build/scratch/vampires/opt/rc/`; prototype branch `vampires-rc`)

**The cheap trial does not mis-rank.** With the turn cut off, the cheap (`u600`) and full trial scores agree on
every candidate in all three deals, except the candidates that win T4 at full settings, which cheap scores T5.
On 31 sampled std bottom decisions the cheap best turn equalled the full best turn 31 times out of 31.

**What fails is reach.** Each T4 line is a three-turn plan at the edge of the real search budget. In 2HG gi 50
the line appears at 13,500 units per decision but not at 9,000; in gi 76 it does not appear even at 13,500 of
d5 b20's 18,000; in std gi 161 it needs 4,800 units (300, 600, 1,200 and 2,400 all miss it). So every candidate
ties at T5, the cheap stage constrains nothing, and `HeuristicBottomPick` decides. With 2 lands and `min_lands 1`
no land is excess, so it bottoms the expensive spells (Stromkirk Captain; Markov Baron in gi 161), which are the
cards the T4 line needs.

| deal | cur bottoms | cheap bottoms | candidates winning T4 at full (all others T5, one T6) |
|---|---|---|---|
| std gi 161 (mull to 5) | Captain + Swamp | Captain + Baron | {Estate, Captain}, {Captain, Swamp} |
| 2HG gi 50 (mull to 6) | Swamp | Captain | Swamp, Voldaren Estate |
| 2HG gi 76 (mull to 6) | Swamp | Captain | either Swamp |

**Every lost T4 line is a ONE-LAND keep that works only because the trial knows the land draws** (gi 161 needs
the T2 Secluded Courtyard and the T4 second Legion Lieutenant for exactly 16; gi 50 keeps only Voldaren Estate and
needs Unclaimed Territory on T2 and T3; gi 76 keeps one Swamp and needs the T1 Estate draw). Every two-land keep
scores T5 even at full settings. That is the bottoming form of a lost clairvoyant win (USER 2026-10-13, Hinata
gi 27: "as clairvoyant as wins get"), which the rule says not to spend a fix on.

**Fixes measured anyway** (units vs cur; sample = be1's std d5 block B x300 + 2HG d5 block A x120):
- units 300 / 1,200 / 2,400: still miss all three; units 4,800 recovers gi 161 only.
- `u600f` (when the cheap stage leaves every card allowed, re-roll the ties at real settings): recovers all three,
  420/420 equal to cur, but 0.63x (std) / 0.72x (2HG) units -- it fires in 38% of decisions to change 3 games, and
  it puts the deck back over the gate (~4.2x by the scaling above).
- `u600p` (one real-settings probe of the unbottomed hand first): misses gi 76 (a bigger hand dilutes a finite
  budget, so the probe is not sound); 0.55x.
- A fixed top-K refine (`b1k3`, `d0k5`) misses for the same reason: 7-21 tied candidates, re-rolled by mask order.

**Recommendation:** classify the three as lost clairvoyant wins and adopt `u600` (`mulligan.bottom_eval_units
600`, KittyEquipment's setting). Its whole measured loss is those three games; it is the cheapest arm with gate
margin; and the residue disappears once the deck has a keep table (the table then decides bottoms).

## The gate with `u600` (2026-10-21; full regression tier, Vampires re-admitted locally)

The box was CONTENDED (another container; load 19 -> 34 during the run), and the inflation was uneven -- the d5
rows ran first at ~34-40 ms per 1,000 units, the d3 rows later at ~80 -- so the run's raw `suite_gate --cost`
(0.85x) is not trustworthy, and its cache refill was NOT kept. Units are deterministic, so the quiet-equivalent
cost is units x each row's QUIET ms-per-unit from the 10-19 refill (~30-34 ms per 1,000 units for both decks):

| row | units/game before -> u600 | quiet-equivalent ms/game |
|---|---|---|
| vampires2hg d5 s2002 (the worst row) | 249,294 -> 100,107 (0.40x) | ~3,420 |
| vampires2hg d3 s2002 | 141,114 -> 68,986 | ~2,150 |
| vampires d5 s3003 | 124,393 -> 44,580 | ~1,350 |
| hinata d5 s3003 (the reference row, today's engine) | 46,984 -> 64,252 (engine moved) | ~2,010 |

Worst Vampires row / Hinata = **~1.7x** on today's engine (~2.3x against the 10-19 reference of 1,468 ms): inside
3x either way. Every other deck in the run was byte-identical (searched and d0: 0 play changes). The official
number is owed on a quiet box (`suite_gate.py --measure-all` off one full regression run, also question 46).
