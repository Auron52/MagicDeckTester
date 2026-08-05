# The bound-qualified no-win memo (`MTG_FS_NOWIN_CACHE`)

**Status: ADOPTED, default ON (2026-08-05). `MTG_FS_NOWIN_CACHE=0` restores the win-only memo.**
Ground truth rebaselined across all three modes. Offline, the labeller forces it on for itself
regardless (`MTG_LABEL_NOWIN_CACHE`, default ON) — see `label-horizon-ladder.md`, where it is what
makes the horizon ladder affordable at all.

## What was thrown away

`FSLineWin` answers one question: *from this state, at this depth, is there a win at turn ≤ cutoff?*
It has an interior-node memo, `FSLineCache`, keyed on `BuildSimKey(state, depth, max_turns,
second_main)`, because the same position is reached by many different move orders. (This is separate
from the leaf transposition table `tt` that `SimulateToEnd` writes.)

That memo stored **only wins**. A no-win was discarded, on the stated grounds that it "may be a
branch-and-bound abort" — true, but it throws away the commonest result in the search. Every dead-end
subtree was re-explored from scratch at every transposition.

## Why a no-win cannot simply be stored

"No win" is not a property of the position. It is a property of the position *and the cutoff it was
asked about*. A node refuted under cutoff `turn+2` has said nothing about whether a win exists at
`turn+4`; reusing that entry for a wider query reports a loss that was never proved.

## The fix

`FSLineEntry` carries `nowin_bound`, meaning *"no win at turn ≤ nowin_bound exists from here"*. A win
carries `INT_MAX` and is reusable by every query — byte-identical to the old behaviour. A no-win is
reused only when `cutoff <= nowin_bound`; a wider question re-searches.

**The bound is exactly the node's own cutoff, and that is not an approximation.** Children are
searched at `std::min(cutoff, best.win_turn)` — ordinary branch-and-bound, tightening as the node
finds better lines. `best.win_turn` is initialised to `max_turns + 1`. If the node's *result* is a
no-win then `best` never improved, so `min(cutoff, best.win_turn)` was the full `cutoff` for every
child. No incumbent tightening ever happened, so the refutation genuinely covers the whole cutoff.

Store helpers keep the entry monotone: a win supersedes a bounded no-win, a wider no-win supersedes a
narrower one, a win is never downgraded. With the memo off no no-win entry can exist, so only the
emplace branch is reachable — exactly the old `lc->emplace(key, line)`.

## The truncation guard

A no-win produced by a search that **ran out of budget** is not a proof; it is "I did not find one
before I stopped". Storing it would poison the memo with fabricated losses — the same failure class
as the labeller emitting `max_turns+1` for a truncated position (see `value-leaf-regeneration-queue.md`).

`g_fs_trunc_events` is a thread-local counter bumped at every truncation point (budget exhaustion,
beam cuts). A node snapshots it on entry and compares after its own loop; if it moved, nothing is
stored. The property that matters is that it **propagates upward**: one truncation deep in the tree
falls inside the watermark span of every ancestor, so none of them store either. A single truncated
leaf cannot launder itself into a proof anywhere up the chain.

## Why it is not a byte-identical speedup

A cache hit consumes **no budget** (budget units are rollout steps). So under a bounded search the
memo does not make a decision finish sooner — it makes the same budget go *further*. Per decision:

* the search **converges** under budget → the memo saves real work, answer identical;
* the budget **binds** → the freed units buy depth, and the line can move.

Both happen. The first dominates heavily — Hinata spends 58.3% fewer search nodes at d5/20ms, which
is only possible if most decisions were converging rather than pinned at the cap.

This is the whole reason the change needed an A/B rather than an identity check: it is a **budget
reallocation** that looks like a speedup.

## Measurement (2026-08-05)

**Soundness.** Unbounded d3, 200 games × 9 decks: byte-identical digests on every deck. With no
budget to reallocate, a sound memo *must* return the same line — this is the discriminator between
"sound memo + budget reallocation" and "unsound memo", and it is the test to re-run if the memo is
ever suspected. Corroborated by the `stale` counter (a query rejected for too narrow a bound) being
**0 on every deck in play** — the bound qualification never costs anything there.

**Play.** smoke 25/27, regression 38/45. Every changed case is same-avg/different-digest except
`hinata_regression_d3_s2002` (5.7150 → 5.7100 — exactly one game won a turn earlier). Of the 15 games
that played differently across both suites, **1 better, 0 worse**. Reference gate: 0 play-drift, 0
enum-gap over 142 references.

The honest reading is *no systematic harm plus a large speedup*, *not* a quality win: 15 changed games
is far too few to claim the engine plays better.

**Cost — the benefit is essentially all Hinata.** d5/20ms, 200 games, timed alone with the arms
ALTERNATED across three repetitions (single reps on this hot path are not trustworthy — see the trap
below):

| deck | off (3 reps) | on (3 reps) | |
|---|---|---|---|
| Hinata2 | 85.2 / 84.8 / 57.5 s | 19.6 / 18.0 / 16.7 s | **~3.5×** (−56.7% nodes at d5) |
| treasure_hunt | 16.1 / 16.1 / 16.0 s | 16.3 / 16.2 / 16.0 s | **1.00×** (−0.6% nodes) |

An earlier draft of this file claimed treasure_hunt at **1.32×**, from a single rep per arm
(22.3 s → 16.9 s). It does not reproduce: the 22.3 s off-arm run was an outlier, and the node counter
had said −0.6% all along. Every other deck is likewise neutral in both instruments. **Do not reduce
the table to one multiplier**, and do not accept a single-rep wall-clock reading on these decks at all.

### Does the benefit scale with depth?

Deterministic node deltas (OFF → ON), 100 games, 20 ms:

| deck | d1 | d2 | d3 | d4 | d5 |
|---|---|---|---|---|---|
| Hinata2 | 0.0% | −21.2% | −18.1% | −30.4% | **−56.7%** |
| treasure_hunt | 0.0% | −0.1% | −0.4% | −0.7% | −0.6% |
| Auras | 0.0% | 0.0% | −2.7% | −2.5% | 0.0% |
| Dragonstorm | 0.0% | −2.3% | −3.6% | −0.5% | −0.2% |
| Goblins | 0.0% | −3.6% | −8.1% | −4.4% | **+2.6%** |

**d1 is exactly 0.0% on every deck** — the memo is inert there, as it must be: depth 0 is the leaf, so
a d1 search has no interior nodes to memoize. Beyond that, only Hinata shows a real curve.

**The confound to respect:** once play diverges, a node count is no longer a controlled comparison —
the two arms are searching different positions in different games. That is what Goblins d5's *positive*
+2.6% is; it is not the memo doing more work, it is two different game sequences. Treat small
percentages on any deck whose digest moved as noise around a divergence, not as a measured cost.

### Which decks benefit, and why

The same predictor as the horizon ladder: **move-ordering quality**. Decks that win fast through
obvious lines (Goblins, burn, slivers) rarely *prove* a no-win, so there is nothing to memoize —
Goblins produced 2 no-win results in 20 games. Decks whose wins do not look lethal to the ordering
(treasure_hunt, Auras, Hinata) generate thousands. Measured hit rates per 1000 memo probes at each
deck's real play policy: Auras 53.9, treasure_hunt 33.7, Hinata 15.4, Dragonstorm 6.4, burn 0.4,
and 0.0 for Goblins / slivers / Knights / Anti-Lifegain. That ordering predicts the node-saving
ordering almost exactly, and burn's 2 hits show up as exactly 28 nodes saved out of 73,419 — two
independent instruments agreeing.

## A measurement trap this hit

The first cost reading came from **pooled-batch makespans** and said the memo was *slower* at d5/20ms
(92 s → 135 s) while the deterministic counters said Hinata did 58% less work. Both cannot be right.

The makespan was wrong: a pooled batch's wall time is set by whichever jobs land in the load-imbalance
tail, not by the deck that changed. Timed **alone**, with arms alternated across three repetitions,
Hinata is 85.2 / 84.8 / 57.5 s off versus 19.6 / 18.0 / 16.7 s on. Note the 32% spread across
identical off-arm reps — that variance is what made the pooled reading misleading.

Pooling is right for *throughput* (see CLAUDE.md) and wrong for *attributing cost to one deck*. When
a deterministic counter and a wall clock disagree, the counter is not the suspect.

## Harnesses

| script | what it answers |
|---|---|
| `test/nowin_play_stats.sh` | store-vs-suppress and hit rates at each deck's real play policy (needs the `MTG_PROFILE` build) |
| `test/nowin_play_ab.sh` | work (counters) and play (batch digest) side by side, OFF vs ON |
| `test/nowin_metric_ab.sh` | the metric A/B on held-out seeds; pass `budget_ms=0` for the unbounded soundness gate |

The counters (`FSLine memo probes / win hits / nowin hits / nowin stale / nowin results / stored`)
live in `src/ai/Profiler.h` and are compiled out entirely in a normal build.

## Related

- `label-horizon-ladder.md` — the offline caller, where this memo is load-bearing rather than optional.
- `value-leaf-regeneration-queue.md` — the fabricated-loss failure class the truncation guard avoids.
- `search-perf-investigation.md` — earlier work on the same hot path.
