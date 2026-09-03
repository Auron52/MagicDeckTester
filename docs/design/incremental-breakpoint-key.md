# Incremental breakpoint key — the one lever left on the recipe's wall cost

**Status: DESIGN (2026-09-03). Authorized by the USER ("go through your list") as the structural
follow-up to the tight-recipe adoption (`ebfb5f74`). Not started.**

## Why this and nothing else

Every residual wall number on the adopted tight recipe points at the same cost: the
`BuildBreakpointKey` full-state walk, paid once per memo LOOKUP — hit or miss.

* dragonstorm +12.9%: **NOT cache thrash** (94% enum hit rate, 176,079 hits / 11,374 misses /
  1 clear / 0 nested per 200 games; `MTG_BP_ENUM_CACHE_CAP=262144` wall-NEUTRAL). The cost is
  ~187k key walks + 11.4k real derivations.
* hinata +6.4%: the scoped canon's remaining fires (~341k/300 games) each pay a walk; the
  UNSCOPED form (`MTG_BP_CANON_REC=1`, 4.03M fires) was quality-better (−0.0126 vs −0.0057) but
  cost +18.6% wall — almost entirely walks (the verdict memo already removed the copies/solves).
* The recorded enummemo negative says it plainly: "Deep-search states are too diverse for a
  full-key memo whose key walk costs as much as a small enumeration... a revisit must make the
  KEY incremental, not tune the cache."

One fix, three payoffs: ds under the bar, hinata toward the noderoot floor (~+4%), and REC=1
affordable again — recovering the parked ~0.005 hinata quality the USER agreed to leave on the
table at adoption.

## What the key is

`BuildBreakpointKey(state, is_pre)` = `BuildSimKey(state, 0, 0, is_pre)` (the full walk: zones,
battlefield, life, library digest...) + mid-turn folds (floating mana, casts, scripted pins,
free-cast bank, m1-hand stamp — all cheap scalars) + call-site folds in `BpEnumBuildKey`
(cantrip-order site, pre-draw hand snapshot, karoo reservation, mana-source count). The
expensive part is `BuildSimKey` alone; everything folded on top is O(1)-ish.

## Design options (stage them; do NOT start with the hard one)

**Stage 1 — one walk per apply (thread the key, no new machinery).** Within a single
`ApplyPlanDirect` fallback the same state is keyed up to three times: canon's verdict lookup
(already shared with the enum entry via `BpEnumBuildKey`/`pre_key`), the greedy fallback's
`TurnSolver::Solve` (solvememo keys the SAME state with the SAME function), and any eligible-
variant enumeration. Compute the base key ONCE per apply and pass it to each consumer (solvememo
would take an optional pre-key exactly as `BpEnumEntryFor` now does). Expected: integer-factor
reduction in walks at the hottest sites for a few dozen lines of plumbing, verified by the
existing VERIFY harnesses. Measure before designing further — this may already put ds under 10%.

**Stage 2 — versioned key cache on GameState.** A `mutable` cached (key, version) pair on
GameState plus a monotonically bumped `state_version` on mutation. The blocker is that GameState
members are mutated directly all over the engine (no chokepoints), so version bumps cannot be
made reliable without the same audit Stage 3 needs. Only worth doing as a stepping stone if
Stage 3's audit happens anyway.

**Stage 3 — true incremental (zobrist) maintenance.** XOR-fold per-component hashes updated at
every mutation site. This is the "make the KEY incremental" the enummemo record asks for, and it
is a large, risky audit (every zone move, tap, counter, life change...). Safety story if
attempted: `MTG_ENUM_MEMO_VERIFY=1` and `MTG_SOLVE_MEMO_VERIFY=1` recompute uncached on every
hit and diff — key bugs surface as counted mismatches, not silent play corruption; plus the A3
digest battery (hatch byte-identity, suite smoke, cross-arm battery at multiple caps).

## Acceptance

Wall (quiet box, `scripts/wall_probe.sh` protocol, cal-subtracted): dragonstorm < +10% vs
all-off; hinata materially under +6.4%. Then re-gate `MTG_BP_CANON_REC=1` on the cheap key
(paired hinata 10k/block): if it holds ~−0.012 at acceptable wall, propose re-widening to the
USER. Play must be byte-identical throughout (the key is an identity, not a policy — any digest
move is a bug).

## Context

`docs/design/bp-greedy-continuation-deletion.md` (the adoption + the full wall tables),
`logs/ngc_sound/wall_sweep/`, `logs/ngc_sound/ds.tight.enumprobe.err`. The stashed
`MTG_BP_NODE_FPSKIP` WIP (blocked on a prefix-resume cache) is adjacent: a prefix-resume cache
would REDUCE apply volume, which multiplies with any key win.
