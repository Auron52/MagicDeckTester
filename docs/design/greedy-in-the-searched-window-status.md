# Greedy inside the search: what we found, and what it means

**Status (updated 2026-09-03): the hinata executor greedy is FIXED and shipped** — ebfb5f74
(2026-09-02, the tight sound recipe) records "Executor at ship settings: NONE greedy main-phase
decisions, zero ROOT-kind fallbacks". `MTG_KE_ORDER` (listed below as a default-OFF lever) is
default ON since d0547999.

**Status: the 14-deck audit is COMPLETE as of 2026-08-25 (section 4). Nothing is running.**
It found two things the first three decks did not show: a rollout-side breakpoint fallback on 5
decks, and **one real greedy DECISION in the executor, on hinata**. No fix has been attempted — the
next move is a USER decision, measured against section 5's two gates. Section 4c lists what is still
unknown. Self-contained: everything needed to pick this up is in this file and in git.

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

**Caveat (RESOLVED 2026-08-25 — see section 4):** this was measured on antilife / fivecolour / kitty
only, and the remaining decks were run through the counters on 2026-08-25. **The clean result above
does NOT generalise.** Five decks fall back to greedy at breakpoint sites the audited three never
touched, and hinata reaches a greedy DECISION in the executor. Read section 4 before quoting
section 3a.


## 4. AUDIT COMPLETE — all 14 decks (2026-08-24)

Every deck run through the greedy counters at its own PLAY settings.

### 4.0 Independent re-measurement (2026-08-25, seed base 9001 — agrees on every conclusion)

A second, independent run of the same audit (different agent, different seeds): the remaining
**12** decks (the 11 listed here plus **minotaur**), 200 games each at resolved PLAY settings, one
pooled batch, `MTG_M2_YIELD_STATS=1`. Manifest and raw stderr are under `logs/greedy_audit/`
(regenerable; the recipe is below). Seed base 9001, disjoint from every suite tier.

Because the counters are **process-global atomics dumped at exit**, a mixed batch yields one
aggregate — which is still decisive when the answer is zero, since zero in aggregate implies zero per
deck. It was NOT zero, so the per-deck attribution below came from 12 single-deck re-runs.

**Provenance — this matters, read it before re-running.** The first pass was measured on a
`build/Release/mtg` that had been rebuilt from a WORKING TREE carrying ~660 lines of unrelated
uncommitted engine work (a mana-order/reserve overhaul, `MTG_PAYSAC_FRESH_HOLD` /
`MTG_M2_RELEASE` / `MTG_ONESHOT_RESERVE` / `MTG_PUMP_TARGET_HOLD` / `MTG_SCALER_PLAN_BIAS`). That
work is byte-identical with its levers OFF (verified: `mirrorwing_smoke_d3_s1001` reproduces GT
`4.7200/bf00585158c29bdc` under the working-tree binary with no flags set) — the 14/42 smoke and
27/70 regression failures visible in `test/logs/` at the time were its lever-ON A/B arms, which is
what an arm is supposed to look like, not a broken tree. Every number in this section was
nonetheless **re-measured on a clean `git worktree` build of HEAD `994feba6`** and came back
**identical, digit for digit, on all 12 decks**. The tables below are
clean-HEAD numbers. If you re-run this while the tree is dirty, check
`test/logs/<mode>/mtg.run.meta` + `mtg.run.diff` first — `git_state=dirty` alone is not alarming
(your own uncommitted test edits set it), but a diff touching `src/` invalidates the measurement.

### 4a. GREEDY SITES, per deck

| deck | s0 | s1 | s2 | s4 | s8 | s90 | verdict |
|---|---|---|---|---|---|---|---|
| hinata | 1,403,839 | | | | 2,653,398 | 345,489 | **bp site 0 falls back** |
| mirrorwing | 541,190 | | | | 1,074,670 | 357,325 | **bp site 0 falls back** |
| burn | 139,294 | | | | | 2,466 | **bp site 0 falls back** |
| th | | 338,591 | | 1,341* | | 52,374 | **bp sites 1 (+4*) fall back** |
| dragonstorm | | | 35,517 | | | 62,941 | **bp site 2 falls back** |
| creature_giving | | | | | 365,956 | 1,567,375 | clean (s8/s90 only) |
| auras | | | | | | 1,569 | clean |
| goblins | | | | | | 27,160 | clean |
| knights | | | | | | 52,045 | clean |
| slivers | | | | | | 95,215 | clean |
| stompy | | | | | | 814,434 | clean |
| minotaur | | | | | | 6,031,599 | clean |

Six decks are clean by the criterion in this doc (s8/s90 only). **Five are not** — and the sites they
hit (0, 1, 2, 4) are exactly the "never happened on the three audited decks" case this doc named as a
genuine finding. These are all the ROLLOUT-side twins of the executor's breakpoint re-solve: when a
candidate plan carries no searched breakpoint continuation, the rollout continues the line greedily.
That is evaluation, not a decision, so it sits inside the accepted design — but it is a fidelity gap
on 5 of 14 decks that was previously believed absent everywhere.

*\* **`s4` is overcounted — instrument bug.** Sites 0, 1, 2, 6 and 8 call `greedysite::Record()`
INSIDE the `if (!bp_searched_plan(...))` branch; site 4 (`TurnSolver.cpp:15805`) calls it
UNCONDITIONALLY, one line before the check. So `s4=1341` counts every visit to that site, not every
greedy fallback, and th's true site-4 fallback count is unknown (possibly 0). Fixing it is a
one-line move with no behaviour change. Do not quote s4 until it is fixed.*

### 4b. The real finding: hinata reaches a greedy DECISION in the executor

```
hinata    EXECUTOR GREEDY Solve(): in-rollout=0 breakpoint-fallback=5 | REAL main-phase decisions:  NONE
<all 11 others>                    in-rollout=0 breakpoint-fallback=0 | REAL main-phase decisions:  NONE
```

`REAL main-phase decisions: NONE` still holds everywhere — the top-level main-phase plan is never
greedy on any of the 14 decks. But **hinata's executor took a greedy breakpoint continuation 5 times
in 200 games**, at `AIEngine.cpp:2747`. That is a DECISION: the continuation it plays is the one
`TurnSolver::Solve()` returns, and the code comment eight lines above it states the stake exactly —
"re-solving greedily here would realise a turn the search never scored (a rollout/execution
divergence)".

`MTG_BP_TRACE=1` on the same 200 games identifies the sub-case, and it is the same one all 5 times:

```
[bp-exec]  turn=8 idx=-1 bp_at=0 bp_choice=-1 searched=0     (x3)
[bp-exec]  turn=6 idx=-1 bp_at=0 bp_choice=-1 searched=0
[bp-exec]  turn=4 idx=-1 bp_at=0 bp_choice=-1 searched=0
```

All 5 have **`bp_choice = -1`: the committed plan carries no breakpoint continuation at all**, yet the
executor hit a draw breakpoint while playing it. `MTG_BP_SEARCH` defaults to W=2 and emits variants
"for every plan that opens a breakpoint", so the search did not classify this line as opening one.
Execution disagreed. Note the trace printed exactly 5 `[bp-exec]` lines total — so hinata's executor
never once replayed a SEARCHED breakpoint continuation in these games; every breakpoint it met was
unanticipated.

This is a search/execution divergence, and greedy is what covers it. It is small (5 in 200 games, one
deck) but it is the defect class the USER named, and it is not the accepted horizon case.

### 4c. What is NOT yet known

* Whether those 5 games play worse for it — no A/B has been run. The count is an occurrence count,
  not a cost. Section 5's two gates are how any fix must be judged.
* Whether the divergence is a misclassification in the search (a breakpoint it should have
  anticipated) or a genuinely unforeseeable one. That is the first thing to find out.
* Whether the 5 decks in 4a lose measurable quality to the rollout-side fallback.
* th's true site-4 count, pending the instrument fix.

### The recipe (unchanged, for re-running)

Build a batch at each deck's PLAY settings — omit `depth`/`budget_ms` so the profile resolves, and do
NOT set `ignore_play_profile`. Run ONE deck per process if you need attribution — the counters are
process-global. **What a clean deck looks like:** `EXECUTOR GREEDY ... REAL main-phase decisions by
depth: NONE` with `breakpoint-fallback=0`, and `GREEDY SITES` showing only `s90` (horizon rollout)
and `s8` (breakpoint-variant baseline).

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
consider a defect") this is the one thing found that qualifies. The 9001-seed re-measurement
(§4.0) saw the same class at a similar rate — 5 in 200 games, all with `bp_choice = -1` (the
committed plan carried NO breakpoint continuation at all, yet execution hit one; see §4.0's
`[bp-exec]` trace) — so the two runs agree the divergence is real, hinata-only, and unanticipated
by the search rather than a replay of a searched continuation.

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

**UPDATE 2026-08-26 — the `depth <= 0` half of this table is now MEASURED and CLOSED.** The row
above is `depth > 0` only; at `depth <= 0` the branch site is greedy on every deck regardless of
every hook, and that is where nearly all of it lives (antilife 100%, kitty 94.9%, fivecolour 83.4%
of branch-site interior second mains). `MTG_M2_D0_SEARCHED` (heurarm, default OFF) converts it.
Result: **the greedy plan is the SAME plan.** Byte-identical play digests over 1,000 unbudgeted
games on antilife AND on kitty, and byte-identical on 35 of the 36 games where the shipped budget
made the arms diverge; at play settings the arm is worse in 6 of 6 cells purely because the interior
spend starves the outer candidate loop (-28.9% / -32.4% pass-0 candidate evaluations). NOT adopted.
Full record: `searched-design-deck-rollout.md` §3c. The transferable half: **a greedy step is worth
removing only where it is measurably WRONG, and one `budget_ms=0` digest comparison settles that
before any conversion work.**

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

## 4.6 CENSUS AT SHIPPED PLAY SETTINGS — where the greedy actually is (2026-08-26)

`bash test/tools/greedy_census.sh 200 600001` — 15 decks x 200 games, each deck's own `value_play`
settings, one process per deck (the counters are process-global atomics, so pooling would make them
unattributable). Greedy `Solve()` calls reached from inside the search, PER GAME, summed over all 15:

| site | /game | share | what it is |
|---|---|---|---|
| **s90** | **53,742** | **61.7%** | `SolveWithLookahead` at `depth <= 0` — the search's own HORIZON |
| s8 | 22,295 | 25.6% | deferred / cantrip breakpoint continuation |
| s0 | 8,670 | 10.0% | staged-draw breakpoint (Light Up the Stage, Expressive Iteration) |
| s1 / s2 / s4 | 2,334 | 2.7% | DrawUntilNonland, impulse-exile, other breakpoint classes |

Per deck (greedy per game, and the d<=0 interior second main for scale):

| deck | play | greedy/game | by site | bp greedy% | m2 d<=0 /game |
|---|---|---|---|---|---|
| minotaur | d5/b20 | 32,627 | s90 only | — | 0 |
| hinata | d5/b20 | 19,105 | s0=5,618 s8=11,840 s90=1,647 | 91.8% | 1,461 |
| mirrorwing | d5/b20 | 9,770 | s0=2,299 s8=5,667 s90=1,802 | 45.9% | 0 |
| creature_giving | d5/b20 | 9,514 | s8=1,729 s90=7,785 | 100% | 0 |
| stompy | d6/b20 | 5,739 | s90 only | — | 0 |
| kitty | d5/b20 | 3,271 | s8=2,493 s90=777 | 62.4% | 778 |
| th | d5/b20 | 2,161 | s1=1,791 s4=9 s90=361 | 64.5% | 0 |
| fivecolour | d6/b20 | 1,926 | s8=298 s90=1,628 | 100% | 1,341 |
| dragonstorm | d5/b20 | 963 | s2=534 s90=428 | 43.0% | 0 |
| burn | d6/b20 | 784 | s0=752 s90=32 | 82.6% | 36 |
| antilife | d5/b20 | 521 | s8=266 s90=255 | 100% | 246 |
| slivers / knights / goblins | d5-6 | 330 / 182 / 140 | s90 only | — | 0 / 0 / 214 |
| auras | d5/b20 | **3** | s90 only | — | 0 |

**The committed line is still essentially never greedy**: 0 real main-phase executor decisions on
every deck, and the only greedy that decides anything on the played line is a breakpoint fallback on
hinata (3) and treasure_hunt (8) across 200 games each.

### What this changes about the plan

* **The dominant site is the HORIZON, and it is different in kind.** At `depth <= 0` there is no
  search left to do, so the replacement for s90 is not "search it instead" -- that is either infinite
  regress or simply a deeper search. It is *"evaluate the leaf with something principled rather than
  playing it out greedily"*, which is what the VALUE LEAF already does where a deck has one. s90 is
  62% of all greedy and it is the biggest single lever available.
* **The breakpoint families (38%) are a REACHABILITY problem, not a policy one.** `MTG_BP_SEARCH`
  is 2 wide and `MTG_BP_DEPTH` is 1 deep; on kitty at shipped settings the continuation lists average
  7.13 and reach 49, and **73.0% of all continuations are unreachable** -- `bp_choice` can never
  index them. Every plan outside that 1x2 window hits the breakpoint and takes greedy BY
  CONSTRUCTION. That is the same lossy-truncation shape the standing USER bar already rejects.
* **Two decks are pure horizon** (minotaur, stompy: s90 only, no breakpoints), and **auras is
  already effectively greedy-free** at 3 calls per game.
* **Three decks report 100% greedy breakpoint continuations** (antilife, creature_giving,
  fivecolour) -- `bp_searched_plan` never resolves for them. Unexplained; worth a look before
  anything is built on their numbers.
* The `depth <= 0` interior second main (§3c of `searched-design-deck-rollout.md`) is 0-1,461 calls
  per game, i.e. a SMALL slice of the total. Converting it was worth measuring but it is not where
  the volume is.

### Method warning this census exists to correct

The first attempt at "why is the conversion expensive" was diagnosed on a `budget_ms=0` run. On the
same KittyEquipment game that reports **124,747** breakpoint encounters and **1,887** interior second
mains at d5/b20, the unbudgeted run reports **5,183,560** and **1,293,023** -- ~700x, and the
DIRECTION is wrong too (the shipped search covers 29.2% of continuations, the unbudgeted one 69.8%).
Conclusions drawn there -- "the memos are thrashing", "condemnation should be bound at more sites" --
were about a configuration nobody runs. **Unbudgeted has exactly one legitimate use: pricing a prune
or a conversion so the budget cannot reinvest the saving. Never for "why".**

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
* `MTG_M2_D0_SEARCHED` — search the `depth <= 0` branch-site interior second main instead of taking
  greedy (default OFF, measured and rejected; see §4.3's update). Also the cheap way to ask of any
  future deck "is this greedy step actually wrong?" — run it at `budget_ms=0` and compare digests.
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
30 s per 4,000, worst 75.3 s — and it had **no rows in `test/regression_cases.sh`**, so that cost
had never appeared in any suite tier. **UPDATE 2026-08-25 (`a9fbb920`): it is now in all three
tiers**, and at suite scale the cost reads far milder — overnight d3 ~0.43 s/game, **d5 ~0.30
s/game** (d5 is ~30% CHEAPER than d3; the value-leaf hybrid is absorbing it), d0 effectively free.
The heavy tail above is NOT contradicted by this — the batch slow-game instrumentation emitted
nothing to that run's log, so the tail remains unverified either way rather than shown gone.
