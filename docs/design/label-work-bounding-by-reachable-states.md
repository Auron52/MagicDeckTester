# Bounding the labeller's expensive cases by reachable STATES, not enumerated plans

**Status: DEFERRED (parked 2026-09-17 for budget). Direction set by the user; nothing built.**
Companion to `label-goff-tractability.md` (the measurements) and `bound-qualified-nowin-memo.md`
(the state-keyed refutation memo the ladder already has).

## 1. What the user asked for (2026-09-17, verbatim gist)

> What we really want is to figure out expensive cases and bound the work done by them. More of a
> "which states can be reached" type of thing rather than a full list of plans. We also want any
> combo-off cases to be detected and ideally even failed combo cases to be bounded.

And, on the bounded label (`MTG_LABEL_HORIZON`, tractability section 6): *"I doubt we want to
restrict it this way."* So the horizon cut is a diagnostic, not an adoption candidate, and the
labeller stays what it is today: an **exhaustive search to the turn-8 cap** (depth is
`max_turns - turn + 1`, i.e. "to turn 8"), one work budget of 1,000,000 virtual ms per sample with
the overrun guard armed (`MTG_VALUE_LABEL_BUDGET_MS`, 0 = unlimited), and the ladder paying only up
to the label's turn. Quality first: a label may not move later to buy time.

## 2. Where the work goes today

Each ladder pass is an exact proof at its cut turn, and its cost is proportional to the number of
**plans** (lines) enumerated, deduplicated only by state key within a pass (`MTG_LABEL_LADDER_DEDUP`)
and by the bound-qualified no-win memo across candidates. The long tail (section 6 of the
tractability doc) is samples with LATE or ABSENT wins: they pay every pass to turn 8, and the deep
passes of a combo deck enumerate every ordering of a loop that either closes (and then any
ordering wins -- a full list is waste) or cannot close (and then every ordering fails -- a full
list is waste again). The per-pass instrument (`[ladder] t<turn> cands=N result=R | dd0=..ms/applies`)
shows this directly: seed 900193's third sample spent 346 s and 2.2 M applies on pass 4 alone.

What already reasons over STATES rather than plans, and is therefore the seed of the design:

| Piece | Where | What it is |
|---|---|---|
| Winless certificate | `ProvenWinlessThisTurn`, `src/ai/DecisionProviders.cpp` | a monotone reachability fixpoint over resources (mana bound, unbounded-loop detection, dig/untap events, outlets); certifies a turn winless with no enumeration. Admissible: may only over-credit. Two under-credits fixed 2026-09-16 (tractability 6b); before that it over-labelled 19 of 1,067 reference samples. |
| Stuck-turn state closure | `MTG_WINLESS_DEVELOP` | infinite-mana turns bucketed go-off / stuck by proof; stuck turns close over states (`BuildDedupKey`), one leaf per state -- the user's doctrine. |
| Combo route (play side) | `Action::Kind::ComboRoute`, x999 = verified win this turn, x1 = develop | a positive go-off certificate the play search already trusts. |
| No-win memo | `bound-qualified-nowin-memo.md` | state-keyed refutations shared across candidates. |
| Dominance prune | `MTG_LABEL_GOFF_DOM`, default ON, width 128 | LOSSY by design; identical labels on the one game checked (900045). Under quality-first, a default to justify or retire. |

## 3. The shape of the design (a sketch, not a plan)

1. **Classify the tail before building anything.** Re-measure job 0 exactly (H=0) on the fixed
   commit -- the "unfinished in 7 h" and "900157 = 109 min" figures were taken with the broken
   certificate -- with the `[ladder]` instrument on, and sort the expensive samples into: late win,
   no win by 8, loop closes (go-off), loop cannot close (failed combo). Each class wants a different
   bound, and the ratio of plans to distinct states per pass (the memo counters) says how much a
   state-level treatment can recover.
2. **A positive certificate for go-off.** The winless certificate proves "no line wins"; its mirror
   proves "some line wins" from the same reachability fixpoint (loop reachable + outlet reachable +
   the bound covers the opponent's life). The play side has this as the verified x999 route; the
   ladder should accept it at a cut instead of searching for the line. A certified win at cut t IS
   the label t. Soundness bar: the certificate must be a real, executable win -- reuse the route's
   verification, do not re-derive it.
3. **Bound the failed combo.** When the fixpoint proves the loop cannot close this turn (a piece
   unreachable: not in hand, not diggable, not wishable within the mana bound), the enumeration of
   partial combo lines is pointless even when the turn is not winless overall (combat may still
   win). Today the certificate is all-or-nothing per turn; the finer result -- "the combo branch is
   dead, develop and combat remain" -- should prune the combo branches of the pass and leave the
   rest exact. That is the stuck-turn closure applied inside a pass.
4. **Work bounded per distinct reachable state.** The target cost model for a pass is
   O(reachable states at the cut), reached by keying the pass on the state closure rather than on
   plans; the dedup and the no-win memo are the existing halves of that key. Measure with the
   memo counters, not wall clock (see `search-cost-attribution-method`).

## 4. Constraints carried over

- Labels may not move later. Every bound here is a proof (a certificate) or a lossless closure;
  anything lossy is a diagnostic, like the horizon.
- The certificate is admissible in ONE direction only: it may over-credit resources and refuse to
  certify, never under-credit. Both 2026-09-16 holes were under-credits; the audit method that found
  them (bisect the label-path levers on an "earlier" row, then trace the winning line) is the way to
  find the next.
- The exact reference rows from the cancelled value-leaf run are over-labelled by the old
  certificate; the value-leaf re-run is a fresh queue on 1806a7e9 or later.

## 5. Step 3.1 ANSWERED for Fungus (2026-09-18): the tail is NO-WIN EDGE NODES

Section 3 step 1 asks to *"classify the tail before building anything"* into late win / no win by 8 /
loop closes / loop cannot close. For Fungus the answer is measured, and it is **none of the combo
classes** -- it is the plainest one, and the cause is a routing decision rather than a search defect.

Method: `MTG_WINLESS_STATS_EVERY=30` on a straggler that never finishes
(`--seed 901762 --game-index 12 --games 1 --threads 1`, phase-A label config). The live reporter
exists precisely for this class and is the only way in. After ~90 s:

```
=== [progress] at t7 cut=7 candidate 177/676 (max seen 960) ===
=== [progress] at t7 cut=7 candidate  37/332 (max seen 1100) ===
=== WINLESS CERT[m1]: checks=13212 fired=0 (0.0%) ===
=== WINLESS CERT scope: fsw nodes all=16006 label=15806 edge=13209
                      | plans all=873680 label=866773 edge=811799 ===
=== WINLESS SEED: tries=15809 wins=3 (0.0%) | edge tries=13212 wins=3 ===
=== WINLESS RESIDUAL: 13209 of 13212 edge nodes (100.0%) resolved by neither ===
```

* **93.7% of all expanded plans sit at horizon-edge nodes** (811,799 of 866,773). Those nodes answer
  one question -- "can I win THIS turn?" -- and they answer it by applying 300-1,100 plans one at a
  time.
* **They are 99.98% no-win**: 3 wins in 15,809 seed tries. This is the `g_res_win`/`g_res_nowin`
  reading section 2's table calls for, and it says the residual class is as prunable as a class gets.
* **The certificate fires 0 times in 13,212 checks**, so none of that is avoided.

**Why it fires zero: `ProvenWinlessThisTurn` is a provider hook with a `return false` generic, and
only `EldraziFlickerProvider` and `SnowProvider` implement it.** Fungus is recognised in
`DecisionProviders.cpp` but routed to `GenericProvider` -- a decision taken because the deck had no
measured *heuristic* to hold. The certificate is not a heuristic; it is the one hook on that
interface whose contract is a proof, and routing to Generic silently gave this deck the
no-certificate path. Any deck whose labels are expensive and whose provider is Generic has the same
hole, so this is worth checking before the next deck's value leaf is costed, not after.

**Consequence for this design.** Items 2-4 of section 3 (positive go-off certificate, bounding the
failed combo, per-reachable-state work) all target combo-shaped tails. Fungus's tail needs none of
them -- it needs item 1 of the table in section 2, the certificate itself, extended to a third
archetype. That is a smaller and much better-understood piece of work than the rest of this doc, and
it should be done first because it is the one with a measured 93.7% denominator behind it.

The full measurement, including the branching census that ruled out the cost-side explanations
(per-node constants, board size, candidate dedup, the activation fold), is in
`fungus-token-search-cost.md`, section "ROOT CAUSE #3".

## 6. Step 3.1 REFINED (2026-09-18, late): the carrier is the ANTHEM sourced from the LIBRARY

Section 5 named the certificate as the lever and the lord-library term as the biggest decline class.
Measured on the two phase-A stragglers that had to be cancelled after 6.5 h each
(`--seed 901386 --game-index 136`, `--seed 901103 --game-index 103`, phase-A label config,
`MTG_WINLESS_STATS=1`), the what-if counters that were built for exactly this question now answer it,
and they **refute the obvious tightening**:

```
FUNGUS WHAT-IF (library lord priced at >=3 mana, NOT APPLIED):
    would-fire=362  still-declines=61918   (0.6% of lord-library)
FUNGUS WHAT-IF CEILING (library lords DELETED -- unsound upper bound on the whole family):
    would-fire=362                          (0.6% of lord-library)
FUNGUS WHAT-IF JOINT CEILING (library lords AND anthem deleted -- unsound):
    would-fire=62280                        (100.0% of lord-library)
FUNGUS lord-library, WHERE THE ASCENSION IS: board=0  hand-only=10  library-only=62270
```

Three readings, and the third is the one that matters:

1. **The proposed tightening is worth 0.6%.** Pricing a library Sporecrown at >= 3 mana ({1} for the
   Psychotrope draw + {1}{G} for the cast) would certify 362 of 62,280 nodes. Not worth the risk of
   touching a certificate.
2. **The whole library-lord FAMILY is dead**, not just that candidate. Deleting the library lord
   credit outright -- unsound, and therefore a hard ceiling on any bound in that family -- also
   scores 0.6%. This is precisely what the ceiling counter exists to say, and it says: stop here.
3. **The joint ceiling is 100%.** Delete library lords *and* the anthem and every one of those
   62,280 nodes certifies. Since the lord half is worth 0.6%, the anthem term is carrying
   essentially all of it -- the two are CO-CARRYING and only the anthem is tightenable.

And `WHERE THE ASCENSION IS` closes the argument: **board=0, library-only=62,270 (99.98%)**. The
section-2 note reserved the possibility that these were board Ascensions, whose +5/+5 is real (no
mana, no draw, nothing to bound) and would have capped the whole line no matter what. They are not.
Every one of them is an Ascension **still in the library**, credited at full +5/+5 to every attacker
merely because a draw outlet is live.

### The next change, therefore

`ba_reachable` is set from a library-sourced Ascension inside the `draw_outlet && fodder > 0` block
with **no cost bound at all**, while `lords_lib` sitting three lines away IS bounded
(`std::min(lib_lords, fodder)`). Give the library-sourced anthem the same treatment the library lord
already has -- it must be drawn ({1} + a Saproling) and cast ({1}{G}) before it can pump anything,
and it needs `ba_threshold` quest counters on top. Bound it by the draw budget and a mana upper
bound (`UntappedManaUpperBound`, which already credits Utopia Mycon's sac-for-mana), exactly as the
lord term is.

**Do the what-if FIRST, again.** The same counter shape that just killed the lord candidate should
price the anthem candidate before any behaviour changes: the joint ceiling proves the anthem term is
where the mass is, but not that a *sound* bound on it recovers that mass. The 0.6% result above is
what this method is for -- the ranked guess would have built the lord bound.

### Soundness note for whoever builds it

`lords_lib` caps a sum of POWER BONUSES with a draw COUNT (`std::min(lib_lords, fodder)`). That is
exact only because the pool's one lord is +1/+1. A +2/+2 lord would make the cap UNDER-credit, which
is the one inadmissible direction. Cap the count and multiply by the max bonus.
