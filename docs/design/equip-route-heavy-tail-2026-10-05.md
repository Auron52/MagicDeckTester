# The equip route's heavy tail: 1.19% of kittyv2 games are 63% of the compute (2026-10-05)

Status: OPEN -- measured, not diagnosed. Recorded during the KittyEquipment v2 card-swap screen so
the next run can be projected honestly and the tail can be profiled when the box is free.

## What was measured

The swap screen (`scripts/deck_compare.py logs/kittyv2_swaps/spec_all.json`: 17 arms x 16,000
games, d5 b20, keep table + value sidecar attached, Equipment provider pinned) was projected from a
300-game probe that ran at ~32 games/s on 32 threads (600 games in 19 s, ~0.65 s of per-game wall).
The real batch runs at **8-13 games/s** -- a third of the projection -- because of a tail the probe
never sampled:

* `[batch] heartbeat: 32/32 workers busy (100%)` -- not a scheduling problem; every core is busy.
* In the first 7,000 games, 76 `SLOW-GAME` lines (1.1%): 100-230 s of wall each, `units` 150k-250k
  against a mean of ~18k per game (the probe's 300 games: 5.44M units / 300). That is 14x the mean
  SEARCH WORK, not wall inflation: load was 33 on 32 cores.
* The heartbeat's slowest in-flight game at 10 min: **0.16 h (9.6 minutes) for ONE game** (base
  gi=117). At 45 min: **0.66 h (40 minutes, still running) for base gi=892** -- repro
  `--seed 930892 --game-index 892 --games 1` under the same profile. A single d5 b20 game of a
  60-card goldfish deck has no business taking forty minutes; whatever regime is unbounded here is
  the first thing to profile.
* 76 games x ~130 s ~= 10,000 thread-seconds out of ~21,000 available in that window: **the tail
  is about half of all the work**.

### The full census, 56,085 games in (2026-10-05 12:37 UTC, 1 h 49 m of batch, 32 workers)

| | |
|---|---|
| games over the 30 s `SLOW-GAME` threshold | 667 of 56,085 = **1.19%** |
| their wall, summed | **131,691 thread-s of the 209,344 available = 62.9%** |
| a slow game's wall | p50 **72 s**, p90 **459 s**, p99 **1,852 s**, max **3,852 s** (64 min, `base` gi=892) |
| a slow game's `units` | p50 **132,614**, p90 **250,350**, max **693,099** |
| a NON-slow game's wall | **1.40 s** (and ~18k units) |

So the distribution is not merely skewed, it is **dominated by its tail**: 1.19% of the games buy
63% of the compute, and the median slow game is already 51x the median game's wall and 7x its work.
Without the tail this 272,000-game screen would be a **3.3 h** job instead of **~8.8 h**.

The tail rate is close to uniform across arms (209 / 189 / 150 / 119 slow games over the first four
arms' dispatch, ~1.2% each), so it is a property of the DECK plus the route, not of any one swap.
`base` holds the worst single game, so it is not caused by an introduced card either. One arm's
*summed* tail seconds can still differ by 2x from another's (56,124 s on base vs 38,469 s on
greaves) because the sum is driven by a handful of p99 games -- which is also why a per-arm wall
comparison out of this batch would be meaningless.
* It is nondeterministic per game. `base gi=59` took 138 s in the batch and was not slow at all in
  the 300-game probe that played the same seed and index -- the documented thread-dependent search
  path of this deck (`docs/design/kittyv2-batch-nondeterminism-2026-10-04.md`): the same game
  reaches the expensive walk on one run and not on the next.

## One hypothesis checked and REJECTED: the plan-cache pool is not the cause

The heartbeat shows the plan cache's global pool at its ceiling --
`plan=1110M/3199M(of 3200M)` (used / hiwater / budget) -- and the pool's own comment names exactly
the symptom a saturated pool produces: *"pool full forever => recompute everywhere => a uniform
silent slowdown with BYTE-IDENTICAL play. Nothing looks broken."* So it is a tempting explanation,
and it is the wrong one.

**It cannot be, because `units` are pool-invariant by a verified contract.** A refused store just
recomputes, and `plancache::BudgetBytes`' comment records that digest AND total units were verified
byte-identical at unbounded / 256 MB / 64 MB (TurnSolver.cpp ~48278-48307). The tail's defining
feature is **7x to 38x the units of a normal game**, i.e. genuinely more search, which no cache
decision can manufacture. A saturated pool can only inflate ms per unit on top of a tail that is
already there.

Two things do follow, though. The pool reaching its cap on this deck is worth knowing in its own
right (it means typical workers are recomputing while one monster holds GBs, which is the exact
trade the global pool was built to allow), and `MTG_PLAN_CACHE_KB` gives a free, result-neutral
knob to test how much of the tail's WALL is recompute: re-run one slow repro at a larger budget and
compare ms at identical units. Units identical plus ms lower = pool pressure; both unchanged = pure
search.

## Where the work most likely is

Under a 20 ms decision budget a 5-turn game should cost on the order of a few thousand units. The
regimes that are NOT bounded by the decision budget, by design, are the equip route's **Apply
replay** (the executor re-walks the proven kill with no cap and no budget cut -- wave 0 first, then
the full tranche menu if wave 0 finds nothing) and the **noroot** regime (the executor's committed
apply re-solving a line from a frame with no decision scope open, kept complete for the census).
The 2026-10-03 lesson in `RunSearch` records exactly this shape: one depth-0 game spent 148.9 s of
152 s in a 10-link replay re-enumerating every dropped group of a 30-card hand, which the wave-0
walk cut to 54 ms. A replay that falls through wave 0 into the tranches on a wide hand is the
obvious candidate for a 200 s game. Unverified: the batch could not be profiled while it was the
only thing allowed on the box.

## Why this is not just a cost line

* **It is the biggest single perf item this deck has.** Removing the tail is worth **2.7x** on a
  screen of this shape. Every collapse adopted on kittyv2 so far measured between 0.88x and 0.98x
  (`kitty-wall-is-not-search`); this one item outweighs all five together, and unlike them it is a
  suspected *defect* (an unbounded regime re-enumerating a proven line), not a tuning gain.

* **Projection.** A 300-game probe cannot see a 1% tail; projecting a 272k-game run from it was
  wrong by 3x (`launch-against-the-projection`). Probe at least ~2,000 games, or read the
  `SLOW-GAME` lines of the first ten minutes of the real batch and re-project from them.
* **Determinism.** A tail that fires or not per run is the same nondeterminism that makes this
  deck's pooled digest irreproducible; whatever makes the search path thread-dependent also makes
  the cost per game a lottery.
* **Completeness is the user's ruling**: the apply must reproduce the trial's proof, so capping the
  replay is not the fix. The fix is finding why the wave-0 walk fails on these boards (the trial
  proved the kill on some menu; the apply walks wave 0 first -- if the proof needed a tranche
  branch, every such game pays the full tree) and whether the trial's menu and the apply's are
  really the same (the lockstep law).

## How to diagnose, when the box is free

1. Replay one `SLOW-GAME` repro at `--threads 1` several times (`build/Release/mtg
   decks/KittyEquipment/v2-puresteel-hammer/KittyEquipment.cod --seed 930117 --game-index 117
   --games 1 --depth 5 --budget-ms 20 --profile logs/kittyv2_swaps/alias/KittyEquipment.profile.json`)
   and see whether the cost reproduces at one thread at all.
2. If it does, `MTG_EQUIP_ROUTE_TRACE=1` on that game and read the `SEARCH ... (regime; N links
   applied, ... ms)` lines: the regime and link count of the expensive walks say which of the two
   unbounded regimes it is and how wide the tree was.
3. If it does not reproduce at one thread, it is the nondeterminism note's problem first.
4. Separate SEARCH from CACHE PRESSURE on whichever repro does reproduce: run it twice, once with
   `MTG_PLAN_CACHE_KB` large enough that the pool never refuses. `units` must come back identical
   (the contract); whatever ms falls was recompute, and whatever remains is the real tail.
5. Only then profile. The repo's own 2026-10-03 case closed on reading the regime and link count of
   the expensive walk, not on a profiler.

## One more reason to care, recorded 2026-10-05

The 64-minute game is `base`, so this is not something a candidate card introduced -- the SHIPPED
list has games like this, and at the shipped play point (d5 b20). Any future measurement on this
deck that is sized from a mean will be wrong in the same direction, including the regression suite's
own cost accounting: a tier whose budget assumes ~1.4 s games will blow it whenever one of these
lands, and a 1.19% rate means a 1,000-game case expects about twelve of them.
