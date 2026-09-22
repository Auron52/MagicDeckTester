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

### The suite's own Snow keys under adoption, and the d3/b10 gate (2026-09-22)

With the per-deck route switched on, the smoke and regression tiers moved Snow's four searched keys
(d0 untouched, every other deck untouched):

| key | base | adopted | games moved |
|---|---|---|---|
| smoke d3 s1001 (100) | 6.1300 | 6.1500 | gi=11 6->7, gi=14 8->unwon |
| smoke d5 s1001 (50) | 6.2400 | 6.2800 | the same two games, the same way |
| regression d3 s2002 (60) | 6.1167 | 6.1167 | play differs, score unchanged |
| regression d3 s3003 (60) | 6.0000 | 6.0167 | gi=8 5->6 |
| regression d5 s2002 / s3003 | 6.0333 / 6.1333 | same | play differs, score unchanged |

Three games a turn later, none earlier, out of 330 searched. `test/classify_turn_later.sh smoke` (the
mandatory pre-accept gate) classifies every one of them as **churn**: at 4x and 16x the case budget
the adopted arm recovers the base's turn (gi=11 -> 6, gi=14 -> 8, at d3 and d5; the regression game
-> 5 at 40 ms and 160 ms). Nothing was deleted; the fast line is reachable. Two of the three are
d3/b10 games, and the play gate above was d5/b20, so the shallow tier got its own gate:

**d3/b10, 2,000 paired games, held-out seeds** (`logs/snowdiag/play_d3b10/`):

| block | base | adopted | delta | better / worse | units |
|---|---|---|---|---|---|
| 9001 | 6.0200 | 6.0220 | +0.0020 | 5 / 6 | 0.887x |
| 9501 | 6.0120 | 6.0260 | +0.0140 | 2 / 9 | 0.902x |
| 10001 | 6.0940 | 6.0960 | +0.0020 | 2 / 3 | 0.900x |
| 10501 | 5.9700 | 5.9700 | 0.0000 | 2 / 2 | 0.894x |
| **all 2,000** | **6.0240** | **6.0285** | **+0.0045 +/- 0.0028** | **11 / 20, sign p = 0.15** | **0.896x** |

So the lever's quality effect depended on the search setting: a win at the deck's play settings
(d5/b20, -0.0065, 20 / 7) and a small, not-significant loss at the shallow diagnostic tier (d3/b10,
+0.0045, 11 / 20). Cost is lower at every setting. The user's read of that split: *"this is still
somewhat unexpected for searching further to give us worse results ... this seems more of a signal
than expected."* It was.

### The d3/b10 loss was a hole, not churn (2026-09-22)

The classifier's "churn" verdict only says the arm reaches the base's *turn* at a higher budget; it
does not say the base's *line* is reachable. The b0 probe does. Seed 1015, T5 (three Islands and a
Frost Augur; hand Boreal Druid x2, Abominable Treefolk, Rimefeather Owl, Arcum's Astrolabe): the
base wins through "cast Astrolabe (draw Ice-Fang Coatl), Augur ability, then Boreal Druid off the
Astrolabe's {1}: any colour", and that line existed ONLY as a breakpoint continuation -- no base
plan carried the Druid, because {G} has no green source on that board. Under the rule the Druid is
an old card, so the continuation is dropped and the line is unreachable at any budget, unbounded
included. Seeds 1012 (T3) and 3011 (T5) had the same shape.

The sibling-coverage argument behind the rule ("a continuation that casts only old cards is the
sibling base plan's line") holds only when that sibling is a *payable* subset at the base. It is not
when the old card is castable only via mana the plan's own cast provides. Two ways to close that:
keep a continuation whose old cast was not castable at the base (built, then removed), or make the
enumerator offer the sibling. The user's ruling: *"we don't want to add extra breakpoints or required
resolves for no reason"*; *"Astrolabe should be treated as a land or other mana source that is played
in the current plan"* (*"though I guess it is actually a filter, not a mana generation source ... But
it doesn't change the ruling"*). So: casts stay new by ARRIVAL only, and the enumerator is fixed.

Why the enumerator could not offer {Astrolabe, Boreal Druid}, in the order the gates were bisected:

1. `MTG_DBG_MULTI=5` showed the pair PASSING the mana gates (`reject=0`) and never being pushed.
   The reject dump fires before the colour gates, so the tracer was extended (`[chain]` line at scope
   exit, armed under the same toggle) and named the final gate: `ef-no-interact`, i.e. the
   colour-PRESENCE check `SubsetPayable` rejected the pair and the sequenced-walk rescue declined.
2. `MTG_SUBSET_ROCK_COLOR` (staged 2026-09-03 for exactly this hole, now default ON and shared by
   both colour-presence gates) widens the board's colour table with the selected rocks' `rock_mana`
   -- but the Astrolabe's `rock_mana` was EMPTY.
3. Root cause, in `AnyColorFilterHasFedSlot` (SpellEffects.h): the fed-slot quota
   `k = min(F, floor((F + S) / 2))` counted filters ON THE BATTLEFIELD only, and the function
   returned "no slot" when that quota was zero BEFORE reaching its "not on the battlefield: stay
   permissive" branch. A hand Astrolabe on a filter-less board therefore read F = 0, quota 0, and was
   credited nothing at all: not by the flat gate, not by the colour widening, not by the count gate.

The fix (af84217a, `MTG_PENDING_FILTER_SLOT`, default ON, `=0` legacy, a heurarm slot): a pending
filter counts itself as one of the F filters and gets a slot iff adding it RAISES the quota, since it
enters behind every filter already there. Exact in both directions: three Islands and no filter ->
the pending Astrolabe is fed (0 -> 1); one Island already feeding an on-board Astrolabe -> it is not
(1 -> 1). The amount side was already right (`wild_phantom` marks the unit as a conversion of a
feeder the pool counts, so {Astrolabe, X} on one Island still fails the amount precheck). Only Snow
holds an `any_color_filter` rock; Capital City is a land and is on the battlefield by the time the
enumerator runs. An earlier piece (54666bf4, `PendingFilterInHand` arming the filter-aware payment
fallback) stays: it lets the real-payment simulation see a castable filter in hand and on its own
moved Snow's d0 key 6.7210 -> 6.7040.

Probes at d3/b10, base / new-only: 1012 6/6, 1015 8/8 (the Druid is now cast BEFORE the Augur
activation, as part of the plan), 3011 5/6 -- and 3011 recovers at unbounded budget on both arms
(5/5), so that one is churn. Flag-off smoke on the fix: snow d0 6.7050 (was 6.7210), snow d3 6.1400
and d5 6.2600 (both from gi=2 6 -> 7: T2 now casts Astrolabe + Druid and attacks instead of
activating the Augur, the draws diverge from T3 -- a different physical game), plus the play-changed
FiveColour d0 key from the rock-colour flip and the other agent's five. Re-measurement of every cell
on the fixed engine, with the legacy enumerator as a third arm: `logs/snowdiag/chain_v5.sh` (the
tables above are the PRE-fix numbers; the post-fix ones follow).

### Post-fix: both play gates, three arms in one pooled batch (2026-09-22)

One `mtg --batch` of 12,000 games (`logs/snowdiag/play_gates_v5.json`): the d3/b10 seed blocks
9001-10501 and the d5/b20 blocks 880000-881500, 500 games each, arms `legacy` (enumerator hole
open), `base` (hole closed) and `newonly` (hole closed + the route). Paired on the same seeds.

**The enumerator fix on its own (`base` vs `legacy`)** -- what "treat the Astrolabe as a mana
source played in the plan" costs or buys at searched depth:

| cell | legacy | fixed | delta | better / worse | units |
|---|---|---|---|---|---|
| d3/b10, 2,000 | 6.0245 | 6.0240 | -0.0005 +/- 0.0018 | 7 / 6 | 0.987x |
| d5/b20, 2,000 | 6.0615 | 6.0590 | -0.0025 +/- 0.0021 | 11 / 6 | 0.993x |

Neutral at d3, slightly better at d5 (not significant), better at d0 (the suite's 1,000-game key:
6.7210 -> 6.7050), and no cost. Not a trade-off: it is a correctness fix that happens to measure
well.

**The route on the fixed engine (`newonly` vs `base`)** -- the numbers the adoption call is made on:

| cell | base | new-only | delta | better / worse | sign p | units |
|---|---|---|---|---|---|---|
| d3/b10, 2,000 | 6.0240 | 6.0230 | -0.0010 +/- 0.0022 | 11 / 9 | 0.82 | 0.898x |
| d5/b20, 2,000 | 6.0590 | 6.0520 | -0.0070 +/- 0.0022 | 17 / 3 | 0.003 | 0.932x |
| d2/b0, 40 | 5.9750 | 6.0000 | +0.0250 (one game) | 0 / 1 | -- | 0.453x |

The d3 loss is gone (was +0.0045, 11 / 20; now -0.0010, 11 / 9): it was the hole, exactly as the
user read it. The d5 win holds (was -0.0065, 20 / 7; now -0.0070, 17 / 3) and every seed block is
better. Against the legacy enumerator the two together are -0.0015 (17 / 14) at d3 and -0.0095
(26 / 7, p = 0.001) at d5, at 0.887x / 0.925x units.

**The label regime on the fixed engine** (the six heaviest phase-A games at a 30 s label ceiling,
both arms concurrent; `logs/snowdiag/heavy6_v5/`):

| gi | base units | new-only units | ratio | base s | new s | positions b / n | labels |
|---|---|---|---|---|---|---|---|
| 33 | 18,687,285 | 9,377,508 | 0.50 | 265 | 142 | 7 / 7 | identical (7) |
| 214 | 29,381,043 | 20,204,570 | 0.69 | 334 | 293 | 6 / 7 | identical (6) |
| 13 | 20,483,931 | 11,474,256 | 0.56 | 253 | 175 | 5 / 5 | identical (5) |
| 195 | 29,277,306 | 25,205,010 | 0.86 | 336 | 369 | 5 / 6 | identical (5) |
| 64 | 30,263,071 | 14,831,266 | 0.49 | 304 | 187 | 5 / 6 | identical (5) |
| 61 | 42,162,523 | 20,362,819 | 0.48 | 430 | 253 | 6 / 7 | identical (6) |

Every label the base produced, the new-only arm produced identically, and it labelled at least as
many positions in each game (one more in four of them: the cheaper arm fits another position under
the same ceiling). gi=214 at the 120 s ceiling, both arms: 0.37x units (55.3M -> 20.2M), 572 s ->
290 s wall, 7 / 7 positions, all seven labels identical.

### The suite's keys on the fixed engine, route off and on (2026-09-22)

Every Snow key moves under the enumerator fix alone (a hand Astrolabe is now a mana source, so
every searched game re-plans), and FiveColour's smoke d0 key moves under the rock-colour widening
(52121d78) at an identical score; every other suite key is byte-identical to ground truth. The
route on top of the fix changes play in every Snow key but the score in only one:

| key | ground truth | fix, route off | fix, route on |
|---|---|---|---|
| snow smoke d0 s1001 (1,000) | 6.7210 | 6.7050 | 6.7050 |
| snow smoke d3 s1001 (100) | 6.1300 | 6.1400 (gi=2, 6 -> 7) | 6.1400 (same game) |
| snow smoke d5 s1001 (100) | 6.2400 | 6.2600 (gi=2, 6 -> 7) | 6.2600 (same game) |
| snow regression d0 s2002 (1,000) | 6.7210 | 6.6930 | 6.6930 |
| snow regression d3 s2002 (60) | 6.1167 | 6.1167, no game moved | 6.1167 |
| snow regression d3 s3003 (60) | 6.0000 | 6.0000, no game moved | 6.0167 (gi=8, 5 -> 6) |
| snow regression d5 s2002 (30) | 6.0333 | 6.0333, no game moved | 6.0333 |
| snow regression d5 s3003 (30) | 6.1333 | 6.1333, no game moved | 6.1333 |
| fivecolour smoke d0 s1001 (1,000) | 5.5690 | 5.5690, digest only | -- |
| fivecolour regression, all 5 | -- | byte-identical | -- |

The route's one score change, regression d3 s3003 gi=8, is seed 3011: the game probed above, which
recovers to 5 at four and sixteen times the budget and at unbounded budget on both arms (churn).
The two games the FIX moves at searched depth are both smoke gi=2 (seed 1003), and they are the hole
itself seen from the other side. Turn 2, two Snow-Covered Islands, Frost Augur out, Astrolabe and
Boreal Druid in hand: the legacy enumerator could not cast the Druid (no Forest; the Astrolabe's
mana was invisible), so it activated the Augur, which put a Snow-Covered Island into hand. The fixed
enumerator casts the Druid off the Astrolabe and attacks for one. From there the fixed line is one
library card behind at every draw, Abominable Treefolk arrives a turn later, and the game ends a
turn later. At d5/b20 the fixed engine recovers to 6 at 16x budget (churn). At d3 the classifier's
4x and 16x re-runs still say 7, but at 1,000x (`--budget-ms 10000`) the fixed engine takes the
Augur line itself and wins on 6, identical to the legacy game to the last turn -- so this too is
budget churn, only with a recovery point the classifier's two rungs do not reach. It is a judgment
the legacy engine never had to make; across the 2,000 paired d3 games the fix is
-0.0005 +/- 0.0018, and at d5 -0.0025 +/- 0.0021.

**An open oddity of the UNBOUNDED path, not of the fix (recorded, not chased).** The same game at
d3 with `--budget-ms 0` on the fixed engine wins on 8: it takes the Druid line at T2 and then, on
T6, with Abominable Treefolk just drawn, three Islands, Rimewood Falls, Scrying Sheets, Coldsteel
Heart, the Druid and the Astrolabe all untapped and six lands in hand, its main phases do
NOTHING -- no land drop, no cast -- and it casts the Treefolk on T7 (`logs/snowdiag/
b0_fixed_1003_trace6/`). The fix's code is not on that path (no rock in hand at T6; the pending
branch is reachable only through a hand rock), and the bounded 10 s run above does not do it.
`MTG_TRACE_SOLVE_TURN=6` shows every traced T6 decision is a depth-1 rollout leaf (33,398 of them,
committing "Treefolk, win 7" in 18,022), and no root-level T6 block at all: the unbounded root runs
through `FullSearchLine`, which that tracer does not cover. Repro:
`./build/Release/mtg decks/Snow/Snow.cod --cards-json src/cards/data/cards.json --profile
decks/Snow/Snow.profile.json --games 1 --seed 1003 --depth 3 --budget-ms 0 --ignore-play-profile
--threads 1 --log-dir <dir>`. Unbounded budget is not a production setting for any deck.

## Mint credit: `MTG_MINT_CREDIT_EXACT` (2026-09-22)

The first Mirrorwing measurement of the new-only rule lost games whose kill needed a Treasure that
a Gold Rush in the SAME plan had just minted: the continuation that spent it cast only old-hand
cards, so the filter dropped it, and a "needs a minted Treasure" keep was added to spare it. The
user ruled that keep the wrong layer:

> *"The right fix might be to credit it correctly instead."* *"We make the credit work for
> everything else in the plan that produces mana, so following that rule we should count creation
> of treasures."* *"We should not need to reconsider things from the original hand."* *"Otherwise
> we need to randomly open breakpoints on treasure creation and worry about lines that we already
> deleted."*

So the general test every breakpoint site has to pass: **a breakpoint exists for a card that
ARRIVES** (a draw, a dig, a reveal, a tutor -- something the base could not enumerate because it
was not in the original hand). Mana a plan's own action PRODUCES is not an arrival; it is priced
at the base like a ritual's float or a rock's tap, and the plan that spends it is a base plan. A
line payable only because a DRAWN card produces mana is new by definition, and the new-only rule
keeps it on that ground.

And the audit half, also the user's: *"I see a purpose to having a framework to locate bugs like
this one with the extra treasure token. We do not want to hide these cases, so we should have a way
to run things with extra breakpoints and reconsiders in order to ensure there are no bugs with the
full line version."* / *"Because the full-line version is a bit bug-prone I don't want to rely on
it fully in isolation."*

### What the lever changes (`MTG_MINT_CREDIT_EXACT`, heurarm slot, default OFF, byte-identical off)

1. Emission stamps `Action::mint_gain` = the exact Treasure count the cast will realise on the
   current board (`MintedTreasuresForCast`: the magnet fan and the Frontline Heroism copies, per
   target). The shipped credit counted one per minting cast.
2. Both odometer mana gates (`ManaPruneBound`, the selection-exact `BuildManaGateIndex`) credit it.
   Neither credited a mint before, so the shipped consider() credit was dead for exactly the
   total-mana shortfall it was written for: `{Gold Rush, Fists}` = 4 pips against a 3-mana pool
   was skipped at the odometer and never priced.
3. Both pricing twins (Solve / EnumeratePlans consider()) credit it, with the payer's own
   spendability gate (magnet live, or Heroism live under the fresh hold) and a sequential
   first-minter precondition (the minter itself is paid from the board).
4. An `{X}` trick is offered at the X the same-plan mint funds.
5. A magnetless subset payable only with its own mint is admitted tagged `freshmode_choice = 1`.
6. A Treasure-only trick payload opens NO breakpoint (`MintPayloadOpensBreakpoint`, one predicate
   read at the rollout arming, the plan's site mask, the executor's d0 pass and its node twin); a
   draw payload still opens it (a drawn card is an arrival). The "needs a minted Treasure" keep is
   off under the lever.

`MTG_BP_MINT_SITE` (slot) re-opens the Treasure-only breakpoint under the lever: **the audit arm**.
`MTG_BP_REPLAY_COST` (slot) is a lockstep fix the work surfaced (`rollout-executor-lockstep.md`
#7) and rides with the lever in every arm.

### The audit route, as a recipe

One pooled batch, three arms on paired seeds: `base` (no flags), `exact` (`MTG_MINT_CREDIT_EXACT`
+ `MTG_BP_REPLAY_COST`) and `exactsite` (the same + `MTG_BP_MINT_SITE`). Read with
`test/paired_arms.py --base exact --arm exactsite --list-moved`. A game the audit arm wins earlier
is a line the base could not enumerate or price: a bug in the full-line version, to be root-caused
in the base (never fixed by re-opening the breakpoint). `MTG_BP_NEW_ONLY_DRY=1` is the same
detector for the new-only filter itself (every reconsideration kept). The per-game reads that
closed each gap: `MTG_FSW_TRACE=1 MTG_FSW_TURN=n MTG_FSW_LINE=1` (`[fsw]` per node plan, target,
mint width, post-apply life), `MTG_FSW_BOARD=1` (the recorded continuation and the post-apply
board), `MTG_BP5_TRACE=1` (`[bp5]` per site-5 consultation: choice, eligibility, list length),
`MTG_PREPAY_PROBE`, `MTG_ORDER_RANGE_PROBE`, `MTG_EXEC_TAP_TRACE`, `MTG_DBG_MULTI=<turn>`
(`[dbgapply]`: the realised order of every opaque apply on that turn -- hoist, enablers, land, pool
-- and each cast's outcome as `hand=<before>-><after>` copies of the name plus the pool after it;
read the two together, since a cast whose own draw puts another copy in hand reads before == after.
This is the rollout's own read, where `MTG_TAPDBG` prints only under the executor), `MTG_FD_TRACE=1` (`[fd]`: each turn's
searched line, its win turn, whether it was verified in horizon and committed, and each later turn's
POP of the committed phase or FALLBACK to a fresh search -- the read that separates "the search never
found it" from "it found it and did not commit it"), `MTG_FS_ROOT_DUMP=<turn>` (`[fs-root]`: every
root plan's tail win per deepening pass).

### The base-line gaps the audit route found (all lever-gated, flag-off byte-identical)

| # | seed | gap | fix |
|---|---|---|---|
| 1 | 700252 | the funding ladder walked the wrong cast when the MINTER was the one failing | ladder walks the failing minter earlier |
| 2 | 701456 | the whole-turn prepay paid a mint line by tapping the pump target | prepay declines a mint line when every hold rung fails (`PP_MINT_HOLD`), so the per-cast payer cracks the Treasure |
| 3 | 701456 | `mid_turn_casts` flagged by a minter with no open breakpoint burned Treasures the pump counts | flagged only while the mint breakpoint is open |
| 4 | 700628 | the reviewed cast order paid Oracle's off the last Forest and Gold Rush then tapped the target | the ladder projects with the pump target HELD first, plain projection as fallback |
| 5 | 700628 | the per-cast payer's equal-rank dork tie was battlefield order = the target | pump-target narrow rung in `TapForCostSharedImpl` |
| 6 | 701403 | seven pips on five board mana + two minted was never enumerated: the same-plan Heroism copies were not credited | `SamePlanHeroismMint` in both gates and both twins; Heroism releases the fresh hold; `FirstUnpayablePos` reads the live width |
| 7 | 701403, 700799 | every explicit trick target was the Hierarch the payment must tap; the Soldiers a same-plan Heroism makes have no number at enumeration | cast-time target `kTrickBestOwnTarget` resolved by `FindBestOwnAttacker` in the shared resolver; REPLACES the explicit target when the pre-plan best attacker is a mana dork (adding it instead cost 1.32x and starved the budget) |
| 8 | 701706, 700096 | fix 7's target resolved after payment tapped the only creature, found no ATTACKER and fizzled the whole trick, draw included | `FindBestOwnCreature` floor: a tapped creature is a legal target; only no creature at all fizzles |
| 9 | smoke d0 (greedy) +0.032 | fix 7's "no attacker" extra variant was enumerated in subsets that cast NO body: `{Gold Rush@best}` alone on an empty T2 board, picked by the greedy for its own-pump value, fizzled -- no Treasure. The searched tiers never chose it (the rollout sees the fizzle) but paid to enumerate it | `SubsetHasMissingTrickTarget` rejects the variant in a subset without a body-making cast when no attacker exists (board fact once per enumeration in `SubsetFilterPre`) |
| 10 | regression d3/d5 s3003 gi97 (seed 3100, 4 -> 8) | LOCKSTEP: the replay derived its payment traits from one nesting level of the record (`mana_casts=1`, Treasure held, Anger paid off the pumped Hierarch) where the rollout had paid under the whole planned continuation (`mana_casts=2`, Treasure cracked, 15-power attack) | the record carries the scope it was paid under (`rec_mana_casts` / `rec_pump_target`; `rollout-executor-lockstep.md` #8) |
| 11 | mirrorwing 2HG seed 1012 T4 (smoke d3 gi11, 4 -> 5 at 1x, 4x and 16x budget; the T2 root's `{Needle}` projects T4 at flag-off and T9 on the arm -- the one worse smoke game that was a REACHABILITY gap, not a tie-break) | two halves. The same-plan Heroism credit was ONE copy where a magnet target fans onto the Heroism's ETB Soldier AND its trigger's Soldier: `{Heroism, Gold Rush@Dragon}` mints FOUR (the apply's own count: original, Heroism's copy, the Dragon's copies onto both Soldiers), credited two. And the credit's precondition counted EVERY hoisted enabler ahead of the minter -- Ignoble Hierarch is a creature, so Heroism + Hierarch + Gold Rush = 6 on 5 mana failed it -- where the apply's hoist would have put the Rush right after the magnets, BEFORE the Heroism (one Treasure either way). Flag-off wins through the Treasure breakpoint (`{Heroism, Gold Rush}` + continuation `{Hierarch, Fists}`); with that route closed by the rule the four-cast kill was unreachable at any budget | `SamePlanHeroismMint` counts the fanned bodies (`Action::mint_magnet` stamped at emission beside `mint_gain`); a hoisted minter waits for the LONGEST PREFIX of the hoist the base pool pays together with it (`PlaceHoistedMinters`, both apply twins -- the user's "after the Magnets at the earliest, but preferably you would be able to wait"), and the pricing twins credit exactly the Heroisms in that prefix (`MintHoistPrefixHeroism`: the same walk, so credit and realisation agree). A THIRD half surfaced once those two were in and 1012 was still T5: `[dbgapply]` showed the hoist order right (`[Heroism, Gold Rush, Hierarch]`) and Heroism paid `5(r4 g1) -> 2(r2 g0)` -- the per-cast payer's depletion "leave out if you can" hold, judged one cast at a time, kept a Needle counter and spent the FOREST on Heroism's generic pip, so Gold Rush's `{G}` was stranded (`[dbgapply]` read the Rush still in hand at an empty pool) and the whole line collapsed. The hoist up to and including the first minter is now paid JOINTLY (`BatchPrepayMintPrefix`, after `PlaceHoistedMinters` in both apply twins, through `BatchPrepayMainCasts(mint_prefix=true)`, which skips the `PP_MINT_HOLD` decline because a base-pool prefix has no later Treasure to meet). After all three: 1012 T4 at d3 b10 and b40 (Heroism `pool_after=2(g1 w1)`, Gold Rush mints four, Hierarch and Fists cast), 3100 stays T4, flag-off byte-identical |
| 12 | mirrorwing regression d3 s3003 gi61 (seed 3064, 4 -> 5 at d3 b10, b40 and b160; BOTH arms T4 at the shipped d5 b20) | OPEN -- a d3-only reachability gap the credit exposes, pinned to the ply but not fixed. `MTG_FD_TRACE`: flag-off's T2 search commits a VERIFIED T4 line `{Crag} -> {Heroism} -> {Forest; Anger}` (Anger's Heroism copy draws Mystic and Oracle's; the breakpoint casts Oracle's, whose copy draws Gold Rush and Heroism; Gold Rush mints two; Fists off the two Treasures: 28 damage). On the arm the same T2 root reads tail=4 at the d1 and d2 passes -- the GREEDY tail finds the kill -- and tail=5 at the d3 pass, whose searched T4 ply cannot reach it, so the arm commits `{Crag} -> {Hierarch, Fists} -> {Anger, Heroism, Mystic}` and wins T5. The Anger breakpoint's continuation list is IDENTICAL on both arms (10 entries, 7 kept; `{Oracle's}` alone is kept rank 6, past W=2 and past the chain slot, which resolves to `{Fists, Mystic}`), and the line then needs a nested Gold Rush pass and a post-mint pass. The winning leaf returns before any node print, so the route flag-off's ply takes to rank 6 was not read directly. Single-flag probes on the fix-11 binary: `MINT_CREDIT_EXACT=0` alone -> T4; `BP_NEW_ONLY=0`, `BP_REPLAY_COST=0`, `MTG_BP_MINT_SITE=1` (the audit route: post-mint site re-opened -- so the closed site is NOT the whole story), `MTG_ORDER_OPAQUE=1`, `MTG_BP_NESTED_CANON_PLAYOUT=1`, `MTG_BP_DEPTH=2`, `MTG_BP_SEARCH=4` -> all T5 | none yet. Next step when picked up: a per-gate mask over the credit's fourteen `MintCreditExactOn()` readers (one build, fifteen runs of `logs/snowdiag/envpair.sh`) to name the gate the d3 ply loses the rank-6 route through; then decide whether it is the odometer credit, the fresh-hold clause or the ladder. Time-boxed out (the shipped depth is unaffected) |

### Measured (Mirrorwing, shipped d5/b20, paired: 4 x 500 games 20-life + 2 x 500 2HG)

| build | lean vs base (mw) | 2HG | units | audit vs lean |
|---|---|---|---|---|
| v13 (fixes 1-3) | -0.024 (52 / 5) | -- | 0.91x | 52/1 == |
| v15 | -0.028 (56 / 3) | -0.035 (39 / 4) | 0.91x | == |
| v17 (fix 7 as an EXTRA variant) | -0.0345 (74 / 7) | -- | **1.32x** | == |
| v19 (fix 7 REPLACING) | -0.0395 (83 / 6) | -0.052 (63 / 11) | 1.07x | 5/5, 4/5 |
| v20 (fix 8) | -0.0410 +/- 0.0048 (84 / 4) | -0.0600 +/- 0.0082 (65 / 5) | 1.07x / 1.09x | 5/6, 2/5 (flat) |
| **v21 (fix 9)** | **-0.0430 +/- 0.0048 (84 / 1)** | **-0.0610 +/- 0.0080 (64 / 3)** | **0.92x / 0.93x** | 5/4, 2/3 (flat) |
| v21 + `MTG_BP_NEW_ONLY` | **-0.0445 (87 / 1)** | **-0.0620 (67 / 5)** | **0.83x / 0.83x** | 7/4, 6/5 vs lean |

Lean and audit are flat against each other at every build (the detector finds nothing left), and
new-only on top of the lever is the best arm at the lowest work. Remaining lean-worse games at v21:
700215 (a base-plan fan-out cap case, below) and three 2HG games (700001 gi214 = the same seed,
gi283 7 -> 8, 700501 gi271 4 -> 5; unexamined).

### The suite's smoke tier, v21 binary, env-flag arms against flag-off (2026-09-22)

Three sequential `regression.sh --smoke` runs on one binary (the suite is the A/B; the five red
d3 keys another agent owns fail identically in all three and are excluded here):

| arm | keys better | worse | digest-only | game-weighted delta | total case ms |
|---|---|---|---|---|---|
| lever (`MINT_CREDIT_EXACT` + `BP_REPLAY_COST`) | 4 (mirrorwing d0 -0.100, d3 -0.047, d5 -0.040, 2hg d3 -0.020) | 0 | 3 (antilife d3/d5/2hg d3) | -0.0037 | 0.99x |
| lever + `MTG_BP_NEW_ONLY` everywhere | 10 (the four above plus auras d3, creature_giving d3/d5, critter d3, hinata d5, hinata2hg d3) | 0 | 8 | -0.0040 | 1.02x |

The antilife digest moves are the pump-target hold (fixes 4/5) firing on a deck with own pumps;
replay-cost alone is byte-identical there. Before fix 9 the same runs showed mirrorwing d0 at
+0.032 (the fizzle), which is what found it.

### The fan-out cap is not the answer (measured)

700215's lean loss is `MTG_BP_MAXBASE` (16): only the first 16 breakpoint-opening base plans in
sorted order get wave-0 variants, the lever's extra payable three-cast mint plans outrank the T2
cantrip plan, and its CHAIN SLOT (the continuation that opens a further breakpoint) vanishes --
the deferred wave walker starts a capped plan at rank 0 but hands out numeric ranks only, so the
chain slot, the empty arm and the uniform arm are wave-0-only. Cap 256 wins the game. Cap 32,
measured on the same 3,000 paired games: base 0 / 1 moved, lean 1 / 0 moved, +1-2% units, 700215
not flipped. Not adopted. The targeted remedy, if ever worth it, is to hand a capped plan its chain
slot before rank 0 in the waves.

## Status

**ADOPTED GLOBALLY 2026-09-22** under the user's quality-and-speed rule (adopt when better or
neutral on both quality and speed and no win is made unreachable; discard when worse; bring a
net-positive trade to the user): `MTG_BP_NEW_ONLY`, `MTG_MINT_CREDIT_EXACT` and
`MTG_BP_REPLAY_COST` all default ON, in one adoption. The per-deck Snow route
(`MTG_SNOW_BP_NEW_ONLY`) and the new-only filter's "needs a minted Treasure" keep are deleted; the
lever's `=0` forms are the A/B hatches; `MTG_BP_MINT_SITE` stays as the audit arm.

The evidence, all on one binary against its own flag-off:

* Mirrorwing paired 3,000 at shipped settings (the deck the mint credit exists for): lean
  -0.043 (84 / 1) 20-life and -0.061 (64 / 3) 2HG at 0.92x units; with new-only -0.0445 (87 / 1)
  and -0.062 (67 / 5) at 0.83x; lean == audit.
* Smoke tier: 10 keys better, 0 worse, 8 digest-only at 1.02x case time. Regression tier: 11
  better, 4 worse, 20 digest-only, game-weighted -0.0013 at 1.11x summed case time (per-case ms
  in a pooled batch is scheduling, not compute: the hinata d0 cell that read 6.85x takes 0.2 s
  either way; the batch makespans were 100 s both).
* Snow's own record above (-0.0070 at d5/b20, 0.45x-0.93x units) stands; its eight keys move
  digest-only at identical scores under the global flip.

The accepted build (fix 11 included, rebased onto the wave fix, 2026-09-22 v26), one binary
against its own flag-off, flag-off identical to origin's rebaselined GT on every key but the
Snow / FiveColour keys the earlier flag-off filter-slot fix moved:

* Smoke tier: 7 keys better, 1 worse (hinata d3 +0.0067: seed 1060, the tie-break artifact
  below), 9 digest-only, game-weighted -0.0040 at 0.75x summed case time. Mirrorwing 2HG d3
  4.70 -> 4.64 (seed 1012 back to T4 -- gap 11 closed).
* Regression tier: 9 better, 5 worse, 21 digest-only, game-weighted -0.0015 at 0.73x. The five
  worse keys hold the same movers as before fix 11: hinata d3 s2002 / s3003 and hinata 2HG d3
  (seed 2068 tie-break artifact, seven draw-divergent games), creature_giving d5 x2 (two
  draw-divergent games). Mirrorwing d3 s3003 is BETTER (4.15 -> 4.125) and still holds seed 3064
  (gap 12, open) and the two batch-only movers (seeds 2156 and 3037: T4 standalone on the same
  binary, a turn later only inside the pooled tier -- the cross-game memo effect recorded below).
* Mirrorwing paired 3,000 at shipped d5/b20: -0.0450 +/- 0.0049 (88 / 1) 20-life, -0.0650 +/-
  0.0084 (70 / 5) 2HG at 0.80x / 0.79x units; the lean-vs-audit arms flat (5 / 3 and 2 / 3
  moved). Seed 3100 T4 standalone.

The regression tier's four worse keys were root-caused before the accept. hinata d3 s2002 gi66
(seed 2068, and its 2HG twin) has two parts. The by-name arrival rule read a second drawn Reality
Spasm as old when the main-2 kill `{Spasm, Spasm, Crackle}` needed both copies (the staged copy
from Soulfire's exile and the copy Ponder then drew; no sibling base plan has two) -- fixed by the
COUNT-AWARE arrival test (a name cast more times than the old hand holds copies is an arrival),
which the executor's main-2 derivation now keeps (`KEEP {Crackle, Spasm, Spasm} <new>` in the
`MTG_BP_NEW_ONLY_TRACE=5` read). The game's own 5 -> 6 is NOT that drop, though: it is a
TIE-BREAK ARTIFACT at T5 main 1. `MTG_FSW_TRACE` shows every T5 plan projecting a T6 win in BOTH
arms, at b10 and at b160 (no `tail=5` anywhere: the search never sees the main-2 kill from main
1). Flag-off wins T5 because its first-ordered plan `{Island, Ponder}` carries the CHAIN-SLOT
greedy continuation that casts the old Soulfire Eruption in main 1 (`after=15`), and the real
main-2 root, with the seven staged cards in hand, then finds the kill. On the arm that
continuation is an old card and is dropped, correctly; the equivalent base plan `{Island, Ponder,
Soulfire}` is in the list at the same T6 projection but later in order (Soulfire's greedy plan
value is 0, so the plan with it never outranks the plan without), and the strict `<` on the tail
keeps the first. The win is reachable; the tie-break does not pick it. That is a search
tie-break question (prefer the plan that does more at an equal projection, or give a seven-card
dig a plan value), recorded below, not a new-only defect. The other hinata movers are
draw-divergent (a fetch/shuffle resolved differently: variance, not a line lost); th d5 s3003
gi164 recovers at 4x budget (churn).
The audit also surfaced lockstep #8 (`rollout-executor-lockstep.md`): the replay's payment traits
covered one nesting level of the record where the rollout's covered the whole planned continuation
-- Mirrorwing seed 3100's committed T4 kill realised nothing -- fixed alongside.

Still open, recorded here rather than chased: gap 12 (Mirrorwing seed 3064, d3-only: the credit
arm's searched depth-3 ply cannot reach a rank-6 continuation the greedy tail and flag-off's ply
both find; shipped d5 unaffected), the non-hoisted minter's slot in `ord` (a minter that is not
hoisted is paid by the per-cast payer under the same depletion hold that stranded 1012 -- no prefix
prepay covers it), 700215 (the fan-out cap, above), the equal-projection
tie-break (hinata 2068 above: flag-off "does more" through the chain slot's greedy where the
search cannot tell, the arm "does less" because the first-ordered plan wins a tied tail -- a
general search change with its own measurement, and Soulfire Eruption's plan value of 0 is the
Hinata-specific half), three 2HG games in the lean's worse list, the standalone-vs-batch disagreement on a few seeds (a cross-game memo effect
in one process; classify with the harness's own numbers), the prepay generic-placeholder laundering
(`prepay-generic-placeholder-laundering.md`, a shipped-arm illegal line that flatters flag-off on
Hinata), and site 9's entered-this-turn gate (the other agent's side). Two things to know when
extending: the filter keys only `ActivatePermAbility` (other activation kinds are kept, so a deck
that leans on one gets less of the saving until its kinds are keyed), and the "no attacker"
cast-time trick target is legal only in a subset that casts a body.
