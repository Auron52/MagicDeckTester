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

## 2a. CORRECTION (same day): "no hot spot" was the wrong framing

USER: *"No hot spot is stupid. I'm talking about branching here. There are obviously hot spots."*
Correct, and §2/§3 below are written around my error, so read this first.

I answered *"where do CPU cycles go"* with a flat profile, found it diffuse, and let that framing
overwrite the question actually asked — **where does the branching concentrate.** The branching hot
spot is in the data in §4 and it is severe:

| | share of calls | share of odometer space |
|---|---|---|
| `groups=5-8 board=16+` | **7.2%** | **64.5%** |
| top four situation buckets | 27% | **93%** |

The claim *"there is no equivalent win here"* in §2 is therefore **withdrawn as unsupported**. The win
is not in a function, it is in **not generating those plans**. A diffuse flat profile is *consistent*
with a concentrated branching hot spot rather than evidence against one: if one situation class builds
most of the plans and each plan pays a spread of small per-plan costs, the cycles smear across a dozen
functions while the cause stays in one place. That is exactly the shape here.

**And the `Mycoloth` driver attribution in §4 is weak — do not act on it.** `odo` is the product over
*all* groups (`Π(1 + |group_i|) × 2^independent`), while `driver` merely names the card owning the
*largest* group. So "73.8% of odometer under `Mycoloth [+1 tied]`" does **not** mean Mycoloth causes
it. Two further facts kill that reading:

- **`MTG_FUNGUS_DEVOUR_CANDS` is already ADOPTED default ON**, so devour `k` is already a short
  candidate list (fodder floor + contested bodies), not `0..own`. The obvious narrowing is done.
- Working backwards from avg_odo 4,042 across `groups=5-8`: that is roughly **6 groups averaging size
  ~3**, multiplying. No single group is large.

**So the real statement of the hot spot is structural, not per-card:** on wide boards the solver
jointly enumerates the **cross product of 5–8 simultaneously-open option groups**, and the
subset-reject predicates in §2 (`SubsetOversubscribesSacFodder`, `SubsetWastesCreatureSacMana`,
`SubsetHasDuplicateSacSource`, 6.76% combined) are the evidence of *why* it is joint rather than
independent: those groups **compete for shared sac fodder and shared mana**, so they cannot simply be
chosen separately.

That reframes the whole problem from "find the slow function" to: **the enumeration is
generate-then-reject against a shared-resource constraint, when the constraint could bound the walk
instead.** Whether that is achievable is the open question — but it is a far more promising direction
than anything in §2, and it is the direction §6.1 should have led with.

## 2b. THE PER-TURN VIEW, and what the shared-resource constraint actually rejects

Measured with a new instrument, `MTG_BRANCH_SHAPE` (`TurnSolver.cpp`, `namespace shapestats`),
default off and byte-identical when off (smoke 101/101, configs-changed 0). It answers the two things
§6 listed as unknown, on the same replayed rollout.

```
MTG_DECISION_WORK_X=1000 MTG_BRANCH_SHAPE=1 MTG_BRANCH_STATS=1 \
MTG_KEEP_REPLAY="Utopia Mycon x2; Hickory Woodlot x1; Doubling Season x2; Deathspore Thallid x1; Brightcap Badger x1" \
MTG_KEEP_REPLAY_R=2 MTG_KEEP_REPLAY_PD=0 \
MTG_EQUIV_CACHE=logs/fungus_retention/gencache.HEAD.x1000.json \
MTG_KEEP_OUT_RAW=logs/fungus_retention/shape.raw.json \
build/Release/mtg-analyze decks/Fungus/candidate-b-2026-09/Fungus.cod \
  --cards-json src/cards/data/cards.json --gen-mulligan fast
```

### The real turns (§6.4, asked for twice, now answered)

| turn | calls | sum_odo | plans | dedup | avg_odo | avg groups | avg board |
|---|---|---|---|---|---|---|---|
| 1 | 682 | 11,260 | 277 | 274 | 16.5 | 3.44 | 1.4 |
| 2 | 2,779 | 69,251 | 5,073 | 4,419 | 24.9 | 3.88 | 2.8 |
| 3 | 6,288 | 143,684 | 21,944 | 18,673 | 22.9 | 3.46 | 5.3 |
| 4 | 13,619 | 380,732 | 76,425 | 67,916 | 28.0 | 2.72 | 7.6 |
| 5 | 26,123 | 2,792,237 | 301,272 | 277,981 | 106.9 | 2.97 | 10.1 |
| **6** | 24,219 | **41,283,327** | 1,245,159 | 956,302 | **1,704.6** | 3.74 | 12.8 |
| 7 | 19,690 | 3,161,935 | 727,626 | 691,036 | 160.6 | 3.46 | 12.2 |
| **8** | 22,459 | 8,175,380 | **2,757,165** | 2,745,272 | 364.0 | 4.32 | 15.5 |
| | 115,859 | 56,017,806 | 5,134,941 | 4,761,873 | | | |

**The cost is NOT spread across the turn range, and the two blowups are different blowups.**

* **Turn 6 owns the odometer SPACE: 41.3 M of 56.0 M = 73.7%**, at avg_odo 1,704 — 5–16x every other
  turn — from only 20.9% of the calls.
* **Turn 8 owns the PLANS: 2.76 M of 5.13 M = 53.7%**, from a space of only 8.2 M (avg_odo 364).
* So turn 6 is a wide space that prunes well (dedup removes 23.2%, the only turn where dedup earns
  its keep), and turn 8 is a narrow space that prunes barely at all (dedup removes **0.4%**) and
  simply builds everything it enumerates.

Turn 8 is the horizon edge (`max_turns=8`), which fits `fungus-value-leaf-status.md`'s finding that
99.7% of this deck's evaluations sit at the horizon edge. Half the constructed plans are built on the
last turn the search will ever look at.

### The subset funnel — the real work unit is 8.5x bigger than "5.4 M plans"

| turn | subsets entered | dupSacSrc | wasteSacMana | overFodder | other | PASSED |
|---|---|---|---|---|---|---|
| 3 | 20,600 | 1,449 | 176 | 0 | 0 | 18,975 |
| 4 | 219,740 | 16,602 | 14,746 | 16,186 | 0 | 172,206 |
| 5 | 1,689,371 | 71,798 | 180,474 | 204,312 | 0 | 1,232,787 |
| 6 | 7,143,659 | 347,537 | 452,700 | 884,971 | 0 | 5,458,451 |
| 7 | 15,220,528 | 1,516,631 | 407,720 | 2,195,334 | 0 | 11,100,843 |
| 8 | 19,370,442 | 1,603,940 | 936,120 | 2,080,275 | 0 | 14,750,107 |
| **total** | **43,664,421** | 3,557,957 (8.15%) | 1,991,936 (4.56%) | 5,381,078 (12.32%) | **0** | 32,733,450 (74.97%) |

Three things fall out, and the first is the most important:

1. **The walk visits 43.7 M subsets to produce 5.1 M plans — 8.5 subsets per plan.** Every previous
   number in this document (including my own "5.4 million constructed plans") understated the work
   by nearly an order of magnitude. **43.7 M is the number an optimisation has to move.**
2. **The shared-resource constraint rejects 25.03% of the walk** — and `other` is **exactly zero**.
   No other predicate in that funnel rejects a single subset on this deck. §2a's reframing was right
   about the mechanism: the only thing throwing work away here is fodder/mana contention.
3. **The rejection is back-loaded**: turns 7 and 8 are 79% of all subset visits and 76% of all
   rejects. It tracks board width, as the mechanism predicts.

### How big is the prize, honestly

25.03% of visits is what a leaf-level constraint currently discards. That is **not** a 4x, and the
temptation to read it as one should be resisted — it caps a leaf-level fix at about **1.33x**.

**And the obvious escalation does not work as stated.** I expected `SubsetOversubscribesSacFodder` to
be monotone in the subset — if a prefix already oversubscribes, so does every extension — which would
let the walk prune whole subtrees rather than leaves and blow well past 25%. Reading it, **it is not
monotone**: adding a candidate can add *supply* as well as demand (`plan_fodder_credit` counts a cast
creature as +1 body, and a spore pop as `k x spore_creates_tokens`), so a violating prefix can become
legal by extending it with a creature cast.

That does not kill prefix pruning, it just makes the sound version weaker than the leaf test: prune a
prefix only when `demand > supply + (maximum fodder any remaining candidate could still add)`. That
bound is computable and it cuts subtrees, so its saving in *visits* can exceed 25% even though it
rejects fewer *leaves*. **Whether it does is the next measurement, and it is now a well-posed one.**

## 2. Where the time goes — the flat profile (read §2a first)

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
4. ~~**Per-turn shape.**~~ **ANSWERED in §2b** (`MTG_BRANCH_SHAPE`): turn 6 holds 73.7% of the
   odometer space, turn 8 holds 53.7% of the constructed plans, and the walk visits 43.7 M subsets —
   8.5x the plan count everything above is written around.

5. **Does a sound prefix bound beat the leaf test?** Opened by §2b. The leaf-level shared-resource
   reject rate is 25.03% of visits, which caps a leaf-level fix at ~1.33x. A prefix prune on
   `demand > supply + max-remaining-credit` cuts subtrees instead, so it could save more visits than
   it rejects leaves — but the constraint is not monotone, so the bound has to be the admissible one.
   This is the highest-value next measurement.

6. **Why does turn 8 dedup only 0.4%?** Every other turn removes 4-23%. Turn 8 builds 2.76 M plans
   and discards almost nothing, on the horizon edge where `fungus-value-leaf-status.md` says 99.7% of
   evaluations already sit. Either those plans are genuinely distinct, or the dedup key is too
   specific exactly where it matters most — which is the same suspicion as the 42.6% memo hit rate in
   item 2, one layer up.

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
