# Feed-aware tap choice — deferred work item (2026-09-01)

**Status:** DEFERRED. Identified during the strict-filter-feed adoption's final adjudication
(`filter-feed-strict.md` §4c); affects 3 of 1,016 adjudicated games (~0.3% of slower games,
~0.02% of all games in the two filter decks' suite footprint). Not a correctness bug — a
search-quality gap the strict model exposed.

## The gap

When a turn's payments are applied cast-by-cast (and especially across a mid-turn breakpoint
re-plan), the tap-choice machinery picks WHICH source pays a pip without knowing that a filter
(Cascade Bluffs) later in the line will need a feed in the filter's own colours. Under the
lenient feed model this could never matter — any floating unit fed the filter. Under the
adopted strict model, spending the last U/R-capable source on an ordinary pip strands the
filter for the rest of the turn.

Traced concretely on hinata gi68 (seed 3071, gi 68, d3 b10):
the T3 kill (Hinata → Ponder → **breakpoint: Ponder draws Irencrag** → Spasm → Irencrag →
Crackle) is strictly legal ONLY if the Hinata/Ponder payments preserve Mountain's {R} for the
Bluffs feed. The tap chooser spends it on Hinata's {R} pip (Ornithopter could have paid it),
the feed is stranded at the breakpoint, the continuation reads unpayable, and the ROLLOUT
scores the branch a turn worse than it really is. Because the mis-valuation is inside the
rollout, no search budget or depth recovers it — d7 at b10000 still plays the other line.

The same mechanism explains th gi448/gi249 (Saprazzan Skerry's {U}{U} burst must feed the
Bluffs; the T1 land-drop branch that enables it is mis-scored) and the lone hinata d0
residual (gi1516).

## What is already proven (so the fix starts from evidence)

- Payment capability is NOT the problem: unit tests "strict filter feed" (Island feed),
  "gi68 T2 shape" (dork feeds filter inside a joint bill) and "gi448 T2 shape" (depletion
  burst feeds filter) all pass — the backtracker finds these solutions when asked directly.
- The executed-record matcher in `logs/mana_robust/harden/final_adjudicate.py` re-derives
  per-turn strict-legality from a game log; useful as the acceptance oracle for any fix.

## Candidate directions (measure via the heuristic-optimization loop, never adopt on intuition)

1. **Feed-reservation in tap choice**: when an untapped/unfed filter exists and the plan (or
   the hand) holds later casts, prefer paying coloured pips from sources whose colours the
   filter cannot use, tie-broken by the existing ManaSourceRank. This is the tap-order
   analogue of the scarce-colour hold.
2. **Backtracker-first for filter boards**: on boards with an unfed filter, route the whole
   turn's payment through the joint solve (which already sequences feeds correctly) instead
   of per-cast greedy taps — measure the cost; the joint solve is pricier.
3. **Breakpoint continuation probe**: at a bp re-plan, if the continuation is unpayable,
   retry the PRE-bp payments with the backtracker constrained to leave the continuation
   payable (a one-shot repair, bounded).

Ship any of these behind a heurarm lever, measure on the hinata + th suite cells (the §4c
residual games are the direct probes: gi68/gi448/gi249/gi1516 should flip), and remember the
mana-cache rule: a lever that changes backtracker/tap-choice answers must enter the memo keys
it flows through (`batch-pool-contamination.md`).
