# Dragonstorm cast-ordering search (targeted, Hook 28)

2026-07-21. The fix for the fea3a2c cast-order tradeoff (see `fea3a2c-regression-slowdowns.md`) and
the general realization that a **fixed** CastOrderRank leaves a lot on the table for a combo deck.

## The problem fea3a2c left

`fea3a2c` gave Dragonstorm a fixed cast order (mana rituals @15, Irencrag @18, before the payoff @20)
to stop the canonical order casting the payoff first and self-stranding the rituals. It is net strongly
positive, but a fixed order is a blunt instrument: it reorders ~12 combo turns into *worse* lines than
the pre-fea3a2c plan-order found (e.g. `gi22`: old Irencrag→Dragonstorm win T4, fea3a2c durdles to T6),
and — measured below — it leaves broad value on the table (hundreds of games go off a turn later than
they could). Root-caused as a real eval regression, **not** budget (persists to 200× budget), not a
front-loaded rock (Ruby Medallion is a rank-20 cost reducer, not a rock), not enumeration (old and new
enumerate identical menus).

## The right lever: search the cast order (but cheaply)

The general knob already existed — `MTG_SEARCH_ORDER` (cast-ordering search C): expand each action set
into the distinct orderings of its hand casts, dedup by end-of-phase state, score each, commit the best
via `Plan::searched_order` (the executor replays that exact vector order → lockstep). Enabling it for
Dragonstorm was a big win, but the brute-force k!-permutation is wasteful **and** its k!≤120 cap SKIPS
the biggest go-off hands (k≥6) — exactly where the value is.

The user specified the actual decision structure, so we enumerate only the principled orderings:

- **Mana rituals** (Rite of Flame, Pyretic/Desperate Ritual, Seething Song) → **cheapest-first**; they
  self-fund the chain, so their relative order is never searched.
- **Irencrag Feat** ("cast only one more spell this turn") → fixed: immediately **before the finisher**.
- **Finisher** (Dragonstorm / Apex of Power / a closing Dragon) → **last**.
- **Ruby Medallion** → the one genuinely **searched** position: **as early as it can be paid** (earlier
  discounts more red rituals). Tried at every slot, earliest-first, so the rollout keeps the earliest
  that still goes off; the *subset* enumerator separately offers no-Medallion lines, so "drop it only if
  necessary" is handled by plan selection, not ordering.
- **Multiple Desperate Ritual vs Seething Song** → two variants: **splice-after** (preferred — splice the
  Desperates once Seething's mana is up) and **interleaved-by-cost** (fallback — cast them individually);
  which is payable depends on the mana, so the search decides.

This is `DragonstormCastOrderings()` in `TurnSolver.cpp` — O(k) principled candidates (2 splice variants ×
Medallion insertion points, + the identity/canonical order as a floor so the search can never do worse
than the fixed line). It replaces the k!-permutation only for Dragonstorm (Hook 28
`WantsCastOrderingSearch`); every other deck stays byte-identical, and the global `MTG_SEARCH_ORDER`
A/B knob still drives the full-permutation search on non-Dragonstorm decks.

## Measured (regression, 2 seeds; and smoke seed 1001)

| case | default (fixed order) | full-perm search | **targeted** |
|---|---|---|---|
| regr d3 s2002 | 5.560 | 4.947 | **4.937** |
| regr d3 s3003 | 5.600 | 4.980 | **4.893** |
| regr d5 s2002 | 5.356 | 4.820 | **4.768** |
| regr d5 s3003 | 5.396 | 4.868 | **4.824** |
| smoke d3 | 5.620 | 5.040 | **5.033** |
| smoke d5 | 5.667 | 5.027 | **5.000** |
| searched slower / faster (regr, vs stale GT) | 30 / 631 | 20 / 845 | **17 / 851** |

The targeted generator **matches and slightly beats** the full-permutation search (it covers the k≥6
hands the cap skips) at a fraction of the cost (smoke makespan 119s vs full-perm 152s vs ~105s baseline).
`d0` is byte-identical (ordering search is a search-only expansion). Every non-Dragonstorm deck is
byte-identical (provider-scoped gate).

## Not fixed by ordering (separate follow-up)

`gi22`-class durdles (no Ruby Medallion in the line) are **not** an ordering problem — the old line won
by casting a *smaller* subset (Irencrag→Dragonstorm, no Rite) that the fixed order's search misprices;
the ordering search leaves them unchanged. They fall to the subset/eval side (the value-leaf, a separate
deferred item). The remaining ~17 regression slowdowns (vs the stale pre-fea3a2c GT) are these plus
budget churn; net is overwhelmingly positive (851 faster).
