# Turntimber Symbiosis: two copies multiply, and most of the product is waste

**Status: DEFERRED / not implemented.** Raised by the USER 2026-09-16: *"They shouldn't multiply
together. That should definitely be deduplicated."* Recorded here rather than acted on because a
StompySurprise keep-table generation was mid-flight and an engine change would have invalidated the
night's comparability (CLAUDE.md: freeze the binary while measuring).

## What the engine does today

`CollectActions` (`src/ai/TurnSolver.cpp` ~11672) expands Turntimber Symbiosis into one Action per
distinct creature name among the CURRENT top `look_top_put_creature_count` (7) cards, plus an explicit
`TURNTIMBER_NONE` decline. Each becomes a separate plan variant, deliberately: *"every legal put is a
distinct plan the search scores"* — that part is a core invariant and is correct for the FIRST copy.

The in-tree branch stats make the cost concrete:

> Turntimber [is] this deck's #1 driver (sum_odo 6.2M/8.2M at d3/d5, avg 117-133x, max 6144x, and two
> castable copies multiply TOGETHER). Under a saturated budget that width is paid in search QUALITY,
> not wall-clock.

## Why the multiplication is mostly not a real decision

Two independent defects, and the second is the serious one.

### 1. Permutation redundancy — deduped in one path, not the other

`plan_signature` sorts the cast list (`std::sort(s.begin(), s.end())`) before hashing, and each put
rides its cast entry as a `#T<name>` term. So in **EnumeratePlans**, `(copy1→X, copy2→Y)` and
`(copy1→Y, copy2→X)` produce the same sorted multiset and collapse. Good.

But **`TurnSolver::Solve` has its own odometer and no signature dedup** — and it is d0 *and every
rollout leaf*, i.e. the hot path. There the ordered cross-product survives in full, so identical
multisets are scored repeatedly.

### 2. The later copy's named variants are enumerated against a STALE library

`PerformLookTopPutCreature` (`src/core/SpellEffects.h` ~6938) resolves with
`ap.library.DrawN(look, looked)` — the looked-at cards LEAVE the top (the chosen one to the
battlefield, the rest to the bottom). So once the first Symbiosis resolves, the second sees an
entirely different seven cards.

Yet the second copy's candidate names were collected from the **pre-resolution** top 7. At resolution
the named pick is simply not there, and the code falls back:

```cpp
if (pick < 0)   // empty choice OR a named pick the post-shuffle top no longer holds
{
    int best_mv = -1;
    ... // highest-MV creature among the ACTUAL looked cards
}
```

**So nearly every named variant of the second copy resolves to the same board.** The search enumerates
and scores ~k distinct plans that are one outcome wearing k hats. With k ≈ 78 that is ~78x pure waste
multiplying the legitimate ~117x width — consistent with the measured 6144x worst case.

It is also, strictly, clairvoyance the plan has no right to: at plan-construction time the contents of
the second look are unknowable, so there is no honest named choice to make.

## Proposed fix (cheapest first)

1. **Emit only ONE variant for the second and later Turntimber in a plan** — the empty target, which
   already routes to the highest-MV fallback at resolution. The first copy keeps its full fan. This
   is semantically *more* correct (it stops pretending to know the second look) and removes the
   multiplication outright. Gate it on `look_top_put_creature_count > 0` so no other deck moves.
2. **Give `TurnSolver::Solve` the permutation collapse** — sort-and-dedup identical cast multisets,
   or at minimum skip odometer positions whose sorted multiset was already scored.
3. `MTG_TT_PUT_WIDTH` (already implemented, default 0 = uncapped) caps the fan by `EvalCard`. It was
   swept W∈{2,3,4} on 2026-08-21 and NOT adopted — W=3/4 outcome-identical, W=2 won exactly one train
   game and zero held-out. It is a blunt instrument for this: it narrows the FIRST copy's legitimate
   choice too, rather than removing the second copy's fake one.

## Why this matters beyond tidiness

The 2026-08-21 conclusion was that the odometer is "architecturally ugly but the deterministic budget
converts it into (measured-negligible) search-quality dilution". That measurement's held-out samples
were 200 games x2 and 100 games — blind to effects of ~0.005t. It explicitly flagged the condition for
re-opening: *"If a future budget CUT (b5/b10) or a Turntimber-heavier list revisits this, re-measure
W=2 first."*

A Symbiosis 3-vs-4 comparison is exactly that revisit, and on 2026-09-16 it measured **+0.0048 (1v1) /
+0.0060 (2HG) against the 4-copy arm** at t = −1.40/−1.59 — the right size and sign for dilution, and
the same magnitude as the single train game W=2 won (−0.005/−0.010). The user's objection is that this
cannot be a card-quality result, because Turntimber's back face is a land that enters untapped for 3
life and life is nearly free against a passive opponent, so Symbiosis DOMINATES a Forest here.

If the dilution is real, every Turntimber-count comparison this engine produces is biased against the
higher count, and the bias is an artifact of plan enumeration rather than a fact about the card.

## Test that settles it

Run the same paired head-to-head twice at high N (200,000 games/arm, se ≈ 0.001 — ~30x the resolution
of the 2026-08-21 sweep): once as shipped, once with the multiplication removed (fix 1, or
`MTG_TT_PUT_WIDTH=2` as a proxy) applied to BOTH arms. If the 4-copy list gains under the fix, the
Forest's win was search dilution.
