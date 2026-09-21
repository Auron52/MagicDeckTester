# New-card-only breakpoint continuations (`MTG_BP_NEW_ONLY`)

Self-contained. Built and measured 2026-09-21 as the answer to Snow's phase-A degeneracy
(`snow-breakpoint-degeneracy.md`: 2.9M breakpoint consultations per game, 900M-unit positions,
23-hour games). **Default OFF; per-deck opt-in via `DecisionProvider::NewOnlyBreakpointContinuations`.**
Adoption is a user decision -- see "Status" at the end.

## The rule, in the user's words

> *"letting plans be fully formed and run without stopping at the breakpoint and only do
> reconsideration of plans we haven't already done at the breakpoint. So, we would skip plans that
> only use existing cards at the breakpoint reconsideration."*
>
> *"we would always keep track of the new spells and abilities at each breakpoint and make full
> plans that use them. Only those plans would be emitted at the breakpoint."*
>
> *"The rule also needs to include abilities (that are newly accessible)."*

A breakpoint's continuation list is filtered so that an entry survives **iff** at least one of:

1. it **casts a card that arrived at this breakpoint** -- by NAME, with the staged-expiry exception
   (`BpNamePassedOnBefore`, the same test the candidate-level filter already uses: a second copy of a
   name the plan declined is not new; a copy that expires earlier is);
2. it **plays such a card as the land drop**;
3. it **activates an ability of such a card** (a found Scrying Sheets played by this very continuation
   and activated in its trailing pass -- the land axis enumerates on the post-drop copy, so the entry
   exists);
4. it **activates an ability of a permanent that entered this turn** (Arcum's Astrolabe cast by the
   plan's own prefix; a Sheets played as the base plan's drop). Such a permanent was not on the
   battlefield when the base plans were enumerated, so no sibling carries its activation -- site 9
   exists for exactly this gap. Keyed on `entered_this_turn`, which over-keeps a main-1 entrant at a
   main-2 breakpoint: the safe direction;
5. it **casts a card the plan itself still has pending** (`BpPlanCasts`, in hand before the site). At a
   truncating site the continuation is what realises the plan's own tail; dropping it would delete the
   plan's line rather than a copy. Inert at a trailing site (the plan's casts are done).

Everything else is dropped. **Deliberately no "pull-order" exception.** An old draw/tutor card cast
AFTER the site's look is a different library order from casting it before, and under clairvoyance the
two can put different cards in hand (a Skred on top whiffs the look in one order and is drawn past in
the other). USER: that line is only "useful" because the search can see the top card -- *"I don't care
about how effective our clairvoyance is"* -- so it is not a line to preserve.

## Why it is a plan-level rule and not another condemnation gate

The subset enumerator emits every payable subset of the hand. For a base plan `P` that reaches a
breakpoint, the sibling `P u {X}` was enumerated alongside it for every old card `X` that `P` could
have added. A continuation of `P` that casts only old cards is therefore that sibling's line with an
EMPTY continuation -- same cards, same activations, same end state -- reached a second time.

Condemnation tries to say this one CANDIDATE at a time, and needs seven guards to avoid deleting
`{X, F}` (F = the found card) along with `{X}`. On Snow those guards block nearly everything.
Measured on seed 901283 (the 22-hour game, bounded to a 3 s label ceiling), 1,249,500 consultations
with `MTG_BP_CLASSIFY=1 MTG_BP_CONDEMN_WHYNOT=1`:

| gate | blocked | share |
|---|---|---|
| `noplancast` -- the plan cast nothing, so "declined nothing" | 598,637 | 48% |
| `managrew` -- source count grew since the snapshot | 281,819 | 23% |
| `PEER` -- candidate's slot at/after the site's | 227,784 | 18% |
| reached the dominance test | 125,091 | 10% |
| ... spared by `newoption` (site put a new payable card in hand) | 45,057 | |
| **dropped** | **5,447** | **0.44%** |

`PLAN_CAST=0` doubles the drops (11,396) and moves units by 0.0002%; `CONDEMN_ACTIVATION=1` fires 30
times. And at site 8 the `newoption` guard fires by construction: the breakpoint only OPENS when the
found card is playable, which is the exact condition that guard spares everything on. So per-candidate
condemnation cannot reach this deck without deleting a guard the record shows is load-bearing
(`breakpoint-condemnation-status.md`, gi=1357).

At the PLAN level the distinction is free: `{X}` alone is a sibling's line and is dropped; `{X, F}`
uses the found card and stays; so does EMPTY (it is not in the list -- it is the base plan's own
continuation). No exclusive-slot exemption is needed because the alternatives it exists to protect are
all still in the list.

## Where it lives, and the lockstep

* The filter runs inside `BpDeriveContinuationList` (TurnSolver.cpp), after `EnumeratePlansWithLand`
  and before the ranking sort. That is the ONE derivation both worlds share: the rollout's
  `bp_searched_plan` and the executor's `resolve_draw_breakpoint` index the same list by position
  (`bp_choice`), so a list filtered in one world and not the other would replay a continuation the
  search never scored. Survivors take their true ranks.
* The snapshot it reads (`g_bp_hand_before`, `g_bp_plan_casts`) is bound by `CantripOrderScope`, which
  gained a `new_only` argument. Both worlds construct that scope at every breakpoint site already; the
  rollout's site-8 scope was gated on `BpClassifyActive` and now also constructs under this lever, with
  `classify_active` passed honestly so condemnation's own gate is untouched.
  `BreakpointHandSnapshotWanted` (both overloads) includes the lever, so the pre-site hand is captured
  on the cast hot path only when something consumes it.
* The bp-enum cache key (`BpEnumBuildKey`) already folds the hand snapshot and the cast set whenever
  they are bound, so two lines reaching one state under different snapshots do not share an entry.
  The lever binds only those two (not the site, not the ordering watermark), so the key gains exactly
  the folds the filter's output depends on.
* Human play is exempt (the human owns the continuation and sees the full menu).
* Firing counters under `MTG_ROLLOUT_STATS`: `[rollout-stats] bp_newonly lists= seen= dropped=
  drop_rate= kept_new= kept_plan=`.
* Heurarm slot `BP_NEW_ONLY` (`"flags": {"MTG_BP_NEW_ONLY": true}` per batch job), so both arms run in
  one pooled batch and the per-job arm folds into the memo keys.

## Measured

### Flag off: byte-identical

`test/regression.sh --smoke`: **87 passed, 0 failed, configs changed 0 / unchanged 87.**

### The 22-hour game, bounded (seed 901283 gi=33, `MTG_VALUE_LABEL_BUDGET_MS=3000`)

| | base | new-only |
|---|---|---|
| continuation entries seen / dropped | -- | 2,907,986 / **2,272,454 (78.1%)** |
| kept because new / plan-pending | -- | 556,860 / 78,672 |
| `snow_look_top` list length (mean / max) | 8.12 / 125 | **2.76 / 40** |
| `put_in_hand` list length (mean / max) | 5.00 / 96 | 3.02 / 39 |
| wave applies (`scored`) | 1,660,647 | 890,236 |
| ... landing on an already-seen state | 997,738 (60%) | 294,017 (33%) |
| label rows | 6 of 7 positions | 6 of 7, **byte-identical** |

At a FIXED ceiling units cannot move (the budget is the cap; freed work is re-spent), so this cell is
the soundness canary, not the cost measurement: same positions completed, same labels.

### Unbudgeted cost: the d2/b0 cell (40 games, seed 8008, two arms in one pooled batch)

| | base | new-only |
|---|---|---|
| units (sum over 40 games) | 81,679,226 | **34,336,603 (0.420x)** |
| per-game ratio | -- | 0.22x .. 0.96x, **every game cheaper** |
| batch ms (sum) | 1,355,542 | **683,498 (0.50x)** |
| avg win turn | 5.9750 | 6.0000 |
| games play-changed / better / worse | -- | 8 / 0 / 1 |
| entries dropped | -- | 9,945,660 of 13,366,667 (74.4%) |

The one worse game (gi=28, 6 -> 7) diverges **before turn 1**: the mulligan-to-six BOTTOMING pick
flips (base bottoms an Ice-Fang Coatl and keeps a second Forest; new-only keeps the Coatl). Bottoming
evaluates each six-card subset with rollouts at the real play depth, and at d2 the two subsets tie
closely enough that filtering the continuation lists flips the pick; everything after is a different
game with different draws. All seven other play-changed games (gi 0, 5, 8, 9, 12, 29, 38) diverge
in-game (turns 1-5, main 1) and end on the SAME turn. This is the class
`breakpoint-condemnation-status.md` records as "T1 land churn, not a deleted line".

**The gi=28 flip isolated.** Depth ladder (b0): d2 base 6 / new-only 7; d3 base 6 / new-only 7; d4 in
flight (`logs/snowdiag/gi28_*_d4.log`). With `MTG_BOTTOM_ROLLOUTS=0` (heuristic bottoming, identical
in both arms) **both arms win on turn 7** -- so the base's 6 is the rollout-bottoming evaluation's
pick, and what the filter changed is that evaluation's near-tie, not the in-play search. The
new-only arm's pick plays out exactly as the heuristic's does.

### The phase-A regime itself: the six heaviest phase-A games at a 30 s label ceiling

Same six games that ran 14-23 h in phase A (repro seeds from the queue's SLOW-GAME lines), both arms,
`MTG_VALUE_LABEL_BUDGET_MS=30000`, one process per game, all twelve concurrent:

| gi | base units | new-only units | ratio | base wall | new wall | positions labelled b/n | labels on common positions |
|---|---|---|---|---|---|---|---|
| 33 (the 22 h game) | 17,318,177 | 6,769,841 | **0.39** | 401 s | 170 s | 7 / 7 | identical |
| 214 | 29,377,326 | 23,191,076 | 0.79 | 557 s | 516 s | 6 / 7 | **turn 3 differs: 6.67 -> 7.33** |
| 13 | 33,912,609 | 16,251,588 | 0.48 | 622 s | 371 s | 5 / 5 | identical |
| 195 | 28,963,571 | 23,674,940 | 0.82 | 582 s | 555 s | 5 / 6 | identical |
| 64 | 30,213,514 | 13,860,504 | 0.46 | 502 s | 277 s | 5 / 6 | identical |
| 61 | 42,148,059 | 19,595,389 | 0.46 | ~700 s | 402 s | 6 / 7 | identical |

Every game cheaper, every game labels at least as many positions (four of six label MORE -- the
ceiling that dropped the base's position never trips), and 32 of 33 common labels identical.

**The one open item: gi=214 turn 3.** The value label is the mean over K=3 reshuffled libraries of the
ladder's EARLIEST achievable win, and the reshuffle is deterministic per (seed, turn, k), so a later
label under new-only means that on one reshuffle the filtered search found a later earliest win than
the unfiltered one -- i.e. a line the filter removed, on that library order. Not yet root-caused;
a 120 s-ceiling re-label of both arms is in flight (`logs/snowdiag/heavy6_120k/`) to rule the
ceiling in or out, and the next step is `MTG_BP_TRACE`-style tracing of that position's three
searches to name the line.

### Play-settings quality gate (d5/b20, 2,000 games per arm, four seed blocks, paired)

`logs/snowdiag/play_d5b20.json`, one pooled batch, `test/paired_arms.py`:

| block | base | new-only | delta | better / worse | units |
|---|---|---|---|---|---|
| 880000 | 6.0560 | 6.0480 | -0.0080 | 7 / 3 | 0.913x |
| 880500 | 6.0640 | 6.0580 | -0.0060 | 4 / 1 | 0.922x |
| 881000 | 6.0380 | 6.0300 | -0.0080 | 6 / 2 | 0.918x |
| 881500 | 6.0920 | 6.0820 | -0.0100 | 6 / 1 | 0.953x |
| **all 2,000** | **6.0625** | **6.0545** | **-0.0080 +/- 0.0027** | **23 / 7, sign p = 0.005** | **0.926x** |

Better on every block; 30 of 2,000 games moved. Batch ms per block 0.85-0.91x (contended by the other
experiments running alongside -- take the units).

## Status

BUILT, DEFAULT OFF, flag-off byte-identical suite-wide (smoke 87/87). Measured on Snow: a QUALITY
WIN at play settings (-0.0080, t ~ 3, all four blocks) at 0.93x units; the phase-A label regime
0.39-0.82x units on the six heaviest games with at least as many positions labelled; one label of 33
differs and is being root-caused. Per-deck adoption for Snow would be
`SnowProvider::NewOnlyBreakpointContinuations() { return true; }` plus a GT rebaseline of Snow's
suite keys (smoke `snow 3 1001 100 10` and the regression tier). **Not adopted -- the user's call**,
and the gi=214 label is the thing to close first.
