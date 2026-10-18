# Remand on our own spell (self-targeted counterspell)

Status: IMPLEMENTED 2026-10-17 (branch `niv-remand`), pending the orchestrator's play-change testing
(Hinata2 plays differently; NivMizzetCombo / Unpredictable Cyclone run Remand too). This doc holds the
model, its folds, and the DEFERRED gaps.

## The card

Remand {1}{U}, Instant: "Counter target spell. If that spell is countered this way, put it into its
owner's hand instead of into that player's graveyard. Draw a card."

The passive opponent never casts, so the only legal target (CR 115.1, "target spell" is any spell) is
one of OUR OWN spells on the stack. That is a real line, not a dead card: pay X's cost + {1}{U}, X's
cast-time effects happen (storm count, cast triggers -- Niv-Mizzet, Parun draws on BOTH casts --
prowess, Phyrexian life, Hinata's per-target discount on the Remand), X returns to hand, Remand draws.
Until 2026-10-17 it was `goldfish_inert` (never cast), which was wrong.

## The model

* `CardParams::counters_target_spell_to_hand` (Remand; template `draw_spell`, `draw: 1`,
  `discount_max_targets: 1`) and `CardParams::cant_be_countered` (Niv-Mizzet, Parun; Emrakul).
* Remand is NEVER a cast of its own (`CastableOnItsOwn`, SpellEffects.h): it rides the hand cast of X
  as ONE combined Action, `Action::remand_by` (the Remand's name) + `remand_slot` (its hand index).
  `hand_index` stays X's slot so every reader that dereferences it sees the card that is cast.
* Cost: X's bill + the Remand's `EffectiveSpellCost`, paid in ONE tap pass (the splice idiom).
  Recompute sites add it through `AddRemandRiderCost` (ManaPayment): apply_one, CastSpellFromHand,
  SubsetPayableSequential, BatchPrepayMainCasts, the condemn stamper.
* Realisation, both worlds through the shared `CastAndResolveRemand` (ManaPayment.cpp), called right
  after X's own cast-time block: the Remand leaves the hand, counts as a cast (storm, CFT
  accumulator, Irencrag budget, main-2 stamp), fires its own cast triggers, then resolves (LIFO): X to
  its owner's hand (`ReturnCounteredSpellToHand`, `HandEntryReason::Countered`, staged copies come
  back un-staged -- a new object, CR 400.7) unless X can't be countered, then the draw through the
  draw-spell path (`DrawIntoHand(..., HandEntryReason::Draw)`, cards_drawn, deck-out), then the
  Remand to the graveyard. A countered X skips its whole resolution (the rollout's chain and tail;
  in the executor X's stack entry is removed). A remanded Irencrag Feat installs no "one more
  spell" budget (that is a resolution effect).
* A void rider (no Remand copy in hand, or the Irencrag budget leaves no cast for it, or a free /
  alt / graveyard cast) casts X plainly -- identically in both worlds.
* Breakpoints: Remand's resolution IS a plain cantrip, so apply_one arms exactly the cantrip
  breakpoint for it (`cantrip_breakpoint`, the DrawSpell branch's own code moved into a lambda),
  after X resolves when X can't be countered. The executor classifies a remanded cast by the
  REMAND's name (`BpNameOf`), PlanOpensBreakpoint marks site 3, IsPlainCantrip is true. The
  continuation sees X back in hand, which is how "Probe, Remand, recast Probe" is reached.
* Search: the variants of one Remand copy share that copy's odometer group (PlanGroupKey keys
  `remand_slot`), so the cost is (1 + targets) per Remand copy rather than one extra member in every
  castable card's group. `SubsetHasDuplicateSacSource`'s `kDupRemand` clause rejects a remand plus a
  plain cast of the same X slot, and keeps one arrangement of two remands by two copies.
  `plan_signature` keys `#RM<name>:<x>`.
* Viewer: menu label "X + Remand on it (...)", action JSON `remand_by` / `remand_slot`, the plan's
  `casts` lists both cards, a line is declared `cast=X;cast=Remand`, CheckLine's `remand` sub picks
  the target when the line casts several spells.

## Folds (identities, stated at the code)

1. Resolution-only riders are irrelevant to a countered X (tutor target, Ponder keep, devour count,
   loyalty pin, Theft Equipment, land-put credit, float colour): one remand target per (name, cost,
   X, cast mode).
2. Copies of X are interchangeable (both worlds take X and the Remand BY NAME): one variant per key,
   pinned to the LAST hand slot holding it -- the slot that keeps the hand-cast canonical-prefix fold
   sound (FinalizeFoldTags exempts remand variants from tagging and from its source count).
3. Two Remand copies answering two spells: only target order == Remand-slot order is kept.

PROVISIONAL (needs the USER's sign-off): Remand's instant-speed windows (combat, end step, the
opponent's turn) collapse to the main phases -- with no opponent spells there is nothing to answer
there, and every own spell it could target is cast in a main phase.

## Deferred gaps (not modelled; each is a disclosed modelling gap, not a prune)

* Remand targeting another Remand (a chain): legal, niche; the second needs the first on the stack.
* Remand on a free (Maelstrom bank) / alternative-cost / sacrifice-land cast, on a cascade or
  demonstrate spell (the executor pushes those cast triggers above X and would resolve them after
  the Remand), on an untap ritual / ETB-untap creature (tap-ahead payment), on a replicate creature,
  on a Natural Order sacrifice. None is in a Remand deck today.
* A cascade / free-cast walk casting Remand on the spell that cast it (`FreeCastHasLegalTargets`
  refuses Remand).
* Different-NAME targets with an identical cast profile (Ponder / Preordain) are not folded. It is
  NOT an emission-level identity: R(Ponder) alone equals R(Preordain) alone, but {R(Ponder),
  Preordain} and {R(Preordain), Ponder} differ (which spell resolves), so dropping a variant deletes
  a line. The sound form is a SUBSET-level canonical rule: with a cast-profile key (every type /
  supertype / colour / keyword mask, subtypes, MV, the exact bill, staged expiry, plain cast
  legality), reject R(Y) for a non-representative Y only when its representative X is not also cast
  plainly in that plan. An open collapse (it would shrink Hinata2's cantrip fan), never a width cap.
* The rollout LEAF (estimating) does not offer remand variants (the phyrexian decision-space
  precedent); the searched collect, the executor and human play do.
* Learned-evaluator / keep-model plan summaries (`PlanCastNames`, `SummarizePlanByNames`) still name
  a remanded X as cast. No Remand deck carries a learned evaluator; fix train and serve together.
* Memory Lapse (same Hinata2 list) is the same mechanic with "on top of its owner's library" -- left
  `goldfish_inert`; it would reuse this rider with a different destination.
* Hinata's cleanup discard still sheds Remand by name with the dead cards (a USER keep ranking).
* Hinata2's keep table and value leaf were fitted to play in which Remand was dead, and
  EquivalenceDiscovery no longer merges Remand with the inert cards -- both artifacts are stale
  against the new play.

## 2026-10-18: the plan evaluators must not credit a countered X (fixed), and the d0 valuation (USER decision)

CollectActions zeroes a remand variant's resolution projections, but both plan evaluators (Solve's
`consider` -- the d0 decision and every rollout leaf -- and EnumeratePlans' `eval_and_push`) also read
X's DEFINITION. Three readers priced a countered X's resolution; each now asks `RemandCountersCast`
(TurnSolver.cpp), in both evaluators:

1. **The leftover-mana FILL** (`FillScaledCastFace`, and `FillScaledXTrick` for the same reason) scaled a
   REMANDED Magma Opus's face: the plan was credited the extra face damage and eval of a spell that goes
   back to hand, and both apply worlds then paid the filled face's bill (they price the cast from
   `crackle_targets`). Hinata2 d0: 557 such fill calls per 1000 games. It reached the search too: the
   searched root returns the first candidate whose PROJECTED `wins_this_turn` is true without a rollout,
   so a phantom fill could be committed as a kill. Test: "the leftover-mana FILL never scales a COUNTERED
   Magma's face".
2. **The cast restrictor** (Irencrag Feat, "one more spell") counted a remand variant as ONE cast; it is
   two (`RestrictorCastsOf`). `{Irencrag, X + Remand}` with one cast left was admitted, priced "X
   countered, draw a card", and played as plain X (both worlds void the rider at
   `casts_remaining_this_turn == 1`). Rejecting it loses nothing: the plain `{Irencrag, X}` subset
   realises that line on less mana. Test: "Remand after a cast restrictor counts TWO casts".
3. **KillTriggerComboCredit** counted a remanded burn spell's creature damage (latent: no Searing Blood
   deck runs Remand).

Still open, latent (no Remand deck has the model): the provider's `ExtraLethalDamage` /
`ProjectsAlternateWin` receive X's definition in `casting` for a countered X.

**The Hinata2 d0 regression is NOT one of these** (d0 s2002 1000 games: base 6.791, rider 6.849, rider +
fixes 6.848; d3 b10 60 games: 5.817 / 5.750 / 5.750). It is the generic greedy valuation. The lethal plan
of the reported game (s2017 gi15 T5) is on neither binary's ROOT menu -- it appears only after Soulfire
Eruption's dig resolves (the breakpoint re-solve). At the root the greedy compares {Soulfire} = 0 against
{Soulfire + Remand on it} = 100: `EvalCard` prices Soulfire Eruption at `damage (0) x 100` (its real
damage and its dig of N staged cards are unvalued), and a non-haste creature from turn 6 at 0
(`ExpectedAttacks` assumes a 6-turn game), while the Remand's draw is the DrawSpell 100. The rider
"monetises" those zeros: of the 71 slower games, the remand at the first divergence targets Soulfire
Eruption (~51), Hinata (16), Irencrag (2). The 16 faster games are mostly a stuck hand cycling Remand
through an otherwise dead Irencrag Feat (5 unwon -> won). Withholding riders from the d0 runner (as the
rollout leaf already does) is byte-identical to base on those 1000 games. A naive Soulfire dig credit
(+100 per target) measured much worse (6.871). OPTIONS for the USER: (a) d0 follows the leaf (no
riders at depth 0); (b) a provider-owned rider policy at d0 (e.g. Hinata2: never answer Soulfire /
Hinata); (c) a real fix of the greedy's value for Soulfire Eruption and late creatures (a heuristic
adoption, needs the heuristic-optimization loop); (d) keep the current behaviour.
