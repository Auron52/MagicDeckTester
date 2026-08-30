# The Aether Vial charge: root heuristic + a real search node (retire the probe oracle)

**Status: ADOPTED 2026-08-18 — user-directed.** Two changes shipped together:

1. **The hand-aware charge policy is the ROOT default** (`GenericProvider::WantVialCharge`), not a
   `VialProvider` opt-in. `VialProvider`'s now-redundant override is deleted.
2. **The charge is decided IN-SEARCH** as a plan axis (`Plan::vial_charge_choice`), replacing the
   out-of-band probe. `MTG_VIAL_AXIS` default ON; `=0` is the exact legacy hatch (probe restored).

Predecessor: `searched-vial-charge.md` (the probe this retires). Template:
`searched-discard-as-search-node.md` (the same ruling applied to the cleanup discard) and the
`bp_choice` pattern in `post-breakpoint-search.md`.

## How this was found

A sweep of the shipped search against all 208 saved references (`scripts/ref_bench.py`, which
reconstructs each reference's exact opening hand via `--force-mulligan` so PLAY is isolated from
mulligan policy) produced 2 shortfalls in 208. One was Goblins `claude_s18_gi17`: human turn 6, the
search no lethal by the turn-8 cutoff.

## Defect 1 — the root default froze the Vial in two decks

`GenericProvider::WantVialCharge` returned a flat `false`, commented "only Aether Vial decks charge;
archetype overrides". **That was never a guard.** All three call sites already gate on
`params.upkeep_adds_charge`, so the hook is only ever consulted for a real charge permanent and is
inert for every deck without one. Its only live effect was to freeze the Vial in any deck routed to
a non-`VialProvider` archetype:

| deck | provider | Vial |
|---|---|---|
| knights, slivers_vial | `Vial` | worked |
| **Goblins** (4x Aether Vial) | `Goblins` | **never gained a counter** |
| **Minotaur** | `AntiLifegain` | **same latent case** |

`SelectDecisionProvider` tests `if (goblin)` before `if (vial)`, and `GoblinsProvider` derives from
`GenericProvider` without overriding the hook. Goblins had 4 Vials that never charged.

## Defect 2 — the probe could not see this decision, and structurally cannot

`MTG_SEARCHED_VIAL` (default ON since 2026-07-31) rolled the game out under both answers and took
the earliest win. It never fired. Measured 2026-08-18:

- **671 probe firings** across goblins/knights/slivers → **0 deviations** from the heuristic;
  71–90% of firings the two rollouts tie outright.
- Disabling it is **byte-identical over 16,000 held-out goblins games**.

The reason is structural, not a bug. The trial rollouts model every *later* upkeep with the same
heuristic (`TurnSolver::SimulateBeginningPhase`; nesting the searched pass is hard-blocked by
`m_in_rollout` because it would explode). So both arms continue under "never charge again":

- trial A (hold): counters stay k forever
- trial B (charge): counters reach k+1 and stay there forever

The Vial deploys a creature whose mana value **equals** its counter count, so one extra counter pays
off only if the hand holds a creature at exactly k+1 *and* that changes the rollout's win turn.
Goblin Chieftain needs three consecutive charges; Muxus needs six. The probe only ever asks *"is
charging once better, given I never charge again?"*, and `win[1] < win[0]` demands a strict
improvement, so ties go to the heuristic — which, with defect 1, said hold at 0 forever. Every
upkeep of the shortfall game traced `win(heur)=9 win(alt)=9`.

This is also the shape the user's 2026-08-06 ruling retired for the cleanup discard: an out-of-band
estimator that plays nested engine games per candidate and hands the executor a pick — neither the
search deciding nor a heuristic pruning.

**Historical note, unresolved:** `searched-vial-charge.md` recorded held-out goblins −0.0720 across
8 cases when the probe was adopted. `GoblinsProvider` already existed at that commit (`a5b2198d`),
ahead of `if (vial)`, with identical `goblin` detection — so the Goblins Vial was already frozen and
that number cannot be reconciled with the routing. The knights (−0.0130) and slivers (−0.0030)
components DO reproduce today (+0.0140 / +0.0020 when the probe is disabled).

## What was built

Mirrors `discard_choice` exactly, one turn further out:

- `TurnSolver::Plan::vial_charge_choice` (0 = hold, 1 = charge, −1 = heuristic).
- Copied onto `GameState::scripted_vial_charge` by `ApplyPlanDirect` — it rides the STATE because
  the upkeep that reads it is **next turn's**, long after the apply returns. It must therefore
  survive the turn boundary: do NOT clear it alongside `scripted_cheat_choice` in
  `SimulateEndAndStartNextTurn`, which runs BEFORE the upkeep that consumes it.
- Rollout consume in `SimulateBeginningPhase`: first vial of the upkeep takes the pin and clears it;
  later vials fall back to the heuristic (the one-choice-per-plan convention).
- Executor lockstep: `AIEngine::m_vial_choice_pin`, written when the plan commits, consumed in
  `DecideVialCharge`, saved/restored around shared-engine rollouts alongside `m_committed_line`.
- Variant emission: post-dedup binary fan (hold + charge), base plans only so cost stays additive,
  gated on a Vial being on the battlefield or cast by this plan.
- Shared reader `VialAxisEnabled()` in `EngineFlags.h` (read by both TurnSolver and AIEngine).

The axis re-fans at **every level of the recursion**, so a multi-charge climb is a reachable line —
the thing the probe structurally could not represent. Proof: with the heuristic still hard-wired to
`false`, the axis ALONE takes the shortfall game from no-lethal to **turn 6**.

## Measured — held-out (overnight) seeds, sum of per-case Δ avg win turn

| arm | goblins | knights | slivers | total | searched cost |
|---|---|---|---|---|---|
| heuristic fix only | −0.2095 | 0 | 0 | −0.2095 | 1.00x |
| axis only (open fan, broken heuristic) | −0.0170 | +0.0120 | +0.0640 | +0.0590 | 1.70–2.03x |
| heuristic + narrow axis (**rejected**, see below) | −0.2085 | +0.0100 | +0.0140 | −0.1845 | 1.18–1.38x |
| **SHIPPED: heuristic + open axis** | **−0.2035** | **+0.0120** | **+0.0640** | **−0.1275** | 1.70–2.03x |

Goblins per-game (heuristic fix, 16,000 held-out games): **342 faster, 9 slower**. Only the three
Vial decks move; the other nine suite decks are byte-identical at every tier.

The knights/slivers cost is a **budget race, not worse judgment** — the fan doubles the plan count
at a fixed per-decision budget, and those decks run tight budgets (slivers d3 b10, d5 b20). At 4x
budget the slivers loss vanishes entirely (4.2330 axis vs 4.2330 baseline, identical). The fan also
fires ~20x more often there than on goblins (488/542 vs 25 per 3 games).

## Rejected: the obviousness gate

A narrowed variant was built and measured: fan only when the call is genuinely two-sided (a creature
at exactly the current counter to deploy AND a bigger one in hand to climb to) — the same shape as
`PonderAxisPartial`. It worked as intended: **4.6x less fan-out on knights, 9.8x on slivers**, and it
cut the slivers cost from +0.0640 to +0.0140.

**Rejected anyway, user-directed:** *"the option to search needs to remain for all of the decks …
even though we won't be taking it in most cases, maybe ever. There are cases where we might want to
take it, though. If it is really not obvious what decision to make."*

Two grounds:

1. It is a **lossy prune under Rule 0b** — "hold at k because I will draw an MV-k creature next
   turn" becomes unreachable even at infinite budget.
2. It decides which calls are obvious **using the very heuristic the search exists to overrule**.
   The gate also demonstrated the failure directly: with Goblins' heuristic broken, the gate
   correctly reasoned "charging is obvious here, no branch needed" and then relied on the heuristic
   to take the obvious action — which it did not, reverting the shortfall game to no-lethal.

The branch stays open on every deck. The measurement is recorded here so the narrowing is not
re-derived.

> **SUPERSEDED 2026-08-30 — see "The default flipped" below.** This section stood, and an agent
> re-derived the narrowing anyway while editing the very flag whose comment carries this note. If
> you are changing a flag, read its documentation first; that failure is the reason this warning is
> now at the top of the section instead of the bottom.

## The default flipped (2026-08-30): heuristic ships, the fan is opt-in

**User direction:** *"In general yes, we don't want to fully search vial decisions, because that is
a waste of effort. However, that doesn't prevent the option from being open if it is needed. Though,
if we did take it, we would only do so under certain circumstances."*

| config | behaviour |
|---|---|
| default (`MTG_VIAL_AXIS` unset) | the hand-aware root heuristic decides; no fan, no probe |
| `MTG_VIAL_AXIS=1` | fan, GATED to the circumstances below |
| `MTG_VIAL_AXIS=1 MTG_VIAL_AXIS_NARROW=0` | the exact pre-2026-08-30 unconditional fan |

`MTG_SEARCHED_VIAL` also defaults OFF now. It had to: it was dead on the shipped path only *because*
the axis owned the decision, so flipping the axis alone would have silently revived a retired
out-of-band probe as the shipping decider — not the configuration that was measured.

**The gate.** `WantVialCharge` resolves every ordinary call deterministically off the hand. The one
it declines is the tradeoff its own comment defers — "deploy a cheaper creature now vs climb to a
lethal bigger one" — which is live only when the hand holds a creature ABOVE `vial_target_mv`.

### Why this supersedes the 2026-08-18 adoption rather than just disagreeing with it

`b289661b` bundled TWO changes: it fixed `WantVialCharge` (which returned flat `false`, so a Vial in
a non-VialProvider deck never gained a counter in its life) AND it added the axis. Its held-out
−0.1275 is the **sum of both**, dominated by goblins −0.2035 — plausibly the heuristic fix, which was
enormous. That commit's own table already showed the axis COSTING knights +0.0120 and slivers
+0.0640, excused at the time as "a budget race, not worse judgment".

Isolated on 2026-08-30, with the heuristic already correct:

| tier | removing the fan |
|---|---|
| smoke | −6.98 turns / 1,125 games |
| regression | −25.02 / 4,700 |
| overnight (held out) | −76.00 / 28,500 |
| **all three** | **−108.00 / 34,325** (−0.00315/game) |

Per deck on the overnight: slivers −62, minotaur −10, goblins −6, knights +2. Cost on an idle-box
4-deck probe: wide 1.00x, gated 0.71x, heuristic **0.32x**.

**The deck it helps most is slivers — exactly the deck the 2026-08-18 table said it cost.** The two
measurements agree with each other; what is new is that the budget race is a standing net loss, not
a wash that disappears at 4x budget.

### What has NOT changed

Ground 1 of the original rejection still bites the gated form: it is a lossy prune under Rule 0b —
"hold at k because I will draw an MV-k creature next turn" is unreachable under the gate even at
infinite budget. That cost is now accepted knowingly, and is only ever paid by someone who has opted
into the axis at all.

Ground 2 (the gate judges obviousness using the heuristic the search exists to overrule) was
*demonstrated* with a BROKEN heuristic — the Goblins case where the gate correctly said "charging is
obvious" and then relied on a `WantVialCharge` that returned false. The principle stands; that
particular demonstration no longer reproduces, because the heuristic it depended on is fixed.

**Not re-run:** the 2026-08-18 comparison itself — the axis alone against the fixed heuristic on the
208-reference bench, which is where the original found its shortfall game. The supersession argument
above is an inference from `b289661b`'s own numbers, not an isolation of them.

## Follow-up

`MTG_SEARCHED_VIAL` is now dead on the default path (the axis owns the decision; the probe is
reached only under `MTG_VIAL_AXIS=0`). It should be deleted outright once this has ridden a few
cycles, along with `ChargeRemainingVialsHeuristic` and the `ResumeAt::UpkeepTail` entry that exist
only to serve it.
