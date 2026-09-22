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
>
> And, clarifying the activation half after the first cut keyed it on the source having ENTERED this
> turn: *"the activation rule should be: 'activates an ability that was not previously available'
> just like the rule for when breakpoints occur. For example if you gave something haste or added
> counters to a Fungus so it can now activate, that activation should be a possible continuation (or
> should be part of a continuation)."*

A breakpoint's continuation list is filtered so that an entry survives **iff** at least one of:

1. it **casts a card that arrived at this breakpoint** -- by NAME, with the staged-expiry exception
   (`BpNamePassedOnBefore`, the same test the candidate-level filter already uses: a second copy of a
   name the plan declined is not new; a copy that expires earlier is). The other from-hand actions
   (suspend, channel, cycle, discard-to) follow the same test;
2. it **plays such a card as the land drop** (or puts one onto the battlefield by name -- a Vial or
   Stoneforge put of a found card);
3. it **activates an ability that was not previously available**. "Previously" is the state the base
   plans were enumerated from: every activation is enumerated against the battlefield the plan started
   from, so that is exactly the set a sibling could carry. `TurnSolver::PrePlanActivationKeys` captures
   one key per (permanent, ability) the enumerator would have emitted there, at ApplyPlanDirect entry
   and `TakeTurn` entry (the two points that already capture site 9's `pre_plan_numbers`), using the
   enumerator's own availability test: untapped and able to tap for a `{T}` mode (summoning sickness,
   haste), counters for a spore pop, the effective cost payable from the pre-plan pool **by colour**,
   the gated look's top-card test, a useful target for an ice counter. A continuation's activation whose
   key is absent is new -- the source entered during the plan (an Astrolabe the plan cast; a found
   Sheets this very continuation plays and taps), or it was there but blocked and something the plan
   did unblocked it (haste, a counter, the snow pip its Astrolabe now supplies). One whose key is
   present was enumerable at the base, so the sibling that carries it exists.
   Only `ActivatePermAbility` (every activation Snow has) is keyed; every other activation kind is
   **kept** and counted as `kept_unknown` -- the kinds with a target axis (blink, pod, an outlet's
   victim, a walker's Elk target) become emittable when the plan supplies the target, which a
   source-keyed snapshot cannot see, so keying one without its emission test in front of you would
   be an over-drop;
4. it **casts a card the plan itself still has pending** (`BpPlanCasts`, in hand before the site). At a
   truncating site the continuation is what realises the plan's own tail; dropping it would delete the
   plan's line rather than a copy. Inert at a trailing site (the plan's casts are done).

Everything else is dropped. The first cut's rule 4 -- "activates an ability of a permanent that
entered this turn" -- is subsumed: such a permanent is absent from the pre-plan set, so all of its
activations are new; and a main-1 entrant at a main-2 breakpoint, which that rule over-kept, is now
correctly in the set (its activations were enumerable at the main-2 base). **Deliberately no "pull-order" exception.** An old draw/tutor card cast
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
* The pre-plan activation set (`TurnSolver::PrePlanActivationKeys`) is captured once per apply at
  ApplyPlanDirect entry and once per phase at `AIEngine::TakeTurn` entry -- the same two points, and
  the same lockstep twin, as site 9's `pre_plan_numbers` -- and handed to every scope the apply opens
  as `acts_before`. It is empty (no battlefield scan, no pool) when the lever is off. The keys are
  `(card.m_number << 8) | PermAbilityMode`; the availability test is `BpAvailablePermAbilityModes`,
  a mirror of the enumerator's ModeSpec loop kept next to the filter's reader. Two caveats it
  inherits from site 9's capture: a `bp_resume` apply (MTG_BP_NODE) captures from the resumed
  mid-plan state, and tokens (number 0) are never keyed -- their activations always read as new.
* The bp-enum cache key (`BpEnumBuildKey`) folds the hand snapshot and the cast set whenever they are
  bound, and now the activation set too, so two lines reaching one state under different snapshots
  do not share an entry. The lever binds only those three (not the site, not the ordering watermark),
  so the key gains exactly the folds the filter's output depends on.
* Human play is exempt (the human owns the continuation and sees the full menu).
* Firing counters under `MTG_ROLLOUT_STATS`: `[rollout-stats] bp_newonly lists= seen= dropped=
  drop_rate= kept_new= kept_act= kept_unknown= kept_plan=` -- `kept_act` is the newly-available
  activation keep, `kept_unknown` the unkeyed-kind keep (0 on Snow; a deck where it is large is the
  place to extend the mirror).
* Site 9 itself still opens only for a permanent that ENTERED this turn (`PostEntryActivationPending`),
  so a haste grant or a counter added mid-plan does not open a breakpoint today. The user's
  clarification states the principle as the breakpoint rule; this filter implements the principle,
  and the site-9 gate is unchanged -- flagged, not altered.
* Heurarm slot `BP_NEW_ONLY` (`"flags": {"MTG_BP_NEW_ONLY": true}` per batch job), so both arms run in
  one pooled batch and the per-job arm folds into the memo keys.

## Measured

Two cuts were measured. The FIRST CUT keyed the activation half on the source having entered this
turn; the SHIPPED RULE (below, "new rule") keys it on the ability not having been available at the
pre-plan state, per the user's clarification. Where a table has both columns, the difference between
them is the activation clarification alone -- every other line of the filter is unchanged.

### Flag off: byte-identical

`test/regression.sh --smoke`: **87 passed, 0 failed, configs changed 0 / unchanged 87** -- on the
first cut and again on the new rule (44 s makespan).

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

| | base | first cut | **new rule** |
|---|---|---|---|
| units (sum over 40 games) | 81,679,226 | 34,336,603 (0.420x) | **37,068,211 (0.454x)** |
| per-game ratio | -- | 0.22x .. 0.96x, every game cheaper | **0.22x .. 0.96x, every game cheaper** |
| batch ms (sum) | 1,355,542 | 683,498 (0.50x) | 758,041 (0.56x) |
| avg win turn / play digest | 5.9750 | 6.0000 / `0fa61c9a` | **6.0000 / `0fa61c9a` (identical play)** |
| games play-changed / better / worse | -- | 8 / 0 / 1 | 8 / 0 / 1 (the same games) |
| entries dropped | -- | 74.4% | 73.4% (3-game probe; `kept_act` 7.4% of seen, `kept_unknown` 0) |

The new rule keeps ~8% more work than the first cut (the newly-available activations it now admits)
and plays these 40 games identically to it at d2: the admitted entries were scored and never won.

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

| gi | base units | first-cut units | **new-rule units** | ratio (new) | base wall | new wall | positions b / new | labels on common positions (new rule) |
|---|---|---|---|---|---|---|---|---|
| 33 (the 22 h game) | 17,318,177 | 6,769,841 | **8,463,329** | **0.49** | 231 s | 124 s | 7 / 7 | identical |
| 214 | 29,377,326 | 23,191,076 | **19,691,415** | **0.67** | 327 s | 277 s | 6 / 7 | **identical (the first cut differed at turn 3)** |
| 13 | 33,912,609 | 16,251,588 | **16,563,859** | **0.49** | 369 s | 240 s | 5 / 5 | identical |
| 195 | 28,963,571 | 23,674,940 | **23,772,343** | **0.82** | 342 s | 349 s | 5 / 6 | identical |
| 64 | 30,213,514 | 13,860,504 | **14,610,407** | **0.48** | 290 s | 190 s | 5 / 6 | identical |
| 61 | 42,148,059 | 19,595,389 | **20,390,543** | **0.48** | 420 s | 254 s | 6 / 7 | identical |

(Units under the label ceiling are deterministic -- the base column reproduced exactly across two
runs a day apart -- so the ratios are load-independent; the wall columns are from the new-rule run,
twelve processes concurrent plus one more, and are indicative only.)

Every game cheaper, every game labels at least as many positions (four of six label MORE -- the
ceiling that dropped the base's position never trips), and **all 33 common labels identical**.

**The gi=214 turn-3 label, closed.** Under the first cut this game's turn-3 label read 7.33 against
the base's 6.67, and it persisted at a 120 s ceiling (base 6.67, first cut 7.33), so it was a real line
the entered-this-turn rule removed on one of the three reshuffles -- not the ceiling. Under the new
rule at the same 120 s ceiling:

| turn | base | first cut | **new rule** |
|---|---|---|---|
| 1 | 7 | 7 | 7 |
| 2 | 6 | 6 | 6 |
| 3 | 6.67 | **7.33** | **6.67** |
| 4 | 6.33 | 6.33 | 6.33 |
| 5 | 6.67 | 6.67 | 6.67 |
| 6 | 8 | 8 | 8 |
| 7 | 7 | 7 | 7 |
| units | 54,245,759 | 23,191,076 | **19,691,415 (0.36x)** |

All seven labels identical at 0.36x the base's work. The removed line was an activation that the
first cut judged "previously available" because its source had been on the battlefield, and that the
new rule judges new because it was not activatable when the base plans were made -- exactly the
case the user's clarification names. (The new-rule units at 120 s equal its units at 30 s: its
searches complete inside the smaller ceiling; the base's do not.)

### Play-settings quality gate (d5/b20, 2,000 games per arm, four seed blocks, paired)

`logs/snowdiag/play_d5b20.json`, one pooled batch, `test/paired_arms.py`:

First cut:

| block | base | first cut | delta | better / worse | units |
|---|---|---|---|---|---|
| 880000 | 6.0560 | 6.0480 | -0.0080 | 7 / 3 | 0.913x |
| 880500 | 6.0640 | 6.0580 | -0.0060 | 4 / 1 | 0.922x |
| 881000 | 6.0380 | 6.0300 | -0.0080 | 6 / 2 | 0.918x |
| 881500 | 6.0920 | 6.0820 | -0.0100 | 6 / 1 | 0.953x |
| all 2,000 | 6.0625 | 6.0545 | -0.0080 +/- 0.0027 | 23 / 7, sign p = 0.005 | 0.926x |

**New rule** (`logs/snowdiag/play_d5b20_v2/`, same manifest, same seeds):

| block | base | **new rule** | delta | better / worse | units |
|---|---|---|---|---|---|
| 880000 | 6.0560 | 6.0500 | -0.0060 | 6 / 3 | 0.913x |
| 880500 | 6.0640 | 6.0600 | -0.0040 | 3 / 1 | 0.928x |
| 881000 | 6.0380 | 6.0300 | -0.0080 | 6 / 2 | 0.921x |
| 881500 | 6.0920 | 6.0840 | -0.0080 | 5 / 1 | 0.959x |
| **all 2,000** | **6.0625** | **6.0560** | **-0.0065 +/- 0.0026** | **20 / 7, sign p = 0.019** | **0.929x** |

Better on every block under both cuts; the new rule moves 27 of 2,000 games (the first cut 30) and
keeps ~0.3% more work. Same verdict as the first cut: a quality win at 0.93x units, now without the
removed line the first cut was carrying. The gate ran alone on the box this time (batch ms 0.93-1.04x
per block; take the units).

## Status

BUILT on the user's rule with the activation half as clarified ("an ability that was not previously
available"), DEFAULT OFF, flag-off byte-identical suite-wide (smoke 87/87, changed 0, on both cuts).
Measured on Snow under the shipped rule: a QUALITY WIN at play settings (-0.0065 +/- 0.0026, 20 / 7,
better on all four blocks) at 0.93x units; the phase-A label regime 0.48-0.82x units on the six
heaviest games with at least as many positions labelled and **every one of 33 labels identical**; the
one label the first cut moved (gi=214 turn 3) is identical again at 0.36x the base's work. Nothing
open on the measurement side.

Per-deck adoption for Snow would be `SnowProvider::NewOnlyBreakpointContinuations() { return true; }`
plus a GT rebaseline of Snow's suite keys (smoke `snow 3 1001 100 10` and the regression tier).
**Not adopted -- the user's call.** Two things to know before deciding: the filter keys only
`ActivatePermAbility` (every Snow activation; other kinds are kept, so another deck adopting it gets
less of the saving until its kinds are keyed), and site 9 still opens only for permanents that
entered this turn, which is narrower than the principle the clarification states.
