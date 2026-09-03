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

### Step 2 — acceleration-ordering heuristic  (IMPLEMENTED, commit `4e68c8c`, gated behind MTG_UNPRUNED)

**Implemented** as `GroupChoiceNonPrefixAccel(...)` in TurnSolver.cpp — a generate-time odometer skip
mirroring `GroupChoiceOverSplices`, applied in BOTH `Solve` and `EnumeratePlans`. Provider-owned via
`DragonstormProvider::UseAccelPrefixCollapse()` (Hook 27, base returns false → byte-identical everywhere
else) and opened by `UnprunedGate::AccelPrefix` (`MTG_UNPRUNE=accelprefix` / `MTG_UNPRUNED=1`). Accelerant
= `ritual_floating_mana > 0 && kind==CastFromHand`; **verified** this set is exactly the 5 rituals
(Rite of Flame MV1, Pyretic/Desperate MV2, Seething Song MV3, Irencrag MV4 — so Irencrag naturally sorts
last among accelerants) and that Apex/Dragonstorm/Utvara/Ruby Medallion/Lotus Bloom are `rfm=0` and thus
**excluded** (payoff + cost-reducer selection stays full-search). Ordering key is choice-independent
(min effective `cost.ManaValue()` over the group's ritual-cast options, `group_hand_index` tiebreak) →
stable across every odometer position. Splice-k within a cast Desperate Ritual stays free (step 1 handles
over-splice legality); Irencrag's `max_casts_after:1` legality is enforced by the existing consider/eval
loops (no special-casing needed — it just ranks last).

**Gate results:** smoke **18/0/0** exact digests (independently re-run — GT decks carry no ritual
accelerant so byte-identical by construction); bounded d5 Dragonstorm measurement **completes** (EXIT 0)
with the collapse on and reproduces the pre-change stall (**EXIT 124**) when reopened via
`MTG_UNPRUNE=accelprefix`; d0 matched A/B (deepest depth where the unpruned side terminates) pruned vs
unpruned **identical avg + identical unwon set** → drops zero winning lines where apples-to-apples is
possible. The d5 A/B is unmeasurable because the unpruned d5 side is non-terminating *by construction* —
which is exactly the bug fixed. Theoretical caveat (accepted, `MTG_UNPRUNED`-openable): cheapest-first
maximizes storm-count/affordability per prefix, but a dearer ritual can net marginally more mana, so a
pure big-X (Crackle) payoff could in principle reach slightly more mana via a non-prefix subset — not
observed.

### Step 3 — Desperate Ritual splice-count collapse  (IMPLEMENTED, gated behind MTG_UNPRUNED)

**The residual atom after Steps 1–2.** With the over-splice skip (Step 1) and the accel-prefix collapse
(Step 2) in, the R=40 mulligan gen still stalled on `Desperate Ritual x4` hands. A `perf` capture of the
worst rollout put ~35% of self-time in `GroupChoiceOverSplices` + ~8% in its `card_name` `memcmp` on top
of the `Solve` recursion. Root cause: Step 2 deliberately leaves the **splice-k WITHIN a cast Desperate
Ritual free** ("storm-vs-mana is still a search choice"), so each of the N=4 copies emits k=0..N-1 cast
variants → the odometer powersets ~N^N splice-count assignments per node and rejects the illegal/dominated
majority. That per-node blowup is the atom Steps 1–2 collapse *down to* but cannot remove.

**Mechanism — cap the splice at k=1; a bounded (m, s) search.** Splicing Desperate Ritual onto an Arcane
spell REVEALS the other copies (they stay in hand) and adds **no storm**, so the splice count is purely a
*mana* lever with no storm cost — splice-then-cast is net-positive mana at the same eventual storm. The
first design offered per storm level m two families, bare `{0,…,0}` and the full max chain `{N-1,…,N-m}`.
The user (who owns this deck) rejected the max-chain end: a `k=3` splice needs **8 mana up front** — the
line is unaffordable for most ramp turns, and betting on it overshoots the deck's typical mana. The *ideal*
is to derive the splice count from the mana actually on the table (a `k=1` splice needs 4 mana → makes 6,
`k=2` needs 6 → makes 9, each self-funding the next), but a deterministic mana-realizing fill would need the
search projection and the executor replay kept in lockstep across color pips and a self-funding chain (the
[[dragonstorm-d0-mana-realization-strand]] cost) for marginal gain, since the search already discards
unaffordable splices in rollout. So we take the user's endorsed practical form — **cap the splice at k=1**
("one splice per," which handles the majority) plus **bare** ("fewer than one splice"): `CollectActions`
emits only `k∈{0,1}` per copy, and because same-named copies are identical a selection is fully described
by **(m = bases cast, s = how many splice one)**. `SpliceCollapseViolated` keeps only the canonical
representative of each (m, s) — cast bases occupy positions `{0..m-1}` and the k values are non-increasing
in position order (the s single-splices are first) — so e.g. `{0,1,1,0}` and `{1,1,0,0}` collapse to one
line. The SEARCH still picks the affordable (m, s) by **rolling each out** (a splice the mana can't front
just fails in the rollout), i.e. mana-derived *within the k≤1 bound* with no fill machinery. Provider-owned
via `DragonstormProvider::UseSpliceCollapse()` (Hook 30, base false → byte-identical everywhere else) and
opened by `UnprunedGate::SpliceCollapse` (`MTG_UNPRUNE=splicecollapse` / `MTG_UNPRUNED=1`). Applied in BOTH
`Solve` and `EnumeratePlans`; the triangular legality (s ≤ N−1: the last cast copy has no other to splice)
is still enforced by Step 1's `GroupChoiceOverSplices`, which runs alongside. The go-off short-circuit is
unaffected: `BuildStormGoffLine` still finds each copy's bare (`k=0`) representative. (The fully
mana-derived unbounded fill — k up to 2, spliced exactly to the running mana — remains the theoretical
ideal if its lockstep cost is ever worth paying; k≤1 is the measured-good practical form.)

**Measured (captured slow rollouts, d3/b10 via `MTG_KEEP_REPLAY`, reusing the fada47c bucketing cache):**
| hand (all `Desperate Ritual x4`) | collapse OFF | collapse ON (k≤1) | win_turn |
| Lathliss + Karrthus x2 (durdle)  | 48.7 s | **7.5 s** (6.5×) | 9 = 9 |
| Scourge + Lotus + Karrthus (**win**) | 58.0 s | **2.2 s** (26×) | **7 = 7** |
The winning go-off is preserved at the same turn, and the durdle hands stop stalling the pool.

**Gate results.** `MTG_UNPRUNE=splicecollapse` reproduces the full smoke GT (**21/0/0** byte-identical) →
the collapse-off path is a proven no-op (the emission refactor changed nothing when off). Every
non-Dragonstorm deck is byte-identical (splice cards live only in Dragonstorm). Dragonstorm is
**score-identical at searched depth on all three seed sets** — avg win turn unchanged at d3/d5 for smoke
(1001, also digest-identical) and regression (2002, 3003). Because k≤1 drops the k≥2 lines the full search
sometimes picked, **4 searched games on the held-out seeds play a different splice mix at the SAME win turn**
(audit `[searched] play-changed=4`, e.g. `gi191` d3 s2002 "score T5 unchanged, play differs") — a
score-neutral line swap, the expected consequence of the cap and the only thing that changed. d0 (greedy,
no lookahead) is net-neutral: smoke 6.2500 → 6.2480 (−0.002; 2 faster / 1 slower / 2 play-changed, **no
game to unwon**), regression d0 score-identical. Adopting therefore rebaselines the Dragonstorm d0 + d3/d5
GT digests (scores unchanged); non-Dragonstorm GT is untouched.

### Step 2 (original spec) — acceleration-ordering heuristic

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
8. **Payoff (last): Dragonstorm or Apex of Power** — the two primary win payoffs. **Fallback:**
   hard-cast a Dragon directly (e.g. **Utvara Hellkite**) — strictly a bad-hand fallback when the
   storm/Apex combo can't be assembled, so the deck can still pull out a win. Rank the primary payoffs
   above the direct-dragon line; only reach the dragon when neither combo payoff is castable.

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

### Mechanism decision (do NOT use EnumGroupCap for the accelerants)

`EnumGroupCap` (base default **12**, `DecisionProvider.h:292`) is the WRONG lever here: it DROPS the
lowest-`SituationalCardRank` groups beyond the cap, i.e. it would drop ritual/accelerant spells the
combo wants to cast. A combo deck wants to cast ALL its acceleration — it just should not enumerate
every permutation/subset of it.

**Right mechanism — acceleration-prefix collapse:** identify the accelerant actions (ritual_floating_mana
> 0, plus the Medallion cost-reducer as a pre-accelerant) via a DragonstormProvider predicate; instead
of letting the odometer powerset them (`2^K`), enumerate only the **K+1 cheapest-first prefixes** ("cast
the j cheapest accelerants," j=0..K) crossed with the payoff choice. Cheapest-first is optimal for a
ritual chain (each cheap ritual funds the next), so the reachable mana/storm outcomes are preserved
while the `2^K` explosion becomes linear. Layer the constraints on top: splice-vs-separate stays a kept
option (splice lowers spell count → matters for storm), Irencrag Feat is placed second-to-last and only
when the payoff is Dragonstorm (never before Apex), and the payoff is ranked Dragonstorm/Apex first with
a direct-Dragon (Utvara Hellkite) bad-hand fallback last.

**Status** *(this is the ARCHIVED original spec — Step 2 shipped as GroupChoiceNonPrefixAccel,
4e68c8c, per the section above; and `MTG_CARD_SCORES_ONLY` further down was later deleted when
it became the analyzer's only mode, 8d4bdefb; noted 2026-09-03)*: NOT yet implemented. Step 1 (byte-identical over-splice skip, commit `d6cb727`) confirmed
INSUFFICIENT on its own — a backtrace of a bounded d5 measurement showed the worker still grinding the
`Solve` odometer (the legal ritual enumeration is itself huge). Step 2 is required for feasibility.
Implement in DragonstormProvider + a gated enumeration path; validate with smoke 18/0/0 (GT byte-
identical, no splice cards) + `MTG_UNPRUNED` A/B on Dragonstorm (measure win-turn cost) + a re-run of
the light-analyze to confirm the straggler is gone.

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

### Measured resolution (Dragonstorm, post-step-2)

After step 2, the full light-analyze (`MTG_SKIP_GRID`, grid skipped) was measured: the **baseline scan
straggler is GONE** (the phase that ate 2h18m pre-fix now clears in ~4 min), but the full run is still
**~1 hr** — dominated NOT by card_scores but by the Phase 1/2/2b confirmation phases (baseline scan +
per-candidate required-pieces confirm at CONFIRM1=3000 / CONFIRM2=5000 games + colour-source confirm at
two RunForRecords batches per colour across R/B/G). A 60-min run never even reached `card_scores` (the
cheap final ~1000-game pass).

**User decision: card_scores-only.** Added `MTG_CARD_SCORES_ONLY` (AnalyzerEngine.cpp, default off,
implies `SKIP_LAND_GRID`) which skips Phase 1/2/2b entirely and runs only Phase 3a card scores on the
DEFAULT keep profile. Measured **6m43s** (well under the user's 30-min cap; card_scores kept at d5, not
dropped to d0). Profile committed at `decks/Dragonstorm/Dragonstorm.profile.json`: 19 card_scores,
`hand_score_threshold = NO_GATE`, empty `required_pieces` / `min_color_sources`.

**Caveat (accepted):** card_scores are computed on the default-keep profile, so the scoring games PLAY
bloated ritual hands a real mulligan would throw — that is where the ~6-min run's single tail-straggler
game lives (step 2 bounds it to minutes, not the old hours). The scores are a bottoming tiebreak signal,
not a correctness input, and the user opted for speed. Standing rule for future decks: if 30-min-capped
`MTG_CARD_SCORES_ONLY` still overruns, ship WITHOUT card_scores (play tolerates empty).
