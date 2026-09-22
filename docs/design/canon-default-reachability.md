# Canon-default reachability: what is left, and what it takes to close it

**Status (2026-09-22):** ~124,000 unchallengeable canon defaults → **1,273 on 4 decks**, and the
audit that measures them is finally asking its question at the right state. The remainder is more
clause work, in three named shapes — see THE OPEN ITEM. An earlier revision of this doc concluded
the opposite ("not clause work; it needs a caller"); that conclusion rested on a measurement
artifact and is corrected in place below rather than deleted, because the way it went wrong is the
most reusable thing here.

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

## THE OPEN ITEM — 1,273, and they are CLAUSE work after all

Measured 2026-09-22 at 60 games/deck, with the audit asking pre-apply:

```
auras     site 4   [NOHOST]  467   (inline cast)
melira    site 10  [NOHOST]  466   Chord of Calling
stompy    site 10  [NOHOST]  291   Natural Order 178 / World War Hulk 68 / Turntimber Symbiosis 45
th        site 1   [NOHOST]   41   (inline cast)
th        site 4   [NOHOST]    8   (inline cast)
                             -----
                             1,273   on 4 decks;  [capture] = 0 and [variant] = 0 everywhere
```

**A PREVIOUS VERSION OF THIS SECTION SAID THE OPPOSITE AND WAS WRONG.** It claimed 3,400 of the
remainder were `[NOHOST]` and therefore needed a *caller*, citing a site-7 widening that moved
melira's `unmarked` 13,079 → 4,476 while leaving `UNCHALLENGEABLE` at 1,079. Two errors compounded:

1. **The 1,079 were never real.** They are zero once the audit asks pre-apply. The widening looked
   inert because the thing it was being judged against was measuring the wrong state.
2. **`[NOHOST]` does not mean "a clause cannot help".** A base plan's variants are scored as
   **sibling candidates**, not as children of the apply that reached the breakpoint. Marking the
   plan is therefore exactly what makes the alternatives reachable, whether or not a node could
   have hosted this particular apply. The tag separates *node hosting* from *reachability*, and
   those were conflated.

So the remedy for all 1,273 is the same kind of work that took ~124,000 down to this: name the
route in `PlanOpensBreakpoint`. Three distinct shapes remain, and each is a **play change** needing
its own GT rebaseline:

* **`stompy` site 10 (291) — put-in-hand the static predicate cannot see.** Natural Order,
  Turntimber Symbiosis, World War Hulk. Site 10's arming asks the *outcome* ("did the hand gain a
  card?"), which is why the clause has to name routes; these three are routes it does not name.
* **`auras`/`th` site 4 (475) — a plan that creates its OWN dig source.** `BpDigFanoutForPlan` asks
  `HasAnyDigSource` on the pre-apply board, and the header already notes a source "can only be
  ADDED by a plan's own casts, never removed" — which is conservative for the fan-out and is
  precisely the hole: the plan whose cast *adds* the source is never fanned out. The fix is a
  plan-**action** test (does this plan cast a dig source?), which is state-independent and so
  cannot regress into the trap above.
* **`melira` site 10 (466) — Chord of Calling**, and **`th` site 1 (41) — `DrawUntilNonland`** on
  an apply whose plan does not cast Treasure Hunt (a dug Treasure Hunt cast inside the dig's own
  re-solve is the likely shape).

**Hosting is a separate axis, not the remedy for these.** It is still worth costing on its own
merits — `MTG_BP_NODE_ROOTTURN=0` hosts every searched turn (priced in
`greedy-continuation-deletion-route.md` Addendum B: recovers hinata at +24% units) and
`MTG_BP_NODE_HOST2` buys a third for ~2% — but it is about giving a slot a node *in place*, not
about whether its alternatives exist at all.

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
