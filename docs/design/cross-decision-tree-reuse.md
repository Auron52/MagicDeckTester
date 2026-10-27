# Reusing the searched tree across a game's decisions (DEFERRED, 2026-10-27)

**Status: deferred by the USER until the LandfallElementals / NivMizzetCombo shape work is done.** Nothing is built.
The first step is a measurement, not the cache.

## The idea (USER, 2026-10-27)

*"Sometimes I wonder if we should be caching some part of the searched tree for subsequent turns. So, for example,
if we searched 3 full turns deep and didn't find a win next turn we could start with 2 turns already fully
explored (naturally keeping only the part that is relevant given the decisions we made in the previous turn)."*

## Why it can be EXACT here, not approximate

* Goldfish: between two decisions nothing changes except our own committed play and the draw, and the search
  already played those draws (its keys fold the full ordered library). The next decision's root and its subtree
  are states the previous decision already searched.
* Precedent: `MTG_LEAF_CACHE` (a game-persistent ROLLOUT-leaf cache, `src/ai/TranspositionTable.h`) was verified
  on every hit -- 0 stale out of 283,014 (`MTG_LEAF_VERIFY`). It did not pay only because recurring rollout
  leaves are mostly intra-decision (~0.14pp added hit rate); the interior was never tried.
* The interior memo `FSLineCache` is already keyed by (state, remaining depth); it is discarded at the end of
  every FullSearchLine.

## What it would buy -- the exact overlap

The previous decision (turn t, depth d) searched every child at turn t+1 with d-1 turns remaining. The new
decision needs depth d from t+1. So, by key:

1. **The warm-up passes are free.** The old entries ARE the new decision's pass d-1 (LandfallElementals b20:
   warm-ups are 12-14% of units).
2. **Move ordering.** Last decision's best line orders the new passes (`MTG_ID_ORDER_HINT`: 0.928x units on
   Landfall when carried pass to pass).
3. **Proofs carry upward.** A verified in-horizon win at d-1 is the answer at any greater depth; a no-win proof
   bounds the deeper query (`FSLineEntry::nowin_bound` already qualifies a no-win by its cutoff).
4. (The case where the previous decision found a verified WIN is already handled: the committed line is replayed,
   not re-searched.)

**The larger prize is "2 turns already explored" in the STRUCTURAL sense.** In the new depth-d pass every node at
turns t+1..t+d-1 already exists: its plan enumeration and plan applications (the `fs_pre` / `fs_bp_wave` units,
61% + 19% of a LandfallElementals staged decision) are identical; only the VALUES change, because the leaves moved
one turn deeper. Caching EXPANSIONS (child states per node) rather than values leaves only the new deepest ply as
fresh work. That is the "leaf-independent interior child-state cache" `escalation-interior-reuse.md` lists as
un-built. At a ~6x growth ratio per ply the re-derived interior is roughly 15-20% of a pass, so with the warm-ups
the plausible saving is 20-30% of a decision.

## Costs and rules that apply

* Memory: child-state caches hold GameState copies; batch workers run 32-wide under `MTG_RSS_CAP_GB=20` on the
  small box. Needs a per-game cap and an eviction rule that cannot change answers (a miss recomputes).
* Determinism: per GAME only (cleared with the other per-game caches in `TurnSolver::ClearPerGameCaches`), never
  per worker -- see `batch-run-to-run-nondeterminism.md`.
* Truncated work: only entries from COMPLETED (non-truncated) subtrees may be reused as answers; the existing
  truncation watermark (`g_fs_trunc_events`) is the test.
* Budget: a hit frees budget and the start gate spends it on depth. That is a play change, adopted by a held-out
  A/B on the decks that move (NO UNBUDGETED-ONLY CHANGES; COLLAPSE WASTED SEARCH -- judge on work removed).

## First step (do this before building anything)

A read-only instrument: a game-persistent set of the FSLineWin node keys (state hash + remaining depth) each
decision computed, and per decision count (a) nodes whose key the previous decision of the same game already
computed at the same remaining depth, (b) nodes whose STATE it computed at any depth (the expansion-cache
overlap), and (c) the units charged under each. One pooled batch on LandfallElementals and NivMizzetCombo at play
settings. If (b) is near the arithmetic above, build the expansion cache; if only (a), the cheaper value cache.
