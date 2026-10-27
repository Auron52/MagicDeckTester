# Reusing the searched tree across a game's decisions (2026-10-27)

**Status (2026-10-27): the VALUE cache is BUILT, verified sound, DEFAULT OFF** (`MTG_GAME_LINE_CACHE`, results in
"The game line cache" at the end). It is answer-identical at unlimited budget and removes 0.3-9.9% of units there. At
a budget it changes play (freed units buy depth), so adoption needs a held-out A/B and GT acceptance. Built after the
LandfallElementals / NivMizzetCombo shape work, as the USER asked.

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

## Measured overlap (2026-10-27)

**Instrument:** `MTG_TREE_REUSE_PROBE=1` (TurnSolver.cpp, `namespace treereuse`). It is read-only: probe on vs off
gives byte-identical digests and units on all four arms below. It prints one `[tree-reuse] job=<job> turn=<t>`
line per batch job and decision turn. Each FSLineWin node reached inside a decision (the outermost
FullSearchLineHybrid) is looked up in per-game sets of the earlier decisions' keys:

* **exact** -- the same (state, remaining depth) key. Its units are the inclusive per-game-meter delta under the
  TOPMOST exact hit. This is what a game-persistent VALUE cache could answer.
* **re-expanded** -- an interior node whose state an earlier decision already EXPANDED (searched at depth >= 1, at
  any depth). Its units are the node's OWN units (its delta minus its children's): the plan enumeration and
  applies an EXPANSION cache could skip.
* **leaves** -- a leaf node's own units (the rollout or leaf evaluation).

The shares are an UPPER bound. Keys are recorded at node entry, so truncated subtrees are included; a real
cache could reuse only completed ones.

Setup: one pooled batch, seed 600000, 500 games per arm, d5. Shares are of all units in the arm's decisions.

| arm | units | exact (value cache) | re-expanded own (expansion cache) | leaves' own |
|---|---|---|---|---|
| LandfallElementals live b20 (rollout ladder) | 8.72M | 12.1% | 1.2% | 90% |
| NivMizzetCombo live b20 (rollout ladder) | 11.95M | 7.8% | 0.6% | 90% |
| NivMizzetCombo emulnl b20 | 13.07M | 15.4% | 1.0% | 89% |
| LandfallElementals staged b16 (model leaf) | 11.82M | 25.6% | 18.7% | 17% |

By decision turn, staged LandfallElementals (turn 1 has no earlier decision, by construction):

| turn | decisions | units | exact | re-expanded own |
|---|---|---|---|---|
| 1 | 500 | 7.24M | 0 | 0 |
| 2 | 235 | 2.85M | 61% | 40% |
| 3 | 91 | 1.29M | 76% | 63% |
| 4 | 39 | 0.43M | 69% | 60% |

**Reading.**

* On the rollout shapes the work is the leaf rollouts (~90%). Re-expanding the interior is ~1%, so an expansion
  cache buys nothing there. The exact overlap (8-15%) is the warm-up arithmetic predicted above: the previous
  decision's pass k searched this decision's root at depth k-1.
* On the model-leaf shape the interior is the work (83%), and most of it in a turn-2+ decision was already
  computed. The exact overlap is 66% of turn-2+ units, 26% overall. That is the doc's 20-30% estimate, and it
  is concentrated where staged pays its premium. At b16 here staged spends 11.82M vs live b20's 8.72M; a 26%
  cut would bring it to ~8.75M.
* The two kinds overlap (an exact hit's subtree contains re-expanded nodes), so their shares do not add.

**Restricted to what a cache could actually reuse** (probe as committed now). A key is recorded only when its
subtree completed (no `g_fs_trunc_events` inside it), and the exact key also folds the leaf kind (forced
heuristic / value / constant leaf). Same batch, still byte-identical to probe-off:

| arm | exact: upper bound -> reusable | re-expanded own: upper bound -> reusable |
|---|---|---|
| LandfallElementals live b20 | 12.1% -> **11.4%** | 1.2% -> **1.0%** |
| NivMizzetCombo live b20 | 7.8% -> **7.5%** | 0.6% -> **0.6%** |
| NivMizzetCombo emulnl b20 | 15.4% -> **10.5%** | 1.0% -> **0.8%** |
| LandfallElementals staged b16 | 25.6% -> **10.6%** | 18.7% -> **10.8%** |

Most of staged's upper bound was truncated subtrees: 30% of its units run past the budget, so its passes are
often cut. Part of it was passes on a different leaf. On turn-2+ decisions the reusable exact share is
24-33% on LandfallElementals (both shapes) and 11-25% on NivMizzetCombo.

So the realistic prize is **~7.5-11.5% of all units for the value cache, on every shape**, about half the
20-30% estimated above. The expansion cache pays only on the model leaf (~11%), and much of that overlaps the
value cache.

The key is exact across decisions. `BuildSimKeyBase` folds an ordered digest of the whole live library, so a
real-play shuffle between decisions changes the key: hits are lost, nothing is reused wrongly. The "per-call"
comment on `line_cache` in `FullSearchLine` predates that digest.

**Decision by the rule above: build the exact-key VALUE cache first.** It is the cheaper cache and the larger
share on every shape. The expansion cache only adds what the value cache misses on the leaf shape (subtrees
whose depth changed).

Constraints it must keep (from the costs section):

* Reuse only entries from completed subtrees, under the same leaf kind and the same FSLineCache key semantics.
  That means wins on the order-exact key, no-wins on the canonical key with `nowin_bound`, and the same
  rule as the intra-decision `FSLineCache` that a warm-up pass's entry never answers the committing pass on a
  different leaf.
* Per-game lifetime.
* (Done: the probe's completed-subtree, same-leaf numbers above are the expected ceiling.)

## The game line cache (built 2026-10-27, `MTG_GAME_LINE_CACHE`, default off)

`src/ai/TurnSolver.cpp`, `namespace gamelc` (block comment above it). A per-thread map, cleared per game, holding
the answer of every non-root `FSLineWin` node whose search completed (no truncation, no budget bind, decision
ceiling not hit, line not truncated). Keyed by the node's `FSLineCache` key folded with the effective leaf, the
escalation beam, the coarse second-main mode and the labeller mode. Read with the per-decision cache's rules: a
win answers any query, a no-win only a query no wider than its bound. `MTG_GAME_LINE_CACHE_VERIFY=1` recomputes
every hit, plays the fresh answer and counts disagreements; `MTG_GAME_LINE_CACHE_CAP` (default 200000 per game).

**The bug verify found, and the rule it added.** A node searched under cutoff `c` proves only its best win at or
before `c`. A win it returns past `c` (a horizon rollout that ran on) is a line it met, not its best. The first
build stored such wins; NivMizzetCombo seed 600016 (live, unlimited d3, turn 3, depth 1) stored a turn-7 win
found under cutoff 4, and a later node with no incumbent (cutoff 9) reused it and missed a turn-5 win. The cache
now stores a win only when `win_turn <= cutoff`, and otherwise an empty no-win under bound `cutoff`.

**Open question for the per-decision memo.** `FSLineStoreWin` (the intra-decision `FSLineCache`) has the same
shape: it stores any win whatever its cutoff, and a win hit ignores the querying cutoff. Whether a looser-cutoff
query ever reaches such an entry within ONE decision is not measured. A read-only counter (store cutoff beside
each past-cutoff win; count later win hits with a wider cutoff) would answer it. If it fires, it is a
completeness defect at large budget, independent of this cache.

**Verification** (500 games/arm at b20 d5 seed 600000; 200 games/arm unlimited d3; arms LFE live, LFE staged b16,
Niv live, Niv emulnl):

* Cache off and verify mode: digests and units byte-identical to the pre-cache binary.
* Verify: 31,781 hits checked, **0 material disagreements** (an answer within the querying node's cutoff that
  differs). The remaining ~1,200 differ only past the cutoff, where the caller reads both as no-win.

**Work removed at unlimited budget** (play identical, every digest equal to cache off):

| arm | units off | units on | change | core-ms |
|---|---|---|---|---|
| Niv live | 146.35M | 131.85M | -9.9% | -7% |
| LFE live | 19.24M | 18.27M | -5.0% | -4% |
| LFE staged | 21.68M | 20.70M | -4.5% | -4% |
| Niv emulnl | 109.76M | 109.45M | -0.3% | ~0 |

**At b20 it changes play**, as a sound cut under a budget does: units fall only 0.2% on the rollout shapes and
5.4% on LFE staged, because freed units buy deeper passes. One seed, 500 games: LFE staged 4.370 -> 4.362, LFE
live 4.416 -> 4.418, both Niv arms unchanged.

**Next:** the per-decision memo counter above, then a held-out A/B on the decks that move at play settings (every
deck searches through this path, so that is the suite), then GT acceptance if adopted. Memory: entries are full
lines; the run above never filled the cap (`full_refusals=0`), but RSS is unmeasured at suite scale.
