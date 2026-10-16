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

## 6. The gi38 runaway: SolveWithLookahead's passes had no overrun ceiling (2026-10-15)

Branch `landfall-runaway` (from `223cbba4`). Regression job `landfall_regression_d3_s2002` gi 38
(seed 2040, d3 b10 = 9,000 units per decision, the profile's `search_leaf_depth: 0`) ran **606 s and
38.6M units**; its census put 37.94M of them in ONE root: `turn=1 root=1 probe=1`.

### 6.1 The state, and where the units went (instrumented, then the instrumentation removed)

* **Whose root.** The real game mulligans to 5: the mull-2 hand is Evolving Wilds, Forest, Rampant
  Growth, Khalni Heart Expedition x3, Rampaging Baloths, bottom 2. Landfall ships no keep table and no
  `bottom_eval_*`, so lookahead bottoming plays one full TRIAL game per name-distinct legal subset (11
  here) at the deck's own play settings. The 9th bottoms Forest + Baloths: turn 1, empty board, hand
  **Evolving Wilds, Rampant Growth, Khalni x3**. 99.98% of the game's units are trial-game units
  (38,637,194 probe vs 8,322 real); the realised game is cheap.
* **Why SolveWithLookahead.** FullSearchLineHybrid (root 2) spends 303 units and has no verified win
  to commit (none exists inside a d3 window at turn 1 of 8), so `AIEngine::TakeTurn` runs the FALLBACK
  `SolveWithLookahead` on a fresh decision budget (`AIEngine.cpp:3626-3631`).
* **The ladder.** 4 root candidates. Pass 0 = 4 units, pass 1 = 144 (ratio 36); the start gate
  estimates pass 2 at 144 x 36 = 5,184 <= 1.1 x 8,852 and starts it. Candidate 0: 143 units.
  **Candidate 1 ("play Evolving Wilds, nothing else"): 37,936,253 units, 665 s.** Only then could the
  between-candidate guard abort the pass (it committed pass 1's plan, which is why the realised game
  never showed anything).
* **Inside that one candidate.** `SimulateToEnd(.., sub_depth = 2, ..)` re-solves EVERY later turn at
  depth 2 (`SimulateToEndImpl`: `turn_depth = depth` for every turn), and each of those solves rolls
  its candidates out re-solving every later turn at depth 1 -- the cost of pass k grows like
  `(sum_t W_t)^k`, so a line that reaches a wide board late is paid for multiplicatively. By turn 7
  this line has Primeval Titan, 6 Forests and 2 Evolving Wilds in play (Khalni x3, Search for Tomorrow,
  Terramorphic in hand). Per solve (logged at >= 20k units): t5 depth-2: 59 plans, 190k units; t6
  depth-2: 159 plans, 1.1M; **t7 depth-2: 6,616 plans, 36.6M** -- each of its plans rolls into a turn-8
  depth-1 re-solve, and the logged ones held **21,182-61,688 plans** (mode ~23-30k). It is `W7 x W8` at
  the last two turns of the 8-turn horizon, charged to a turn-1 decision.
* **What a turn-8 list is made of** (one state: Forest x6, Khalni x2, Titan, Evolving Wilds x2 in
  play; Khalni, Search for Tomorrow x2, Terramorphic in hand): 43,684 plans = **432 base plans x ~101
  variants**. Bases = {cast Khalni or not} x {each Search for Tomorrow: hard-cast / suspend / no} x
  {Khalni crack activations 0-2} x {Evolving Wilds activations 0-2} x {Terramorphic first / after the
  casts / no drop}. Variants: 61.5% carry a Titan attack pin (`attack_land_search_choice`), 41.3% an
  in-combat Khalni crack (`combat_crack_choice`, which keeps a Titan pick too), 26.8% in-plan
  land-search pins (`land_search_pins`, product <= 64), 0.2% breakpoints.
* **They almost never discriminated.** `[leaf-eval] published=17,898,674 ties=17,855,669
  life-equal-ties=17,839,900` over the whole game (98% of whose units are that one root): 99.9% of
  the equal-win-turn comparisons were also ties on opponent life, the graded leaf's only live term for
  this deck (the board terms are off by default).

### 6.2 Q1 -- is any of the width a sound fold? No identity fold was found

Every axis above is a different line under the engine's model: a different fetched land is a
different library and a different board, and the exact folds that exist already run on this list
(post-apply dedup, the combat-pin fold after combat, the combat-pin main-phase share). The waste is
at the horizon's LAST turn: there a land-search pin changes only which (tapped) land arrives and
which landfall/ETB fires for the rest of that turn, and with no damage-relevant landfall payoff and
no second main every variant has the same opponent-life leaf. Collapsing them is a RELEVANCE
argument ("nothing left in this turn reads the fetched land's identity": landfall watchers, ETB
counters, Courser's top, a second main, the leaf's optional board terms), decided per state -- not an
identity fold. Not built; recorded in 6.7.

### 6.3 Q2 -- why the budget did not stop it

The budget WAS threaded (that is how the census charged 37.9M units to the root); nothing armed a
ceiling that the rollouts poll.

* The rollouts poll one thing, per turn-step (`SimulateToEndImpl`, `TurnSolver.cpp:56611` at
  `223cbba4`): `if (budget && budget->Overrun()) { ++g_fs_trunc_events; ...; return max_turns + 1; }`
  -- and `SearchBudget::Overrun()` is `(m_overrun_limit > 0 && m_used >= m_overrun_limit) || ...`,
  false while `m_overrun_limit == 0`.
* Only the FullSearchLine / FullSearchLineHybrid ladders arm it: `budget->SetOverrunLimit(SearchBudget::SatAdd(
  used_before, SearchBudget::SatMulD(kOverrunBudgetMult, budget->EffectiveLimit())))`
  (`TurnSolver.cpp:63394`; also 64469, 65136, 65252) -- the 25x proportional ceiling of 2026-09-09.
* SolveWithLookahead never armed it. Its one guard runs at the TOP of each root-candidate iteration
  (`:66922`): `if (gate && budget->Exhausted() && candidates_done > 0) { ... way_over = pass_used >
  kOverrunBeta * remaining_at_start; finish_expensive = projected > remaining_at_start; if (way_over &&
  finish_expensive) { pass_aborted = true; break; } }` -- so it can only act BETWEEN candidates, and the
  candidate's rollout (`:67542`, `SimulateToEnd(std::move(copy), sub_depth, max_turns, budget, ...)`)
  runs its nested solves with `enforce_budget=false`, where `gate` is false and there is no guard at
  all. "The rollout runs to completion, only consuming budget" is the design comment.
* Not host-specific, not a nullptr, not trial-specific (6.4).

### 6.4 Q3 -- leaf depth and real roots

* **Not specific to `search_leaf_depth: 0`.** SolveWithLookahead's rollouts re-solve each turn at the
  pass's sub_depth; the leaf depth only shapes FullSearchLine's leaf (`EffectiveFdLeafDepth`, FSLineWin).
  `MTG_FD_LEAF_DEPTH=1` on the same game (base binary) changes every trial game's trajectory and the
  runaway moves: trial 8 (hand Forest, Rampant Growth, Khalni x3) has a turn-1 SolveWithLookahead root of
  **79,459,883 units** (2,927 s on shared cores; run stopped there). With the fix, the same leaf-1 game
  is 4,276,948 units / 221 s in total.
* **Not specific to trial games.** `--force-mulligan "2:13,50"` makes the REAL game keep the trial's
  hand: its turn-1 root is `root=1 probe=0`, **37,936,744 units, 1,410 s** (shared cores), and the game
  does not win by turn 8. Same mechanism, real play. With the fix the whole game is 238,227 units
  (that root 236,157, 6.4 s) and plays identically (turn-by-turn log equal).

### 6.5 The fix (`89dee2e2`)

Every SolveWithLookahead pass after the first, at a budgeted root (`gate`), arms the same ceiling
FullSearchLine uses (`used_before + kOverrunBudgetMult x EffectiveLimit()`; an enclosing armed
ceiling is kept if tighter and restored on exit). A pass whose candidate loop ran into it -- including
inside its last candidate -- rolls back to the last COMPLETED pass, keeping a strictly better win the
pass had already proven (FullSearchLine's anytime rule: a cut rollout only returns a no-win). A trip
during the bp/group waves leaves the pass complete (the waves only take strictly better wins). Pass 0
is never armed, so the root always has a complete ranking. The ceiling only re-prices work: inert at
an unlimited budget (the saturating multiply), proportional to the budget below it, turn-complete on
rollback, same constant (no calibration touched). `MTG_LA_PASS_CEILING=0` (a per-job heurarm slot,
default ON) restores the uncapped pass.

### 6.6 Measured

**gi38** (single game, single thread, census on): before **38,645,516 units, 689 s**; after
**944,929 units, 47.7 s** (shared cores). The heavy root is now 235,959 units (the 225,000 ceiling +
the unwind) and commits the same pass-1 plan, so the realised game is identical (turn-by-turn log
equal, win turn 7). 99% of the remaining units are still trial games (6.7, item 1).

**Suite:** unit tests 649/649, scenarios 184/184, smoke 119/119 byte-identical:

```
AUDIT smoke   (old = committed gt_logs   new = test/logs/smoke/wins)
  configs changed: 0   unchanged: 119   no-run-dir: 0
  [searched] slower=0  faster=0  play-changed=0  (slower/faster by loss-penalized score; a win becoming unwon is just the maximal slowdown)
  [d0      ] slower=0  faster=0  play-changed=0  (slower/faster by loss-penalized score; a win becoming unwon is just the maximal slowdown)
```

Regression tier too (not required, run for the cadence rule): 166/166 byte-identical, audit `configs
changed: 0   unchanged: 166`, viewer protocol `195 ok, 385 repaired, 0 play-drift, 0 shuffle-dead, 0
board-diverged, 0 enum-gap, 0 mull-drift, 0 contract-fail (580 refs)`. So no suite deck's decision ever
reaches the ceiling at the shipped budgets; only a runaway does.

**Pooled Landfall A/B** -- ONE `mtg --batch` (`89dee2e2` binary, 6 threads on 6 shared cores), both
arms interleaved per case; "off" = the per-job `MTG_LA_PASS_CEILING: false` hatch:

| case | games | off avg | on avg | paired better / worse | units on/off | wall (job ms) on/off |
|---|---|---|---|---|---|---|
| d3b10 s2002 | 50 | 5.8600 | 5.8600 | 0 / 0 (digest identical) | 10,686,765 / 48,387,352 = **0.221** | 493 s / 1,554 s = **0.317** |
| d3b10 s3003 | 50 | 5.9200 | 5.9200 | 0 / 0 (digest identical) | 1.000 | 0.994 |
| d5b20 s2002 | 25 | 5.8000 | 5.8000 | 0 / 0 (digest identical) | 1.000 | 0.816 (noise: identical work) |
| d5b20 s3003 | 25 | 5.8800 | 5.8800 | 0 / 0 (digest identical) | 1.000 | 0.963 (noise) |

Exactly one game's units moved in 150 paired games: s2002 gi38, 38,645,516 -> 944,929 (pool wall
1,091.5 s -> 46.3 s). The off arm reproduces the `223cbba4` binary byte for byte (same digests and
units, run as a separate base-binary batch of the same jobs). The two identical-work rows show what
the wall column is worth on this host (+-20%).

### 6.7 Measured but NOT shipped (user decisions)

1. **`bottom_eval_depth: 0` + `bottom_eval_topk: 5` for Landfall** (the Melira Pod settings; a
   profile copy, same pooled batch, ceiling on):

   | case | games | on avg | beval avg | paired better / worse | units beval/on | wall beval/on |
   |---|---|---|---|---|---|---|
   | d3b10 s2002 | 50 | 5.8600 | 5.8400 | 1 / 0 | 0.566 | 0.485 |
   | d3b10 s3003 | 50 | 5.9200 | 5.9400 | 0 / 1 | 0.692 | 0.600 |
   | d5b20 s2002 | 25 | 5.8000 | 5.8400 | 0 / 1 | 0.564 | 0.666 |
   | d5b20 s3003 | 25 | 5.8800 | 5.8800 | 0 / 0 | 0.565 | 0.487 |

   Cheapening the bottoming trial games removes 31-44% of Landfall's units with the ceiling already
   on (and they are 99% of what is left of gi38), so this is the next large cost lever. Quality is unresolved: 1 better / 2 worse in 150 paired games (net +1 turn,
   +0.007 turns/game) -- far too small a sample (Melira's adoption used 2,000-3,000 paired games per
   config). A held-out A/B of that size is the measurement this needs; adopting it is the user's call.
2. **A last-turn relevance collapse** (6.2): at `turn_number == max_turns` inside a rollout, fold the
   land-search / attack / crack pin variants of a base plan when nothing that remains of the turn can
   read the fetched lands (no landfall watcher or land-counting ETB in play or castable, no second main,
   no Courser, leaf board terms off). It would remove most of the `W8` factor (99.9% life-equal ties),
   but it is a per-state relevance proof with many readers, i.e. a projection with a soundness argument
   to check, not an identity fold. Not built.

### 6.8 Open

* The start gate's growth estimate (`c_prev x c_prev / c_prev2`) was off by 7,300x on this root: pass
  costs grow like `(sum_t W_t)^k` and the per-turn width explodes late in a rollout line, so the ratio
  measured on the shallow passes says nothing about pass 2. The ceiling now bounds the damage at 25x;
  a better estimator was not attempted (calibration is the user's).
