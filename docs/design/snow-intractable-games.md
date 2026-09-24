# Snow's intractable games: a third of the deck's population cannot be played at searched depth

Status: **OPEN**. The Snow value-leaf depth matrix was cancelled at 11% on 2026-09-24 because it
projected ~150 h of wall clock, and the reason is this. Everything below is measured from the
cancelled run's own artifacts; nothing here was re-derived or re-run.

This doc is the POPULATION-level record. The MECHANISM is already root-caused in
`snow-breakpoint-degeneracy.md` (2.95M breakpoint consultations per game from the deck's
card-to-hand engine) and is not repeated here.

## The number

| seed | offsets reached | skipped | intractable |
|---|---:|---:|---:|
| 8008 | 75 | 26 | 34.7% |
| 9009 | 75 | 25 | 33.3% |
| 11011 | 75 | 25 | 33.3% |
| 10010 | 50 | 13 | 26.0% |
| **total** | **275** | **89** | **32.4%** |

"Intractable" = the pool abandoned the game at its per-game ceiling, so it banked no result. It was
32.0% at 200 offsets reached and 32.4% at 275, so this is not a small-sample artifact. The deepest
arm is worse: **H5 alone is 34.5%**.

Overlap across seeds is at chance (25 of 57 offsets intractable in more than one seed, against ~19
expected at p=0.32 over four seeds), so the blow-up is a property of the individual shuffled game,
not of the offset. There is no index arithmetic to fix.

## What it cost

`logs/snowopt/slowgames/` holds the ledger: `observations.tsv` (one row per slow game, with the
repro line the pool printed) and `by_game.tsv` (rolled up per distinct game), beside raw copies of
`slow_games.log`, `matrix.txt.skipped.json`, `matrix.txt.cells.json` and `matrix.log`.

* 1,391 slow-game observations over **227 distinct games**
* **419.6 core-h — 78% of the run's entire CPU spend** (541 core-h total: 271 banked + 270 wasted)
* **64.4% of that was abandoned**, i.e. banked nothing

The multiplier that makes this so expensive is structural: **the same game is attempted once per
cell**, 9-13 times, and abandons in most of them. The worst offenders:

| seed | gi | attempts | abandoned | total | worst | repro |
|---|---:|---:|---:|---:|---:|---|
| 11011 | 7 | 11 | 7 | 15.70 h | 3.50 h | `--seed 11018 --game-index 7 --games 1` |
| 11011 | 26 | 11 | 2 | 11.19 h | 3.36 h | `--seed 11037 --game-index 26 --games 1` |
| 11011 | 42 | 10 | 3 | 9.99 h | 3.50 h | `--seed 11053 --game-index 42 --games 1` |
| 11011 | 10 | 12 | 6 | 9.27 h | 3.50 h | `--seed 11021 --game-index 10 --games 1` |
| 9009 | 34 | 9 | 7 | 8.02 h | 2.00 h | `--seed 9043 --game-index 34 --games 1` |

3.50 h is exactly `max_game_wall_sec`; 2.00 h is the `max_game_predict_sec` cut. Seed 11011's games
pin the hard cap repeatedly while 9009/10010 stop at the predictive one.

**The caps bound a GAME, not the run.** `never_condemn_depth: 5` exempts every H cell from
condemnation by design (`BatchRunner.cpp`), and the skip list makes `target` GROW (`target() =
args.target + len(skiplist)`), so each abandonment both wastes its own wall clock and pushes the
finish line further out.

## Why the matrix cannot simply be made cheaper

The obvious lever is fewer games in the expensive cells -- `valueleaf_depth_matrix.py` even declares
`--hgames-depth D:G` for exactly this, with help text naming the combo-deck case. **It does not
work, for two independent reasons.**

1. The flag is dead code in the incremental path. `args.hgames`, `args.vgames` and `hgames_depth`
   are declared and never read; `target()` uses `args.target` alone.
2. More fundamentally, **400 games/cell is not padding -- it is the minimum that resolves the
   tolerance the derivation uses.** `valueleaf_table_to_metadata.py` reports its own resolution as
   `3/n` over the PAIRED game set: 0.01775 at n=169, against `tol=0.0020`. The full target of
   400 x 4 seeds = 1,600 gives 3/1600 = 0.001875, just under tol. Cutting games does not buy a
   cheaper verdict, it buys no verdict.

So the cost is not a scheduling or configuration defect. It is the sample size the question needs,
multiplied by a population where a third of the draws never terminate.

## What the partial table already says

Paired over the common game set at 11% completion (`trust=UNSET`):

```
H:  5.7799     —     5.7614   5.7733   5.7733
V:  5.9929  5.8692  5.8009  5.8087  5.7733  5.7733  5.7683  5.7683
```

H3 is the best cell in the table and the value leaf never beats it; the derivation flags
`H3->H4 WORSENS by +0.0119` as non-monotonic. This is consistent with the independent 2026-09-20
shape probe (6,000 games: leafless `fit_nl` 1.160x cost, z=+2.35 BETTER play) and with this
pipeline's own phase E (staged model −0.005 at t=−6.5 for 1.25x = a trade, sidecar never installed).

It is **formally inconclusive** at tol=0.0020 -- every measured gap is inside 0.012, well under the
0.01775 resolution. The open question for a human is whether a leaf-necessity verdict needs
0.002-turn precision at all, given three independent measurements all pointing at "the leaf buys
this deck nothing".

## Causes RULED OUT (do not re-investigate)

Beyond the three in `snow-breakpoint-degeneracy.md` (plan-cache ratchet, mana-side enumeration,
prefix-scoped prepay), one more was proposed and killed on 2026-09-23:

* **The cleanup discard is NOT a cost.** Priced with `ShedStats::CostScope` at **0.011%-0.11% of
  CPU** across budgeted play and two unbudgeted tail games (0.160 s of 145.05 s; 0.179 s of
  473.85 s; 0.017 s of 148.85 s). All three searched forms are inert on this deck: the out-of-band
  trial-per-candidate pass is retired by `MTG_DISCARD_NODE`, and the in-search axis needs
  `CleanupDiscardSearchWidth() > 1` against a default of 1.
  The deck DOES shed constantly in branches it does not follow -- 138,331 in-search cleanups against
  1 real shed per 50 games, at hand sizes 8/9/10 -- so a real-play census answers the wrong question
  here. That reach is why the discard RULE was still worth authoring (see
  `SnowProvider::CleanupDiscardCandidates`); it is a play-quality hook, not a performance one.

## What has not been tried

* **A keep table.** Lookahead bottoming is 37-48% of Snow's CPU in shipped play and was 75.7% of one
  tail game from a single decision. Snow is the only suite deck with no exhaustive keep table, so
  every bottoming decision falls through to a clairvoyant rollout. This retires the largest single
  cost AND closes the standing 2026-09-14 directive ("I don't want to use lookahead bottoming for
  anything"). The complication is ordering: the table is an output of the mulligan stage, which runs
  after the value leaf and inherits this same 32% population, so it needs the feasibility pre-check
  in `mulligan-profile.md` before it gets the box.
* **Shipping `leaf: none`.** Would make the V arm's 571 core-h moot and stop phase A ever running
  again. StompySurprise and Goblins already ship this shape; note their sidecars still carry a real
  `eval_model` and a full `value_leaf_table`, so it is a `value_play` setting, not an absent file.
* **Truncate-at-emission**, the named-but-unbuilt fix from `bp-node-partition.md` for the 1,192,267
  cross-bucket duplicate children (~10% of node units).

---

# ROOT CAUSE, measured 2026-09-24

The sections above are the ledger. This is the diagnosis, taken after the USER asked for the cause
to be root-sourced with profiling rather than inferred. It is three stacked mechanisms, none of
which is "Snow is a hard deck to play" -- the intractable games are ordinary turn-6 and turn-7 wins.

**Tooling correction first, because it is why nobody had profiled this.** `perf` is NOT broken in
this container. It fails *only* when writing `perf.data` onto the repo bind mount
(`/workspaces/...`), which returns EFAULT -- "failed to write perf data, error: Bad address" --
before the workload starts. The same command with `-o /tmp/...` records and symbolises fine.

## 1. Mana payment degrades superlinearly in the number of SOURCES (~50% of runtime)

Flat profile of s11011 gi26 (d3, unbudgeted, the ledger's 2nd-worst game): `TapForCostBacktrackWorker`
and its lambda are **28.7% of all samples**, and the wider payment family (`TapForCostShared*`,
`ColorFeasibility::Payable`, `SourceMaxNetLive`, `PermanentManaYield`, `ManaPool::CanPayFlat/Add`,
the `TapBacktrackMemoHash` table) is **~50%**.

| | control game (9 perms, 7 sources) | gi26 (18 perms, 13 sources) |
|---|---|---|
| payment questions | 10,410 | 1,381,215 |
| backtracker nodes per question | **3.2** | **251.5** |
| ...on *payable* questions | 4.2 | **367.6** |
| greedy solves it outright | 99.9% | 85.0% |
| total backtracker nodes | 32,969 | **347,351,346** |

UNPAYABLE costs reject in 1.0 nodes on BOTH boards. So this is not hard mana -- the USER's point,
and the data agrees: every one of those nodes is spent finding an answer that exists and is easy.

**Partly fixed (650cc8ba).** `MTG_MANA_CACHE_CANON` now defaults ON: nodes 347.4M -> 141.3M (-59%),
hit rate 80.5% -> 90.3%, 86.2s -> 69.3s, byte-identical. Levers TESTED AND REFUTED here, do not
retry blind: `MTG_FLOW_ORDER` (-4.8% nodes, -2.4% wall -- not the 13.6x it gave elsewhere) and
`MTG_TAP_TRIM` (inert). The remaining term is question VOLUME (684k vs 10k), i.e. search breadth.

## 2. The breakpoint wave is EXHAUSTIVE under an unlimited budget

`BpWavesHere` (TurnSolver.cpp) returns true unconditionally when `budget->Unlimited()`, and the wave
loop's only exit is `!Unlimited() && Exhausted()`. `BpWaveMode`'s own comment: *"MTG_BP_WAVES=1
DEFAULT: waves run while the node's budget allows; unlimited => exhaustive."* The matrix runs every
cell at `budget_ms: 0`. Snow's SHIPPED settings never do -- `regression_cases.sh` is d3/b10 and
d5/b20 -- and the wave's adoption (abdecb42) measured its gains entirely on budgeted tiers.

`MTG_BP_WAVE_PROBE` on gi26: **`scored=5,128,800` continuation variants, `improved=2`,
`max-rank=93`, `budget-stopped=0`.** Switching waves off is byte-identical on **82 of 82** games
(61 control at d3, 20 at d5, gi26) while running 1.26x-4.7x faster. On the ten worst ledger games:
all ten FINISH (three of them abandon at a 2400 s cap shipped), 7/7 comparable digests identical,
and the set costs 2,474 s against >=15,010 s.

**The fix is NOT a cap.** USER, 2026-09-24: *"It seems to be just another way to budget something
that is intended to be unbudgeted by construction. Our job is to not budget and make the vast
majority affordable and leave only a few special cases to be dropped by the bounds."* Correct --
a ceiling under `Unlimited()` silently redefines the regime. `MTG_BP_WAVES=0` is also not shippable
for a second reason: `FSLineWin` returns the first in-horizon win it finds, sound only if the
shallower passes were complete, and the unlimited-budget wave is what makes them complete.

**The direction instead: prove more variants REDUNDANT and drop them losslessly**, the way
`w0_nobp` (12.4M skipped), `w0_unif_collapse` (10.5M) and `bp_newonly` (56.7% dropped) already do.
`improved=2` out of 5.1M says the ranking is already putting good lines early, so the deep ranks are
near-pure duplication rather than merely low value.

### Where the wave's cost actually is (corrected 2026-09-24, on the rebased engine)

The first thread taken was the probe's `stillborn` / `first-empty` line -- variants resolving to
EMPTY, which looked like work producing nothing. **Both that thread and the probe's own cost split
were wrong, and the probe was wrong first.** `[bp-waves]` printed `retired` + `dupstate` as THE
split of the scored-minus-rolled gap while incrementing them at only ONE of the two wave-scoring
loops, so on gi26 they accounted for 0.5% of the gap they were printed next to (4,483 + 13,820
against 3,438,085). The tell was an impossibility, not a hunch: `slots_stillborn` is incremented
inside `Walker::Report` and so fires for every walker, and `stillborn=368,059` cannot be a subset of
`retired=4,483`. Fixed in `0e2a10bd`; the gap now closes exactly, and relocates the cost:

| population | gi26 | share of wave applies |
|---|---:|---:|
| duplicate post-apply state (`dupstate`) | 3,069,985 | **57.7%** |
| reached a rollout (`rolled`) | 1,879,016 | 35.3% |
| past-the-end probe (`retired`) | 368,100 | 6.9% |

The **LOOKAHEAD** walker is 99.4% of every wave apply (5,286,396 of 5,317,101). `FSLineWin`'s wave
phase -- which this document treated as the cost centre -- is 0.6%. Attributed:
`dup_cross=1,742,926 (56.8%)`, `dup_w0=701,020 (22.8%)`, `dup_self=626,039 (20.4%)`.

**Two candidate lossless declines are now REFUTED by measurement, not argument.**

* *The EMPTY pre-skip.* The node host (site 3) declines its EMPTY arm whenever the continuation list
  already holds an apply-empty entry -- exact, because reaching `k == n` means every cands index was
  applied. That argument transfers to the walker exactly in the `n == k0` case. But the input does
  not exist here: `g_bp_cands_has_empty` is filled only for node-hosted sites and `BpNodeSites()`
  defaults to `1<<3`, while Snow's is site 8. `MTG_BP_EMPTY_CENSUS` (added in `0e2a10bd`) widens the
  scan: **0 of 1,330,535 lists** hold an apply-empty entry, across all three Snow sites. So the
  368,058 first-empties are genuinely distinct lines -- "a line no other rank produces", as
  `BpProbe`'s own comment says -- and there is no decline to take.
* *Pre-apply dedup on plan identity.* `MTG_ROLLOUT_STATS`' list-fingerprint census on gi26:
  `lists=1,330,535 entries=5,941,057 distinct=5,941,057 duplicate=0 (0.0%)`. The lists are not
  internally redundant by plan, so nothing is declinable on the safe "same plan, same state"
  inference.

Together those say the 3.07M duplicates are **different plans converging on the same state** -- the
inference the codebase has twice refuted as unsound to assume, but which is here the measured
majority of all wave work. That is the population any further lossless work has to address, and
`dup_cross` (a different SLOT got there first) is 56.8% of it. Next concrete test: whether those
colliding plans are order-permutations of one another, i.e. whether an order-free canonical plan key
identifies them BEFORE the apply -- the same shape of argument that made the mana payment cache's
canonical key sound, and testable the same way (byte-identical digests or it is not lossless).

## 3. London bottoming, on the 17.6% of these games that mulligan

`[bottom-cost]`: **682 SECONDS per bottoming decision** at d3 unbudgeted (af6ecbf8 measured 4.8 s at
d5/b20), and **97.6%** of that subset's CPU (6,819 s of 6,986 s; the `MTG_BOTTOM_ROLLOUTS=0` arm
reports `cpu=0.000s` and halves total CPU). Bounded by census: 40 of the 227 ledger games mulligan
at all, carrying **17.3% of the tail's CPU** -- a game that never mulligans contributes exactly 0,
by `AIEngine.cpp:852`. So the keep table above is worth ~17% of the tail, not the whole of it.

**Do not re-run the 10-worst-games A/B to test bottoming.** It returns exactly 0.0% and the number
is meaningless: all ten keep their opening seven, so `BottomCards` is never entered and the arm
cannot show an effect. That selection (the most expensive games) anti-correlates with mulliganing --
14 of the 15 most expensive games in the ledger take zero mulligans.
