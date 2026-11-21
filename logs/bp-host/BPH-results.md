# BPH: host-gated breakpoint-node wave stand-down (MTG_BP_NODE_HOST_GATE)

Defect: `docs/design/bp-node-wave-drop-in-lookahead-host.md`. USER 2026-11-21 (verbatim): "Okay, if
there is a potential hole in our design it should be closed."

## What was already there

`MTG_BP_WAVE_UNHOSTED_SITES` (default ON) already gave a walker with `host_pends=false` the full
`BpSiteMask`, but only in a SEARCHED apply (`g_rollout_nest == 0 && g_bp_enum_depth == 0`). The repro's
holed solve is the lookahead host inside a depth-1 rollout (`RolloutPreCombatSolve` ->
`SolveWithLookahead`), i.e. NOT a searched apply, so it still took the turn-keyed drop. The fix
(`BpWalkerSiteMask`, TurnSolver.cpp) drops the `searched_apply` condition for non-hosting walkers when
`MTG_BP_NODE_HOST_GATE` is on (default). `=0` restores the old expression exactly.

## Caller classification

Readers of the node drop: `BpNodeHostsThisTurn` is called only by `BpNodeWaveDropAt`;
`BpNodeWaveDropAt` only by `BpWave0SiteMask` and `BpWaveSiteMask`; `BpWaveSiteMask` only by the
`BpWaveWalker` constructor (now via `BpWalkerSiteMask`).

| Reader | Site | Host bit | Class | Mask after fix |
|---|---|---|---|---|
| `BpWaveWalker` in `FSLineWin` m1 loop | ~121063 | `host_pends=node_host_here` | HOSTING when node_host_here | drop (unchanged); full when node_host_here false |
| `BpWaveWalker` in `FSLineTail` m2 loop | ~117505 | `host_pends=node_host_here` | HOSTING when node_host_here | same as above |
| `BpWaveWalker` in `FSLineWin` group-wave tranche | ~121442 | `false` | NON-hosting | full `BpSiteMask` in every apply |
| `BpWaveWalker` in `SolveWithLookahead` candidate loop | ~129699 | `false` | NON-hosting (the repro) | full in every apply |
| `BpWaveWalker` in `SolveWithLookahead` group-wave tranche | ~129978 | `false` | NON-hosting | full in every apply |
| `WaveWalkerTraceForTest` / `WaveWalkerDevSeqForTest` | test hooks | param / `false` | test | follows the param |
| Nested discovery / `AddHostedNested` / dev-seq slots | inside the walker | inherit the walker's base set | same as the owning walker | no separate mask read |
| `BpWave0SiteMask` (`AppendBreakpointVariants`, from `EnumerateM2PlansBody`, `EnumeratePlansWithLandUncachedBody` x2) | enumeration | none (host-agnostic, enumeration time) | n/a | unchanged; see note |
| `BpSiteMask()` direct: `PodBreakpointClassOn`, `Site8NarrowIsWindowBase`, `BpWave0SiteMask` base, `ApplyPlanDirect` (class_on ~74055, opens ~74529, site 9 ~81097, deferred site ~81251) | various | none | no node drop | unchanged |

Note on wave 0: `BpWave0SiteMask` is built at enumeration time, where no host is known, so it cannot
take the host bit without keying the plan caches on it. Its node drop is an ordering only: a plan wave 0
did not fan has `bp_wave0 == false` and its walker slots open at rank 0 (`AddSlots`: `k0 = 0`), so once
the non-hosting walker keeps the node's sites nothing is unreachable. At defaults the wave-0 node drop
is also inert (node drop = {3} and `MTG_BP_SITE3_DEFER` already removes site 3 from wave 0); it matters
only under `MTG_BP_NODE_D56` (sites 5/6). Under `MTG_BP_WAVES=0` (no walker at all) the ordering argument
does not hold, but there every rank >= W is unreachable anyway; not a default shape.

Nothing could not be classified.

## Results

Repro (`--seed 434023 --game-index 22`, Maze's End 2HG d3 b10, `MTG_LEAF_VERIFY=1 MTG_NOWIN_VERIFY=1`,
one thread, binary built from this branch):

| arm | leaf STALE hits |
|---|---|
| `MTG_BP_NODE_HOST_GATE=1` (default) | **0** |
| `MTG_BP_NODE_HOST_GATE=0` (today) | 1 -- key 983036f572f4ce2, t7 d1, cached 8 / fresh 9, SAME-STATE DRIFT |
| `MTG_BP_NODE=0` | 0 |

The base count on this tree is 1, not the 3 in the design doc (the tree moved since; the surviving key is
the doc's first one). The game's average win turn is 7.0000 in all three arms.

Units: full `mtg-test` 3807/3807 test cases, 3,637,174 assertions, all passed, with the lever on AND with `MTG_BP_NODE_HOST_GATE=0`.
`test/unit/test_bp_wave_empty.cpp`'s "non-pending host walks it" case had to pin the new lever: its
`unhosted=0` arm expected the turn-keyed mask, which the gate now also closes.
