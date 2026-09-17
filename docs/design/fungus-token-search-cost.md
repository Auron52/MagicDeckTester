# Fungus: pathological per-game search cost on token boards

**Status:** diagnosed, not fixed. Deferred here per the CLAUDE.md rule that deferred work lives in
`docs/design/`, not private agent memory.

**Found:** 2026-09-17, during the Fungus deck's Stage 5c2 `leaf_tiebreak_check` run. Surfaced by
the batch runner's own `SLOW-GAME` reporting, which is exactly the signal CLAUDE.md says to watch.

**Correctness is NOT affected.** Every Fungus gate is green: coverage clean, `nonconv` 0,
`fd-diverge` 0, claude-play sweep 16/16 with all flags resolved, smoke 80/0, regression 108/0,
`verify_deck.py` GATE PASS. This is purely a cost problem.

---

## The measurement

From one pooled 24,000-game run (12 seed blocks × 1000 games × 2 arms, `[batch] heartbeat` at
24/24 throughout, so this is not a scheduling artefact):

| metric | value |
|---|---|
| games over 30 s | **526** of 24,000 (~2.2%) |
| median slow game | 53.8 s |
| p90 | 195.8 s |
| **worst single game** | **1,446 s (24 min)** |
| total wall time inside slow games | **14.6 h** |

Slow games concentrate at **win turns 6-7** (444 of 526) and hit the `base` and `leaf` arms about
equally — so it is the deck's board, not the tie-break lever under test. The same game indices go
slow in both arms, which is itself confirmation that it is board-driven rather than lever-driven.

## Where the cost is

Counter-profiling build (`cmake -DMTG_PROFILE=ON -DCMAKE_BUILD_TYPE=Release`), same deck, one slow
game vs one fast game:

| | fast game (seed 9101 gi0, wins T5) | slow game (seed 1600607 gi607, wins T6) | ratio |
|---|---|---|---|
| search nodes | 12,467 | 968,430 | **78×** |
| wall | 74 ms | 333,909 ms | **4,483×** |
| **ms per node** | 0.006 | 0.345 | **58×** |
| GameState deep copies | 6,722 | 616,923 | 92× |
| decisions | 5 | 154 | 31× |
| TT hit rate | 9.5% | 7.5% | — |
| enum-memo hit rate | 0% (304 misses) | 6% (21,825 misses) | — |

**Both factors explode, and the per-node one is the surprising half.** A 78× node count on a longer
game is unremarkable; a **58× cost per node** is not. That is the signature of per-operation
board-size scaling — `GameState` deep copies and battlefield walks — rather than a plan explosion.

**It is NOT a plan explosion.** `MTG_BRANCH_STATS` on the slow game: 28,159 enumerations,
442,676 plans, **avg 15.7 plans per enumeration, max 207**. That is a small, healthy plan space.
`MTG_ENUM_HIWATER_KB=2048` printed nothing, which by that diagnostic's own contract means the cost
is *accumulation across the recursion*, not one giant enumeration.

**It is NOT memory.** Peak RSS on the slow game was 128 MB.

**It is NOT the real board.** At d0 the same game's battlefield tops out at **15 permanents**. The
big boards exist only inside *rollouts*, where the search explores Doubling Season / Mycoloth /
spore token-engine lines that real play never reaches. (The repo's standing lesson applies:
*rollout, not play, is the denominator*.)

This is the same class as the recorded clue-fusion incident — *"token floods, not units, ate the
wall"*, 67-69 permanent rollout boards, 807 s → 49 s — and the work-unit counters under-count it
there too.

## What was tried and REJECTED

**`MTG_BP_SEARCH=0` (disable mid-turn breakpoint re-solving).** On the single worst game it looked
excellent: **334 s → 178 s, a 1.88× speedup with the win turn unchanged** (6.0 either way).

**It does not generalise.** Over 100 games at play settings (d5, b200, seed 8801):

| arm | avg turns | wall |
|---|---|---|
| shipped (breakpoints on) | **5.6000** | 127.19 s |
| `MTG_BP_SEARCH=0` | 5.6100 | 125.70 s |

1.2% faster and a slightly *worse* average. **Rejected.** Recorded because the n=1 result was
genuinely persuasive and someone will re-derive it otherwise — this is the
"one run is not evidence" lesson in its natural habitat.

Note also that disabling breakpoints did **not** reduce the decision count (still 154), so the 31×
decision inflation on slow games has a different cause and is still unexplained. That is the
loose thread most worth pulling next.

## The direction that remains

The repo already has a USER-blessed doctrine for exactly this shape: **fuse fungible tokens for the
search** (`clue-fusion-search-shortcut`). Represent identical vanilla 1/1 Saproling tokens as one
`Permanent` carrying a stack count for search/rollout purposes, keeping the viewer per-token. That
attacks the 58× per-node term directly, which is the half a wider plan-prune cannot touch.

Two guardrails from the same doctrine:
* **Do NOT reach for a token cap.** That is a Tier 1-3 clause silently dropped, which the
  analyze-deck skill forbids outright.
* **Do NOT narrow with a generic limiter.** Per the core invariant, any narrowing belongs in a
  deck/archetype provider and must be measured against the unpruned arm.

Before building it, get the missing measurement: **what is the actual rollout board size
distribution on a slow game?** The real board is 15; the rollout board is unknown and is the number
that decides whether fusion is worth it. There is no existing instrument for it —
`MTG_ENUM_HIWATER_KB` reports `bf=` only when a *plan set* crosses a size threshold, which never
fires here.

## Practical consequence today

Fungus **cannot go into the regression suite at current cost** — `verify_deck.py` correctly warns
that nothing tracks the deck's play digest because it is not a suite case, but a 24-minute game
would blow the smoke (<15 min) and regression (<45 min) budgets on its own.

A value-leaf generation on this deck should also be costed against these numbers before being
started: the skill quotes tens of hours for a normal deck, and ~2% of this deck's games are
minutes-to-tens-of-minutes each.
