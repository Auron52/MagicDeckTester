# What actually drives Hinata's branching: the unfiltered tutor

2026-07-31. Measured with `MTG_BRANCH_STATS=1 MTG_ROLLOUT_STATS=1`, Hinata, seed 4004, 20 games,
d3, budget 10 (the suite's overnight d3 budget), `--threads 1`.

Motivation: the plain-cantrip breakpoint class (`MTG_BP_SITES` bit 3) is measured **correct but
unaffordable** — a genuine −0.0500 at unlimited budget, +0.0090 at the suite budget, with the
degradation being pure dilution (`cantrip-first-collapse.md`). "Unaffordable" is only actionable if
we know what the budget is being spent ON. So: what is it being spent on?

## Answer: Gamble. 75% of the enumeration odometer from 3.7% of the calls.

Class OFF (today's default, `MTG_BP_SITES=0x17`):

| driver card | calls | sum_odo | share of odo | avg_odo | max_odo |
|---|---|---|---|---|---|
| **Gamble** | **2632** | **2 845 016** | **75.0%** | 1080.9 | **155 648** |
| Ponder | 15832 | 554 520 | 14.6% | 35.0 | 2304 |
| Hinata, Dawn-Crowned | 21316 | 138 896 | 3.7% | 6.5 | 64 |
| Soulfire Eruption | 12262 | 130 465 | 3.4% | 10.6 | 512 |
| Preordain | 2607 | 27 768 | 0.7% | 10.7 | 128 |
| *(total)* | 71764 | 3 792 728 | | | |

Gamble is 3.7% of `EnumeratePlans` calls and three quarters of the work. The in-code note at
`TurnSolver.cpp` (the Ponder branch) saying Ponder was "the #1 branching source (~47%)" is **stale** —
it was true before the Ponder keep/shuffle branch was cut to a heuristic-first 3-way; Gamble has
been the driver since.

The `by situation` table agrees and localises it: every expensive bucket is `hinata=1`
(`groups=9-12 board=11-15 hinata=1` → avg_odo 9465, max 155 648), which is exactly the condition
`HinataProvider::TutorCandidates` uses to stop narrowing.

## Why: an unfiltered tutor is a multiplicative factor, not an additive one

`Gamble` is `tutor_to_hand` with **no `tutor_types` filter** — "search your library for a card". So
`GenericProvider::TutorCandidates` returns *every distinct library card name*, and
`HinataProvider::TutorCandidates` deliberately does not narrow that set once Hinata is online:

> "Hinata is online: return the full legal set (search-primary -- still branches over everything),
> but ORDER it by situational need (SituationalCardRank)."

Hinata2 has **34 distinct card names in 83 cards**, so mid-game the group is ~25–30 options. Those
variants all share `hand_index`, so the plan enumerator treats them as **one option group** — and
the odometer is the *product* of group sizes. A 30-option group therefore multiplies the entire rest
of the turn's enumeration by 30. A single call peaked at an odometer of 155 648.

This is not a bug and not a bad heuristic: searching the tutor target is exactly right, and the
provider ordering (`SituationalCardRank`) is a proper heuristic-as-tie-break. The problem is purely
structural — the target choice is priced as a *factor* when every other searched sub-decision in
this engine (`fetch_target`, `land_face`, `scry_choice`, `bp_choice`) is priced as a second *axis*.

## What the cantrip class costs on top of that

Same probe, `MTG_BP_SITES=31`:

| | class OFF | class ON | change |
|---|---|---|---|
| EnumeratePlans calls | 71 764 | 92 914 | +29% |
| sum odometer | 3 792 728 | 4 739 489 | +25% |
| rollout calls | 48 668 | 49 651 | +2% |
| rollout turn_steps | 103 911 | 96 987 | −7% |
| **interior nodes** | **35 131** | **74 668** | **+113%** |
| interior_frac | 0.253 | 0.435 | |

The class barely changes the number of leaf rollouts; it **more than doubles interior expansion**.
At a 9000-node budget (`budget-ms 10` × `NODES_PER_VIRTUAL_MS=900`) that is the dilution mechanism
in one number: 43% of the budget goes to expanding the tree instead of evaluating it, so every
candidate is judged by a shallower, noisier leaf. It matches the earlier finding that width is
irrelevant at unlimited budget and monotonically harmful at a fixed one.

## The lever this points at

`MTG_TUTOR_WIDTH=<n>` (added 2026-07-31, default 0 = uncapped = byte-identical) caps the tutor group
at the provider's n most-preferred targets. It is a pure **cost** prune of the same family as
`MTG_BP_W0_SITES`: the provider already orders candidates best-first, so `n=1` is exactly the
heuristic pick and larger n restores the search's freedom in preference order. It exists to answer
one question — *is the cantrip class budget-limited?* — by freeing budget somewhere else and
re-measuring the class delta.

If the class delta improves toward its −0.0500 unlimited-budget value as the tutor group shrinks,
budget competition is confirmed and the real fix is structural rather than another prune:

**Restructure the tutor target as a second AXIS, not a factor.** Emit the plan set once with the
provider's top target, then fan out variants over the remaining targets — exactly what the land-ETB
scry disposition does ("Running AFTER AppendBreakpointVariants and skipping the breakpoint variants
keeps this a second AXIS rather than a cross product: cost is L+S, not L*S"). The one case that
genuinely needs the target inside the group is a target that gets **cast in the same turn**, because
only then does the fetch interact with the rest of the subset; a fetched card that cannot be cast
this turn just sits in hand and cannot change the turn's other choices. That split is a dominance
argument, not a guess, so it keeps every target reachable while removing the ~30× factor.
