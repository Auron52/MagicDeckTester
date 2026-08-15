# End-of-turn dominance pruning (proposed, measurement staged)

**User proposal (2026-08-14):** drop clearly-dominated states at end-of-turn boundaries inside the
search recursion. Domination axes: life total, cards in hand, creatures on board. The stronger
heuristic form — count a creature ON BOARD as superior to the same card IN HAND — is useful but
"does not fully generalize across decks" (user), so it is a separate, per-deck tier.

## Why now (the measured motivation)

The Class B monster anatomy (gi=17 census, `mirrorwing-gen-perf-profile.md`): a single T1 no-win
decision = 20,355 tree nodes x 50-way branching = 1.02M candidate lines, each leaf-priced by a
~138-step rollout = 140.9M steps. The FSLine memo managed 5.8K hits against that — the lines reach
*distinct* states, so identity-memoization cannot collapse what enumeration created. Most of those
distinct states differ in ways that are strictly WORSE (same board, fewer cards; same hand, less
float), i.e. dominated — enumerated, rolled out, and never able to beat the line that dominates
them. Dominance is the only collapse principle that touches this mass.

## Prior art already in-tree

- `MTG_CANON_SIMKEY` (experiment, default off): memo-key canonicalization — collapses play-order
  PERMUTATIONS of the *identical* position (the x12/ply sequence explosion,
  `th-d5-five-hour-game.md`). Dominance is the next rung: collapse *ordered* (strictly-worse)
  positions, not just equal ones. Canon's caveat applies doubly here: greedy tie-breaks read
  vector order, so neither is provably byte-identical — both are play-affecting changes that need
  the full standing gate.
- Local plan-level dominance folds with the same "never hides a line" argument: sac-for-nothing
  (TurnSolver ~1593), cheapest-j accelerant subsets (~1979), turn-winning-plan-dominates-powerset
  (~2550), Twinflame magnet fold (~3590). This proposal is the same idea applied to STATES instead
  of plans, so it composes with (does not replace) those.

## The sound core (lossless tier)

Two states are comparable ONLY when their futures are identical apart from the compared resources:

- **END OF TURN ONLY — a hard rule, not a preference** (user, 2026-08-14): dominance comparisons
  happen at the end-of-turn cleanup boundary and nowhere else. Identity keying mid-turn is
  tolerable because a complete key can account for mid-turn state (floats, until-EOT effects,
  pending continuations — the 2026-08-14 key-hole audit shows how much there is); dominance
  CANNOT be soundly judged there, because the monotonicity arguments assume ephemeral state has
  washed out. The implementation must ENFORCE the boundary (cleanup done, floats empty, until-EOT
  effects expired, no pending breakpoints), not assume it;
- **same draws consumed** (same library position). Without this the comparison is unsound: an extra
  draw changes every future.

Then B is dominated by A iff, as multisets/values:

- hand_B SUBSET-OF hand_A;
- board_B SUBSET-OF board_A, and each matched permanent in A is in at-least-as-good state
  (untapped >= tapped; summoning-sick only if B's copy is too);
- **counters compare by per-TYPE monotonicity direction** (user refinement, 2026-08-14): identity
  needs equality, dominance needs only the right inequality per type -- MORE dominates for +1/+1,
  storage charges, depletion charges remaining; FEWER dominates for -1/-1 / other negative counters
  and marked damage. **Vial charge is NOT monotone** (the useful charge tracks the curve: 2 is
  ideal for 2-drops, 5 overshoots it) -- Vial and any other aim-for-a-value counter require
  EQUALITY. Any type without a declared direction: require equality (safe default). The 2026-08-14 storage key-hole find (BuildSimKey
  omitted `storage_counters`, exposed by canon on dragonstorm) is the cautionary tale for this
  table: every counter type IS future-determining, so the dominance comparison must enumerate them
  all -- an omitted type must fail closed (equality), never be silently ignored;
- graveyard_B SUBSET-OF graveyard_A when any deck card reads the graveyard (gy_self_power_bonus,
  retrace, Deathrite fuel) — else graveyards may be ignored;
- **opponent board is a per-deck direction too** (user, 2026-08-14): for nearly every goldfish deck
  it is inert (equality trivially holds — the opponent never gains permanents), but creature_giving
  GIVES the opponent creatures as its drain fuel, so there MORE enemy creatures dominates. Any deck
  that can change the opponent's board needs the axis declared (more-dominates / fewer-dominates /
  equality), defaulting to equality when undeclared — same fail-closed rule as unknown counters;
- floating_B <= floating_A per colour; life_B <= life_A; storm/turn counters equal.

Under goldfish rules every axis above is monotone (more resources never hurt), so dropping B never
drops a strictly-earlier win. It is still NOT byte-identical (a dominated line can win tie-breaks
today), hence: temporary selector flag, A/B on the suite, GT rebaseline if play moves — the
heuristic-optimization route, not a silent switch.

## The heuristic tier (per-deck, opt-in)

The user's "board creature >= same card in hand" rule collapses far more (deploy-order near-misses
become comparable) but is deck-dependent: THIS deck sometimes wants instants IN HAND (a Zada turn
casts from hand; an empty hand is a dead magnet); a discard/madness deck inverts the axis entirely;
and **ETB abilities are the sharpest counterexample** (user, 2026-08-14) — a creature whose
enter-the-battlefield trigger has a payoff condition can be strictly better HELD (cast it when the
payoff is live: more counters to place, a board to pump, a trigger to double), so board >= hand is
wrong exactly when the ETB is why the card is in the deck.

Framing (user, 2026-08-14): the rule works for MOST decks most of the time — even ETB-holding
decks mostly want their permanents played — so it is a very good DEFAULT, and the exceptions are
per-deck configuration, not engine logic. **The opt-in/out decision belongs to the ANALYSIS stage**
(analyze-deck, where the profile/archetype provider is built), recorded as a per-deck profile
flag, correctable later if analysis got it wrong. Examples: treasure_hunt opts OUT of the
play-a-land dominance (its lands-in-hand are retrace ammo — Flame Jab); "play a land each turn you
have one" genuinely dominates for nearly every other deck. The analysis stage can auto-suggest the
setting from the list itself (retrace / discard costs / madness / conditional-ETB payoffs are the
opt-out signals), with the user confirming. **The flag itself lives in the HEURISTIC (archetype)
provider** — the analysis stage picks its value, the provider carries it — never root engine truth;
measured A/B settles disputed cases.

## Instants/sorceries: dominance keeps them in hand (falls out of the sound core)

For a nonpermanent spell the preference INVERTS (user, 2026-08-14): a line that CAST it and
changed nothing else is dominated by the line that held it — hand-with-spell is a superset, the
rest equal, so the sound core already prunes it wherever the graveyard axis is ignorable (no
retrace/gy-payoff). Normally a resolved spell changes something, so this tier only removes WASTED
casts — fizzled tricks, pumps on nothing, the magnetless Gold-Rush-as-ritual lines from the
2026-08-14 branching investigation (cast {1}{G}, crack the Treasure, end with less of everything).

**Layering rule (user, 2026-08-14): dominance SUBSUMES the plan-level waste folds correctness-wise
but does NOT replace them.** A plan-level fold (SubsetWastesAccelerant, sac-for-nothing, the
existing ritual-waste prunes) removes the line BEFORE it is enumerated/applied/simulated — zero
budget spent; EOT dominance catches the same line only after paying its whole turn of work. Keep
every cheap early fold; dominance is the general backstop for the waste no plan-level rule
anticipated.

## Implementation (in tree, 2026-08-15 — both flags default OFF)

`src/ai/Dominance.h` holds ONE comparator; `MTG_DOM_CENSUS` (bucket, prune nothing) and
`MTG_DOM_PRUNE` (drop the dominated sibling) are two consumers of it. That is a direct response to
the withdrawn pricing above: a census that can measure something the prune does not use is worse
than no census.

Application point: the candidate loop's post-`SimulateEndAndStartNextTurn` boundary, archived per
PASS — the SIBLING FRONTIER the must-find gate asks for first. Every candidate in one pass reaches
its boundary having consumed the same draws, so comparability holds by construction. Frontier width
is capped (`MTG_DOM_ARCHIVE`, default 256); overflowing degrades to today's behaviour rather than
costing soundness. The check sits AFTER this turn's win checks, so a candidate that wins has already
returned and can never be pruned.

**Every field is dispositioned, and there is no fourth category.** Each field of `Permanent` /
`Player` / `GameState` is exactly one of:

- a **declared directional axis** — a per-deck `DomDir` from the provider (`DomAxis`), normalised so
  bigger always dominates, so domination is componentwise `>=`;
- an **exact-match field** — folded into a match key. This is where anything undeclared lands, so
  forgetting a field costs prune REACH, never soundness;
- a **boundary assertion** — `AtCleanBoundary` refuses to compare a state carrying floats, storm,
  a non-empty stack, pending per-turn choices, or any un-expired until-EOT permanent state. The doc's
  hard rule is enforced, not assumed; the one that realistically fires is floating mana (an echo cost
  paid off a lumpy source leaves float when `UpkeepFloatClearEnabled` is off).

The counter directions live where the user placed them (2026-08-15): the **default provider** carries
what is strictly better for any deck, **archetype providers** carry the overrides.

| Axis | Default | Why |
|---|---|---|
| `+1/+1`, storage, verse, loyalty (+ its mirror counter), **depletion** | MoreDominates | stored resources that only ever buy something; depletion counters are the ones REMAINING (each tap removes one, sacrifice at zero) |
| `-1/-1` | FewerDominates | smaller creature |
| tapped, summoning-sick | FewerDominates (engine-owned, not overridable) | monotone under the rules, not per deck — an untapped permanent does everything a tapped one does and banks a storage counter at cleanup |
| **Aether Vial charge** | EqualRequired | the doc's named non-monotone case: useful charge tracks the CURVE, 5 overshoots a deck of 2-drops |
| **age (cumulative upkeep)** | EqualRequired; `CreatureGiving` → MoreDominates | generically a cost, but for a deck built on what it pays out the payment IS the payoff (Varchild's gifts `age` Survivors per upkeep, nothing is ever sacrificed) |
| poison | EqualRequired | direction depends on WHOSE (our loss clock vs a win con) and the per-axis table cannot say "per side"; nothing creates one today |
| opponent board | EqualRequired; `CreatureGiving` → MoreDominates | inert for a goldfish (the passive opponent never attacks and never blocks — verified, no blocker logic exists); creature-giving drains per enemy body |

Adding a `Counter::Type` does not compile until it is dispositioned here AND given a direction.

**The graveyard axis is the one that decides whether this prune has reach at all.** At a fixed EOT
boundary the line that cast MORE has the smaller hand and the LARGER graveyard, so demanding
graveyard equality collapses dominance to hand equality — which is precisely why the withdrawn
pricing read ~0 on treasure_hunt. It is therefore a DECK property, computed once by
`GoldFishRunner::DeckReadsGraveyard` and stamped onto `GameState::deck_reads_graveyard` (the same
shape as `deck_feeds_combat`): a deck holding no graveyard reader ignores the zone; every other deck
compares it exactly. The reader list is every `gy_`/graveyard param in `CardParams`, and both the
scan and the field default fail CLOSED, so a forgotten param costs reach only.

Within a match group the members differ only in their axes, so "A's group covers B's" is a bipartite
matching under a partial order. The matcher is greedy over both sides sorted ascending and aligned at
the top: it can only FAIL to find an assignment that exists, never invent one — sound, and exact on a
single axis (two Sandstone Needles at different depletion counts).

### The soundness argument is about an OPTIMAL player — the engine is heuristic

Worth stating plainly, because it is the sharpest reason the must-find gate exists rather than a
formality. "A's resources ⊇ B's ⇒ every future of B is available to A" is a claim about what is
REACHABLE. The engine does not reach optimally: it plays a heuristic line. So A can hold a strict
superset of B's hand and still *realise* worse, because the extra cards change a heuristic's answer —
`CleanupDiscardCandidates` sheds a card B was never holding, an extra body flips `ShouldAttackWith`,
a wider hand hits `EnumGroupCap` and drops a group B kept. None of these break the dominance
relation; they break the assumption that dominating a line means playing at least as well as it.

This is a heuristic-quality risk, not a correctness one, and it is exactly what the suite A/B and the
`--budget-ms 0` must-find sweep measure. It also predicts where a regression would show up: on decks
whose cleanup discard actually fires (treasure_hunt) rather than on decks that never reach a
discard.

## Measurement plan (before any adoption)

1. `MTG_CANON_SIMKEY=1` on the gi=17 monster (zero new code; running 2026-08-14): how much of the
   1.02M is order-permutation identity, and does the answer change? **DONE — canon ADOPTED
   default-ON 2026-08-14/15** (suite nets −0.047/−0.054/−0.10, must-find 7/7 after the
   order-exactness fixes; see `th-d5-five-hour-game.md`).
2. Dominance census probe (MTG_BP_DUP_PROBE precedent — temporary, stripped after): at each
   end-of-turn boundary, bucket sibling states as {identical, dominated, incomparable} under the
   sound core. This prices the lossless tier before it is built.

   **FIRST PRICING (2026-08-15, `MTG_DOM_CENSUS` probe) — WITHDRAWN, the probe's board axis never
   ran.** The probe classified a permanent as "complex" (fail-closed to exact match) with
   `p.aura_attached_to >= 0 || p.equipped_to >= 0`. Both fields use **0 = unattached** and are never
   negative, so both tests were **always true**: every permanent on every board fell into the
   exact-match bucket and the multiset board comparison was dead code. The reported floors —
   mirrorwing 100g 1.6% dominated, gi=17 4.2%, treasure_hunt ~0 — were therefore measured with only
   **hand-subset + life** live, on top of an exact board AND graveyard match. They are a floor under
   a floor, and are superseded by the numbers below.

   The probe also had the opposite defect: its `complex` predicate omitted `verse_counters`,
   `loyalty`, `loyalty_activated_this_turn`, `garth_chosen_mask`, `echo_resolved`,
   `colored_cast_lifegain_used_this_turn`, `pending_death_trigger`, `marked_for_destruction` and
   `is_token`, all of which are future-determining — fail-OPEN holes of exactly the shape the
   storage-counter find warns about. Both defects are why the census and the prune now share one
   implementation (below) rather than being written twice.

   **PRICING (2026-08-15, shared comparator).** 60 games, d5, budget 20 ms, seed 1001:

   | deck | EOT siblings | **dominated** | n | unusable |
   |---|---:|---:|---:|---:|
   | burn | 2,737 | **36.0%** | 986 | 0 |
   | fivecolour | 454,749 | **10.7%** | 48,618 | 0 |
   | knights | 1,376 | 8.3% | 114 | 0 |
   | goblins | 5,861 | 4.6% | 272 | 0 |
   | slivers | 970 | 3.8% | 37 | 0 |
   | antilife | 1,861 | 3.6% | 67 | 0 |
   | mirrorwing | 353,708 | 2.5% | 8,925 | 0 |
   | hinata | 100,968 | 0.14% | 140 | 0 |
   | th | 3,768 | 0.05% | 2 | 0 |
   | dragonstorm | 28,346 | 0.04% | 10 | 0 |
   | creature_giving | 1,027,504 | 0.00% | 0 | 0.5% |
   | auras | 13,618 | 0.00% | 0 | 0 |

   Reading: the mass is not a uniform single-digit percent — it ranges over three orders of
   magnitude, and the shape is per-DECK rather than per-budget. Decks whose lines differ mostly by
   which cheap spell got spent where (burn, knights) price high, which is exactly the wasted-cast
   mass the hand-superset rule is predicted to catch. mirrorwing — the deck this whole line of work
   started from — is only 2.5%, and hinata/th/dragonstorm/creature_giving/auras are ~0.

   **BUT SEE "the application point is wrong" below: every number in this table is measured on the
   ROLLOUT's frontier, not the search's.**

   **THE GRAVEYARD IS A PROJECTION, NOT A ZONE (USER, 2026-08-15).** Refinement on the conditional
   gate: "it doesn't even have to be graveyard_size — it's more the TYPE of cards in the graveyard",
   e.g. "is there Throes of Chaos (retrace) in TH, or fetchlands (for DRS) in fivecolour?". So the
   comparison folds only the PROJECTION a deck's readers can observe (`GyReader` bits on
   `GameState::deck_gy_readers`), not the zone: Deathrite can only ever see per-type COUNTS (lands
   for mana, instants/sorceries for the drain, creatures for the lifegain — all three fungible
   within their filter), retrace can only ever see its own castable names, and a learned model can
   only ever see SIZE (`GraveyardSize` is the only graveyard-derived feature in the whole set).
   Measured observability, mainboard-only:

   | deck | observes from cards | + model |
   |---|---|---|
   | auras, creature_giving, goblins, knights, slivers | **nothing** | — |
   | antilife, burn, hinata | nothing | SIZE |
   | th | retrace names (Throes of Chaos), Land's Edge names | SIZE |
   | mirrorwing, dragonstorm | self-copy names (Ancestral Anger / Rite of Flame), colour demand | SIZE |
   | fivecolour | DRS type counts, Jared multicolour names, **all names** (Garth conjures Regrowth), colour demand | SIZE |

   **The projection changed no number — every deck priced identically to the all-or-nothing version.**
   The reason is structural and worth keeping: at a fixed EOT boundary with the same draws consumed,
   two siblings differ precisely because one CAST more, so their graveyard SIZES differ. Size is the
   discriminator, and size is exactly what the coarsest reader (the model) sees, so finer granularity
   has nothing left to relax. It is the right thing to have built for soundness and clarity — a
   missed reader is now a missed *bit* rather than a missed zone — but it buys no reach at this
   application point.

   **It did, however, surface the first evidence of the prune dropping a win.** With the projection
   in, `MTG_DOM_PRUNE=1` on smoke moved `mirrorwing_smoke_d5_s1001 gi51` from **T6 to T7** (1 searched
   SLOWER, 0 faster) where the previous, strictly-more-conservative version was byte-identical. That
   is the judgment-prune failure mode showing up in a plain suite A/B. It is NOT yet diagnosed, and
   the two candidate explanations need separating: a genuine missed reader (dominance ignoring
   something mirrorwing can observe) versus the optimal-vs-heuristic gap (the dominance relation
   holding, but the engine realising the surviving line worse). This is exactly what the must-find
   gate exists to settle — and it still cannot be run, because of the application point.

   **THE GRAVEYARD/EXILE AXES ARE CONDITIONAL (USER, 2026-08-15).** Direction: handle the graveyard
   "in a very similar way to storm — on for certain cards and/or decks, otherwise ignored", because
   "many many cards do not" read it (and Deathrite Shaman in fivecolour is the case that must stay
   on). The first attempt at this used a `gy_*` param scan, which is not sufficient; the second
   removed the optimization entirely, which gives up real reach. The shipped answer is conditional
   with a **corrected, engine-audited** gate.

   Two future-stable halves, ORed — and future-stability is the point: a per-STATE liveness test
   would be unsound, since a zone that is unreadable now can become readable later in the same line.
   - **Deck half** (`GoldFishRunner::DeckReadsGraveyard` → `GameState::deck_reads_graveyard`),
     rebuilt from an ENGINE-side audit of every graveyard-reading site rather than from param names,
     because the name-based version missed four classes (three deck-level, listed below).
   - **Model half** (`dominance::ModelReadsFeature`): `ExtractMidGameFeatures` feeds `GraveyardSize`
     and `ExileSize` into the learned models and `UseValueModel()` is adopted **default-ON**, so an
     attached model that BRANCHES on either feature makes that zone live for a deck whose cards
     never touch it. "Branches on" is exactly checkable — a GBDT reads a feature only where a tree
     SPLITS on it. Across the twelve shipped sidecars: **seven split on `graveyard_size`** (antilife,
     dragonstorm, fivecolour, hinata, mirrorwing, burn, th), **five do not** (auras, creature_giving,
     goblins, knights, slivers), and **none splits on `exile_size`**.

   The liveness bits are themselves folded into the comparison, so two states can only be compared
   when they agree about which zones matter. Result — the gate recovers the reach without giving up
   soundness anywhere:

   | | goblins | hinata | creature_giving | mirrorwing | fivecolour | burn |
   |---|---:|---:|---:|---:|---:|---:|
   | always compared | 4.6% | 0.14% | 0.00% | 2.52% | 10.69% | 36.02% |
   | **conditional (shipped)** | **11.60%** | 0.14% | 0.03% | 2.53% | 10.69% | 36.02% |

   goblins gets its full reach back (no reader in cards or model); hinata correctly does NOT (its
   model splits on graveyard size); fivecolour stays live on Deathrite. Nothing in the engine reads
   `state.exile`'s CONTENTS at all — only its size, as a model feature — so with no model splitting
   on it, exile is ignored everywhere today.

   **Why exact-match and not the doc's proposed SUBSET rule:** the model reader makes the zone
   non-monotone. A GBDT can push the leaf estimate either way on graveyard size, so there is no
   direction to declare and the subset tier is closed.

   The earlier measurement that motivated all of this (graveyard ignored vs compared, before the
   gate was corrected):

   | | burn | goblins | fivecolour | knights | slivers | antilife | hinata | mirrorwing |
   |---|---:|---:|---:|---:|---:|---:|---:|---:|
   | graveyard ignored | 36.1% | **11.6%** | 10.7% | 8.3% | 3.8% | 3.6% | **2.6%** | 2.5% |
   | graveyard compared | 36.0% | **4.6%** | 10.7% | 8.3% | 3.8% | 3.6% | **0.14%** | 2.5% |

   Only goblins and hinata move at all; every other deck is IDENTICAL, because their dominated
   states already had equal graveyards. The USER flagged the risk that motivated the audit
   (2026-08-15: "a few cards are relevant in the graveyard or exile; we shouldn't discount those
   cases"), and it found four classes the original `gy_*` param scan missed — all four are now in
   the gate:
   - **Jared Carthalion's −6 regrow** lives in `loyalty_abilities[].effect` (`regrow_multicolored`),
     not a `gy_*` param — fully implemented, reads the graveyard at resolution AND at the
     enumeration gate. fivecolour is only accidentally safe (Deathrite already flags it);
   - the **Land's Edge lookup** falls through hand → library → GRAVEYARD (`DecisionProviders.cpp`);
   - **`ChosenFloatColorCandidates`** sums colour demand over the graveyard for any deck with a
     sac-for-mana source — a **Treasure token qualifies**, so mirrorwing reaches it;
   - **`ExtractMidGameFeatures` feeds `GraveyardSize` AND `ExileSize`** straight into the learned
     eval / value models, making both zones live for ANY deck shipping a sidecar.

   The first three are deck-level and are in `DeckReadsGraveyard`; the fourth is per-model and is
   `ModelReadsFeature`. (Verified inert, so genuinely costing nothing: Gryff's Boon / Rancor /
   Audacity graveyard-return, Haytham's exile, Dragonlord Kolaghan's opponent-graveyard trigger —
   each documented inert vs a passive opponent and carrying no param.)
3. If built: A/B win-turn + cost on the regression suite train seeds, validate on held-out, full
   standing gate (play-affecting). Measure the heuristic tier separately, per deck.

   **FLAG GATE — PASSED (2026-08-15).** Per the coding-conventions verification pattern:
   clean-env smoke byte-identical (36/36 PASS, 0 configs changed, viewer protocol 0 play-drift);
   `MTG_DOM_PRUNE=0 MTG_DOM_CENSUS=0` smoke ALSO byte-identical (the `=0`-means-off semantics);
   `MTG_DOM_CENSUS=1` smoke byte-identical (the census really is play-neutral); `MTG_DOM_PRUNE=1`
   smoke diverges (the lever functions).

   **FIRST A/B — smoke only, NOT an adoption signal.** `MTG_DOM_PRUNE=1` on smoke: **1 config
   changed of 36** — hinata d5 s1001 gi37, a like-for-like line change at an unchanged win turn
   (T8 → T8, kept hand and draws identical); **0 slower, 0 faster** at both searched and d0 depth.
   So the prune is essentially play-neutral where it fires on this seed. One seed settles nothing
   about quality and nothing at all about COST (the smoke makespan moved 48 s → 51 s, which is
   noise at this granularity and in the wrong direction to read as a win). Still outstanding, in
   order: a timing measurement that can actually see the saving; the regression-mode A/B on train
   seeds; held-out validation on overnight seeds; and the must-find gate below.
4. **MUST-FIND gate (user, 2026-08-14, from the canon adoption):** dominance is a JUDGMENT prune
   (its failure mode is a reachable win silently dropped — no per-state validity argument exists,
   unlike identity collapse), so it additionally passes the unbounded-budget reproduction test:
   every previously-found win in the gate sweep must be found at `--budget-ms 0` with dominance ON,
   or the prune is wrong — the only exemption is a win outside the search window from every
   diverging decision. Prefer the SIBLING-FRONTIER application point first (same decision, same
   consumed draws — comparability by construction); cross-decision archives only after that form
   is proven.

## DIAGNOSIS of the mirrorwing gi51 T6→T7 regression (2026-08-15)

Verdict: **apparatus noise, not a soundness hole in the dominance relation** — on the evidence
available at the current application point.

The census now measures the thing directly. `harm` counts a dominated candidate whose ROLLOUT
returned a strictly better win turn than its dominator's — i.e. a win the prune would have deleted.
It is measurable without the must-find gate (census rolls every candidate out anyway), which is what
makes it usable while the prune is still on the wrong loop.

| deck (60g, d5) | b20 dominated | b20 harm | b200 dominated | b200 harm | b200 rate |
|---|---:|---:|---:|---:|---:|
| mirrorwing | 8,939 | 15 | 58,059 | 28 | **0.048%** |
| fivecolour | 48,618 | 4 | 303,508 | 108 | **0.036%** |
| goblins | 680 | 3 | 63 | 0 | 0% |
| knights | 114 | 0 | — | — | — |

So harm is real: mirrorwing's 15 events at b20 are what gi51 is made of. The question is whether
that means the RELATION is wrong (a missed reader) or the SEARCH is (dominance claims what is
REACHABLE under optimal play, while the rollout scoring these candidates is a greedy walker that may
simply fail to re-find, from A, the win it found from B).

**The control that answers it:** run the same "later candidate scored better" test on the engine's
own canon simkey — a provably EXACT identity relation, where two matching states have the same true
value by construction, so any mismatch is pure apparatus (memo warming, budget truncation, a
tightening branch-and-bound cutoff).

| deck, b200 | canon-identity mismatch | dominance harm |
|---|---:|---:|
| mirrorwing | 539 / 479,432 = **0.112%** | 28 / 58,059 = **0.048%** |
| fivecolour | 548 / 505,112 = **0.108%** | 108 / 303,508 = **0.036%** |

Dominance's harm rate is **below** the noise floor of an identity collapse the engine already ships
and has adopted. The effect is therefore a property of the scoring apparatus, not of the dominance
axes — and gi51's T6→T7 is the same variance the engine already tolerates on provably-identical
states. Consistent with this, harm falls as budget rises (mirrorwing 0.175% at b60 → 0.048% at
b200): more search, fewer missed re-finds.

**One probe came out VACUOUS and must not be read as evidence — IN CENSUS MODE.** A second check
counted states this comparator judged EQUIVALENT (mutual coverage) that then scored differently; it
reported 0. But `eq_byaxes` — how often mutual coverage fired at all — is **0 on every deck measured
under census**. The canon simkey is tested first and catches every such pair, so the comparator's
equivalence relation is never exercised and its clean result says nothing. A real validation of the
axes still needs the must-find gate.

**The same probe is LIVE under prune-only, and it is worth watching there.** The simkey branch in
`domin::Check` is guarded by `if (census && ...)`, so when only `MTG_DOM_PRUNE` is set every equality
decision falls through to the axes. See "EQUALITY PROBE UNDER PRUNE-ONLY" below.

Also noted: at b200, knights and slivers produce NO census output at all — the site is never reached,
exactly as at `--budget-ms 0`. More evidence for the section below.

## THE APPLICATION POINT — diagnosed, then FIXED (2026-08-15)

The census probe was placed at `SolveWithLookahead`'s candidate loop and the prune inherited it.
`SimulateEndAndStartNextTurn` has **ten** call sites and that is the wrong one: the default engine is
commit-the-line (`s_full_depth = !MTG_LEGACY_SEARCH`, ON) — `FullSearchLine` → `FSLineWin` →
`FSLineTail` — while `SolveWithLookahead` is reached only from *inside* the leaf rollout. So the
prune was trimming the rollout's per-turn decisions (the leaf estimator), never the frontier that
chooses the committed play. Two symptoms confirmed it: `--budget-ms 0` produced ZERO snaps on every
deck (so the must-find gate, specified at budget 0, could not exercise the prune at all), and
`MTG_DOM_PRUNE=1` was byte-identical across smoke despite firing on 36% of burn's siblings.

**Fixed.** The frontier is FSLineWin's `pre` loop — every plan is applied to the same state, so the
states they reach at the next boundary consumed the same draws and are comparable by construction.
The boundary itself is one frame down in `FSLineTail`, so the archive is OWNED by FSLineWin and
THREADED into FSLineTail (`dom_arch`, defaulting to nullptr for its other callers), checked at both
its EOT sites — the `post` loop for second-main decks and the single-main path. Every
(pre-combat × second-main) combination reaching there came from the same node with the same draws,
so they are all siblings in the sense the argument needs.

The census now fires at unbounded budget (burn b0: 7,367 dominated of 19,623), which is what makes
the gate below runnable.

### MUST-FIND GATE — PASSED, ALL 12 DECKS

`--budget-ms 0`, per-game win turns via `MTG_DUMP_WINS`, prune OFF vs ON: **zero wins lost** on
burn, slivers, knights, antilife, goblins (24 games each) and th (12 games); then auras,
creature_giving (24 each), dragonstorm (16), hinata, fivecolour, mirrorwing (12 each). No game's
win turn regressed on any deck, none gained either — the two arms find an identical win set.
Aggregate averages identical to 4 dp.

Method note for anyone re-running it: the arms must differ ONLY in `MTG_DOM_PRUNE`, and the budget
must be 0 (unbounded). At a finite budget the arms search different amounts of tree, so a win-turn
difference no longer isolates the prune from budget truncation, and the gate stops meaning anything.

### Suite A/B at the corrected site

`MTG_DOM_PRUNE=1` on smoke: **0 slower, 1 faster, 4 play-changed at the same score** (5 configs
changed of 36). The gi51 T6→T7 regression from the old site is gone. Clean-env smoke stays 36/36
byte-identical, and the prune arm costs nothing measurable (50 s vs 48 s makespan).

### RE-PRICED at the corrected site (60g, d5, b20, s1001)

The frontier is far larger here, and so is the mass:

| deck | EOT states | dominated | n | harm |
|---|---:|---:|---:|---:|
| burn | 130,998 | **31.9%** | 41,777 | 36 |
| goblins | 53,590 | **25.8%** | 13,819 | 3 |
| fivecolour | 8,326,004 | **18.1%** | 1,509,744 | 8,685 |
| antilife | 163,894 | 6.5% | 10,674 | 425 |
| knights | 167,324 | 4.2% | 6,978 | 0 |
| mirrorwing | 2,402,430 | 2.2% | 51,956 | 1,521 |
| slivers | 118,588 | 2.0% | 2,381 | 123 |
| dragonstorm | 159,279 | 0.5% | 831 | 0 |
| hinata | 1,934,003 | 0.2% | 3,445 | 50 |
| th | 118,377 | 0.13% | 156 | 4 |
| creature_giving | 1,094,161 | 0.03% | 298 | 0 |
| auras | 65,065 | 0.01% | 8 | 0 |

fivecolour alone offers 1.5M prunable states against 48.6k at the old site.

### THE EARLIER "APPARATUS NOISE" VERDICT DOES NOT SURVIVE THE MOVE

At the old site harm (0.048%) sat BELOW the canon-identity noise floor (0.112%), which is what
justified calling it noise. At the corrected site it is 0.086%–5.2%, i.e. **100–300x** the measured
floor (slivers 5.17% vs 0.000%, antilife 3.98% vs 0.000%, mirrorwing 2.93% vs 0.017%).

Two caveats on that comparison, both important:
- **The canon-identity control is DEGENERATE here.** Two canon-identical states share an FSLineCache
  entry, so they agree by construction; ~0% measures the memo, not the noise. It cannot be used as
  a floor at this site.
- Harm is nonetheless REAL, and neither confound explains it. It is **identical at b20 and b0**
  (slivers 3/101 both, burn 19 both), so it is not budget truncation; and **identical with
  `MTG_NO_GROUP_CAP=1`**, so it is not the enumeration breadth cap starving the richer state.

**Two mechanisms IDENTIFIED, both in the EVALUATOR rather than the axes.** A first hypothesis (the
forced CR 514.1 cleanup discard, unsearched at `CleanupDiscardSearchWidth() == 1`) is probably wrong
for most of these decks — five of nine suite decks never reach a cleanup discard at all — so it is
dropped in favour of what measurement actually shows:

- **The learned value leaf (slivers).** `MTG_VALUE_MODEL=1` → 3 harm of 101 dominated (2.97%);
  `MTG_VALUE_MODEL=0` → **0 harm of 1,525** (0%). A GBDT is not monotone in resources, so a
  dominating state can simply SCORE worse at the leaf. This is the same non-monotonicity that closed
  the graveyard subset rule, now biting the prune directly.
- **The greedy beyond-horizon rollout (burn).** Not the model: `MTG_VALUE_MODEL=1` → 0.205%,
  `MTG_VALUE_MODEL=0` → **0.335%** (harm goes UP without it). Burn's harm survives an exact rollout
  leaf at unbounded budget with no group cap. FSLineWin searches `depth` complete turns and then
  hands the tail to a GREEDY per-turn walker; dominance guarantees what is REACHABLE, and a greedy
  walker is not guaranteed to reach it.

So the harm is not evidence of a hole in the dominance axes — it is the evaluator being non-monotone
in exactly the resources dominance orders by. That is inherent to combining any dominance prune with
a heuristic leaf, which is why the MUST-FIND gate (not the harm counter) is the adoption test.

**Yet must-find still passes** (below): node-level harm is absorbed, because FSLineWin explores every
sibling and takes the minimum, so a line lost at one node is usually re-found at another. Harm is
therefore the right early-warning instrument, not a verdict.

### A NONDETERMINISM BUG, FOUND AND FIXED

Moving the check exposed one. Three identical prune-arm runs gave **3, 4 and 5 failures** — the repo
invariant is that a run is deterministic and thread-invariant, and the baseline was stable at 36/36,
so this was ours. Cause: `ModelReadsFeature` memoised the model feature mask in a `thread_local`
keyed on POINTER IDENTITY. A freed profile and a newly-allocated one can land on the same address,
so the memo returned a STALE mask, graveyard/exile observability flipped with heap layout, and the
prune diverged run to run. Classic ABA.

Removing the memo restored determinism (three identical runs) and confirmed the diagnosis, but cost
40% (70 s vs 50 s) because the mask means scanning 120 trees per snapshot. The fix is to compute it
ONCE where the models are attached and carry it as a stamped VALUE on `GameState::m_model_feat_mask`
— correct and free (back to 50 s, three identical runs). The general lesson: a per-game constant
derived from a POINTER must be stamped beside the pointer, never memoised on its identity.

## STATE OF PLAY (2026-08-15) — read this first on resume

**Shipped, both flags default OFF, clean smoke 36/36 byte-identical.** Commits `7ec5c75e` →
`3b6c309b` on `phase-1-2-deck-analyzer`.

- `src/ai/Dominance.h` — one comparator, two consumers (`MTG_DOM_CENSUS` prices, `MTG_DOM_PRUNE`
  drops). Every field is a declared axis, an exact-match field, or a boundary assertion; undeclared
  fails closed. `MTG_DOM_ARCHIVE` (default 256) bounds the frontier.
- Applied at the **FSLineWin `pre` frontier**, archive threaded into `FSLineTail` (both its EOT
  sites). This was moved off `SolveWithLookahead` — see the application-point section.
- Axis directions: generic monotone table in `DecisionProvider`, per-deck overrides in the archetype
  (`CreatureGivingProvider` declares opponent-board + age counters as MoreDominates).
- Graveyard/exile are a per-reader PROJECTION (`GyReader` bits on `GameState::deck_gy_readers`
  + the model's `graveyard_size`/`exile_size` branch check via `GameState::m_model_feat_mask`).

**Measured (60g, d5, b20, s1001):** burn 31.9% dominated, goblins 25.8%, fivecolour 18.1%
(1.5M states), antilife 6.5%, knights 4.2%, mirrorwing 2.2%, slivers 2.0%; hinata/th/dragonstorm/
creature_giving/auras ≈ 0.

**Suite A/B (smoke, prune on):** 0 slower, 1 faster, 4 play-changed at the same score. No cost
change (50 s vs 48 s makespan).

### REGRESSION A/B on train seeds — PASSED (2026-08-15)

`MTG_DOM_PRUNE=1 bash test/regression.sh` (seeds 2002/3003, 60 configs, 26,300 games), audited
against committed ground truth:

```
[searched] slower=0  faster=2  play-changed=11
[d0      ] slower=0  faster=0  play-changed=0
```

Exactly one config's aggregate moved: `fivecolour_regression_d5_s3003` 5.0300 → **5.0100**. NET mean
delta across all 60 configs: **−0.000333 turns** (negative = faster). All 11 play-changed games are
"kept hand + draws IDENTICAL → clean like-for-like LINE change" at an unchanged win turn. Batch
makespan 104 s, i.e. no measurable cost at a fixed budget.

**Zero slowdowns at searched depth on train seeds.** Held-out (overnight seeds) still owed.

Two counters in that run must NOT be read as evidence:

- **`harm=0` is structural, not a result.** Prune mode never rolls the pruned candidate out, so
  there is no win turn to compare — the harm counter is census-only by construction. A prune-mode
  run will always print `harm=0`.
- **`mismatch: axes=N` is the one to actually watch here** (see next section).

### HELD-OUT VALIDATION on overnight seeds — PASSED (2026-08-15)

`MTG_DOM_PRUNE=1 bash test/regression.sh --overnight` (seeds 4004/5005/6006/7007, 144 configs,
makespan 10m08s):

```
[searched] slower=2  faster=15  play-changed=139
[d0      ] slower=0  faster=0   play-changed=0
```

NET mean delta across all 144 configs: **−0.000183 turns**. Nine configs improved, one worsened by
0.0010 (`antilife_overnight_d5_s7007`); the largest single move was
`fivecolour_overnight_d5_s5005` 5.0333 → 5.0233.

**Both SLOWER games classify as `churn`**, via `test/classify_turn_later.sh overnight` — each
recovers its original win turn at 4x AND 16x the case budget:

| game | old | new | classification |
|---|---|---|---|
| `antilife_overnight_d5_s7007` gi74 | 6 | 7 | churn (4x=6, 16x=6) |
| `fivecolour_overnight_d5_s6006` gi62 | 5 | 6 | churn (4x=5, 16x=5) |

So neither is the prune deleting a reachable line; both are budget-boundary variance. Consistent
with must-find, which loses nothing at unbounded budget.

Coverage note: the audit reports `no-run-dir: 18`. Those are all **d0** entries at seeds 5005/7007 —
stale `gt_logs` from an older matrix that the current matrix does not run at those seeds. They are
not a gap in this A/B: d0 does no search, so a search-side prune cannot reach them (and d0 shows
0/0/0). All 144 configs the matrix does define were run.

### EQUALITY PROBE UNDER PRUNE-ONLY — the axes' own soundness signal

Because the simkey branch is census-gated, a prune-only run exercises the comparator's equality
relation on every pair: the regression run above logged `eq_byaxes=34,473,864` with
`ident_mismatch=2,851` (0.008%). That counter is supposed to be **0**: it means two states the axes
call EQUAL scored differently, i.e. the engine can tell apart something the axes fold together. It
matters beyond the equality case, because the same axis set powers `Covers()` — a genuine hole there
would mean a `Covers()` that is also wrong, which is an unsound PRUNE.

Localized (12 games, d5, s3003, b20) to **two decks only**:

| deck | eq pairs | ident_mismatch | rate |
|---|---:|---:|---:|
| mirrorwing | 103,085 | 20 | 0.019% |
| fivecolour | 504,218 | 32 | 0.006% |
| burn / goblins / antilife / knights / slivers / dragonstorm / creature_giving / th / auras | — | **0** | 0 |

**The confound is CACHE WARMING, and it must be ruled out before calling this a missed field.** The
"a later equivalent state can only score WORSE" argument rests on the branch-and-bound cutoff only
tightening across a pass. At a FINITE budget that argument is incomplete: a later candidate searches
a warmer TT/line cache and can therefore complete more search within the same ms, finding a
genuinely better win with no missing field involved. The two decks implicated are the two heaviest
searches in the suite — precisely where a 20 ms budget binds hardest, exactly as the warming
hypothesis predicts. burn/goblins/antilife show 0 at b20 AND b0 (identical counters at both, so the
budget never binds for them).

**Warming was RULED OUT.** At `--budget-ms 0` the signal survives and mirrorwing's rate more than
doubles: mirrorwing 1,583 / 3,904,802 (0.041%), fivecolour 105 / 3,387,253 (0.0031%). Not budget
truncation.

**The mechanism is the TRANSPOSITION TABLE, not the axes** (mirrorwing, 4 games, d5, b0):

| arm | eq pairs | ident_mismatch | rate |
|---|---:|---:|---:|
| baseline | 1,940,494 | 1,545 | 0.0796% |
| `MTG_FSL_CAP=1` (line cache off) | 3,110,170 | 2,165 | 0.0696% |
| `MTG_TT_CAP=1` (TT off) | 1,939,319 | **58** | **0.0030%** |
| both off | 3,146,594 | 76 | 0.0024% |

Dropping the TT removes ~96% of the disagreement at an essentially unchanged pair count (1.939M vs
1.940M) — a 27x rate drop. Dropping the line cache removes none of it. So the probe is mostly
measuring TT bound fidelity: a TT hit can return a value computed under different alpha/beta bounds,
which lets a later equal state score better without any missing field. That property is
**pre-existing and prune-independent** (the TT is on either way); this probe merely made it visible.
It is a separate question from dominance and is NOT a blocker here.

**Residual: ~0.003% (58-76) with caches off — still not 0.** Note `MTG_TT_CAP=1` is the minimum, not
"off" (0 = unlimited), so some TT effect remains in even that arm. The residual is therefore an upper
bound on any genuine missed field, and it is small. Left OPEN at low priority; the direct test of the
axes is the must-find gate, which passes 12/12 including both decks implicated here.

Worth keeping straight: the equality case **never prunes** (`if (is_dominated && DomPruneOn())` —
identical states are deliberately left to the existing dedups), so an equality hole deletes nothing
by itself. Its value is purely as a diagnostic for `Covers()`, which shares the axis set.

### What is DONE

1. ~~Move the check to the FSLineWin/FSLineTail frontier~~ — done (`3c3f06bf`).
2. ~~Re-price at the new site~~ — done (`4f71986c`, table above).
3. **Must-find gate: ALL 12 decks PASS, zero wins lost** — burn, slivers, knights, antilife,
   goblins, th, auras, creature_giving, dragonstorm, hinata (2026-08-14/15), plus **fivecolour and
   mirrorwing** (2026-08-15, 12 games each at `--budget-ms 0`). No game's win turn regressed on any
   deck, and no deck gained one either — prune ON and OFF find an identical win set. The gate is
   now COMPLETE; the two heaviest decks, which are also two of the three highest-harm ones, clear
   it despite node-level harm of 1,521 (mirrorwing) and 8,685 (fivecolour). That gap between
   node-level harm and zero game-level loss is the point made under "Yet must-find still passes":
   FSLineWin re-explores, so a locally-deleted better line is recovered elsewhere.
4. ~~Diagnose the harm signal~~ — done (`3b6c309b`): two mechanisms, both the EVALUATOR being
   non-monotone (learned value leaf; greedy beyond-horizon rollout), not a hole in the axes.

### What is OPEN — resume here

1. **ADOPTION DECISION — the user's call, and the only thing left.** Every gate now passes (see
   "THE EVIDENCE, ASSEMBLED" below). Adopting means flipping `MTG_DOM_PRUNE` to default-ON and
   rebaselining GT via `--accept` on each mode, since play moves at an unchanged score. Do NOT flip
   the default without the user saying so.
2. **Equality-probe residual** (low priority): ~0.003% ident_mismatch survives with both caches
   capped. Bounded above by the TT cap not being a true off switch. Only worth chasing if the
   must-find gate ever regresses.
3. **Cost.** Partly answered: the train-seed A/B ran 104 s vs a ~100 s baseline makespan, i.e. no
   measurable cost at a fixed budget. At a fixed ms budget the prune buys more search per unit budget
   rather than wall time, so the honest measurement is quality at fixed budget (the A/B above) plus
   wall time at `--budget-ms 0`.
4. **Two unexplained zeros** (low priority): auras' ~0 comparable pairs (suspected: the attachment
   fail-close makes every permanent unique by copy id) and creature_giving's 0.5% unusable states
   (not the storm path — that deck has no suspend).
5. **The heuristic board-vs-hand tier**, still deliberately excluded. NOT the graveyard subset rule:
   the learned-model reader makes that zone non-monotone in either direction, so that tier is closed.

### THE EVIDENCE, ASSEMBLED (2026-08-15)

Everything the adoption decision rests on, in one place:

| gate | result |
|---|---|
| **Must-find**, `--budget-ms 0`, all 12 decks | **zero wins lost**; prune ON/OFF find an identical win set |
| **Train seeds** (2002/3003), 60 configs, 26,300 games | 0 slower, 2 faster, 11 play-changed; NET **-0.000333** |
| **Held-out** (4004/5005/6006/7007), 144 configs | 2 slower (**both churn**), 15 faster, 139 play-changed; NET **-0.000183** |
| **Cost** | none measurable (104 s vs ~100 s; overnight 10m08s) |
| **Clean-env smoke** (both flags off) | 36/36 byte-identical |
| **Prunable mass** | up to 31.9% of EOT states (burn); 1.5M states on fivecolour |

Reading: the prune is SOUND on the direct test (must-find), NEUTRAL-to-slightly-POSITIVE on quality
across both train and held-out seeds, and FREE at a fixed budget. Its payoff is not the -0.0002 turn
delta -- that is noise-level and the honest way to state it is "costs nothing" -- but the state mass
it removes, which buys more search per unit budget on exactly the decks that are hardest to search.

What adoption does NOT rest on: `harm` (structural 0 under prune) and `ident_mismatch` (96% TT, not
the axes). Both are documented above precisely so they are not mistaken for evidence either way.

### Traps worth not re-learning

- A per-game constant derived from a POINTER must be STAMPED beside the pointer, never memoised on
  pointer identity — the `thread_local` version was an ABA hazard that made the search
  nondeterministic (3/4/5 failures across identical runs). Fixed via `m_model_feat_mask`.
- The canon-identity "noise floor" control is DEGENERATE at the FSLineWin site (canon-identical
  states share an FSLineCache entry, so they agree by construction). Do not reuse it as a floor here.
- `eq_byaxes` is 0 **in CENSUS mode only** — and knowing which mode you are in is the whole point.
  The canon-simkey equality branch in `domin::Check` is gated on `census`, so with census ON the
  simkey catches every equal pair first and the comparator's own equality case never fires (a clean
  result from it is vacuous). With **PRUNE alone**, that branch is skipped and every equality
  decision goes through the axes: `eq_byaxes` jumps to 34.4M on a regression run and the
  `ident_mismatch` probe becomes LIVE. Do not carry the "vacuous" reading across modes.
- `--budget-ms 0` IS unbounded (`FromVirtualMs(<=0)` → `Unlimited()`), not "no search".

## Sequencing

Independent of the search/play mismatch fix (`mirrorwing-search-play-mismatch.md`) and of the
keepgen/bottoming work — attacks the same monsters through a third door (state count, vs early
exit and leaf pricing). Composes with the Expedite / Scale-the-Heights group folds the 2026-08-14
branch-stats probe surfaced.
