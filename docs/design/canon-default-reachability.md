# Canon-default reachability: closed, and the four ways it hid

**Status (2026-09-22): ZERO unchallengeable canon defaults on all 23 suite decks.** ~124,000 → 0.
**Read the two caveats before quoting that number:** the audit asks whether a plan was *offered* to
the variant machinery, not whether the machinery had a second option (26% of these lists hold
exactly one continuation — see THE COST, ROOT-SOURCED); and the rebaseline cost +0.0014 avg win
turn, traced to one clause on one deck and to a pre-existing wave-0 defect.
The audit that measures them (`MTG_BP_CANON_AUDIT`, `scripts/canon_audit.sh`) is the enforcement and
should keep running: two of the last four gaps were opened by changes with nothing to do with
breakpoints — an adopted tutor lever that stopped filling a field a clause read, and a card added to
a deck — and neither was findable by reading the code.

An earlier revision concluded the remainder was "not clause work; it needs a caller". That rested on
a measurement artifact; it is corrected in place below rather than deleted, because the way it went
wrong is the most reusable thing here.

## The rule this is all enforcing

There is no greedy continuation. The greedy `TurnSolver::Solve()` inside `bp_searched_plan` was
deleted on 2026-09-17 (`greedy-continuation-deletion-route.md`) and there is no hatch back. What
replaced it is a **default**: at an un-branched slot the continuation is the value-best enumerated
entry (`MTG_BP_NESTED_CANON`, `MTG_BP_BASE_CANON`).

Doctrine permits a heuristic as a **branch's default** and forbids it as a **substitute for
branching**. So that default is legitimate *only while the alternatives are reachable*. When they
are not, the effect is indistinguishable from the greedy this repo spent months removing — and
**invisible to every greedy counter, because no `Solve()` is involved**. That is why
`MTG_BP_CANON_AUDIT` exists: the clause that closed the first instance of this ends *"If a route is
ever added, ADD IT HERE TOO — nothing enforces it."* The audit is the enforcement.

USER, 2026-09-22: *"There should be 0 greedy continuations ANYWHERE! I'm tired of these
resurfacing."*

## How to run it

```
bash scripts/canon_audit.sh              # every suite deck (DECK_FILE), each at its OWN play settings
OUT=logs/foo EXTRA="MTG_X=1" bash scripts/canon_audit.sh
```

Read the output as: **any nonzero `UNCHALLENGEABLE` is a doctrine violation**, to be fixed by
giving the site a route — never by suppressing the audit. Two further columns matter:

| column | meaning | remedy |
|---|---|---|
| `UNCHALLENGEABLE` | the plan opens **no** masked site: no variant of it is ever emitted | a route |
| `site-unmarked` | the plan *is* selected on some other site, so the waves reach this slot positionally | optional; a clause makes the coverage intended rather than incidental |
| `[NOHOST]` / `[capture]` / `[variant]` | what, if anything, was offering **this apply** a search node | see below |

The host tag was a two-way `[hosted]`/`[NOHOST]` split until 2026-09-22, and the two-way form was
actively misleading: it lumped the one tier a clause can close together with the one tier that
*proves* no clause is needed. Three states:

* **`[NOHOST]`** — no capture pointer, no carried `bp_choice`. Nothing hosted *this* apply. Note
  carefully what this does **not** mean: the base plan's own variants are scored as **sibling
  candidates**, so a `[NOHOST]` violation is still closed by marking the plan (a clause). The tag
  says only that a node could not have hosted the slot in place.
* **`[capture]`** — a capture pointer, but the plan carries no choice. The fan-out was available
  and declined this plan: a missing clause, fixable in `PlanOpensBreakpoint`.
* **`[variant]`** — the plan itself carries `bp_choice >= 0`. Exactly three things set one
  (`AppendBreakpointVariants`, `BpWaveWalker::Next`, the node's children) and each emits only for a
  base plan it **selected**, so this is a *record of fact* that the plan is inside the variant
  machinery. A violation reported here is an audit bug, not a clause gap.

## Three ways the audit lied before it was trustworthy (and the fourth, which was the reverse)

Worth keeping, because each is a trap the next instrument will hit too.

1. **It asked `>> site & 1`.** Fan-out selection is **per-plan**; the slot index is **positional**
   (`bp_at = k` into whatever enabled-class breakpoints the apply reaches). So a selected plan's
   k-th breakpoint is challengeable *whatever site it lands on*. Asking per-site reported
   mirrorwing's 16,448 site-0 defaults as violations — every one a nested cantrip inside a Gold
   Rush continuation on a plan the site-5 clause already selects.
2. **It re-derived state-keyed predicates at the wrong state.** The biggest one, and it took four
   rounds to kill because each round fixed a *symptom*. The fan-out evaluates them **pre-apply**;
   the audit sat **mid-apply**, after the plan's own casts, taps and sacrifices had run. The dig
   loop *consumes* its source, so `HasAnyDigSource` read false on the very plans the bypass had
   fanned out; a site-7 Pod pre-scan that needs two untapped sources sees one, because the plan's
   own activation tapped the other.
   * Round 1 preferred `plan.bp_wave0` — a record of fact, but for **wave 0 only**. (~8,000)
   * Round 2 added `plan.bp_choice >= 0` — the other record of fact, covering every plan the
     **wave walker** or a node handed out, which stamp nothing. (fluctuator 194 → 0, all of them)
   * Round 3 stopped patching and **took the state out of the question**: snapshot
     `PlanOpensBreakpoint` and the dig bypass at the apply's *entry* and read them back.
     (melira site 7 1,079 → 0, dragons 116 → 0, auras 1,690 → 467, and `site-unmarked` collapsed
     across four decks.)
   * **Round 4 is the one to remember: the disagreement was never one-directional.** Every round
     above chased *invented* violations. The same wrong state was also **concealing** them —
     `stompy` went **0 → 291** (Natural Order 178, World War Hulk 68, Turntimber Symbiosis 45),
     plans whose put-in-hand only becomes visible mid-apply, so no fan-out ever saw them and their
     canon default had no alternative at any budget. The audit had reported that deck clean since
     the day it was written. **An instrument that calls a deck clean for the wrong reason is the
     exact failure this audit exists to prevent**, and nothing had thought to look in that
     direction, because over-reporting is the direction that hurts *you* and under-reporting is
     the direction that hurts the *user*.
3. **Its runner reported a green verdict for 22 decks that never ran.** `MTG_A=1 MTG_B=1 $EXTRA
   "$BIN"` runs `$EXTRA`'s first word as the *command* — bash decides what is an assignment
   syntactically, before expansion. Every deck died with "command not found" and the script still
   printed `RESULT: ZERO unchallengeable canon defaults on every deck`. Now routed through `env`,
   with a did-not-run gate that makes a missing audit line **INVALID**, not green.

## What the clause work closed

`PlanOpensBreakpoint` gained the routes the audit *named* (it names the arming **card**, which is
what made this tractable — see the `[canon-armed-by]` histogram):

* **site 3 — the acquisition family.** Site 3 is not "the plain cantrip class"; it is the deferred
  resolve's **default index**, and five armings land on it. Only the cantrip had a clause. Added:
  tutor-to-hand (cast *and* creature-ETB), tutor-to-top, Soulfire Eruption's stage, Garth's
  Braingeyser/Regrowth. `MTG_BP_ACQ_CLAUSE`.
* **site 4 — the searched dig axis.** The bypass asked `ShouldConsiderDig` — *the dig heuristic* —
  so on a turn the heuristic said "don't dig", no variant of any plan could ever score digging.
  Gating a fan-out on the heuristic the search exists to second-guess is the defect in one line.
  `MTG_BP_DIG_AXIS_FANOUT`.
* **site 10 — the creature-enters watcher** (Vaultborn Tyrant), plus modal draw, ETB discard-draw,
  blink, and tutor-to-battlefield into a tutor body.

Result: **~124,000 unchallengeable defaults → ~3,600; 15 of 22 decks affected → 5.** Quality net
faster (net −0.3101 over 21 moved keys; fluctuator 5 of 5 cells, hinata 3 of 3, creature_giving
4 of 4), cost +10% makespan, no game genuinely worse (22 of 24 slower games recover at 4×/16×).

## CLOSED — ZERO unchallengeable canon defaults, 23 of 23 decks

The 1,273 that remained once the audit asked pre-apply resolved into **four clause gaps**, not the
missing *caller* an earlier revision of this section concluded. All four are closed
(`MTG_BP_DIG_SELF_SOURCE`, `MTG_BP_TUTOR_BF_UNNAMED`, `PutBodyGainsCards`,
`MTG_BP_WATCHER_SELF_PUT`, `MTG_BP_CASCADE_CLAUSE`; each has a `=0` hatch).

```
auras   site 4   467   the plan's own LAND DROP is the dig source (Horizon Canopy)
melira  site 10  466   one question, three hand-rolled fetch lists, already drifted (Celes)
stompy  site 10  291   178 a clause keyed on a field an adopted lever stopped filling
                       113 the watcher arrives PUT, not cast (Vaultborn Tyrant is self-inclusive)
th      site 1    41   CASCADE -- the breakpoint belongs to a card the plan cannot see
th      site 4     8   as auras (Fiery Islet)
                 -----
                 1,273 -> 0        and ~124,000 -> 0 since the class was first measured
```

**The four causes are worth reading as one family**, because none of them is "we forgot a card":

1. **A predicate asked of the BOARD when the answer is a property of the PLAN.**
   `HasAnyDigSource` is a board question, and its own note says a source "can only be ADDED by a
   plan's own casts, never removed" — true, and safe for the dig LOOP, which is why it read as
   harmless. For the FAN-OUT it is the entire hole: the plan that adds the source is precisely the
   plan whose dig nothing ever offers a variant for.
2. **A clause keyed on a field an ADOPTED LEVER STOPPED FILLING.** The site-10
   tutor-to-battlefield branch reads `Action::tutor_target`; `MTG_TUTOR_AXIS_RESOLVE` (adopted,
   default ON) "binds NO name at all" and moved the pick to `Plan::tutor_choice`. The branch has
   been **dead code on every deck** since that lever shipped, and nothing anywhere would have said
   so. This is the quietest way for the class to come back.
3. **One question, three hand-rolled lists.** "Does the fetched body gain a card?" was asked at
   three sites with three different lists, none holding `etb_discard_any_draw_bonus`. Now one
   `PutBodyGainsCards()` — the drift hazard the site-10 clause's own note warned about, made
   structural instead of remembered.
4. **The plan brings the thing the predicate is looking for.** Both the dig source and the
   creature-enters watcher arrive *during* the apply the fan-out is deciding about. Vaultborn
   Tyrant is self-inclusive, so it arms on its own entry — and on stompy it never gets cast, it
   gets **put** (Turntimber's look, World War Hulk's free chapter-I cast, Natural Order's fetch).

Every new clause is keyed on a **Plan field or a card param, never on board state**. That is
deliberate: all four audit over-reports came from predicates whose answer moved between the
fan-out's state and the apply's, and a clause with the same property would simply relocate the bug.

**Cascade is the one that cannot be answered precisely, and says so.** The hit is chosen during
resolution by a walk down a hidden library, so naming its site would be inventing knowledge. The
clause marks the cast-reachable SET (bits 0–3). Over-marking is free in the fan-out — both routes
only test the mask for nonzero — and a variant for a class the hit does not arm collapses onto its
base plan.

## THE COST, ROOT-SOURCED — and the pre-existing defect it exposed

The rebaseline moved the suite +0.0014 avg win turn (8 keys, ~4.1 baseline). Flat is not the bar:
**we want it break-even or better.** Attributed per clause with the `=0` hatches (avg win turn is
DETERMINISTIC, so this needs no quiet box — only enough games):

```
MTG_BP_DIG_SELF_SOURCE     -0.0020   BETTER  (improves 3 keys, costs 2)
MTG_BP_TUTOR_BF_UNNAMED    +0.0034   WORSE   (stompy d3_s3003, ONE game)
MTG_BP_WATCHER_SELF_PUT     0.0000
MTG_BP_CASCADE_CLAUSE       0.0000
                           -------
                           +0.0014   = exactly the GT delta
```

**Held out on 4,000 fresh games** (stompy, seeds 5005/7007/9009/11011, d3 b10), because one game in
300 is not a signal: **+0.0005 — two flipped games, and never better on any seed.**

### Why: wave 0 has no stillborn skip, and a quarter of these lists have ONE entry

`MTG_BP_PROBE`, stompy 300 games, site 10:

```
                    clause off   clause on     delta
  searched              7,124       7,633       +509
  ...on committed line  6,399       7,410     +1,011
  OVERRUN               6,243       8,181     +1,938     <-- 3.8 wasted applies per usable one
```

An *overrun* is a variant whose rank is past the end of its continuation list: it reaches the
breakpoint it targets, finds nothing there, and collapses to EMPTY — a whole apply spent
rediscovering its own base plan. `MTG_BP_CANDS_PROBE` says why there are so many:

```
  len: 1=798  2=1069  3=394  4=405  5-8=236  9-16=131  17-32=14  33-64=4
       mean 3.19   capped=1184 (38.8%)   unreachable=4440 (45.6% of all continuations)
```

**26.2% of site-10 breakpoints have exactly ONE continuation.** `AppendBreakpointVariants` emits W
ranks per marked plan unconditionally, so for those every rank >= 1 is a guaranteed overrun and rank
0 duplicates the canon default. **This is pre-existing** — 6,243 overruns before any clause landed
— and the `BpWaveWalker` already has the countermeasure (`BpLenMemo` / `MTG_BP_NSKIP_GLOBAL`
stillborn skip) that wave 0 simply does not have. The clause is not the defect; it is the first
thing to pay enough into it to show up in GT.

### A QUALIFICATION ON "ZERO" — read this before trusting the headline

The audit asks *"was this plan offered to the variant machinery?"* It does **not** ask *"did the
machinery have a second option to offer?"* For the 798 len-1 slots above, rank 0 IS the canon
default and the only genuine alternative is EMPTY — and `MTG_BP_EMPTY_ARM` is **default OFF**. By
the USER's own rule (2026-09-16, *"Empty needs to be a valid option ... for every segment"*) that
slot is still a one-option decision. ZERO is true for what it measures and narrower than it sounds.

### Two things already tried, so do not re-derive them

* **Gate the unnamed-tutor clause on "the LIBRARY holds a hand-gaining body"** — built and
  **REFUTED by the audit**: stompy went straight back to **184 unchallengeable**. A plan can add a
  body to its own library mid-apply (Natural Order's sacrificed Worldspine Wurm shuffles in and is
  a legal fetch target), so the gate has an unsound direction. Reverted. Note this is the audit
  earning its keep on a change made *after* it went green.
* **Drop site 10 from wave 0 and let the walker take it** (`MTG_BP_W0_SITES`) — the same move was
  measured on site 6 (2026-08-20) and refuted: quality identical, **cost WORSE** (+89.1% vs
  +79.0%), because the deferred phase re-derives what wave 0 already had.

### The real follow-ups, in order of prize

1. **Stillborn skip in wave 0.** More than half of stompy's site-10 fan-out work is already
   stillborn. Fixing it pays for this clause many times over and helps every deck. Needs its own
   A/B; the walker's `BpLenMemo` is the model.
2. **`MTG_BP_EMPTY_ARM` for len-1 slots.** Closes the qualification above AND replaces W-1
   overruns with one genuinely different line. Changes every deck's GT.

## Hosting is a separate axis, and it is still open

It is worth costing on its own merits — `MTG_BP_NODE_ROOTTURN=0` hosts every searched turn (priced
in `greedy-continuation-deletion-route.md` Addendum B: recovers hinata at +24% units) and
`MTG_BP_NODE_HOST2` buys a third for ~2% — but it is about giving a slot a node **in place**, not
about whether its alternatives exist at all. Those are now all reachable.

**Keep running the audit.** Two of the four gaps above were created by changes that had nothing to
do with breakpoints (an adopted tutor lever; a card added to a deck), and neither would have been
found by reading the code.

## Built, measured, NOT adopted: `MTG_BP_WAVEDROP_HOSTED`

`BpNodeWaveDrop` removes site 3 from **both** wave masks unconditionally, on the premise that the
node searches those continuations in full. But hosting is conditional — `BpNodeRootTurnOnly`
(default ON) hosts only on the turn the outermost solve is choosing. On every other searched turn
the site has no variants, no waves **and** no node. `BpNodeWaveDrop`'s own header already called
this *"a reachability LOSS bought for a node at one depth"*.

`MTG_BP_WAVEDROP_HOSTED=1` gates the drop on `BpNodeHostsThisTurn`. It is a real hole and this is
the right shape of fix, but **it is not the cause measured here**: with it on, the 100%-unreachable
decks did not move at all, because their carrier was the missing site-3 clause. Default **OFF**
pending its own measurement, now that the clauses have changed what it would be measured against.

*Note the trap in re-measuring it:* the audit's raw counts are **not** a cross-arm comparator.
Restoring a site to the walker creates more applies, so the absolute count of canon defaults rises
mechanically. Compare per-site reachability (a boolean), or measure play directly.

## 2026-09-27 — `MTG_BP_HAND_ENTRY=1` RE-OPENS THE HOLE THIS DOC CLOSED, and no built lever shuts it

**Found while fixing the Acclaimed Contender same-turn-deploy defect** (see
`contender-same-turn-deploy-searched.md`). `MTG_BP_HAND_ENTRY=1` is the only thing that fixes that
defect — it closes the deploy hole at every depth d1/d3/d5/d7, 105/105 scenarios pass — **but it
re-introduces unchallengeable canon defaults, which is why it must stay OFF.**

### The measurement, on `test/scenarios/whiteknights_contender_same_turn_deploy.json` at d5

| config | canon defaults | unchallengeable | site-unmarked |
|---|---|---|---|
| **shipped (flag off)** | **0** | — | — |
| `MTG_BP_HAND_ENTRY=1` | **7** | **7 (100%)** | 0 |

```
[canon-audit]   site 10  total=7  UNCHALLENGEABLE=7  unmarked=0  <-- NO ROUTE INTO THE VARIANT MACHINERY
[canon-armed-by] UNCHALLENGEABLE site 10 [NOHOST]  (inline cast) 7
```

So the correct play this flag produces (the Knight cast on turn 4) is an **unchallengeable one-option
heuristic**: `cands.front()` with nothing scored against it. It is right here and was wrong on auras gi428
(T4 -> T5). This is the shape this document's own precondition forbids: *"A class in `BpSiteMask` MUST have
a route into the variant machinery."*

### `unmarked=0` IS THE KEY DETAIL — the clause is not the problem

Site 10 **is** marked by `PlanOpensBreakpoint` for these plans. The failure is entirely **`[NOHOST]`**: an
apply with **no capture and no carried choice**, so there is no fan-out to be selected by. That matches this
doc's own earlier negative result — extra marking *"buys intent, not reachability"*. **Do not reach for a
`PlanOpensBreakpoint` clause here; it is already marked.**

### NEGATIVE RESULT — all four built levers were tried and NONE moves it

Measured this session, same fixture, `MTG_BP_HAND_ENTRY=1` throughout:

| lever | unchallengeable site-10 defaults |
|---|---|
| baseline | 7 (100%) |
| `MTG_BP_NODE_ROOTTURN=0` (host every searched turn) | **7 (100%)** |
| `MTG_BP_WAVEDROP_HOSTED=1` | **7 (100%)** |
| `MTG_BP_EMPTY_ARM=1` | **7 (100%)** |
| all three together | **7 (100%)** |

**Why `ROOTTURN=0` cannot help, and this is the generalisable point.** It only decides whether
`node_host_here` is set at the **two** hosting call sites (`TurnSolver.cpp` ~43676 / ~45127). An apply that
passes **no capture pointer at all** is unaffected by every hosting *gate*, because the gate is downstream of
a capture that was never offered. Counted: **53 `ApplyPlanDirect` call sites, 7 pass a capture, 2 are
hosting entry points.** The hand-entry arming fires at applies outside that set.

So hosting is not one axis with a knob — it is **which callers offer a capture**, and that is code.

### THE DESIGN TO BUILD (USER, 2026-09-27, in their words)

* *"Fully searched within the search window."*
* *"We should open breakpoints exactly when they add a new option, which could be a new spell to cast or an
  ability we can activate."*
* *"Obviously just deleting greedy is not the solution. We need something to pick up the slack and that
  should be search (but bounded just to new lines that are possible because we stopped at the breakpoint)."*
* *"Only Heuristics can narrow the search to reduce branching."*

Read together: a heuristic may **order or narrow** candidates; it may never **be** the decision. Every
breakpoint opened inside the window must have its alternatives scored, with EMPTY among them.

**The arming rule is a tightening, not a widening, and that matters for cost.** Today site 10 arms on
`HandGainedACard` — a proxy that over-arms (a card gained that cannot be cast this phase still opens a
breakpoint) and under-arms (an ability that becomes activatable without any hand change opens nothing).
Arming on *"a new option exists"* is strictly more precise, and it subsumes this doc's own follow-up #1
(*"more than half of stompy's site-10 fan-out work is already stillborn"*) — a stillborn fan-out IS a
breakpoint opened where no new option existed. So precision here pays for the reachability work rather than
adding to its bill.

### Work programme, in dependency order

1. **Arming precision** — replace/weaken `HandGainedACard` as the site-10 trigger with "a new OPTION is
   available": a spell in hand now castable with the mana actually left, or an ability now activatable.
   Reduces arming volume and kills stillborn fan-out. Must be computed identically in both worlds.
2. **Caller hosting** — offer a capture at the applies inside the search window that can arm site 10, so
   the continuation is enumerated and scored rather than canon-picked. This is the `[NOHOST]` fix and the
   only one that moves the audit.
3. **`MTG_BP_EMPTY_ARM`** for len-1 slots — the user's 2026-09-16 rule (*"Empty needs to be a valid option
   ... for every segment"*), and the standing qualification on this document's "ZERO".
4. **Then** `MTG_BP_HAND_ENTRY=1`, gated on the audit reporting **zero** unchallengeable defaults. GT moves
   broadly; accept flow.

*Re-measurement trap (restated):* the audit's raw counts are not a cross-arm comparator — restoring a site
to the walker creates more applies, so absolute counts rise mechanically. Compare **per-site reachability as
a boolean**, or measure play directly.

### 2026-09-27 — THE FIX IS BUILT AND MEASURED. Correctness closed; searched-depth quality is a BUDGET problem.

**Built** on branch `bp-canon-caller-audit` (`1b3be5b9`), two pieces, both required:

1. `PlanOpensBreakpoint` marks site 10 for an `etb_dig_count > 0` cast, **gated on `BpHandEntryEnabled()`**
   (clause exists exactly when the arming route needing it exists ⇒ shipped config byte-identical).
   The site-10 clause carried `!ParamKeyedDrawClass`, correct for the CAST-window arming but **not** for the
   hand-entry route that fires anyway. The comment above it already demanded this pairing
   (*"If a route is ever added, ADD IT HERE TOO … nothing enforces it"*). The route was added; the clause was not.
2. **`BpNodeHostSites()`** (`MTG_BP_NODE_S10`, default OFF), kept **separate** from the renamed
   `BpNodeSitesBase()` so hosting a site does not also strip its wave coverage.

**MY EARLIER CALL IN THIS DOC WAS WRONG:** I read `unmarked=0` as proof the site was marked. `site-unmarked`
is a separate SOFT tier; 0 there proves nothing. Both a clause AND a host were missing — *marking alone
cannot host, hosting alone is never selected.*

**Correctness result** (Contender fixture d5, play correct at `opponent_life=-3` throughout):

| config | canon defaults | UNCHALLENGEABLE |
|---|---|---|
| `HAND_ENTRY=1` | 7 | **7 (100%)** |
| `+ clause` | 7 | **0 (0.00%)** |
| `+ clause + NODE_S10=1` | **none at all** | — |

**SMOKE A/B — and the depth pattern is the finding.** Shipped defaults: **97 passed / 0 failed / 0 new,
ALL PASS** (byte-identity gate ⇒ the change is inert with its flags off). With both flags on: **56 passed /
41 failed**, of which **26 are digest-only** (play changed, average identical), **8 faster, 7 slower**:

| direction | cases |
|---|---|
| **faster** | fungus **d0**, hinata **d0**, auras **d0**, goblins **d0**, stompy **d0**, melira **d0**, melira d5, snow **d0** (−0.0680) |
| **slower** | th d3, knights d3 (+0.0120), knights d5 (+0.0133), melira d3 (+0.0200), hinata2hg d3, knights2hg d3, melira2hg d3 (+0.0400) |

**Almost every gain is at d0 and almost every loss is at d3/d5** — i.e. it helps exactly where the node
already hosts (at d0 the root turn IS the only turn) and hurts where hosting is root-turn-only, so the newly
opened breakpoints compete for a fixed `BpSearchWidth()`=2 window on non-root turns. **Opening more
breakpoints without more reachability dilutes the search.** That is a reachability/budget problem, and by the
user's standing rule (2026-09-05 / 09-17) *"a red measurement here is a BUDGET problem to remedy, never
authorization to keep an unchallengeable default."*

**Do NOT read these deltas as settled.** Smoke cells are 500–1,000 games, and this very flag's history is a
train table that held-out refused to confirm. The wall-clock comparison is also **not** usable here: the
flags-off run overlapped the tail of a live screen (155s vs 54s is contention, not a speedup).

**Next:** host site 10 beyond the root turn (`MTG_BP_NODE_ROOTTURN=0` now becomes meaningful *because* site
10 is in the host set — it was inert before), and/or widen the window for newly-opened sites; then re-measure
on the regression tier with held-out seeds before any adoption call.
