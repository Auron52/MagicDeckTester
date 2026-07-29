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
