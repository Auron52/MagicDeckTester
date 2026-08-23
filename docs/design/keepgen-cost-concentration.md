# Keepgen cost concentration: why the Mirrorwing mulligan gen collapsed

**Status:** diagnosed, not fixed. Found 2026-08-22/23 while generating the first exhaustive keep
profile for the new Mirrorwing list. The run was killed at 6h15m having produced no profile.

## What happened

`--gen-mulligan fast` on `decks/Mirrorwing Dragon/` ran 6h15m on 32 cores and never left the floor
phase:

| elapsed | phase | fed | rate | frozen |
|---|---|---|---|---|
| 0h05m | floor | 16,294 | 54/s | 0 / 435,594 |
| 3h30m | floor | 2,197,796 | **335/s** | 0 / 435,594 |
| 4h40m | floor | 2,808,253 | 10/s | 0 / 435,594 |
| 6h15m | floor | 2,900,140 | **15/s** | 0 / 435,594 |

Throughput fell ~22x while the process held 3178% CPU (32/32 cores, verified *instantaneously* --
`ps pcpu` reports a lifetime average and is useless here). Not starvation, not lock contention: the
cores were doing real rollouts, just enormously expensive ones. At 15/s across 32 cores that is
**2.1 core-seconds per rollout against a ~0.2s median game**.

## Root cause: three things compounding

### 1. This deck's search cost is extremely concentrated

Measured on the shipped list at the keepgen's own settings (`d2`, `budget 3ms`), instrumented build
(`-DMTG_PROFILE=ON`):

| sample | median nodes | worst game | max/median |
|---|---|---|---|
| 20 | 8,469 | 0.64s | 3.4x |
| 300 | 9,296 | 4.05s | 29.3x |
| 2,000 | 11,185 | 7.09s | **74x** |

```
top  1% of games hold 12.9% of nodes
top  5% of games hold 33.0% of nodes
top 10% of games hold 48.5% of nodes
```

Half the cost sits in a tenth of the games, and the tail keeps deepening with sample size -- the
same shape as FiveColour (75% of an arm in 14% of its games), which is why that pipeline grew a
work ceiling.

The expensive games are the deck's **go-off turn**, confirmed by deterministic repro
(`--seed 900289 --game-index 289`, 272,098 nodes / 4.23s):

```
T5: battlefield=6  nonland=2
T6: battlefield=13 nonland=8   oppLife=-2   <- won
```

Mirrorwing Dragon lands, then ten spells chain through it in one turn (3x Libation, 3x Gold Rush,
2x Fists, Impolite Entrance, Oracle's Restoration), each fanning out over the board and each
*creating* bodies (Citizens, Treasures) that widen the next fan-out. Cast order is not
interchangeable -- Fists scales with cards drawn, Gold Rush with Treasures, Draught with life
gained -- so the search explores sequences over a board that grows as it goes.

**It is NOT the `{X}` branching and NOT target selection.** Both were checked and both are already
narrow: `GenericProvider::XCandidates` returns a single value (`{max_affordable}`), and
`MirrorwingProvider::TrickTargetCandidates` collapses to the magnet under the user's 2026-08-12
rule. `MTG_MW_ORDERED` (the reviewed full cast order) is default-on. Those levers are spent.

### 2. Exhaustive enumeration weights every hand equally

The gen evaluates **every** composition once, regardless of draw probability. A `Libation x3`
opening hand is ~0.1% of real draws but gets exactly the same evaluation as any other hand. So the
gen pays the pathological tail far more often than gameplay ever does -- and unlike a sampled run
it cannot miss it.

### 3. Adaptive refinement preferentially selects the expensive cells

This is the part that turns a cost problem into a collapse. Refinement targets high-variance /
near-argmin cells. On this deck the high-variance hands **are** the go-off hands: they either
combo out on T5-T6 or fizzle entirely. So the refinement budget lands precisely on the rollouts
that cost 30-40s each.

Every entry in the final slow log contains Luxurious Libation, and the rollout indices span the
floor *and* deep refinement:

```
r=0   size7  34228ms   Libation x3
r=2   size7  36203ms   Libation x3
r=5   size6  35692ms   Libation x2
r=8   size6  36124ms   Zada + Libation x2
r=10  size6  31919ms   Zada + Libation x2
r=15  size5  39723ms   Zada + Libation
```

Sub-refinement also advances in **waves**, and a wave is a barrier
(`sub_refine_step`: `if (sub_converged || sub_wave_pending != 0 || sub_remaining != 0) return;`).
A wave containing one 40s rollout takes 40s. This is the pattern CLAUDE.md names outright ("WAVES
ARE A LOOP"), and it is worth confirming whether the wave structure is a material contributor
before changing it -- it was NOT proven here.

## What keepgen lacks that the value-leaf matrix has

The matrix pipeline hit this exact problem and grew an apparatus for it: `abandon_units`,
`abandon_floor_units`, `abandon_calib`, plus a `max_game_sec` backstop -- a per-game work ceiling in
**deterministic work units**, so the same games are dropped on every machine and the run stays
reproducible and poolable. It discloses the filter in the table (`~~ FILTERED`) because it changes
the estimand to "games that complete within the ceiling".

**Keepgen has none of this.** It only *reports* slow rollouts (`<raw>.slow.log`, which cannot be
disabled); it never bounds one. A 36s rollout is paid in full, and on a contended cell up to R=30
times. Grep confirms: no `MTG_KEEP_ABANDON*`, no `max_rollout`, no ceiling of any kind.

## Proposed fix (not implemented)

Port the matrix's ceiling to keepgen:

1. A per-rollout work-unit ceiling, relative to the cell's own median (cells span orders of
   magnitude, so an absolute ceiling would filter a cheap cell's normal rollouts and an expensive
   cell's not at all -- the `abandon_floor_units` lesson).
2. Deterministic units (`ai/GameWorkMeter.h`), never wall clock -- wall clock is not portable and
   breaks the cross-machine pooling the raw sidecar exists for.
3. Disclose it in the profile, as the matrix does, since it changes the estimand.
4. Calibrate the threshold against *this* workload rather than a proxy -- a first cut on the matrix
   side used the comp scorer and was 10x off.

An alternative worth measuring first: whether the refinement schedule should be **cost-aware** at
all, i.e. weigh a cell's expected information gain against its rollout cost. Spending the entire
budget on the most expensive cells is only correct if they are also the most decision-relevant, and
that has not been established.

## Related

* `docs/design/adaptive-batched-keepgen.md` -- the continuous/journal design
* `docs/design/keepgen-no-off-switches.md` -- why there is one execution path
* `.claude/skills/value-leaf.md` -- the matrix ceiling apparatus and the three fixes behind it
* Commit `0555ad0d` -- the journal now persists progress, a separate defect found in the same run
