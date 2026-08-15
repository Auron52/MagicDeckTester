# Single-consideration: where spells are considered multiple times, and the collapses

**USER directive (2026-08-14, verbatim):** "The main idea is to figure out where we are
considering spells multiple times and try to figure out a way to consider them only once."

This is the successor arc to main-phase classification (`main-phase-classification.md`), which
was step 1 of the same principle: the classification partition gives each spell ONE phase, so
the "cast now vs after combat" question stops being asked twice per turn. This document maps the
*remaining* multiplicity, with measurements, and records the collapses in order.

## The instrument

`MTG_CONSIDER_STATS=1` (default off, zero cost off; TurnSolver.cpp `namespace considerstats`)
attributes every `CollectActions` harvest to its **call-site context**, built from thread-local
markers: `solve`/`enum` caller, `m1`/`m2` phase, `.live` (executor-level, no search frame),
`.root` (committed-decision enumeration, `g_bp_root_enum`), `.fsN` (FSLine nest), `.m2solve`
(inside `SolveSecondMainInSearch`), `.bpcont` (breakpoint continuation enum), `.tranche`
(group-wave re-enumeration). Per site it counts calls, Actions emitted, per-card emissions, and
**distinct per-decision states** (BuildSimKey xor a decision epoch `g_decision_epoch`, bumped at
each committed-decision driver) — so `dup_calls = calls - distinct` is the honest fraction a
per-decision memo could collapse. Run one game, `--threads 1`.

## The measured map (one game each, seed 300001 gi=0, d3 budget 200ms, classify-family levers on)

**FiveColour** (deck_feeds_combat = true, no collapse): 1.68M harvests, **4.5M action
considerations in one game**. Dominant sites:

| site | calls | actions | distinct states | dup |
|------|-------|---------|-----------------|-----|
| solve.m2.fs3.m2solve (greedy 2nd main in rollout interiors) | 708k | 3.15M (70% of game) | 143k | **80%** |
| enum.m1.fs3 (rollout-interior enumerations) | 335k | 355k | 85k | 75% |
| solve.m1.fs3 | 288k | 335k | 115k | 60% |
| enum.m1.root (committed root) | 889 | 889 | 889 | 0% |

Maelstrom Archangel alone: 819k considerations at the one m2solve site. The root is already
single-consideration; the duplication lives in the rollout interiors, overwhelmingly the greedy
second main that every rollout ply re-solves.

**Hinata2** (deck_feeds_combat = false, total-Main2 collapse): 138k harvests, 86k considerations.
Same shape (solve.m2.fs3.m2solve 42k calls, **86% dup**), plus a collapse-specific finding:
**~51k harvests (37% of all calls) are main-1 harvests that return ZERO actions** — the filter
erases everything, but `CollectActions` still pays the full harvest first (and even those empty
harvests are 84% state-duplicates).

## Collapse #1 — greedy-Solve memo (`MTG_SOLVE_MEMO`, built 2026-08-14, DEFAULT OFF)

`TurnSolver::Solve` is a pure function of (state, phase): no draws, no budget, no cutoff
dependence. The memo (TurnSolver.cpp `namespace solvememo`; `Solve` is now a thin wrapper over
`SolveUncached`) caches the Plan per decision:

* **Key = `BuildBreakpointKey`** (BuildSimKey + floating mana + spells_cast_this_turn +
  casts_remaining), NOT BuildSimKey — that key's own history (a BuildSimKey-only first attempt
  silently changed Hinata's play) is exactly the mid-turn trap. One shared key, one place to
  keep exact.
* **Scope = one decision** (`g_decision_epoch`): the key's library digest (size + front) only
  implies content within a decision — the TranspositionTable's own scoping argument.
* **Search interiors only** (`g_cs_solver_nest > 0 || g_fsline_nest > 0`): live executor calls
  and human play bypass, so ship d0 configs are untouched by construction.
* **`MTG_SOLVE_MEMO_VERIFY=1`**: on every hit also recompute uncached and compare field by
  field.

**Validation (2026-08-14, all green):** verify mode: 5c gi=0 **796,611 hits / 0 mismatches**
(70% hit rate), hinata gi=0 90,448 / 0 (82%). Memo on-vs-off: game logs byte-identical (modulo
runId); 5c wall 7.52 s -> 5.97 s (−21%) on one game. **Suite smoke ALL PASS with the memo
forced on (36/36 digests identical to GT).** Battery (classify levers, 100 games x 6 jobs):
**0 diverged games in every job**, pooled wall 1:34 -> 1:24 (−11%), battery-wide hit rate 64%
(42M hits / 24M misses), RSS +235 MB. Adoption proposal: default-ON + `MTG_NO_SOLVE_MEMO`
hatch, no GT rebaseline needed — awaiting the user's call.

## Collapse #1 residual (measured with the memo ON)

Hinata is solved: every solve site shows dup = 0 (each distinct state solved exactly once);
86k -> 23k considerations. 5c: 4.5M -> 1.63M; the residual leader became the rollout-interior
enumeration (enum.m1.fs3: 335k calls, 250k per-decision dup) — which motivated collapse #2.

## Collapse #2 — enum-side memo (`MTG_ENUM_MEMO`, built 2026-08-14): **REJECTED for adoption**

Same pattern applied to `EnumeratePlansWithLand` interiors (wrapper over
`EnumeratePlansWithLandUncached`; excludes root / tranche / bp-continuation / human play;
groupwave `max_dropped` stored as the call's own contribution and max-merged on hit —
byte-equivalent to the unmemoized accumulation; promotion-on-second-visit so cold states store
only a marker). **Identity holds everywhere**: verify 0 mismatches (5c 65k + hinata 5k hits),
smoke ALL PASS with both memos, battery 600 games 0 diverged.

**But it LOSES wall clock at scale, and that is the honest result.** The single probe game
(5c d3, 1 thread) showed −9% on top of the Solve memo (5.97 -> 5.26 s) — misleading. At
battery scale the hit rate collapses: 24% raw (M2), 14% with promotion (M3), and on the heavy
5c **d5** game it is **1%** (5k hits / 470k misses, 51 cap-thrash clears) — the deeper search
visits too-diverse states, so ~every call pays a full `BuildBreakpointKey` state walk (its cost
is comparable to a small enumeration) for nothing. Battery wall: M1 (solve memo) 1:24 -> M3
(both) 2:00; isolated heavy game 19.9 -> 22.7 s (+14%). Why #1 wins where #2 loses: Solve's
body is heavy relative to the key and its states recur (64% battery-wide hit rate); the
interior enumerations are often tiny relative to the key and their states don't recur at d5.

The lever stays in-tree DEFAULT OFF as a measured negative (and the verify machinery is
reusable); do NOT flip it on. If it is ever revisited, the fix must remove the per-call key
cost (e.g. an incrementally-maintained state hash), not tune the cache.

## Collapse #3 — canonical cantrip ordering (`MTG_CANTRIP_ORDER`, built 2026-08-15, DEFAULT OFF)

USER design: within a turn's cantrip chain, explore only the canonical order ("allow only
Ponder + Preordain by disallowing Ponder after a Preordain"). Canonical rank = mana value
(cheaper first), then semantic tier within equal cost (reorder/shuffle manipulators before
scry-setters before plain draw before staging/impulse engines), then name. The **lossless
guard** (approved alongside): the ban fires only when the banned cantrip's printed cost is
componentwise <= the site's -- then the cantrip-first twin chain was always enumerable and the
prune deletes a true permutation duplicate, never a subset. X/hybrid costs bail out.

Implementation: `TurnSolver::CantripOrderScope` binds the continuation's site in BOTH worlds
(rollout deferred site-3/5 re-solve in ApplyPlanDirect incl. the BpPrefixSnap resume path;
executor resolve_draw_breakpoint -- the lockstep pair); `CollectActions` suppresses banned
candidates; the bp-enum cache key folds the site; the (default-off) memos bypass under a bound
site. v1 scope: the deferred plain-cantrip/trick continuation; nested/inline sites (0/2) and a
drawn-card exemption are the extensions if measurement wants them.

**Measured (2026-08-15, canon tree, classify-family levers):** off = byte-identical (smoke ALL
PASS). On: battery 599/600 games identical, the ONE diverged game IMPROVED (hi_d5 gi=38 wt
7 -> 6 -- the pruned duplicate freed a breakpoint-width slot for a genuinely different
continuation, the single-consideration thesis in action). Wall ~neutral on this battery (an
apparent -24% was cross-run noise; FiveColour holds no ordered-class cantrips and dominates
the makespan). The lever's value scales with cantrip-chain density -- re-measure when a
chain-heavy deck/config is in front of us, and extend scope before judging it inert.

## Attribution round (2026-08-15) -- who owns which delta

Isolated arms on the canon tree (battery, 100 games x 6 jobs, S0 = no levers):

* **Searched second main (C0 vs S1, its own delta): NET POSITIVE.** 5c neutral (1 game better
  at d3), hinata better at ALL depths (-0.04/-0.04/-0.03; 12 better vs 3 worse incl. an unwon
  game recovered). The earlier "searched-m2 regresses" reading conflated it with the classify
  family's own ship cost. With the m2-search memo the cost is recovered too (1:56 -> 1:33).
* **MTG_BP_SITES=63**: confirmed depth-conditional -- helps d3/d5 (+0.01/+0.04 vs +0.06/+0.08
  without), costs ship (+0.07 vs +0.04). Adoption shape: on at explicit depths, off at ship.
* **5c_ship +0.10 (14 games, IDENTICAL in every classify-family arm)**: bisection says
  MTG_MAIN2_DROP alone flips gi=1 and gi=24, others need lever combos. Dissection of BOTH
  dissected games shows the divergence is a TURN-1 LAND-CHOICE TIE FLIP into a different
  FETCH -> reshuffle -> decorrelated draw stream -- the disposition-flip apparatus class, not a
  main-2 decision at all. Whether the new tie resolution is systematically worse (12-2 is
  asymmetric, ~0.7% under pure luck) is exactly what the held-out adoption A/B must decide;
  per-game dissection cannot (the games are incomparable after the shuffle).

## Suite A/B round 2 (2026-08-15) -- what the phase boundary was secretly doing

Round 1's lesson (burn: bolts classified Main2 starved prowess -- fixed via the prowess
stand-down, f4698d1) generalizes: **the m1/m2 boundary carried implicit semantics beyond
scheduling, and each collapse defect is one of those semantics surfacing.** Antilife (+0.15
at d3, 35 games worse / 1 better -- far too asymmetric for the fetch-flip apparatus class)
root-caused to TWO of them, dissected via gi=9 (identical draws, pure play divergence):

1. **Enabler-first emission (fixed, necessary but small alone).** Base play cast Plague
   Drone in m1, so the m2 harvest saw a live Remedy and emitted Reverent Silence / stacked
   the safe payloads. In the collapsed main the enabler is still in HAND at enumeration, so
   the risky/speculative alt gates (all keyed on a battlefield inverter) never fired. Fix =
   the Swords `RemedyActiveOrInHand` precedent applied to alt payloads: collection-time
   in-hand emission (search nodes only, `MainPhaseFilterActive`-gated -> base byte-identical),
   plan validity `SubsetHasUnbackedAltPayload` (same-subset enabler required; Reverent
   Silence needs a CREATURE enabler unless its payload is lethal on the spot), enabler-first
   `CastOrderRank` + the existing cast-time re-check make it safe end to end. Measured:
   -0.02 alone -- necessary for the combo lines but NOT the main defect.

2. **Attack/cast mana ordering (fixed, the main defect).** Base order is casts -> attack:
   main 1 spends the mana, the attack gets the leftover bodies. The collapse inverts it:
   attack -> casts, so the greedy attack policy (exalted chip with a lone 0-power dork) taps
   a mana creature BEFORE the deferred main runs -- antilife's m2 saw 3 non-creature sources
   every turn and {3}{B} Plague Drone was unreachable at tempo (the FSLineTail plan dump
   showed the T3 m2 list as literally `[Fiery Justice] | []`). Fix =
   `DecisionProvider::AttackWith`, a NON-virtual wrapper every combat site now calls
   (declaration + all projections -- never call `ShouldAttackWith` directly at a combat
   site): under `TurnSolver::CollapsedMainActive` it holds an untapped 0-power mana dork
   when some hand spell is affordable only with creature mana, then defers to the
   archetype's `ShouldAttackWith`. Pure pass-through when the filter is off.

Result: antilife stack 4.4367 -> 4.3267 (control 4.2867 = GT exactly; 35 worse -> 11
worse / 1 better). **Residual +0.04 open**: gi=58 shows the collapsed m2 picking
{Swords,Swords,Cutter} (13 dmg) over the lethal {Cutter,FJ,Swords} (22 dmg) with identical
resources -- a post-combat plan-enumeration gap (not MAIN2_DROP; the design combo alone
reproduces it), next round's dissection target.

## Ranked next collapses (not yet built)

3. **Skip the always-empty m1 harvest on total-Main2 decks** (~37% of hinata's harvest calls).
   Exactness condition: with `deck_feeds_combat == false`, no haste access and no scaling
   attacker (the same inputs the classifier reads), every class maps Main2 — but a provider
   `MainPhaseOverride` returning Main1/Both would break a blanket skip, so the skip must be
   derived per-hand from the real classifier, or simply left to the Solve memo (a hit returns
   the empty plan without harvesting; covers all but the ~16% distinct states).
4. **Land-fan sharing**: `EnumeratePlansWithLand` re-enumerates the full spell odometer once per
   land candidate (5c: 78k host calls -> 335k inner enums, ~4.3x). The user's land-after-draws
   TIMING doctrine (recorded in `main-phase-classification.md`, unimplemented) removes part of
   this fan at the root by construction: plans with affordable draws emit ONLY the defer
   variant, and the land is chosen once, post-draw. This is the right route to the enum.m1
   residual — it deletes the calls instead of caching them.

## Status

* Instrument + collapse #1 built on branch `phase-1-2-deck-analyzer`, both DEFAULT OFF.
* Adoption path for #1: verify-mode sweeps over more seeds/decks, suite smoke + battery
  identity (zero diverged games required — this is a pure perf change, identity is the bar),
  then default-ON + `MTG_NO_SOLVE_MEMO` hatch. No GT rebaseline needed if identity holds.
