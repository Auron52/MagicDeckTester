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


## 4. MONDAY: finish the audit on the other 11 decks

The greedy audit is complete on **antilife, fivecolour, kitty** only. The remaining 11 decks
(hinata, burn, auras, goblins, knights, slivers, treasure_hunt, dragonstorm, creature_giving,
mirrorwing, StompySurprise) have not been run through the counters. This is cheap and needs no code
change — build a batch at each deck's PLAY settings (omit `depth`/`budget_ms` so the profile
resolves; do NOT set `ignore_play_profile`) and read three stderr lines:

```
MTG_M2_YIELD_STATS=1 ./build/Release/mtg --batch <manifest>  2>&1 \
  | grep -aE "GREEDY SITES|EXECUTOR GREEDY|M2 SITE"
```

**What a clean deck looks like** (matching the three already audited):

* `EXECUTOR GREEDY Solve(): ... REAL main-phase decisions by depth:  NONE` and
  `breakpoint-fallback=0` — the executor never decides greedily. **This is the line that matters.**
* `GREEDY SITES` showing only `s90` (horizon rollout) and `s8` (breakpoint-variant baseline).

**What would be a genuine finding:** any non-zero EXECUTOR count, or a `GREEDY SITES` entry other
than s8/s90 — i.e. one of breakpoint sites 0,1,2,4,6 falling back, which never happened on the three
audited decks.

A secondary, lower-priority question if it comes up: most decisions can only afford the shallowest
scoring rollout (section 2), which is legitimate under the accepted design but caps what the
per-deck searched-m2 hooks can ever be worth. Worth knowing whether the 20-virtual-ms budget is
intended before investing more in those hooks. **Do not raise the default budget without measuring
against the adoption bar below** — perf is a shipping constraint.

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
