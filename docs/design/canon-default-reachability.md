# Canon-default reachability: what is left, and why it is not clause work

**Status (2026-09-22):** the CLAUSE half is done and shipped. What remains is a different
problem with a different remedy, written up here so it is not re-derived as "another missing
clause".

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
bash scripts/canon_audit.sh              # all 22 suite decks, each at its OWN play settings
OUT=logs/foo EXTRA="MTG_X=1" bash scripts/canon_audit.sh
```

Read the output as: **any nonzero `UNCHALLENGEABLE` is a doctrine violation**, to be fixed by
giving the site a route — never by suppressing the audit. Two further columns matter:

| column | meaning | remedy |
|---|---|---|
| `UNCHALLENGEABLE` | the plan opens **no** masked site: no variant of it is ever emitted | a route |
| `site-unmarked` | the plan *is* selected on some other site, so the waves reach this slot positionally | optional; a clause makes the coverage intended rather than incidental |
| `[hosted]` / `[NOHOST]` | was *any* search node on offer at this apply (capture pointer or carried `bp_choice`) | `[hosted]` → a clause; `[NOHOST]` → a **caller** |

## Three ways the audit lied before it was trustworthy

Worth keeping, because each is a trap the next instrument will hit too.

1. **It asked `>> site & 1`.** Fan-out selection is **per-plan**; the slot index is **positional**
   (`bp_at = k` into whatever enabled-class breakpoints the apply reaches). So a selected plan's
   k-th breakpoint is challengeable *whatever site it lands on*. Asking per-site reported
   mirrorwing's 16,448 site-0 defaults as violations — every one a nested cantrip inside a Gold
   Rush continuation on a plan the site-5 clause already selects.
2. **It re-derived state-keyed predicates at the wrong state.** The fan-out evaluates them
   pre-apply; the audit sits mid-apply. The dig loop *consumes* its source, so `HasAnyDigSource`
   read false on the very plans the bypass had fanned out — inventing ~8,000 violations across
   auras/dragons. Fixed by preferring `plan.bp_wave0`, a **record of fact** rather than a
   re-derivation.
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

## THE OPEN ITEM: `[NOHOST]` — a missing caller, not a missing predicate

```
auras       site 4   [NOHOST]  1,690
melira      site 7   [NOHOST]  1,079
melira      site 10  [NOHOST]    466   (Chord of Calling)
dragons     site 4   [NOHOST]    116
th          site 1   [NOHOST]     41
th          site 4   [NOHOST]      8
                               -----
                               3,400  = 94.6% of the remainder
```

These applies have **no capture pointer and no carried `bp_choice`** — nobody is offering them a
search node at all. No clause can help: there is nothing to be selected *by*. This is the class
`greedysite::kNoHost` already measures, and its own note prices the shape: *"a capture is passed at
2 of the engine's ~42 `ApplyPlanDirect` sites"*, with 52–100% of the remaining fallback landing
there.

**Do not respond to this with more clauses.** It was tested: a site-7 widening (count the Pod
sources the *plan* casts, not just the battlefield's) moved melira's `unmarked` 13,079 → 4,476 and
its `UNCHALLENGEABLE` count 1,079 → **1,079**, and was reverted. Marking a plan that no host will
fan out buys intent, not reachability, and pays a wider candidate set for it.

The remedy is to give those apply sites a host. Two known routes, neither costed yet:

* **Widen hosting.** `MTG_BP_NODE_ROOTTURN=0` hosts on every searched turn (priced in
  `greedy-continuation-deletion-route.md` Addendum B: recovers hinata at +24% units);
  `MTG_BP_NODE_HOST2` buys a third for ~2%. Both raise cost on turns that get re-decided anyway —
  99.4% of the full node's work is on lookahead turns never played.
* **Offer a capture at more of the 42 apply sites.** The partition design the node doc reserved for
  the inline sites (0/1/2/4) is the blocking piece: those sites resolve mid-apply and the node
  cannot snapshot them today. Measured on Hinata, moving a resolve inline is itself worth +0.33
  before truncation is even considered — so this needs its own design, not a flag.

## The 195 that ARE clause work

`fluctuator` site 4 (194) and `th` site 1 (1), both `[hosted]`. Small, and the last genuine
predicate gaps in the suite. `th` site 1 is `DrawUntilNonland` armed on an apply whose plan does not
cast Treasure Hunt (a dug Treasure Hunt cast inside the dig's own re-solve is the likely shape).

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
