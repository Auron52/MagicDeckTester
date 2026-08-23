# Where Mirrorwing's search cost actually goes

**Status:** PROFILED 2026-08-23, not yet optimized. Supersedes the cost attribution in
`keepgen-cost-concentration.md`, which was written against a ~3.5x slower engine and blamed the
wrong things.

## Instruments

`perf` cannot write samples in this container (`failed to write perf data: Bad address`), same as the
StompySurprise audit found. The instruments that DO work, all already in the tree:

* `MTG_BRANCH_STATS=1 ... --threads 1` -- odometer attributed to the card driving the biggest
  option-group, plus a by-situation table.
* `MTG_ENUM_STATS=1` -- odometer shape, mana-side combo redundancy, and a two-stage gating estimate.
* `build/Profile` + `callgrind` for instruction counts (not needed yet -- the two above localised it).

All numbers below: **640 goldfish games, seed base 900000, gen settings (d2 / budget 3ms),
single-threaded**, on the shipped list with its value sidecar attached.

## Baseline

640 games = 6.76 s wall at 2559 % CPU on 32 cores -> **0.27 core-s/game**. The tail is real but
moderate: 35 games (5.5 %) exceed 1 s and hold ~30 % of total cost; worst is 3.65 s.

Two things worth knowing before reading any older number:

* **The engine got ~3.5x faster.** `keepgen-cost-concentration.md`'s repro game (`--seed 900289`)
  was 4.23 s when that doc was written; it is **1.2 s** now. Its "74x max/median" and "48.5 % of
  nodes in the top 10 % of games" describe an engine we no longer run.
* **This workload is memory-bandwidth-bound.** `gi=289` reads 3329 ms inside a 32-thread batch and
  1.2 s alone -- a 2.7x contention penalty. 32 cores buy ~12x throughput here, not 32x, and CPU%
  peaks around 2559 %, not 3200 %. Any "core-seconds per rollout" figure measured under full load
  (including the 2.1 core-s/rollout in the older doc) is inflated by this.

## Attribution: the tricks, led by Luxurious Libation

`MTG_BRANCH_STATS`, 1,386,557 `EnumeratePlans` calls, **sum_odo = 70,188,467**:

| driver (unambiguous rows only) | sum_odo | share | sum_final | survival |
|---|---:|---:|---:|---:|
| **Luxurious Libation** | 10,840,737 | **15.4 %** | 684,074 | **6.3 %** |
| Gold Rush | 8,451,072 | 12.0 % | 377,253 | 4.5 % |
| Impolite Entrance | 2,895,835 | 4.1 % | 153,550 | 5.3 % |
| Treasure Token | 2,447,325 | 3.5 % | 825,424 | 33.7 % |
| Fists of Flame | 1,851,283 | 2.6 % | 114,682 | 6.2 % |
| Fortifying Draught | 1,210,371 | 1.7 % | 75,250 | 6.2 % |
| **ALL** | **70,188,467** | 100 % | **5,245,906** | **7.5 %** |

**Read `[+N tied]` rows carefully.** They mean N+1 groups tied at the same maximum option count --
*ambiguous attribution*, recorded honestly rather than silently credited to whichever enumerated
first. They do NOT mean N copies of the card. Adding Libation's tied rows gives 19,458,261 (27.7 %),
which is an UPPER BOUND on Libation-driven odometer, not a measurement.

So: **Libation is the deck's single biggest unambiguous branching driver, and one of its least
productive** -- 93.7 % of the odometer positions it drives are discarded. `max_odo` for a single
enumeration call on a Libation-tied node is **65,856 positions**.

Why it earns that: `{X}{G}`, `solo_target_trick`, and it **creates a 1/1 Citizen per resolved
instance**. Under Mirrorwing the cast fans out over every other creature, each resolution adds a
body, and every new body is a further copy target for the *next* Libation. The fan-out is
self-amplifying, which is exactly what the odometer has to enumerate through.

### Do NOT generalise from one game

On the single worst game (`--seed 900413`) the driver table is **Treasure Token 69 % / Gold Rush
27 %, with Libation absent** -- the exact opposite of the deck-wide picture. The same trap hit the
two-stage estimate below: sampled on two games it read 1.35x and *0.63x (a loss)*; deck-wide it is a
1.51x win. Two games settle nothing here.

## CORRECTION (2026-08-23): the 3.5 % Treasure figure is an ARTEFACT — USER was right

The driver table above credits **the single biggest option-group** with the call's ENTIRE odometer:

```cpp
if (sz < 2) { continue; }
if (sz > max_opts) { max_opts = sz; driver = cands[gp[0]].card_name; }
```

But the odometer is a PRODUCT. A node holding nine Treasures (3^9 = 19,683) and one Libation group of
5 has its whole 118,098 credited to **Libation**, because Libation's single group is larger. The
instrument systematically UNDER-credits many-small-groups and OVER-credits one-big-group — and
Treasures are precisely the many-small-groups case. A second bias compounds it: goldfish draws hands
by natural probability, while the mulligan gen evaluates every composition EQUALLY, so
Treasure-heavy boards are far better represented in the gen workload than in this sample.

### Counterfactual measurement (`MTG_SAC_DUP_CAP=1`, 5,120 games, seed 910000)

Capping fungible sac sources to one collapses `3^N` to `3`. This UNDER-states §2a, which removes the
group entirely (`-> 1`).

| set | baseline | cap=1 | speedup |
|---|---:|---:|---:|
| all 5,120 games | 1665.2 s | 1535.0 s | **1.08x** |
| top 1 % (51) | 181.8 s | 154.9 s | 1.17x |
| top 0.1 % (5) | 30.5 s | 21.5 s | 1.42x |

The aggregate is worthless here, because the distribution is **BIMODAL** — the FiveColour Garth shape:

```
11.78x     424 ms ->   36 ms   gi=4890
10.66x    1066 ms ->  100 ms   gi=2662
10.58x     508 ms ->   48 ms   gi=3700
 8.87x     674 ms ->   76 ms   gi=2540
 8.02x    5925 ms ->  739 ms   gi=4046   <- the 2nd most expensive game in the whole sweep
 5.74x    2794 ms ->  487 ms   gi=2275
...median per-game 1.03x
```

**There is a class of games where Treasures are ~85-90 % of the cost and collapse 6-12x.** They are
rare per draw, common in the gen, and they are where the mulligan-gen wall clock goes. Wall gains also
run ~3x the odometer gains deck-wide (odometer -2.6 %, wall -8.0 % at 640 games), confirming §3's
"cost is superlinear in group size".

**So: judge §2a on the tail, and do not quote a mean.** The 15.4 % / 3.5 % split above should be read
as "Libation drives the widest single groups", NOT as "Libation costs 4x what Treasures cost".
## The actual target: 69.5 % of enumerated payoff lines cannot be paid for

`MTG_ENUM_STATS`, same 640 games (this counter includes the enumeration's inner per-land calls, so
its call/position totals are not comparable with the branch-stats table above):

```
enumeration calls          : 5,832,029   (with a mana side: 1,614,824)
odometer positions         : 346,435,051
mana-side combos raw       : 7,503,510
  distinct exact           : 5,685,605   (collapse 1.32x)  [mana+storm+cards+colour]
  distinct mana+storm      : 3,243,793   (collapse 2.31x)  [cards-spent identity DROPPED]

payoff-side lines          : 36,302,314  (unaffordable under EVERY mana line: 25,219,000 = 69.5%)
(mana x payoff) pairs      : 126,164,388
  pairs that are payable   :  39,666,635  (31.4%)
  => two-stage visits      :  83,472,459  vs flat 126,164,388   (1.51x fewer)
```

Two independent redundancies, both **cost-only** -- they change how fast we reach the answer, not
which plans survive, so both are byte-identical candidates needing no GT rebaseline:

1. **Two-stage gating -- 1.51x.** Seven in ten enumerated payoff lines are unaffordable under *every*
   available mana line, and are generated and then rejected one at a time. Enumerating the mana side
   first and gating payoff lines against the set of achievable mana replaces the flat cross-product.
   This is the same shape as the fix in `plan-odometer-factorization.md`, which took Dragonstorm d5
   -33.7 % Ir by not paying to reject positions individually.
2. **Mana-side dedup -- 1.32x, safe.** 7.50M raw mana combos collapse to 5.69M on *exact* identity
   (mana + storm + cards-spent + colour). The bigger 2.31x needs dropping cards-spent identity, which
   this deck cannot afford: Fists of Flame scales with cards drawn and Fortifying Draught with life
   gained, so which cards were spent is load-bearing.

Combined ceiling if independent: ~2x.

## What was ruled out

* **`{X}` enumeration.** `GenericProvider::XCandidates` returns a single value (`{max_affordable}`).
* **Target selection.** `MirrorwingProvider::TrickTargetCandidates` collapses to the magnet under the
  user's 2026-08-12 rule, and IS routed to.
* **Enumeration breadth caps.** `MirrorwingProvider::EnumGroupCap` is already 8 (vs the generic 12).
* **A work ceiling in `GameWorkMeter` units** (the fix `keepgen-cost-concentration.md` proposes).
  Those units count `SearchBudget::Consume`, i.e. *plans applied* -- all 11 call sites say so. They do
  not count enumeration, which is where the cost is; the code states this outright at
  `CapGroupsBySituationalRank` ("the tranche's odometer walk is paid BEFORE any per-plan budget check
  can fire"). A ceiling in those units would miss what it is meant to catch. Odometer positions do
  not predict wall time either: game 900289 walks 4.84M positions in 1.2 s, game 900413 walks 1.57M
  in 2.4 s. Mana-side combos track it better (194 k vs 657 k).

## Related

* `docs/design/plan-odometer-factorization.md` -- the prior art, and the template for fix 1
* `docs/design/keepgen-cost-concentration.md` -- superseded attribution; keep for the keepgen-side
  observations (exhaustive enumeration weights every hand equally; refinement selects expensive cells)
* `docs/design/fivecolour-search-cost.md` -- same instruments on a different deck
