# Incremental breakpoint key — the one lever left on the recipe's wall cost

**Status: DESIGN (2026-09-03). Authorized by the USER ("go through your list") as the structural
follow-up to the tight-recipe adoption (`ebfb5f74`). Not started.**

## Why (CORRECTED 2026-09-03 — read the dragonstorm caveat before starting)

The case for the incremental key is **hinata's REC re-widening**, not dragonstorm:

* hinata: the UNSCOPED canon (`MTG_BP_CANON_REC=1`, 4.03M fires/300 games) was quality-better
  (−0.0126 vs the shipped tight −0.0057) but cost +18.6% wall vs tight's +6.4% — and after the
  verdict memo removed the copies/solves, that gap IS the `BuildBreakpointKey` walk volume
  (~4M walks). A cheap key recovers ~0.005 hinata quality the adoption left parked.
* The recorded enummemo negative: "a revisit must make the KEY incremental, not tune the cache."

**Dragonstorm's +12.9% is NOT this project's problem — every walk/derivation theory measured
DEAD (2026-09-03, all per-200-game pinned cells):**

* NOT cache thrash: 94% enum hit rate, `MTG_BP_ENUM_CACHE_CAP=262144` wall-neutral.
* NOT enum volume AT ALL: off-arm enum traffic 179,535 hits / 11,311 misses vs tight-arm
  176,079 / 11,374 — **the delta is zero**; the baseline already pays all of it.
* NOT canon probe volume: under tight, ds canon fires only 20,997/300 games with 3,336 probe
  Solves and 1,051 copies — an order of magnitude short of the ~1s/200-game delta.
* NOT charged trajectory work: units_total off 922,381 vs tight 919,254 (flat).
* The earlier lever decomposition (one pinned session) put noderoot alone at −0.6% and blamed
  canon — but a contended re-triple was uninterpretable; the ATTRIBUTION IS OPEN pending a
  quiet-box off/noderoot/tight triple. Do not size this project on dragonstorm until that
  lands; it is possible the +12.9% has a session artifact or an owner none of the counters see
  (e.g. per-fire BuildSimKey cost scaling with ds's state size — unproven).

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

The primary target: re-gate `MTG_BP_CANON_REC=1` on the cheap key (paired hinata 10k/block) —
if it holds ~−0.012 at a wall the USER's bar accepts, propose re-widening. Secondary: hinata's
shipped +6.4% shrinks. Dragonstorm is NOT an acceptance criterion until its attribution closes
(see above). Play must be byte-identical throughout (the key is an identity, not a policy — any
digest move is a bug).

## Context

`docs/design/bp-greedy-continuation-deletion.md` (the adoption + the full wall tables),
`logs/ngc_sound/wall_sweep/`, `logs/ngc_sound/ds.tight.enumprobe.err`. The stashed
`MTG_BP_NODE_FPSKIP` WIP (blocked on a prefix-resume cache) is adjacent: a prefix-resume cache
would REDUCE apply volume, which multiplies with any key win.
