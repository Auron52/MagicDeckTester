# Fungus candidate-B: a measured diagnosis of the slow keep-rollouts

**Status: DIAGNOSIS ONLY — no fix proposed here, deliberately.** 2026-09-30, after the candidate-B
mulligan generation was cancelled at 90.8 h (63.7% frozen) because its tail made the run unbounded.

USER: *"we don't have a good diagnosis. We haven't dug enough into why this is a problem"* and
*"essentially, we can't come up with a solution because we don't have a diagnosis."* This file is the
diagnosis. It supersedes the untested hypothesis in
`fungus-doubling-season-rollout-tail.md` §4 — see §5 below, which **refutes** it.

## 1. What was measured, and how to redo it

One captured slow rollout, replayed byte-identically via the generator's own replay hook:

```
MTG_KEEP_REPLAY="Utopia Mycon x2; Hickory Woodlot x1; Doubling Season x2; Deathspore Thallid x1; Brightcap Badger x1" \
MTG_KEEP_REPLAY_R=2 MTG_KEEP_REPLAY_PD=0 \
MTG_EQUIV_CACHE=logs/fungus_slowturn_analysis/gencache.HEAD.json \
MTG_KEEP_OUT_RAW=logs/fungus_slowturn_analysis/scratch.raw.json \
MTG_BRANCH_STATS=1 \
build/Profile/mtg-analyze decks/Fungus/candidate-b-2026-09/Fungus.cod \
  --cards-json src/cards/data/cards.json --gen-mulligan fast
```

It reproduces: **20,700 ms** on current HEAD (30,038 ms in the generation, on the older engine).
`MTG_EQUIV_CACHE` must point at a cache built by the CURRENT engine or discovery re-runs and costs
9 minutes; `MTG_KEEP_OUT_RAW` is essential — without it the replay appends to the real run's
`.slow.log` and rewrites its `gencache.json`, which would break resuming the cancelled run.

perf, with the recipe from `fungus-token-search-cost.md` (write to `/tmp`, software event):

```
perf record -e cpu-clock -F 499 --no-buildid -o /tmp/slowroll.perf.data -- <the command above>
perf report -i /tmp/slowroll.perf.data --stdio --no-children
```

**Caveats that matter.** perf inflated the rollout 20.7 s → 35.9 s (1.7x), so *relative* attribution
is usable and absolute figures are not. The 64-game play-digest battery (~9 s) runs before the replay
and is included; its games are narrow-board, so it contributes low-odometer calls and dilutes rather
than creates the signal below.

## 2. Where the time goes — no single hot spot, and that IS the finding

Flat profile, grouped by family (98.9% of samples accounted):

| family | self % |
|---|---|
| odometer / plan enumeration | **14.82** |
| mana payability (`CanPay*`, `ManaPool`, `ManaGate`, `LiveManaGrant`) | **10.84** |
| allocation / vector churn (`operator new`, `~vector`, `memmove`, `push_back`) | **9.60** |
| subset-REJECT predicates (`SubsetOversubscribesSacFodder`, `…HasDuplicateSacSource`, `…WastesCreatureSacMana`) | **6.76** |
| `SolveUncached` per-subset callbacks | **6.39** |
| memo keying + compare (`BuildSimKey`, `memcmp`) | **6.14** |
| card lookup / subtype | 4.55 |
| **turn simulation / ETB / token creation** | **2.65** |
| other (953 symbols, none above 0.95%) | 37.19 |

The hottest single symbol is **5.66%**. Contrast the 2026-09-17 investigation, where one function
(`CountControlledDragons`) was 45% and a one-signature fix bought 1.43x. **There is no equivalent
single win here.** The 37% "other" was checked symbol by symbol: its largest entry is
`PrePlanAvailabilityKeys` at 0.95%, and the remaining 953 symbols average under 0.03% each.

## 3. The volume that explains the diffuseness

`MTG_BRANCH_STATS` for the isolated rollout (warm cache, so digest + this one rollout):

```
EnumeratePlans calls = 117,937     odometer space = 59,984,021
plans constructed    =  5,448,014  after dedup    =  5,068,608
leaf evals published =    508,766  of which TIES  =    345,188  (67.8%)
solve-memo           = 415,333 hits / 558,908 misses  (42.6% hit rate)
```

**Read `odometer space` carefully — it is not work done.** `odo = Π(1 + |group_i|) × 2^independent`
(`TurnSolver.cpp:38146`) is the *size of the space*, and `EnumeratePlanPositions` prunes during the
walk, so only 5.45 M plans are actually built out of a 60 M space. An earlier draft of this note read
the 60 M → 5.07 M gap as "91% discarded by dedup"; that was wrong. **Dedup removes only 7%**
(5.45 M → 5.07 M). The pruning is already happening inside the walk.

So the cost is **~5.4 million constructed plans for one rollout**, at ~46 plans per
`EnumeratePlans` call, each one paying plan construction, a payability check, the subset-reject
predicates and a memo probe. That is why the profile is flat: the per-plan overhead is spread across
a dozen functions and the volume is the multiplier.

## 4. Where the volume concentrates

By driver (the card owning the largest option group of size ≥ 2):

| driver | calls | odo space | share of odo | avg odo | max odo |
|---|---|---|---|---|---|
| `Mycoloth  [+1 tied]` | 10,944 | 44,239,585 | **73.8%** | **4,042** | 88,200 |
| `Utopia Mycon` | 29,468 | 5,898,273 | 9.8% | 200 | 28,800 |
| `Mycoloth` | 20,388 | 4,303,915 | 7.2% | 211 | 9,000 |
| `Utopia Mycon  [+2 tied]` | 946 | 1,032,710 | 1.7% | 1,092 | 16,000 |

**9.3% of calls carry 73.8% of the odometer space.** By situation the concentration is sharper still:
`groups=5-8 board=16+` is 8,520 calls (7.2%) and 38.7 M odo (64.5%), avg 4,538.

**What `[+1 tied]` does and does NOT mean.** It means one other option group had the *same size* as
the largest, so the instrument declines to attribute the call to a single card
(`TurnSolver.cpp:38176`) — the label exists because a previous version of this table blamed Mana
Cannons for 15% of branching purely for being cheap and enumerated first. It is an **attribution
ambiguity marker, not evidence that the groups are interchangeable.** I initially read it as
fungible-duplicate groups; the code does not support that, and proving or refuting fungibility is the
next piece of work, not a conclusion of this one.

## 5. This REFUTES the standing hypothesis

`fungus-doubling-season-rollout-tail.md` §4 proposed that the cost is the second main's per-turn
`GameState` copy plus enumeration, scaling with **board width**, with Doubling Season compounding
width turn over turn. Two measurements contradict it:

- **Turn simulation, ETB watchers and token creation together are 2.65%.** Playing the turns —
  including every token Doubling Season doubles — is nearly free. A width-driven copy cost would
  appear here and does not.
- **Option-group count dominates board width in the odometer.** `groups=9-12 board=0-6` averages
  2,928 odo on a *narrow* board, while `groups=0-4 board=16+` averages 297 on a wide one — an order
  of magnitude the other way. Width matters only through how many groups it creates.

That is now the second structural hypothesis about this deck to be refuted by a profile (the first
was `GameState` deep-copy scaling, 2026-09-17, also plausible, also wrong). The pattern is worth
naming: **on this deck, "the board is wide so copying is expensive" keeps being the wrong answer, and
"the decision space is wide" keeps being the right one.**

## 6. What is NOT yet known — the honest boundary

1. **Are the tied groups actually fungible?** If two equal-size option groups are permutations of an
   unordered choice, the odometer is enumerating the same decision twice and that is pure waste. If
   they are genuinely distinct, the width is earned. §4 cannot tell these apart. This is the single
   highest-value next measurement, and `sac-outlet-count-pool-refuted.md` records a previous
   count-pooling attempt at this that was **refuted at 2.29x**, so it needs care.
2. **Why is the solve-memo hit rate only 42.6%?** 558,908 misses for one rollout. Either the key is
   too specific (`BuildSimKey` is 1.97% of profile) or the states genuinely differ. If plans that tie
   at the leaf are producing distinct sim keys, the memo is missing on states it should share.
3. **Why do 67.8% of leaf evaluations tie?** A tie means the evaluator cannot separate two plans. If
   the search is constructing millions of plans that are indistinguishable at the leaf, the question
   is whether they are distinguishable at all — and if not, where the earliest point is at which they
   could be collapsed.
4. **Per-turn shape.** All of the above is per-rollout aggregate. Nothing here says *which turns* the
   5.4 M plans land on. `SLOW-ROLLOUT` lines give the hand but not the turn profile.

## 7. Artifacts

* `logs/fungus_slowturn_analysis/replay1.err` — cold-cache run, process-wide branch stats
* `logs/fungus_slowturn_analysis/replay2.err` — warm-cache run, the isolated numbers in §3/§4
* `logs/fungus_slowturn_analysis/gencache.HEAD.json` — equivalence cache at current HEAD (reuse this)
* `logs/fungus_slowturn_analysis/slow_keep.txt` — the 3,288 slow keep-rollouts of the cancelled run
* `logs/fungus_journal_backup/` — the cancelled run's 350 MB journal, gencache and slow.log, byte-identical to what it left behind
* `/tmp/slowroll.perf.data` — the perf sample (not durable)

**The cancelled run's own tail, for scale:** 3,288 slow keep-rollouts ≥ 30 s totalling **199 core-hours**,
median 38 s, p90 232 s, worst **25,712,961 ms (7 h 08 m) in a single rollout**. Doubling Season lift is
5.73x with a per-copy dose response (0 copies 71 s → 2 copies 744 s → 4 copies 960 s); `Psychotrope
Thallid`, despite appearing in the worst hands, has lift **0.90x** and is a passenger, not a driver.
