# Landfall: search cost against the suite gate (2026-10-15)

Standalone record of the performance pass that followed the Landfall deck's card implementation
(branch `landfall-perf`, from `be6f475d`). What the cost was made of, what was removed (all
byte-identical), what was measured but NOT shipped because it is a user decision, and what remains.

## 1. The gate

`scripts/suite_gate.py`: a deck enters the regression suite only if its cost -- the max over its
searched regression cases (d3 b10, d5 b20; profile attached) of the case's core-ms / games -- is at
most 3x the most expensive deck that already has both a value leaf and a mulligan profile. Snow,
7,162 ms/game, so the bar is **21,486 ms/game**.

## 2. Where the time went (profile first, one game at a time)

Base `be6f475d`, the orchestrator's generated profile attached
(`decks/Landfall/Landfall.profile.json`, card_scores + mulligan only, no value_play block).

* **perf, seed 788 (gi 11 of base 777), d3 b20, dwarf call graph** -- `PerformLandSearch` 37.3% of
  all samples inclusive, of which `LandSearchPicks` 20.2% (the class scan, the maximal-multiset walk,
  the string-built pick list and the provider's colour sort, re-run at EVERY resolution) and
  `ShuffleAfterSearch` 8.5%; `FireAttackLandSearch` (Primeval Titan's attack trigger, resolved in
  every simulated combat) 20.0%; `SimulateCombat` 20.8%; `BuildSimKey` 5.8%.
* **turn census (`MTG_TURN_CENSUS`), same game** -- 6.77M units for one game at a 18,000-unit
  decision budget. Eight decisions ran to the 25x overrun ceiling (450k units each) and were
  aborted: 3.6M units, 53% of the game, in passes whose result was discarded. The depth-1 pass alone
  is `W_root x W_leaf`: turn 6 had 642 root plans and each root plan's 1-ply horizon leaf scored
  ~300 candidates (435,504 candidates in that one decision).
* **turn census, seed 3013 (base 3003 gi 10), d5 b20** (7.16M units, the heaviest d5 game of the
  sample) -- 13 of 133 decisions hold 57% of the units; turn 3-5 decisions at 10-25x budget.
* **candidate-axis census (temporary diagnostic), rollout lists** -- seed 2006 d5 b20: Titan attack
  pins (`attack_land_search_choice`, ~24 per attacking Titan, plus the in-combat crack) ~46% of all
  candidates, in-plan land-search pins ~25%, breakpoint variants ~18%, base plans ~6%; seed 3013:
  land-search pins ~31%, breakpoint variants ~36%, attack pins ~12%.
* **`MTG_DUP_DIFF`, seed 2006** -- 291k post-apply duplicates (8.6% of candidates), the largest
  class `PL[lsearch] SAMEACTS` (135k: land-search pin variants that clamp onto a sibling, see 5.3);
  134k post-combat duplicates among combat-pinned candidates (already folded before their rollout).
* **callgrind, seed 3004 d3 b10** -- SolveWithLookahead's own candidate applies (`ApplyPlanDirect`
  from the la_cand loop) were 42% of the game's instructions.

## 3. What was removed (all byte-identical: same digest AND same units on every job)

| commit | what | callgrind Ir, 3 fixed games (seeds 2012 d5b20 / 2008 d3b10 / 3004 d3b10) |
|---|---|---|
| `5b0ae64e` | `LandSearchPicksMemo`: the provider-ordered pick list memoised on an exact key (spec, multiset of legal library land definitions, provider type + `DecisionProvider::LandSearchPickOrderKey`) | -5.5% / -4.0% / -12.9% |
| `cce2446a` | per-node trims: in-place cycle permutation in `Library::ShuffleByKey`; picks taken by definition pointer; precomputed sort keys in the bounce and landfall-target orders; one board walk for Phylath's shroud / attachment tests; subtype by interned id | -1.7% / -2.3% / -9.2% |
| `943c8992` | the **combat-pin main-phase share** (a Titan-pick / crack variant reuses its base plan's post-apply state when the base's apply provably read neither pin, consumed no unit and reached no breakpoint) + a deck stamp for FireEtbWatchers' subtype-enter draw walk | -0.8% / -0.6% / **-32.6%** |
| `513be871` | a sharing variant's post-apply dedup key finishes its base's key prefix (`BuildDedupKeyBeforeLandSearchPins` + `FoldLandSearchStatePins`, same folds in the same order) | 0% / 0% / -7.4% |
| `cf78dafa` | the Karoo bounce rule (`BounceUserRulePick`) computes each land's colour mask once per call instead of once per candidate | 0% / -0.1% / -2.1% |
| cumulative vs `be6f475d` | | **-7.9% / -6.8% / -51.6%** |

Heavy game, callgrind, seed 2006 d5 b20 (the slowest d5 game of the first sample): `be6f475d`
539.2G Ir -> `943c8992` 326.9G Ir (-39.4%; the two later commits were not re-run on it).

The share's exactness argument is in the commit and at the site (SolveWithLookahead, "COMBAT-PIN
MAIN-PHASE SHARE"): the two pins are read only by `FireAttackLandSearch`,
`FireCombatLandSearchCracks`, `BuildSimKey`, `dominance::Build` and the turn-start reset, each of
which bumps `combatpin::t_reads` (GameState.h). **A new reader of `scripted_attack_land_search` or
`scripted_combat_land_search_crack` must bump it too**, or the share can reuse a main phase the pin
steered.

### 3.1 The gate number, before and after (pooled, wall)

The gate's metric is wall time, and this host is shared: the same work measured 13.2 s/game
(base, d5b20 s2002, 12 threads at moderate load) and 24.4 s/game (same binary, same games, host
load ~33 on 24 cores). Units and digests are identical in every run below, so only the clock moved.
Two sequential pairs on ONE manifest (8 cases, 320 games, profile attached, 12 threads, host load
~33), the second in reversed order to bracket load drift:

| case | games | base s/game | after s/game | ratio (pair 1: `943c8992`; pair 2: `513be871`) |
|---|---|---|---|---|
| d3b10 s2002 | 30 | 13.6 / 14.8 | 9.9 / 9.3 | 0.726 / 0.624 |
| d3b10 s3003 | 30 | 9.9 / 10.3 | 7.1 / 7.4 | 0.717 / 0.717 |
| d3b10 s4004 | 60 | 11.4 / 11.5 | 7.6 / 7.3 | 0.669 / 0.637 |
| d3b10 s5005 | 60 | 14.4 / 15.4 | 9.6 / 9.1 | 0.665 / 0.589 |
| d5b20 s2002 | 30 | 24.4 / 24.6 | 17.5 / 17.0 | 0.715 / 0.692 |
| d5b20 s3003 | 30 | 18.9 / 19.3 | 14.4 / 14.6 | 0.761 / 0.754 |
| d5b20 s4004 | 40 | 32.0 / 33.7 | 20.4 / 19.5 | 0.636 / 0.578 |
| d5b20 s5005 | 40 | 30.2 / 32.0 | 20.5 / 20.0 | 0.679 / 0.623 |
| total core-s | 320 | 6043 / 6308 | 4132 / 4004 | 0.684 / 0.635 |

Under that load the base deck is over the bar (33.7 s/game at d5b20) and the shipped tree is just
under it (20.0 s/game); at moderate load the worst case is ~13 s/game -> ~9. **Fits, not "well
under", when the host is busy** -- 4.1 is the lever that would make it comfortable.

## 4. Measured but NOT shipped -- user decisions

### 4.1 `search_leaf_depth: 0` for Landfall (PROPOSAL; the Melira Pod precedent)

The 1-ply horizon leaf (`MTG_FD_LEAF_DEPTH` default 1) scores the leaf's whole candidate list
(Titan picks and land-search pins included) with one greedy rollout each; it is the `W_leaf` factor
above. Melira Pod ships `"search_leaf_depth": 0` (analysis-Melira Pod.md, 2026-09-08). Landfall,
pooled A/B in one batch per sample (base vs a profile copy with the field), `cce2446a`-era binary:

| case | games | base avg | leaf0 avg | paired better / worse | units leaf0/base | wall leaf0/base |
|---|---|---|---|---|---|---|
| d5b20 s2002 | 30 | 5.8667 | 5.8000 | 2 / 0 | 0.64 | 0.57 |
| d5b20 s3003 | 30 | 6.0000 | 5.9333 | 4 / 2 | 0.42 | 0.79 |
| d3b10 s2002 | 30 | 5.8667 | 5.8667 | 2 / 2 | 0.64 | 0.75 |
| d3b10 s3003 | 30 | 6.2000 | 5.9667 | 6 / 0 | 0.45 | 0.52 |
| d5b20 s4004 | 40 | 5.9750 | 6.0000 | 3 / 4 | 0.63 | 0.54 |
| d5b20 s5005 | 40 | 5.8000 | 5.8250 | 2 / 3 | 0.46 | 0.41 |
| d3b10 s4004 | 60 | 6.0667 | 6.0000 | 7 / 4 | 0.61 | 0.53 |
| d3b10 s5005 | 60 | 5.9000 | 5.8667 | 6 / 4 | 0.48 | 0.25 |
| **all** | **320** | | | **32 / 19** | | |

Paired mean (leaf0 - base) **-0.047 turns, se 0.024** -- not worse on this sample, and 2-4x
cheaper.

**Held-out confirmation** (`943c8992` binary, seeds 6006 / 7007 never used above, one pooled batch,
both arms interleaved):

| case | games | base avg | leaf0 avg | paired better / worse | units leaf0/base | wall leaf0/base |
|---|---|---|---|---|---|---|
| d3b10 s6006 | 100 | 6.0600 | 5.9300 | 21 / 10 | 0.58 | 0.61 |
| d3b10 s7007 | 100 | 6.0100 | 5.8600 | 21 / 8 | 0.47 | 0.59 |
| d5b20 s6006 | 60 | 6.0167 | 5.8167 | 13 / 2 | 0.65 | 0.64 |
| d5b20 s7007 | 60 | 5.9500 | 5.8667 | 7 / 3 | 0.49 | 0.53 |
| **all** | **320** | | | **62 / 23** | | |

Paired mean **-0.141 turns, se 0.031**; with the first sample, 640 paired games at -0.094 turns
(94 better / 42 worse). The greedy leaf is not merely cheaper here, it plays BETTER: with the 1-ply
leaf, Landfall's depth-1 pass costs `W_root x W_leaf` and the search rarely completes a deeper one
(census: committed depth 0-1 on the expensive turns); the greedy leaf lets depth 2-3 passes finish.
Shipping it is the user's call (a deck setting; the Melira adoption used 2,000-3,000 paired games per
config on two seed bases).

### 4.2 Not proposed

* Titan land-pair narrowing (~25 pairs): lossy at any budget (open question Land-search Q3 in
  analysis-Landfall.md). Nothing here changes that.
* Restricting the attack / land-search pin fans inside the horizon leaf only: playout policy, a
  quality question like 4.1 but with no precedent; 4.1 is the simpler lever.
* Courser's library-top continuations kept unconditionally by the new-only filter: <1% of leaf
  candidates in the census -- not worth a lockstep change.

## 5. What remains (after `cf78dafa`)

1. **The first-pass explosion.** Decisions still run their depth-1 pass to 10-25x budget
   (`W_root x W_leaf`); the budget machinery (start gate, 25x proportional overrun ceiling,
   anytime commit) is user-calibrated across the suite and was not touched. 4.1 is the lever that
   removes `W_leaf`.
2. **State keys.** `BuildSimKey` is still ~12% of a Titan game (seed 3004): the post-combat
   combat-pin fold builds a full key per candidate on a 40+-permanent board (the post-apply key of a
   sharing variant is now finished from its base's prefix). Any change to key VALUES changes TT
   eviction -> not byte-identical; a cheaper key with the same values was not found.
3. **Clamped land-search pin variants** (~4% of candidates, the `PL[lsearch] SAMEACTS` class): cast
   ordinals are sized at the widest cast source because the fan does not reproduce the apply's cast
   order. A sound pre-apply skip is possible (record each base apply's per-ordinal resolution widths,
   skip a variant whose first deviating ordinal clamps onto an emitted sibling) but it removes unit
   charges, so it moves play under budget and needs the large-budget proof; not built.
4. Per-node remainder: `ShuffleByKey` ~5%, `SimulateEndAndStartNextTurn` ~4%, the greedy walk ~4%.
