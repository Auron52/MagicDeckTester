# Mulligan-gen depth: what d2/d1 actually buy vs d3 (in progress, 2026-08-15)

**User question:** how much do `d2 b3` and `d1 b3` win and lose against `d3 b3`, and what is a way to
answer that for other decks **without hours of regeneration**?

Motivation: the user wants to run BOTH the value-leaf and the mulligan profile for **Mirrorwing**
over the weekend of 2026-08-15, and Mirrorwing's mulligan generation is currently deferred as
infeasible (`mirrorwing-mulligan-gen-deferred.md`).

## Rule 0 for this comparison: it was INVALID before 2026-08-15

`mull_gen_depth` / `mull_gen_budget_ms` used to feed equivalence DISCOVERY as well as the label
rollouts, so changing the gen depth silently re-bucketed the deck. Measured on slivers:

| gen setting | K (before the split) | distinct hands (before) | K (after) | hands (after) |
|---|---:|---:|---:|---:|
| d3 b3 | 10 | 14,117 | 10 | 14,117 |
| d2 b3 | **13** | **61,001** | 10 | 14,117 |
| d1 b3 | 11 | 23,050 | 10 | 14,117 |

So "cheaper" could be a **4.3x cost INCREASE**, and any A/B of gen depth was measuring two things at
once. `fix(keepgen): discovery runs under PLAY settings` decoupled them; the hand count is now
constant across arms, and the depth comparison finally measures only what it claims to.

## The cost side (measured)

Per-rollout cost proxy: 120 goldfish games per deck at `--budget-ms 3`, `--max-turns 8`,
`--ignore-play-profile`, depth 3/2/1 (the label rollout is a game played from a kept hand, so game
wall-clock is the right proxy). Quality is the same run's `avg (turns)`.

| deck | d3 s | d2 s | d1 s | d3 avg | d2 avg | d1 avg | d2 LOSS | d1 LOSS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| slivers | 1.2 | 1.1 | 1.3 | 4.2083 | 4.2167 | 4.2500 | +0.0084 | +0.0417 |
| burn | 0.8 | 0.8 | 0.6 | 4.2083 | 4.2167 | 4.2167 | +0.0084 | +0.0084 |
| goblins | 16.9 | 12.1 | 10.2 | 3.7417 | 3.7417 | 3.7500 | 0.0000 | +0.0083 |
| knights | 1.7 | 1.9 | 1.4 | 4.3750 | 4.3750 | 4.3750 | 0.0000 | 0.0000 |
| antilife | 10.2 | 8.6 | 9.7 | 4.2083 | 4.2083 | 4.2417 | 0.0000 | +0.0334 |
| auras | 6.0 | 6.4 | 5.1 | 4.1250 | 4.1417 | 4.1417 | +0.0167 | +0.0167 |
| dragonstorm | 5.3 | 4.6 | 4.4 | 4.4083 | 4.4083 | 4.4417 | 0.0000 | +0.0334 |
| th | 2.2 | 2.1 | 2.0 | 3.9833 | 4.0000 | 4.0167 | +0.0167 | +0.0334 |
| creature_giving | 3.8 | 3.4 | 3.1 | 4.7083 | 4.7083 | 4.7083 | 0.0000 | 0.0000 |
| hinata | 15.7 | 16.7 | 14.7 | 5.9000 | 5.9000 | 5.9000 | 0.0000 | 0.0000 |
| mirrorwing | 15.6 | 14.3 | 13.3 | 4.9583 | 4.9667 | 4.9667 | +0.0084 | +0.0084 |
| **fivecolour** | **17.4** | **42.1** | **27.8** | 5.1417 | 5.1750 | 5.1833 | +0.0333 | +0.0416 |

**Findings so far:**

0. **ON FIVECOLOUR, LOWER DEPTH IS DRAMATICALLY *MORE* EXPENSIVE: d2 is 2.4x SLOWER than d3
   (42.1 s vs 17.4 s) and d1 is 1.6x slower (27.8 s)** — while ALSO labelling worse (+0.033 /
   +0.042 turns). It is the single worst arm in the table on both axes at once. So "lower depth is
   cheaper" is not a rule; on 1 of 12 decks it inverts hard, and it inverts on a deck that currently
   CARRIES `mull_gen d3 b3`. Suspected cause (UNVERIFIED, do not repeat as fact): fivecolour's
   `value_trust_depth` is 6, so every arm here commits below the trust depth and runs the heuristic
   escalation instead of taking the trusted leaf — which would make the arms differ in escalation
   work rather than in search work. Worth confirming before anyone tunes this deck's gen depth.

1. **Elsewhere the saving is SMALL — ~1.25x at best, not a multiple.** Mirrorwing d3->d2 is 1.25x, d3->d1 is
   1.26x. Against a generation the deferral doc prices in the many-hours-to-days range, 25% does not
   change feasibility; it shortens a multi-day run.
2. **d1 is NOT cheaper than d2** (mirrorwing 1.26x vs 1.25x; goblins 10.2s vs 12.1s is the one real
   further drop). At `b3` the 3 ms budget is already binding, so cutting depth below 2 mostly stops
   buying anything — while still costing label quality. **If a cheap setting is wanted at all, d2 is
   the better of the two.**
3. **Quality loss is small but real and one-directional** — every nonzero delta is a LOSS, on 9 of 9
   decks, which is what a systematically weaker labeller should look like. d2 costs 0 to +0.017
   turns; d1 costs up to +0.042.
4. Cost is deck-shaped: goblins/antilife/mirrorwing/fivecolour pay real seconds; knights/burn/
   slivers are already trivial and have nothing to save. hinata is flat (15.7/16.7/14.7) — depth is
   not its cost driver at all.
5. **The practical rule is therefore: MEASURE THE DECK, do not assume the direction.** A 12-deck
   sweep at 120 games costs a couple of minutes per deck and would have caught the fivecolour
   inversion before a generation was launched on it.

## What this measures, and what it does NOT

The table above is **in-play quality at that depth**, i.e. how well the rollout policy plays. That is
the right proxy for rollout COST, but it is NOT the question that decides a mulligan profile.

**What decides the profile is whether the LABELS RANK HANDS THE SAME.** The keep decision is a
comparison between hands, so a labeller that is uniformly worse can still produce the identical
policy — the user's "relative symmetry of playing worse search on the same hand". A +0.02 turn shift
applied to every hand changes nothing; the same shift applied unevenly changes keeps.

## The no-regeneration route (next step)

**Do NOT regenerate the table per setting** (user, explicit: "very expensive"). Use the existing
targeted comp-scorer:

```
MTG_SCORE_COMPS=1 MTG_SCORE_FILE=<comps> MTG_SCORE_R=<R> \
MTG_EQUIV_DEPTH=<d> MTG_EQUIV_BUDGET=<b> \
  ./build/Release/mtg-analyze decks/<deck>/<deck>.txt --cards-json src/cards/data/cards.json
```

It reads `H:c0,c1,...` composition lines against the deck's COMMITTED bucket map and prints
`draw_mean draw_se play_mean play_se` per comp — i.e. it labels the SAME hands at whatever
depth/budget you ask for, with no table build. Its depth AND budget are both env-settable
(the budget was un-hardcoded precisely to answer "what does a cheaper generation budget do to the
cell values it would label?").

**Plan:** sample N size-7 comps from slivers' committed table, score them at `d5 b20` (the deck's
real setting), `d3 b3`, `d2 b3`, `d1 b3`, then compare **not** the mean shift but:

- keep/mull DECISION agreement against the d5/b20 reference, and
- rank correlation over the comps.

Slivers is the testbed because it is the fastest deck, **not** because it would ever ship `d3 b3` —
it has a trusted leaf and should generate at play settings (user, and now the auto-deriver's rule).

## Bearing on the weekend Mirrorwing plan

- **Play-settings discovery does NOT rescue Mirrorwing's feasibility.** Re-measured under the new
  discovery: **K=17, 202,878 size-7 hands (292,855 total)** — identical to the deferred measurement.
  The hypothesis that stronger-search discovery would merge more buckets and shrink the space is
  REFUTED.
- Discovery itself is cheap: **411 s**, and it is parallel, so its wall-clock is essentially its
  single slowest probe (`Twinflame`, 361 s of the 411 s). An earlier estimate of ~100 minutes in
  conversation was WRONG — it extrapolated slow-probe lines as if they were serial.
- Since the cost is not depth-bound, the lever for Mirrorwing is elsewhere: R, or the tail the
  deferral doc already identified ("the tail is the problem, not the mean" — single R=1 rollouts at
  61 s, 74 s, 139 s, 331 s, 516 s, 1442 s).
