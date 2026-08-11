# Group waves: defer-don't-cap for the enumeration breadth cap

## The problem

`CapGroupsBySituationalRank` (TurnSolver.cpp) keeps only the top-K option groups by the
provider's `SituationalCardRank` when a hand offers more than K castable groups
(K = `DecisionProvider::EnumGroupCap`, engine override `MTG_SOLVE_GROUP_CAP`). The dropped
groups were discarded with **no record and no escalation**: a plan needing a dropped group was
unreachable at ANY budget. By the codebase's own deferred-wave doctrine (BpWaveWalker: *"no rank
is unreachable at an unbounded budget"*) that made the cap a **quality prune**, not a cost
scheduler — the board-lethal short-circuit protects attack-only wins, but a CAST-dependent
this-turn lethal sitting in a dropped group was invisible (the win found a turn later, or never).

Found during the Mirrorwing Dragon onboarding (user question: *"I was told the cap would not
impact the unbounded result… I pushed to have it escalate with remaining budget instead"* — the
bp axes have that escalation; the group cap did not). User approved this design in principle
(2026-08-11).

## The design: tranche-partitioned deferred re-enumeration

After a search node's candidate loop and its bp wave phase, while the node's budget allows,
re-enumerate one **tranche** at a time:

- **Tranche R** (R = cap, cap+1, … cap+dropped−1) keeps the groups ranked `[0..R]` and
  **requires** the rank-R group in every emitted plan (an `eval_and_push` filter on the
  selection's candidate indices).
- A plan's highest-ranked-used group assigns it to exactly one tranche, so wave 0 (the capped
  enumeration) plus the tranches **exactly partition the uncapped plan space** — no overlap, no
  dedup bookkeeping, no signature subtleties. The rank order is deterministic (same state → same
  candidates → same `stable_sort`), so the partition is stable across re-enumerations.
- Tranche plans are scored with the bp-wave contract: **strictly-better win turn only** (so
  stopping mid-phase is safe / anytime), budget-checked per plan, this-turn wins return
  immediately. Their own deferred breakpoint ranks are then walked with a `BpWaveWalker` over
  the tranche list, so the bp doctrine composes (no rank of a tranche plan is unreachable
  either).

At an **unlimited budget** the node's answer therefore equals the uncapped enumeration's — the
cap becomes a scheduler (best-ranked groups first), not a prune.

### Cost guard for budgeted nodes

The odometer walk is NOT budget-counted, and it is paid before any per-plan budget check can
fire — the exact mechanism behind the no-cap arm's known blow-ups (a 1139 s game; >3 h games in
the 5f A/B). So in tranche mode the cap computes the tranche's plan-space bound
(`2^ind × Π(1+|g|)` over kept groups) **before** the walk; a budgeted host passes
`bound_limit = budget->Remaining()` and a tranche that cannot even be 1-for-1 scored is skipped
outright and counted as a truncation (`g_fs_trunc_events`, so an enclosing no-win is not treated
as a refutation). Unlimited hosts pass no limit.

## Where it lives

- `groupwave::` namespace + `CapGroupsBySituationalRank` tranche mode + `EffectiveGroupCap`
  (TurnSolver.cpp, next to the cap).
- `EnumeratePlans`: tranche filter first in `eval_and_push`; early-empty return when the call
  has no rank-R group; Plan-B (spectacle) skipped in tranche mode (it never consults groups, so
  wave 0 emitted all of its plans).
- `EnumeratePlansWithLand`: the per-land "idle" plan suppressed in tranche mode (touches no
  group). Tranches run through the SAME wrapper as wave 0, so the land axis, bp wave-0 fan-out,
  MoveOrder sort and value re-rank all apply to tranche plans identically.
- Host phases (after the bp wave phase, same shape): `FSLineWin` (interior nodes of the
  full-search line; skipped at beam-cut nodes — the escalation beam declined breadth there on
  purpose, and it never applies at the root) and `SolveWithLookahead`'s per-pass candidate loop
  (the rollout/per-turn decision host; tranche lists built lazily once per node, rescored per
  pass exactly as `candidates` are).
- `Solve` (the greedy rollout leaf) keeps the plain cap: it has no budgeted scoring loop, and
  the leaf estimate is greedy by design.
- NOT covered (documented gap, small): the second-main enumerations (`FSLineTail`,
  `SolveSecondMainInSearch`) and the offline value-label dump's root loop. Post-combat hands
  have usually spent their mana, so the cap rarely binds there.

## Flags

- `MTG_GROUP_WAVES` (default **on**; `=0` restores the capped-only engine byte-identically —
  the standing A/B hatch).
- `MTG_GROUP_WAVE_PROBE=1`: per-run counter line
  `[group-waves] nodes/tranches/enumerated/scored/improved/budget-stopped/bound-skips`.
- Interactions: `MTG_NO_GROUP_CAP=1` / `MTG_UNPRUNE=groupcap` open the cap entirely → nothing
  dropped → waves inert. `MTG_SOLVE_GROUP_CAP` still tunes the base K (tranches start above it).

## Measurements (2026-08-11, implementation session)

- Off-arm byte-identity: `MTG_GROUP_WAVES=0` smoke 33/33 PASS (byte-identical to GT).
- On-arm smoke: **only** mirrorwing d3 moved, and it moved FASTER — game 137 (seed 1138) was
  UNWON under the cap and **wins at turn 8** with waves (searched faster=1 slower=0,
  play-changed=0; every other deck and mirrorwing d0/d5 byte-identical). Single-game repro
  confirmed head-to-head. This is precisely the "cast-dependent lethal in a dropped group"
  hazard the design targets.
- Probe (100 mirrorwing d3 b10 games, s5005): nodes=175 tranches=46 enumerated=scored=1058
  improved=0 bound-skips=144 — the phase fires; the bound gate carries most of the cost control
  at suite budgets.
- Doctrine agreement: the open-cap control (`MTG_NO_GROUP_CAP=1`) on game 137 at the same
  budget also wins at turn 8 — waves-with-cap matches the uncapped answer on the recovered
  game. (Opening the cap globally remains infeasible as a POLICY: the earlier cap A/Bs measured
  1139 s single games and 7 CPU-h partial arms; waves get the same answer while the bound gate
  keeps the pathological nodes from materializing their full odometer.)

## Suite A/B protocol

The regression suite IS the A/B: `MTG_GROUP_WAVES=0` reproduces old GT; default reproduces new
GT. Full regression + timings under default before any GT accept; unbounded spot-checks on the
known cap-binding mirrorwing games compare waves-vs-open answers.
