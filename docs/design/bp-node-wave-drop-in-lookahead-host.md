# The breakpoint node's wave stand-down fires in a host that never hosts the node

Status: ROOT-CAUSED 2026-11-20 (RC-3, Maze's End rollout-cache work). No fix is implemented. The fix is a
play change and needs a held-out A/B. Whether to adopt it is the USER's call.

## Symptom

`MTG_LEAF_VERIFY=1` reports leaf-table STALE hits of the "SAME-STATE DRIFT" kind: the stored rollout
state, replayed now with no budget and no table, returns a different win turn from the one it returned
when it was stored. The store-time re-derivation agreed with the stored value, so the drift happens
between the store and the hit.

Repro (single thread, about 5 s; the game is Maze's End 2HG at d3 b10):

```
MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2 MTG_LEAF_VERIFY=1 MTG_NOWIN_VERIFY=1 \
mtg "decks/Maze's End/Maze's End.cod" --profile "decks/Maze's End/Maze's End.profile.json" \
    --seed 434023 --game-index 22 --games 1 --depth 3 --budget-ms 10 --threads 1
```

This gives 3 stale hits on 2 keys: `983036f572f4ce2` (t7 d1, cached 8, fresh 9) and `4c3548248d696b95`
(t6 d1, cached 7, fresh 8). The hits predate the RC-1 bind guards (e201e33a9). The count is the same with
both guards off.

## What it is NOT (lever flips, each one alone; stale-hit count)

| arm | stale |
|---|---|
| base | 3 |
| both RC-1 guards off | 3 |
| `MTG_SOLVE_MEMO=0`, `MTG_DECISION_PRICED_X=0` | 3 |
| `MTG_ROLLOUT_M1_MEMO` / `GAME_LINE_CACHE` / `INWIN_CACHE` / `FS_NOWIN_CACHE` / `EXH_LEAF_MEMO` / `ENUM_MEMO` / `PAY_PROBE_MEMO` `=0` | 3 |
| `MTG_ROLLOUT_GAME_CACHE=0` | 1 (the game changes; key 983036f572f4ce2 remains) |
| `MTG_UNBUDGETED_LEAF_MEMO=0` | 6 (the game changes) |
| `--budget-ms 0` | 0 |
| **`MTG_BP_NODE_ROOTTURN=0`** | **0** |
| **`MTG_BP_NODE_HOST2=0`** | **0** |
| **`MTG_BP_NODE=0`** | **0** |

The memo verifiers report 0 mismatches (`MTG_ROLLOUT_M1_MEMO_VERIFY`, `MTG_GAME_LINE_CACHE_VERIFY`,
`MTG_INWIN_CACHE_VERIFY`). The decision-budget context is not the cause either: the bits from
`PlanCacheCtxBits()` (decision/greedy budget exhausted or overrun, meter, abandoned, nests) are identical
at store and at hit (`c2809` at the verify site, `c7a01` in the replay's walk).

## Mechanism (print-only diagnostic: branch `wip/maze-rc3-diag`, commits a0dfac048 and its successor)

Key `983036f572f4ce2` is traced with `MTG_LEAF_VERIFY_TRACE=983036f572f4ce2 MTG_ROLLOUT_M1_MEMO=0`, so
every replay re-solves. The depth-1 rollout's turn-7 main-1 solve is `SolveWithLookahead`, the "lookahead"
host, reached through `RolloutPreCombatSolve`.

* Store time (decision epoch 31) and hit time (epoch 36) build the SAME 48-candidate list and reach all
  48 candidates. Every candidate scores the same, and every candidate's apply records the same breakpoint
  deltas. The wave inputs are the same too: `bp_nobp` 4, `w0lens` 0, `canon` 0, seeds 0.
* The only difference is the `BpWaveWalker`. At store time it opens **36 slots**; at hit time it opens
  **0**. At store time the slots cover the continuation ranks of candidate 47 (Sylvan Scrying); the wave
  finds `Sylvan Scrying + Maze's End>Gond Gate`, a win on turn 8. At hit time there are no slots, so the
  walk keeps `Manabond + Burgeoning + Maze's End>Baldur's Gate`, which wins on turn 9.
* The walker's slot set depends on `BpWaveSiteMask(state)` = `BpSiteMask() & ~BpNodeWaveDropAt(state)`.
  `BpNodeWaveDropAt` asks `BpNodeHostsThisTurn(state)`, and that function reads **`g_condemn_root_turn`**,
  the turn of the OUTERMOST decision. With `MTG_BP_NODE_ROOTTURN` and `MTG_BP_NODE_HOST2` on (both
  default), it says "hosts" when `state.TurnSeq()` is the root turn or root+1. So the same rollout state
  rolls out differently depending on which real turn is being decided. That turn is not in
  `BuildSimKey`, so the leaf table, and likewise the game-persistent rollout cache (`rgc`), can serve a
  value computed under a different root turn.

## Why it is a search hole, not just a cache-key gap

The stand-down exists because, where the breakpoint node hosts, the node searches those continuations
itself, so wave ranks would only duplicate them. The node is hosted only in the FSLine hosts:
`FSLineWin` (the `node_host_here` gate near the pre-combat host) and the `FSLineTail` m2 host. **The
lookahead host (`SolveWithLookahead`'s candidate loop) never hosts it.** Its own walker is built with
`host_pends=false`, and the code comments say "this loop never passes the node a capture". Its wave-0 mask
(`BpSiteMask`, the `out &= ~BpNodeWaveDropAt(state)` line) and its deferred-wave mask (`BpWaveSiteMask`)
still drop the node's sites on root and root+1 turns. So in this host, on those turns, a site-3 / site-10
continuation has no wave-0 variant, no deferred wave slot and no node. It gets whatever the base canon
hands it, at any budget. That is the exact defect `BpNodeHostsThisTurn` was written to close (its comment:
"the stand-down is ALWAYS gated on the node really hosting", USER hard rule,
`docs/design/no-greedy-in-search-window.md`). That function checks the TURN, not whether THIS host
hosts.

In the repro the cached (stale) answer, 8, is the more complete search. The fresh answer, 9, is the
holed one.

## Proposed fix (not implemented; needs the USER)

Gate the drop on the HOST, not only on the turn. Pass the masks a "this host hosts the node" bit. Only
`FSLineWin` / `FSLineTail`, where `node_host_here` can be true, should drop node sites. The lookahead
host and every other non-hosting caller keep the full `BpSiteMask`. Effects:

1. It closes the hole. Rollout and lookahead solves on root and root+1 turns regain the node-site ranks.
2. It makes the lookahead host's answer independent of `g_condemn_root_turn`, which removes this
   SAME-STATE DRIFT from the leaf table and from the `rgc` game cache (whose header already notes the
   same-state non-identity across decisions).
3. It moves budgeted play: more wave work in rollouts on those turns. So it is a play change, measured by
   a held-out A/B on the decks that move. Every slower game must recover at `--depth 8 --budget-ms 0`.

The alternative of folding the hosting bit into `BuildSimKey` would only make the cache honest about the
hole. It would leave the hole in place, so it is not recommended.

## Open checks before a fix

* Confirm the root turns directly: the store decision should have root turn <= 5 (t7 not hosted) and
  the hit decision root turn 6 (t7 = root+1 hosted). This is inferred from the three lever flips; the
  diagnostic does not yet print `g_condemn_root_turn`.
* Find every caller of `BpSiteMask` / `BpWaveSiteMask` and classify it as hosting or non-hosting. Nested
  discovery inside FSLine also reads `BpWaveSiteMask`.
