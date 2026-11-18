# P5 results (Maze's End provider prunes) -- 2026-11-18

(Filled in by P5-2; sections below.)

## Handed to P5b (engine folds; out of P5's scope)

### Item 6: the Exploration back-to-back land-order fold (NOT BUILT; design copied from logs/maze/P5-handoff.md)
The fold point is the site-10 breakpoint CONTINUATION enumeration. That enumeration has no record of the previous
section, i.e. "the last section was land-only, land X". It would need either a state field folded into the
continuation's cache key or a thread-local set at the continuation (TurnSolver ~78282 `LandDropNewlyOpen` arming /
the deferred re-solve). Rule to build:
- when the previous section played only land X, drop land Y from the continuation's land menu if Y < X (canonical
  order) and the two commute;
- "commute" = neither land has an entry pin choice (colour/Plaza), a fastland condition (Thran Portal), Gond's
  gates_enter_untapped, The Black Gate's pay-life entry, or a Karoo bounce.
USER (VERBATIM): "7. True, the order doesn't matter much here unless the second land is played after other things or
we are paying for a cost like Gateway Plaza." Check at d8 b0.

### The Archdruid's Charm tutor-axis duplicate (possible identity fold)
P1d item 8's "x2" is probably the tutor axis's c==0 duplicate: the base plan (unpinned) plus the pinned (index 0,
pins.front()) variant, TurnSolver ~93525. Check that the unpinned default == pins.front() for colours
(LockEtbChosenColor asks EtbChosenColor first); if so the pinned c==0 variant is an identity fold.
