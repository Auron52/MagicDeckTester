# Failed-cast atomic rollback (ADOPTED)

**Status:** **ADOPTED** — fix committed `432520e`, smoke GT rebaselined `d014c28`
(regression/overnight GT rebaselined in follow-ups). User asked for it (2026-07-19):
"it should always go back to before the cast when you do the wrong order." An earlier
attempt was deferred because it churned GT; a focused per-game audit (below) resolved
the concern — the churn is correct behaviour and a net metric improvement.

## The leak (was confirmed by code reading)

`TurnSolver::TapForCostDirectOnce` and its mirror `AIEngine::TapForCostOnce` pay a cost
greedily (tapping sources, decrementing storage/depletion counters, taking pain-land life
loss / Grove opponent-lifegain). On **total failure** (greedy + backtracker + filter-retry
all fail) they restored `bf_greedy_fail` / `life_greedy_fail` — the **greedy's partial-tap
end-state** — NOT the pre-payment snapshot. So a failed cast leaked tapped lands, spent
storage/depletion counters, and pain-land life. The old comment ("match prior behaviour")
shows it was a deliberate byte-identity choice when the backtracker was added — i.e. the
leak was long-standing.

Callers rely on a failed payment being side-effect-free: the cycling / sacrifice-draw loops
(`TurnSolver.cpp` ~4281/4419, `AIEngine.cpp` ~2469/2513) manually undo **only their own**
`{T}` tap (`state.battlefield[idx].tapped = false`) before `continue`/`break`, so any other
tap the failed greedy made leaked for the rest of the turn. Feasibility probes and
ill-ordered plans (Dragonstorm: a spell listed before its funding source — see
`dragonstorm-plan-execution-fidelity-bug.md`) hit it too.

## The fix (committed `432520e`)

Restore the FULL pre-payment snapshot on total failure, in BOTH mirror sites (lockstep):
capture `opp_pre = players[1-active].life` and `oll_pre = opponent_lost_life_this_turn`
alongside the existing `bf_pre` / `life_pre`; delete the `bf_greedy_fail` / `life_greedy_fail`
captures; on total failure restore `battlefield = bf_pre; players[active].life = life_pre;
players[1-active].life = opp_pre; opponent_lost_life_this_turn = oll_pre;
floating_mana = reserve_pre`. The success paths (greedy / backtracker / filter-retry) all
return early and are **byte-identical**, so only previously-leaky failed casts change.

## Why the GT churn is correct (the resolved concern)

The earlier attempt was deferred because smoke "regressed" 14/18 with hinata a turn later in
2 games. The per-game audit (old binary built from HEAD in a worktree, diffed via
`test/explain_game.py --old-bin`) showed:

- Only decks that hit a failed payment **internally** churn: hinata (search + exhaustive-keep
  mulligan rollouts both call `TapForCost`) d0/d3/d5, and slivers d0. All other decks are
  byte-identical.
- Every changed case is a **net metric improvement** (avg turn-to-win, lower better):
  slivers_d0 4.6510→4.6500, hinata_d0 7.4900→7.4760, hinata_d3 6.0267→6.0133,
  hinata_d5 6.0800→6.0667. The churn is different-but-correct lines / mulligan decisions,
  **all at the same win turn in isolation**.
- The 2 flagged "turn-later" games (d3 gi16, d5 gi19) are **T6 in isolation** — they sit at
  the search's budget margin and only tip to T7 in the oversubscribed batch; they are more
  than offset by other games winning faster / loss→win. The batch is deterministic
  (identical fingerprints on re-run), so the rebaselined GT is stable.

Conclusion: **not** a second bug and **not** the leak being accidentally beneficial — the
old faster lines were relying on illegally-leaked resources, and removing them is both
correct and (net) faster. Accepted + rebaselined rather than gated.

## Reproduction (Dragonstorm ill-ordered cast)

After the fix, an ill-ordered plan whose cast cannot pay at its position aborts leaving the
board exactly as before the cast (no stranded tapped lands, no depletion tick). See
`dragonstorm-plan-execution-fidelity-bug.md` for the storage-under-burst sibling fix.
