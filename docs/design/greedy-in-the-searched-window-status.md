# Greedy inside the search: what we found, and what it means

**Status: PAUSED 2026-08-23, resuming Monday. Nothing is running. Tree is clean.**
Self-contained — everything needed to pick this up is in this file and in git.

---

## READ THIS FIRST IF YOU ARE ANOTHER AGENT

**The OVERNIGHT ground truth is stale.** Dragonstorm's `GradesNoWinLeaf()` opt-out was removed
(commit `8dc20bdc`, USER-approved). Smoke and regression GT were rebaselined and accepted; **the
overnight tier was NOT**. Its `dragonstorm d3 s5005` cell contains game 227, which the change turns
from a turn-8 win into a loss at that gate cell, so **an overnight run will show a real regression
there. That is expected, not a new defect.** Either rebaseline the overnight tier
(`bash test/regression.sh --overnight`, inspect, then `--overnight --accept`) or leave it alone; do
NOT root-cause it as a fresh bug, and do NOT `--accept` it without inspecting.

Everything else is green: smoke 39/39, regression 65/65, reference gate 208 refs clean, CI green on
ubuntu + windows + determinism parity.

---

## 1. The question we were actually answering

The USER's standing directive is *"search should be truly search at every level. Greedy is simply
too unreliable to be part of it."* On 2026-08-23 that was sharpened to **"We shouldn't have any
greedy within the searched window."**

"Greedy" here means `TurnSolver::Solve()` — a fast one-shot heuristic that picks a play without
searching. The engine is supposed to use real search instead. The job was: find every place greedy
still runs inside the search, and remove it if removing it clears the adoption bar.

## 2. CORRECTION — there is no new design, and no demonstrated defect

An earlier version of this file (and commit `73e98ecd`) claimed the engine had a "pass design" in
which a greedy evaluation "IS the decision 83-99% of the time". **That framing was wrong and the
USER was right to reject it.** What is actually there is exactly the design the USER has been
working under.

`SolveWithLookahead` scores every candidate with a rollout, and **deepens that rollout until the
budget runs out** — the code calls it iterative deepening and its own comment says "evaluate EVERY
candidate at increasing ROLLOUT depth (sub_depth = 0, 1, ... depth-1)". So `sub_depth` is **the
depth of the rollout used to SCORE a candidate**, not a search-tree ply and not a separate
architecture. At `sub_depth = 0` that scoring rollout is greedy.

So the measured numbers mean:

| deck | decisions | got past the depth-0 scoring rollout |
|---|---|---|
| antilife | 7,046 | 38 |
| fivecolour | 238,775 | 12,543 |
| kitty | 135,455 | 23,223 |

read as: **at a 20-virtual-ms budget, most decisions can only afford the shallowest (greedy)
scoring rollout.** That is "greedy rollouts beyond the horizon / beyond what budget allows" — the
understood, accepted limitation — NOT greedy taking over a decision.

**The decision itself is never greedy.** Every pass compares ALL candidates; the committed plan is
always a complete comparison. And the executor was instrumented directly to check the stronger
claim:

```
antilife    EXECUTOR GREEDY Solve(): REAL decisions by depth:  NONE
fivecolour  EXECUTOR GREEDY Solve(): REAL decisions by depth:  NONE
kitty       EXECUTOR GREEDY Solve(): REAL decisions by depth:  NONE
```

Zero greedy executor decisions at play settings, 200 games per deck (`MTG_M2_YIELD_STATS=1`). The
`TurnSolver::Solve()` call in `AIEngine.cpp` is the depth-0 runner configuration; it does not fire
in real play.

**By the USER's criterion — "greedy logic taking over mid-search decisions I consider a defect" —
no defect has been demonstrated.** The interior second main is likewise part of EVALUATING an m1
candidate (the real second main is decided separately, by its own top-level search), so greedy
there is evaluation fidelity, not a decision.

## 3. What the per-deck searched-m2 hooks actually do

They make the *scoring* more faithful inside the horizon, on the passes that get past the greedy
rollout. That is a quality lever, not a correctness fix. It also explains three results previously
filed as independent clean nulls — `MTG_5C_SSM=0` byte-identical over 60 games, AL's branch hook
byte-identical over 26,000, and the searched-m2 adoptions generally measuring inert: the hooks only
reach passes >= 1, which the budget rarely affords. Not inert levers; levers on a rarely-affordable
path.

## 3a. ANSWER: is there remaining greedy in the search? (measured, 2026-08-23)

Every greedy `TurnSolver::Solve()` reachable from the search is now counted by site
(`MTG_M2_YIELD_STATS=1` -> `GREEDY SITES` / `EXECUTOR GREEDY`). At play settings, 200 games/deck:

| site | antilife | fivecolour | kitty | what it is |
|---|---|---|---|---|
| s90 | 35,511 | 1,740,445 | 702,380 | `SolveWithLookahead` depth<=0 — the scoring rollout bottoming out |
| s8 | 74,539 | 215,154 | 0 | breakpoint continuation for a candidate that carries NO bp choice |
| bp sites 0,1,2,4,6 | 0 | 0 | 0 | never fall back — always searched |
| EXECUTOR main phase | **NONE** | **NONE** | **NONE** | — |
| EXECUTOR breakpoint fallback | **0** | **0** | **0** | — |

**Both remaining sites are EVALUATION, not decisions.**

* **s90** is the scoring rollout hitting its horizon — the accepted "beyond the horizon / beyond what
  budget allows" case.
* **s8** is subtler and was worth chasing: `bp_searched_plan` searches a breakpoint continuation only
  for a candidate plan that CARRIES a breakpoint choice (`plan.bp_choice >= 0 && seen_before ==
  plan.bp_at`). The search enumerates those breakpoint variants separately and compares them against
  the greedy-continuation baseline, so the greedy continuation is one of the options being compared,
  not a decision imposed on the line. NOTE: it is NOT caused by `BpSiteMask()` excluding site 3 —
  running with `MTG_BP_SITES=0x7F` leaves the count unchanged at 74,539, so do not "fix" it there.

**The executor never executes a greedy decision**: 0 greedy main-phase plans and 0 greedy breakpoint
fallbacks across 600 games / 3 decks. So the committed line always carries a SEARCHED breakpoint
continuation — the greedy variants never win the comparison.

**Conclusion: no greedy DECISION defect exists on these three decks.** All remaining greedy is
evaluation or horizon rollout.

**Caveat:** measured on antilife / fivecolour / kitty only. The other 11 decks have not been run
through these counters; doing so is cheap (`MTG_M2_YIELD_STATS=1` on any batch) and is the obvious
first task if the question comes up again.


## 4. AUDIT COMPLETE — all 14 decks (2026-08-24)

Every deck run through the greedy counters at its own PLAY settings.

### 4.1 Executor greedy DECISIONS — the line that matters

**13 of 14 decks: ZERO.** No greedy main-phase decisions anywhere, and no greedy breakpoint
fallbacks. One exception:

| deck | sample | greedy executor decisions |
|---|---|---|
| **hinata** | 150 games (s777000) | **breakpoint-fallback = 4** |
| **hinata** | 300 games (s900000/s910000) | **breakpoint-fallback = 1** |
| every other deck | — | 0 |

Hinata is the ONLY deck where the executor ever decides greedily. It is a BREAKPOINT continuation
(`AIEngine.cpp`, `if (!bp_searched_here) { extra = TurnSolver::Solve(...) }`) — a real, executed
mid-turn decision made without search. Rate is roughly 1 per 100–300 games: rare, reproducible on
independent seeds, and by the USER's criterion ("greedy logic taking over mid-search decisions I
consider a defect") this is the one thing found that qualifies.

### 4.2 Which decks even HAVE an interior second main

`AIEngine::SetSearchPostCombat` is driven by `GoldFishRunner::DeckUsesSecondMain`, a narrow
CARD-PARAMETER rule (`spectacle_cost`, `lifegain_to_loss`, `hinata_cost_reducer`,
`combat_damage_puts_subtype_from_hand`, `attack_draw_cards`, `combat_damage_free_cast`,
`attack_dig_attach_count` + `draw_on_equipment_etb`). Measured `M2 SITE` confirms it:

| has a second main | antilife, fivecolour, kitty, hinata, goblins |
|---|---|
| **has NONE (M2 SITE all zeros)** | burn, auras, slivers, treasure_hunt, dragonstorm, knights, creature_giving, mirrorwing, stompy |

**This retires most of the searched-design rollout.** Hooks 1/2/3 all concern the m1/m2 split and the
interior second main, so on the nine decks with no second main they are **structurally inapplicable
— not "unmeasured", inapplicable.** Do not spend time rolling them out there.

### 4.3 Greedy at the BRANCH site (the decision site), depth > 0

| deck | BRANCH searched | BRANCH greedy |
|---|---|---|
| antilife / fivecolour / kitty | all searched | **0** |
| goblins | 0 | 0 (only d<=0) |
| **hinata** | **0** | **20,842 per 150 games** |

**Hinata is the only deck where hook 3 (`SearchedSecondMainInSearch`) would bind.** Its interior
second main is decided greedily at the branch site ~20.8k times per 150 games, and it has no
override.

### 4.4 Per-deck greedy site map (all EVALUATION unless noted)

`s0` draw breakpoint · `s1`,`s2`,`s4` other breakpoint classes · `s8` deferred/cantrip breakpoint ·
`s90` `SolveWithLookahead` depth<=0 horizon rollout.

| deck | sites seen |
|---|---|
| knights, stompy, auras, slivers | s90 only |
| creature_giving | s8, s90 |
| goblins | s90 |
| burn | s0 |
| dragonstorm | s2, s90 |
| treasure_hunt | s1, s4, s90 |
| mirrorwing | s0, s8, s90 |
| hinata | s0, s8, s90 |

All are the greedy-continuation BASELINE that the search compares against its searched breakpoint
variants — benign wherever the executor count is 0, which is everywhere but hinata.

### 4.5 Answering the USER's question directly

*"Let's move on to any other decks that have a user approved order."* Seven decks have a
USER-reviewed cast order: antilife, knights, creature_giving, fivecolour, mirrorwing (orders
DEFAULT ON) and kitty, stompy (`MTG_KE_ORDER` / `MTG_STOMPY_ORDER` still default OFF A/B levers).

Of those, the ones not yet touched by this work — **knights, creature_giving, mirrorwing, stompy —
are all CLEAN (zero greedy executor decisions) and all structurally ineligible for the
searched-design hooks (no second main). There is no work to do on them.**

**The deck that needs work is HINATA, which does NOT have a USER-approved cast order.** Per the
standing process gate (cast order is USER-REVIEWED per deck, never adopted from a measurement
alone), a Hinata cast-order review is the prerequisite before any searched-design work there.

## 5. The adoption bar all of this is measured against

USER, 2026-08-23. Both are gates; test both, block on either.

1. **"Are we improving the play generally?"** — OVERALL quality AT PLAY SETTINGS (the deck's
   resolved `value_play` depth/budget, NOT the suite's pinned d3/d5 gate cells), on a LARGE sample.
   Worse on some seeds is fine if the average improves. A change should improve quality, or be
   quality-neutral with other upside (a pure perf win counts). A small quality loss for a large perf
   win is legitimate but is the USER's call — and **look for a strict win first**; only escalate the
   trade if a strict win is genuinely impractical.
2. **"Are we preventing search from finding a win?"** — tested GAME BY GAME, at **unlimited budget**
   and the **depth the game won at BEFORE** the change. Win returns => recoverable, gate 1 decides.
   Win never returns => the change deleted the line structurally, which blocks on its own.

Escalating budget/depth is gate 2's instrument and proves nothing about gate 1. Escalate to
CONVERGENCE, not to a round number.

## 6. What was decided and shipped along the way

| item | outcome | commit |
|---|---|---|
| antilife's `GradesNoWinLeaf` opt-out (never existed; the deck was net-worse on a 2-game sample) | re-measured: **-66 turns / 172,000 paired**, keeps the default | `7038dded` |
| dragonstorm's `GradesNoWinLeaf` opt-out | **REMOVED** — both gates clear; no deck opts out now | `8dc20bdc` |
| the "escalate before opting out" rule | **RETRACTED** — it was gate 2's test used to answer gate 1 | `14505f0e` |
| searching antilife's ROLLOUT second main | **REJECTED** — +10 turns / 30,000 paired at play settings, ~2.2 sigma worse; the `MTG_M2_CAP1` strict-win route is inert on this deck | `b851c8b5` |

## 7. Instruments added (all default OFF or behaviour-neutral)

* `MTG_LEAF_NOWIN_FORCE` — force the no-win tie-break past a provider opt-out, so an opt-out can be
  re-validated without a scratch build.
* `MTG_AL_SSM_ROLLOUT` — re-open antilife's declined rollout second main (default OFF, rejected).
* `MTG_M2_CAP1` — cap the interior m2 solve to depth 1 (per-job form of `MTG_M2_SEARCH_DEPTH=1`).
* `MTG_M2_YIELD_STATS=1` now also prints **`M2 PATH`**, **`M2 SITE`** and **`COMMITTED PASS`** —
  which second-main path ran, at which call site, and which pass committed. This is the instrument
  that found everything in section 2.
* `scripts/leaf_tiebreak_check.py` — per-deck leaf tie-break A/B; measures at PLAY settings by
  default, pools multiple decks into one batch, `--seed-base` to EXTEND a sample, and refuses to
  call a sign below 20 changed games.

## 8. A trap worth not re-discovering

`MTG_NO_SEARCH_SECOND_MAIN` **does not do what its comment claims.** It is documented as "the kill
switch for the per-deck opt-ins"; it is not. `GreedySecondMainEnabled()` returning true only yields
`searched = site_on && provider_hook`, which is the default path anyway — so the flag can cancel
`MTG_SEARCH_SECOND_MAIN=1` and nothing else. Verified on fivecolour: with the switch set, BRANCH
searched 4,529, identical to default. **A/B-ing searched-vs-greedy on an opted-in deck with this
lever produces a false null.** Not fixed — fixing it is a lever change worth a USER decision.

Also unrelated but recorded: **KittyEquipment is expensive at its shipping d5/20** — 23 games over
30 s per 4,000, worst 75.3 s — and it has **no rows in `test/regression_cases.sh`**, so that cost
has never appeared in any suite tier.
