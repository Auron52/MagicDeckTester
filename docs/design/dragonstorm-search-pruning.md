# Dragonstorm search pruning — over-splice skip (byte-identical) + acceleration-ordering heuristic

## Problem

On a **non-lethal, bloated go-off hand** (Apex/dig fill the hand with rituals + a payoff),
`TurnSolver::Solve` / `EnumeratePlans` enumerate an odometer of
`∏_groups (1 + |options|) × 2^(independent)` subsets **per decision node**, and this runs at
**every node of a full-depth rollout** inside the mulligan bottoming lookahead. The lethal go-off
line is already short-circuited fast (`Solve` returns on the first winning combo); the explosion is
on the *non-lethal* setup turns where the search grinds every ritual/splice permutation to pick the
best non-winning play. A single such game becomes a combinatorial straggler that hangs a whole
analyzer phase (observed: 2h+ on one game, `SubsetHasIllegalSplice` hot in the backtrace). The
per-decision node budget does NOT bound this — it caps *between* nodes, not the within-node subset
enumeration.

Root drivers of the product on Dragonstorm:
- **Splice variants:** N copies of Desperate Ritual → N groups, each with `k = 0..N-1` splice-count
  options → most `k`-assignments violate the triangular legality bound and are generated-then-rejected.
- **Permutation redundancy:** the ritual copies are identical, so permuting *which* copy takes which
  `k` yields equivalent outcomes (same mana, same storm count) but distinct odometer positions.
- **{X} payoff variants** (Crackle with Power) + colour-float variants multiply further.

## Fix — staged, least-risk-first

### Step 1 — byte-identical over-splice odometer skip  (IMPLEMENTED)

`GroupChoiceOverSplices(state, cands, groups, choice)` (TurnSolver.cpp) is a generate-time analogue of
`SubsetHasIllegalSplice` applied to the odometer's **group selection**. Splice bases are `CastFromHand`
actions (`hand_index ≥ 0`), so they always live in a group — never in the independent `2^num_ind`
set — which means the group choice ALONE fixes splice legality (an `imask` extension only adds
non-splice-base actions, so it can never make an over-splice legal). Rejecting an over-splicing group
choice before its inner `imask` loop therefore skips exactly the subsets `consider()` /
`eval_and_push()` already discard via `SubsetHasIllegalSplice` (confirmed present in BOTH: Solve
consider + EnumeratePlans eval_and_push) → **identical `best` / plan list**. Gated on a one-time
`any_splice` flag so every non-splice deck runs zero extra work and stays byte-identical. Applied to
BOTH odometers (Solve + EnumeratePlans) to keep them in lockstep. **No `MTG_UNPRUNED` gate needed** —
it changes no results, same category as `ManaPruneBound`.

Validation: full smoke must be 18/0/0 with exact digests (GT decks carry no splice card, so they are
byte-identical by construction; the flag proves it).

### Step 2 — acceleration-ordering heuristic  (TO IMPLEMENT, gated behind MTG_UNPRUNED)

**User's model of the deck:** "On the turn you go off you just use a big burst of mana and win." The
order of pure accelerants does NOT matter for the outcome — only the legal splice multiset and the
total mana/storm count — so enumerating every permutation is wasted work. The heuristic: **order all
acceleration spells cheapest-first and cast them in that order** (one canonical chain instead of the
full permutation set), then the payoff.

**Canonical go-off cast order (user-specified):**
1. **Ruby Medallion** (cost reducer). USUALLY cast on an EARLIER turn (setup); on the go-off turn only
   if there is enough mana to START (it costs {2}, produces none). Cast first when present (reduces the
   red cost of everything after).
2. **Rite of Flame** ({R}; adds {R}{R} + {R} per Rite of Flame in graveyards — escalates).
3. **Pyretic Ritual** ({1}{R}, net +1).
4. **Desperate Ritual — no splice** ({1}{R}, net +1).
5. **Seething Song** ({2}{R}, net +2).
6. **Desperate Ritual — with splice** (a spliced base costs 4 for k=1; more mana per cast).
7. **Irencrag Feat** ({1}{R}{R}{R}, adds 7 {R}, `max_casts_after:1`) — must be **second-to-last**, and
   ONLY when the last spell is **Dragonstorm**. **Must NOT be cast before Apex of Power** (Apex casts
   multiple spells from exile; Irencrag's "only one more spell" clause would cap it). So Irencrag is a
   Dragonstorm-payoff-only accelerant.
8. **Payoff: Dragonstorm or Apex of Power** (last).

**Constraints the heuristic must respect:**
- Splice legality: the triangular `N-1-j` bound (last base splices nothing) — already modelled by
  `SubsetHasIllegalSplice` and step 1.
- Storm count matters for the Dragonstorm payoff (spells cast this turn → dragons), so the *number* of
  accelerants cast is still a search choice even though their order is fixed.
- Total mana matters for the Crackle/big-X payoff.
- `max_casts_after:1` (Irencrag) → Irencrag second-to-last, Dragonstorm-only, never before Apex.

**Gating:** this is a HEURISTIC (it collapses which plans are enumerated → different action masks →
NOT byte-identical), so it lives in the archetype provider (DragonstormProvider) and is opened by
`MTG_UNPRUNED` (the standing pruned-vs-unpruned A/B), exactly like the existing `GroupCap` /
`SituationalCardRank` machinery. Measure its win-turn cost vs unpruned on Dragonstorm and disclose.

## Graceful-degradation scope principle (analyze-light)

The initial "analyze light" stage must **degrade gracefully and never hang**, because we do not know
whether the engine can be optimized enough to run the full exhaustive mulligan profile — so the light
analyze's signals may be all we ever have for this deck. Therefore:
- Every light-analyze output is **best-effort/optional**: `card_scores`, `required_pieces`,
  `min_color_sources` are all "nice to have," and the play path already tolerates their absence (empty
  `card_scores` = scoring disabled; no `required_pieces` = default keep).
- The pruning fix (step 1 + step 2) is ALSO the robustness guardrail: it is what stops a single game
  from exploding and stalling a phase.
- Decide the light-analyze SCOPE (full vs card_scores-only vs degraded) from MEASURED cost AFTER the
  pruning fix — do not pre-emptively cut. If pruning makes it affordable, keep the full light analyze
  (robust fallback); if still too slow, drop the expensive signals and let the play path ignore them.
