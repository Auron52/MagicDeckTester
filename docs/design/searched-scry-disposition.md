# Scry / surveil disposition should be a SEARCH BRANCH (not a policy hook)

2026-07-29. Direction from the user, correcting an assumption in
`treasure-hunt-open-findings.md`:

> "scry should not be a no-branch case. It should be handled the same as any other branch, the only
> difference being that we will usually implement a heuristic."

That is the general rule for this engine: a heuristic is the **default/prune** for a branch, not a
substitute for having one. The scry keep/bottom call was treated as a pure policy hook, which is why
two separate Treasure Hunt investigations dead-ended on "no fixed rule can express this".

## Current state

`DecisionProvider::ScryKeepOnTop` picks ONE disposition and the search never alternatives over it.
The standing `MTG_UNPRUNED` comment in `DecisionProviders.cpp` records this honestly as a gap, not a
design:

> Pure DECISION/POLICY hooks that pick ONE option the search never alternatives over (cast-ORDER,
> vial-charge, **scry-keep**, discard-order, combat) are NOT yet opened here: making the search branch
> on them needs new enumeration ... not just a wider gate.

## Most of the machinery already exists

The look-at-top path was already refactored into a decision object, and it is the right shape:

| piece | where | what it does |
|-------|-------|--------------|
| `TopDisposition` | `SpellEffects.h` | which looked-at indices go on top, in what order; rest away |
| `ApplyTopDisposition` | `SpellEffects.h` | the SINGLE placement routine (bottom / graveyard / shuffle) |
| `HeuristicTopDisposition` | `SpellEffects.h` | reproduces today's provider choice byte-identically |
| `EnumerateTopDispositions` | `SpellEffects.h` | **every legal option**, already written |

`EnumerateTopDispositions` is currently consumed only by `main.cpp` for the claude-play human menu.
Wiring it into the autonomous search is the work; enumerating the options is not.

**The branch factor is tiny.** For Scry/Surveil it is every ordered subset kept on top, and every
modelled card looks at 1-3 cards. Treasure Hunt's only two sources (Temple of Epiphany `etb_scry 1`,
Thundering Falls `etb_surveil 1`) look at exactly ONE card, so the branch is binary: keep or away.
This is nothing like the fetch-target blowup that needed a search cap.

## What is actually missing

The disposition resolves INLINE inside the effect, mid-turn, during a land's ETB. Branching over it
needs a **re-solve breakpoint** at that point -- the same mechanism the draw/staging/cascade
breakpoints already use (see the `CastOrderRank` note on "cast sets with NO re-solve breakpoint",
and `resolve_draw_breakpoint`). In Treasure Hunt every scry is a land ETB, so the breakpoint lands
immediately after a land drop, before the turn's casts are re-solved.

The adoption shape should mirror `TutorCandidates` / `FetchCandidates`: the provider returns the
heuristic's single pick under normal search (byte-identical, zero cost) and the full legal set when
the branch is opened. That keeps every existing baseline intact until the branch is deliberately
switched on and measured.

## What this resolves for free

Three Treasure Hunt decisions are already parked as "leave it to the search", and all three are
board-state dependent in a way a fixed rule provably cannot express:

1. **The first Land's Edge on top** -- the shipped rule deliberately declines to handle it (whether to
   scry it away before a Treasure Hunt depends on land count and spare copies).
2. **A depletion land on top** -- worth its tapped turn only when that turn was going to be spent
   anyway. Measured as an unconditional rule: +0.0200, 14 games slower, 0 faster
   (`treasure-hunt-open-findings.md` section 3b). The T1-only narrowing is just the crudest proxy for
   the real condition.
3. **Colour-vs-tempo trades** -- the residual slower games in section 4 are exactly this.

Same argument as `searched-cleanup-discard.md`: the three static scopes there bracketed the whole
design space and none reached the right answer, because the correct choice depended on board state.

## Order of work

This is a prerequisite for the two open Treasure Hunt items, so it should come BEFORE any further
scry-heuristic tuning -- that avenue is measured out (section 3b: the last four corrections moved 3
games in 9,000). Cleanup discard (`MTG_SEARCHED_DISCARD`) needs the same breakpoint treatment and has
a reproduced win waiting on it, so the two should be scoped together.

---

## 2026-07-31 — the Ponder branch already exists, and it is expensive

`cast_reorder` (Ponder) is the one look-at-top decision that **already has a search branch**:
`TurnSolver.cpp` emits a keep variant and a shuffle variant per castable Ponder, carried on
`Action::ponder_keep` and honoured by both the rollout and the executor. It ships **off**
(`MTG_PONDER_SEARCH` / `MTG_UNPRUNED`) because it was measured as the #1 branching source —
`MTG_BRANCH_STATS` put it at ~47% of all enumeration.

A/B with the branch on (hinata is the only suite deck with Ponder):

| case | heuristic | branch on | delta |
|---|---|---|---|
| hinata smoke d3 s1001 | 5.9000 | 6.1333 | +0.2333 |
| hinata smoke d5 s1001 | 6.0267 | 6.2800 | +0.2533 |
| hinata regression d3 s2002 | 5.9000 | 6.1050 | +0.2050 |
| hinata regression d3 s3003 | 5.8500 | 6.0000 | +0.1500 |
| hinata regression d5 s2002 | 5.8400 | 6.1100 | +0.2700 |
| hinata regression d5 s3003 | 5.8000 | 5.9900 | +0.1900 |

Two separate effects are folded into those numbers, and they need separating before this is read as
"searching the disposition is bad":

1. **d0 is not a search at all.** `hinata_smoke_d0` moved +0.0300 and `hinata_regression_d0_s2002`
   +0.0420 — but at depth 0 there is no rollout to score the two variants, so the branch degenerates
   into a *different fixed rule*: both variants carry the same plan `value`, the sort is stable, and
   the keep variant is pushed first, so d0 simply always keeps. That is "always keep" vs "heuristic
   at resolution", which says nothing about searching.
2. **At searched depths it is the familiar dilution.** Doubling the plan count on ~47% of
   enumerations at a fixed per-decision budget starves the search — the same shape as the cantrip
   breakpoint class in `cantrip-first-collapse.md`.

Whether the branch is *correct but unaffordable* (like the cantrip class) or actually *wrong* is a
budget-sweep question, not an opinion: at unlimited budget a searched decision must be at least as
good as the heuristic it replaces, unless the win is outside the searched window.

### It was a TIE-BREAK defect, not dilution and not a search result

The budget sweep (hinata d3, 40 games, seed 1001) answers the question, and the answer is neither
of the two expected ones:

| budget | heuristic | branch on (keep variant first) |
|---|---|---|
| 10 ms | 6.2750 | 6.6500 |
| 80 ms | 6.2750 | 6.6000 |
| 640 ms | 6.2750 | 6.6000 |
| 2560 ms | 6.2750 | 6.6000 |

640 ms and 2560 ms return **identical digests**, so the search has converged — this is not budget
starvation. A searched decision that is still 0.325 worse than the heuristic it replaced with a
converged search is a defect, and the play logs name it: over 48 Hinata Ponder resolutions the
heuristic shuffled **13** times and the searched branch shuffled **0**.

The search accepts a candidate only on a STRICT improvement (`win_turn < best`, then
`value > best.value`), so **whichever variant is enumerated first owns every tie** — and this
decision ties constantly. Keeping three dead cards on top costs draws on turns 4-6; from a depth-3
horizon those turns are invisible, so both variants look win-equal and the tie-break silently
becomes the decision. The old enumeration pushed the pinned *keep* variant first, so "searching" the
disposition actually meant "always keep".

Note what this is NOT: it is not a horizon problem to be fixed with depth, and not a budget problem.
The heuristic's rule ("shuffle iff nothing in the top N is wanted") encodes knowledge from beyond the
horizon, which is exactly what a heuristic is for. The bug is that the branch threw that knowledge
away on ties instead of falling back to it.

**Fix: enumerate the heuristic FIRST** (`ponder_keep = -1`, decided at resolution), then the two
pinned alternatives. The search can then only override the heuristic on a difference it can actually
see. Re-measured:

| budget | heuristic | branch on, heuristic-first |
|---|---|---|
| 10 ms | 6.2750 | 6.2750 |
| 80 ms | 6.2750 | 6.2750 |
| 640 ms | 6.2750 | 6.2750 (digest identical to the heuristic arm) |
| 2560 ms | 6.2750 | 6.2750 (identical) |

The branch is now **free** rather than harmful. It does not yet win anything on this sample — the
search finds no strictly-better disposition — so what it buys is the *ability* to find one, at the
cost of the extra variants.

### The general rule this establishes

Every plan-level sub-decision must enumerate **the heuristic's answer first**, because the search's
strict-improvement rule makes enumeration order the tie-break, and ties are the common case for any
decision whose consequences fall outside the horizon. Audited the others: `fetch_target` emits
`cands[0]` (the heuristic's top pick) first, `land_face` emits `front` first, `bp_choice` leaves the
greedy base plan ahead of its variants, and `scry_choice` puts `TopDispositionCandidates[0]` (the
heuristic) first. Ponder was the only one inverted.
