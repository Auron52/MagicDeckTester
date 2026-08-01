#pragma once

// Per-deck/archetype heuristic interface.
//
// GUIDING PRINCIPLE: the generic search/decision tree (TurnSolver / AIEngine) must
// contain NO deck/archetype/card-specific DECISION logic. Every such heuristic --
// which card to fetch, whether to dig, how many lands to discard to Land's Edge,
// whether a risky payload is worth casting -- is asked of the deck's DecisionProvider.
// The engine keeps only rules-mechanic EXECUTION (how an effect resolves).
//
// The decisions follow the heuristic-then-search pattern: a candidate-set hook returns
// 1 candidate (heuristic decided -> no search) or >1 (search picks among the narrowed
// set). Other hooks return a gate bool / fire count / policy bit.
//
// Providers are STATELESS and read-only over (GameState, CardDatabase). One shared
// const instance per archetype lives for the whole process (see DecisionProviders.h),
// and GameState carries a non-owning `const DecisionProvider* m_provider` that
// propagates through every search deep-copy. Call sites read state.m_provider; a null
// pointer falls back to DefaultProvider().
//
// The interface grows one hook at a time as decisions migrate out of the engine (see
// the staged plan). Some signatures are deliberately card-specific for now and may be
// generalized once the cross-archetype patterns settle.
//
// ---------------------------------------------------------------------------------------------
// LEGACY "Hook N" DECODER -- do NOT add new numbers.
//
// Hooks used to be identified by number ("Hook 26 force-keep", "Hook 30 splice collapse") in code
// comments, commit messages, and docs/design/. The numbers COLLIDED -- 30, 22 and 16 each named
// two or three unrelated hooks -- which made the historical record ambiguous and had already
// produced a wrong cross-reference (ScaledCastVariants was labelled "Hook 28", which is really
// WantsCastOrderingSearch). Numbering is therefore retired: hooks are referred to by METHOD NAME,
// which is unique and greppable. This table decodes every legacy number so old commits and design
// docs stay readable. Where a number is listed twice, BOTH readings existed -- disambiguate by the
// surrounding text.
//
//    1  TutorCandidates                 16  ShouldAttackWith  |  OpponentPlaysLands
//   1b  TutorToBattlefieldPutOrder      17  CastOrderRank
//    2  FetchCandidates                 18  XCandidates
//    3  ShouldEmitRiskyAltPayload       19  SituationalCardRank
//    4  CanAutoFireAltPayload           20  PreferHoldLandDrop  |  ShouldEmitUntapRitual
//    5  CastEnablerFirst                21  HoldDeferredDropForLethal  |  BranchSoulfireOwnTargets
//    6  HasAnyDigSource /               22  HoldDeferredDropForFurtherDig  |  NcLandDropTempoBonus
//       ShouldConsiderDig /                 |  EnumGroupCap
//       SelectDigSource                23  FetchSearchCap
//    7  LandsEdgeFireCount              24  ManaSourceRank
//    8  WantVialCharge                  25  OpponentLifegainUseful
//    9  ScryKeepOnTop                   26  KeepFloor
//   9b  KeepReorderTop                  27  UseAccelPrefixCollapse
//   10  DiscardLandsFirst               28  WantsCastOrderingSearch
//   11  ShouldStageSpectacleDraw        29  PrunesAcceleratorWithoutPayoff
//   12  ShouldCastDrawEngine            30  UseSpliceCollapse  |  CastCheapestFirstWithinTier
//   13  PostDrawKeepLandName                |  ScaledCastVariants
//   14  HasExtraLethalModel /
//       ExtraLethalDamage
//   15  ArchetypeCardValue
//
// Never assigned: no hook was ever numbered above 30. ImpulseFloatColorRedOnly /
// RestrictSacColorsToHasteAndRed were added unnumbered.
// ---------------------------------------------------------------------------------------------

#include "../core/GameState.h"
#include "../core/ManaPool.h"
#include "../cards/CardDatabase.h"
#include "KeepModel.h"   // KeepGuard (Undecided / ForceKeep / ForceMulligan) for the keep-floor hook
#include <string>
#include <vector>

// A "scaled cast" variant of a spell whose mana cost depends on how much OUTPUT it commits -- the
// divided-damage analogue of an {X} value. `face` damage reaches the opponent; the total `cost` is
// what committing that much face costs (all discounts already applied). An archetype's
// ScaledCastVariants() hook returns the sensible levels; the engine emits one mutually-exclusive cast
// per variant and the plan enumerator + search pick among them. See DecisionProvider::ScaledCastVariants.
struct ScaledCastVariant
{
    int      face = 0;   // damage dealt to the opponent's face at resolution
    ManaCost cost;       // finalized total cost for committing that much face
};

class DecisionProvider
{
public:
    virtual ~DecisionProvider() = default;

    // TutorCandidates -- tutor priority: ordered library card-name candidates for a tutor
    // (Idyllic / Enlightened). 1 = decided, >1 = search picks, {} = whiff.
    virtual std::vector<std::string>
    TutorCandidates(const GameState& s, int controller, const CardParams& pp) const = 0;

    // TutorToBattlefieldPutOrder -- tutor-TO-BATTLEFIELD put ORDER + SELECTION (Dragonstorm). Returns the
    // ordered list of card NAMES to put onto the battlefield for a `tutor_to_battlefield` resolution that
    // puts up to `max_puts` (= the storm total, already capped at library Dragons by the caller). Names may
    // REPEAT to honour multiplicity (two "Scourge of Valkas" entries == put 2 Scourges); the returned length
    // is <= max_puts. Unlike TutorCandidates (which the engine expands to every library copy of each listed
    // name in library order), this is an EXACT max_puts-aware SUBSET selection PLUS a single deterministic
    // put-order -- so when N is small the provider can reserve a slot for a same-turn-relevant Dragon (the
    // haste-Dragon) instead of letting a run of Scourges crowd it out. The engine keeps the PUT mechanism
    // (find/remove/enter + OnDragonEnters cascade + reshuffle); only the which-and-in-what-order decision is
    // provider-owned. Default {} (empty) -> the engine falls back to TutorCandidates' library-order
    // enumeration exactly as before, so every non-Dragonstorm deck (and Dragonstorm under MTG_UNPRUNED) stays
    // byte-identical. Only DragonstormProvider overrides it. See PerformTutorToBattlefield +
    // analyze-Dragonstorm.md.
    virtual std::vector<std::string>
    TutorToBattlefieldPutOrder(const GameState& /*s*/, int /*controller*/,
                               const CardParams& /*pp*/, int /*max_puts*/) const { return {}; }

    // FetchCandidates -- fetch priority: ordered land-name candidates for a fetchland.
    virtual std::vector<std::string>
    FetchCandidates(const GameState& s, int controller, const CardParams& fetch_pp) const = 0;

    // CanAutoFireAltPayload -- auto-fire safe alt payload: per-card predicate (the engine keeps the
    // deterministic re-scan loop; only the "is this free payload safe to fire" decision
    // is provider-owned).
    virtual bool CanAutoFireAltPayload(const GameState& s, int controller,
                                       const CardDefinition& def) const = 0;

    // HasAnyDigSource / ShouldConsiderDig / SelectDigSource -- dig gate + source
    // (Treasure Hunt / Land's Edge cycling/sac-draw).
    virtual bool        HasAnyDigSource (const GameState& s) const = 0;
    virtual bool        ShouldConsiderDig(const GameState& s) const = 0;
    virtual std::string SelectDigSource (const GameState& s, const ManaPool& pool,
                                         bool& out_is_sac) const = 0;

    // LandsEdgeFireCount -- how many lands to discard to a Land's Edge this activation.
    virtual int LandsEdgeFireCount(const GameState& s, int rate) const = 0;

    // WantVialCharge -- whether to add an Aether Vial charge counter this upkeep.
    virtual bool WantVialCharge(const GameState& s, const Permanent& vial) const = 0;

    // ScryKeepOnTop -- scry/surveil per-card keep decision: keep `top_card` on top (true) or
    // bottom/bin it (false). The engine keeps the reorder/bin MECHANISM (ScryTop/
    // SurveilTop); only the keep DECISION is provider-owned.
    virtual bool ScryKeepOnTop(const GameState& s, const Card& top_card) const = 0;

    // KeepReorderTop -- Ponder-style reorder keep-vs-shuffle: a SET decision over the cards looked at
    // (top N). Return true to KEEP them on top (SituationalCardRank then ORDERS them), false to
    // SHUFFLE them all away. Unlike the per-card ScryKeepOnTop gate, this judges the WHOLE set --
    // e.g. the Hinata deck must shuffle a top set that contains no way to advance toward Hinata,
    // even if one card would individually pass ScryKeepOnTop. Default mirrors the legacy rule
    // (keep iff any single card is wanted), so every non-overriding deck is byte-identical.
    virtual bool KeepReorderTop(const GameState& s, const std::vector<Card>& top) const
    {
        for (const Card& c : top) { if (ScryKeepOnTop(s, c)) { return true; } }
        return false;
    }

    // CastEnablerFirst -- cast-sequencing: should this hand cast go in the ENABLER-FIRST pass (cast
    // + resolve before other spells, so a same-turn payload sees the enabler active)?
    // The engine keeps the multi-pass apply MECHANISM; only the partition is provider-owned.
    virtual bool CastEnablerFirst(const GameState& s, const std::string& card_name) const = 0;

    // DiscardLandsFirst -- discard-to-7 policy: when shedding to hand size, prefer discarding a LAND
    // first (true) over the highest-MV card (false). Used when a Land's Edge land outlet
    // makes lands ammunition. An INPUT to CleanupDiscardCandidates below, not a separate decision.
    virtual bool DiscardLandsFirst(const GameState& s) const = 0;

    // CleanupDiscardCandidates -- cleanup discard (hand over its size limit): WHICH card to shed,
    // as hand indices in PREFERENCE order. This is the whole rule, provider-owned: the engine keeps
    // only the mechanism (move the chosen card to the graveyard).
    //
    // [heuristic-then-search]: index 0 is what a non-branching caller takes, so returning ONE index
    // decides the discard with no branch (and is byte-identical to the historical single answer);
    // returning several is what lets the search choose among them instead of trusting the ranking.
    //
    // `required_pieces` is the deck's protected combo-piece list. It is passed in rather than read
    // off the state because the executor sources it from its own MulliganProfile while the rollout
    // reads GameState::m_required_pieces -- the same set, but the caller owns which.
    //
    // The base implementation is the engine's historical rule, so a provider that does not override
    // behaves exactly as before:
    //   1. If DiscardLandsFirst (a land outlet makes lands ammunition), the first non-staged LAND.
    //   2. Otherwise the highest-MV non-staged card that is not a protected required piece
    //      (protection scope per DiscardProtectScope; a redundant copy stays discardable).
    //   3. Last resort, when every non-staged card is protected: max-MV overall, staged preferred.
    // Defined out-of-line (DecisionProviders.cpp) because that rule lives in SpellEffects.h, which
    // includes this header.
    virtual std::vector<int> CleanupDiscardCandidates(
        const GameState& s, const std::vector<std::string>* required_pieces) const;

    // FirebreatheActivations -- combat pump ("firebreathing"): how many activations to pay for.
    // Returns candidate counts in PREFERENCE order; index 0 is what a non-branching caller takes.
    // A NEGATIVE entry means "as many as the pool affords" -- the greedy maximum -- which is the
    // default, so this costs no probe and is byte-identical to the historical behaviour.
    //
    // Provider-owned because "spend the whole pool on pump" is an ASSUMPTION, not a rule: it is
    // right for a goldfish with no second main and wrong for a deck that needs to hold mana (a
    // Scourge ping, an instant). Returning several concrete counts is what lets the search decide.
    // The engine keeps the mechanism (which activation, in what order -- ApplyFirebreathing).
    virtual std::vector<int> FirebreatheActivations(const GameState& s) const
    {
        (void)s;
        return std::vector<int>{ -1 };
    }

    // ShouldEmitRiskyAltPayload -- whether to EMIT a risky alt-cost payload (Reverent Silence: free, but its
    // destroy-all-enchantments wipes the caster's own Aria/Remedy) as a searched action.
    // The engine keeps the alt-cost preconditions (alt_lifegain_cost>0 + Forest control)
    // and builds the Action; this is the wipe-vs-value gate. (The forthcoming antilife
    // refinement -- only when lethal or a Plague Drone / 2nd Remedy survives the wipe --
    // lands here.)
    virtual bool ShouldEmitRiskyAltPayload(const GameState& s, int controller,
                                           const CardDefinition& def) const = 0;

    // ShouldStageSpectacleDraw -- whether to enumerate spectacle-staging plan variants for a draw spell
    // (cast a cheap damage spell first to unlock its cheaper Spectacle cost). The engine
    // keeps the variant-building mechanism; this is the archetype gate.
    virtual bool ShouldStageSpectacleDraw(const GameState& s, int controller,
                                          const CardDefinition& draw_def) const = 0;

    // ShouldCastDrawEngine -- whether to CAST a "flood engine" card (Treasure Hunt's DrawUntilNonland,
    // and cascade/retrace cards like Throes of Chaos that can cascade into Treasure Hunt).
    // Firing the engine when the drawn lands cannot be used wastes them: they hit cleanup
    // discard with no Land's Edge to throw them. The engine asks this BEFORE emitting the
    // cast candidate (so the search and the bottoming rollouts both inherit the gate).
    // Generic = always cast (byte-identical); the Treasure-Hunt provider gates on having a
    // payoff this turn (Land's Edge in play, mana to cast Land's Edge after the engine, or
    // a no-max-hand-size land to keep the draw). `def` is the engine card being considered.
    virtual bool ShouldCastDrawEngine(const GameState& s, int controller,
                                      const CardDefinition& def) const = 0;

    // HasExtraLethalModel / ExtraLethalDamage -- deck-specific extra damage toward THIS turn's lethal, BEYOND
    // the generic combat + direct-damage total the engine already sums. This is the Treasure Hunt / Land's
    // Edge model: lands in hand are Land's Edge ammunition, and a clairvoyant Treasure Hunt cast this turn
    // adds the run of lands on top of the library as further ammo. `casting` lists the CardDefinitions this
    // plan casts this turn, so the provider counts a Land's Edge or Treasure Hunt being cast NOW (not only
    // one already on the battlefield). The engine keeps the generic win-check (projected attackers + direct
    // damage + THIS addend >= opp life); only the deck-specific addend is provider-owned.
    // HasExtraLethalModel() is the cheap gate: when false the engine skips building `casting` entirely
    // (byte-identical fast path), so a deck with no such model pays nothing. Generic = false / 0.
    virtual bool HasExtraLethalModel() const = 0;
    virtual int  ExtraLethalDamage(const GameState& s,
                                   const std::vector<const CardDefinition*>& casting) const = 0;

    // ArchetypeCardValue -- archetype-specific per-card VALUE for the candidate-ordering heuristic
    // (TurnSolver's EvalCard), for cards whose worth is a combo / clairvoyant assumption rather
    // than a generic single-card estimate. The Treasure Hunt provider values a Treasure Hunt
    // (clairvoyant count of lands on top of the library x Land's Edge rate) and a Land's Edge
    // (lands-in-hand x rate). `dmg_unit` is EvalCard's damage-equivalent unit. Returns true and
    // sets `out` when the provider owns this card's value; false -> EvalCard uses its generic
    // estimate (so the engine keeps every generic card type). Generic = false.
    virtual bool ArchetypeCardValue(const GameState& s, const CardDefinition& def,
                                    int dmg_unit, int& out) const = 0;

    // CastOrderRank -- cast-order rank for a non-sacrifice hand cast (LOWER = cast earlier). The
    // engine stable-sorts the turn's hand casts by this rank, so the RELIABLE ordering rules
    // live here as a heuristic instead of in an expensive (and budget-diluting) permutation
    // search. The point is to make the canonical line REALISE what EnumeratePlans already
    // projects -- e.g. prowess. Generic ranks (all provably non-negative in a goldfish):
    //   * creatures (10) before noncreature spells (20): a haste prowess creature cast BEFORE
    //     a noncreature spell catches that spell's prowess trigger and attacks bigger; casting
    //     it after wastes the pump (the executed order, not the SET, decides this).
    //   * on-cast SELF-damage sources (30, LAST): an Eidolon of the Great Revel cast after this
    //     turn's other MV<=3 spells avoids them triggering its self-ping (the spells already
    //     resolved before it entered). Same casts either way, so only the ORDER changes life.
    // Archetypes override (antilife: enablers rank 0 so a same-turn payload sees the flip).
    virtual int CastOrderRank(const GameState& s, const CardDefinition& def) const = 0;

    // XCandidates -- candidate X values for an {X} spell (a branching-PRUNE heuristic). The engine
    // asks BEFORE emitting cast variants and emits one cast per returned value (the variants
    // share hand_index, so they are mutually exclusive in the plan), letting the search pick
    // among the narrowed set. `max_affordable` is the largest X the current mana can pay (spare
    // mana after the base cost). Generic proposes {max_affordable} (goldfish-optimal: an X burn
    // / X effect wants all available mana); MTG_UNPRUNED opens the full 1..max_affordable range
    // for the search to confirm. Empty -> the spell is not cast this turn. See analyze-deck 5f.
    virtual std::vector<int> XCandidates(const GameState& s, const CardDefinition& def,
                                         int max_affordable) const = 0;

    // ShouldAttackWith -- combat: should this eligible creature be DECLARED as an attacker this turn?
    // The engine keeps combat eligibility (CanAttackFull: summoning sickness, tap state,
    // haste) and the damage MECHANISM; this is only the attack/hold DECISION over an
    // already-eligible attacker. Generic = true (attack with everything that can attack --
    // correct for a goldfish with no blockers). An archetype overrides to HOLD a creature
    // back (e.g. a creature whose {T} activated ability beats attacking). Honoured in
    // lockstep by the real declaration (AIEngine::DeclareAttackers) and every search-side
    // attack projection (PendingAttackDamage / prowess count / rollout combat), so an
    // override never makes the search predict an attack the executor won't make.
    virtual bool ShouldAttackWith(const GameState& s, const Permanent& attacker) const = 0;

    // PostDrawKeepLandName -- which land to play AFTER a deferred draw-engine (Treasure Hunt) resolves and
    // the draw is known. Returns the NAME of a card in hand to play as the deferred land drop:
    // the Treasure-Hunt provider returns a drawn no-max-hand-size land (Reliquary Tower) when the
    // hand is flooding and no such land is already in play, so the whole draw is KEPT as Land's
    // Edge ammo (gi=65); "" means "play the best normal land via the engine's generic land-play".
    // The engine keeps the land-play MECHANISM (PlayLandByName / breakpoint land + recording) and
    // the open-land-drop precondition; only this card-choice is provider-owned. Generic = "".
    virtual std::string PostDrawKeepLandName(const GameState& s, int controller) const = 0;

    // SituationalCardRank -- SITUATIONAL card rank: "how much do I want THIS card on THIS turn" (HIGHER =
    // more wanted). Unlike ScryKeepOnTop (a binary keep-on-top / bottom gate) this is a continuous
    // priority that lets the dig spells (Expressive Iteration, Ponder, Preordain) ORDER their
    // looked-at cards DETERMINISTICALLY -- most-wanted to hand/top, least-wanted to bottom -- instead
    // of leaving the selection to a search branch. The decisive nuance is that situational NEED
    // overrides static card power: a land ranks at the top on a turn you need the land drop, even
    // though it is a generically weak card; once lands are covered the missing combo pieces outrank
    // the digging cantrips, which outrank the dead/duplicate payoffs. The engine keeps the selection
    // MECHANISM (which slot each ranked card goes to); only the ranking is provider-owned.
    //
    // Default (here) mirrors ScryKeepOnTop (keep -> 1, bottom -> 0) so every non-overriding deck
    // keeps its current 2-level ordering byte-identical (a stable_sort by a 2-valued key is a no-op
    // within each group). Only HinataProvider overrides it with the real situational ranking, and
    // re-expresses its ScryKeepOnTop in terms of this rank (single source of truth).
    virtual int SituationalCardRank(const GameState& s, const Card& card) const
    {
        return ScryKeepOnTop(s, card) ? 1 : 0;
    }

    // PreferHoldLandDrop -- land-banking: among EQUAL-VALUE plans, prefer HOLDING the land drop (play no
    // land) over developing it. Burn banks spare lands once it has enough mana (its curve tops at MV 2) so a
    // future topdecked Searing Blaze has a land to play for its landfall (3-to-face instead of 1). This only
    // INVERTS the develop tiebreak in EnumeratePlansWithLand -- it never reorders plans of DIFFERENT value,
    // so on a turn that actually casts Blaze the land drop (which raises the plan's value via landfall) still
    // wins on value, not the tiebreak. Honoured identically in the search and the rollout (both call
    // EnumeratePlansWithLand). DEFAULT false -> every other deck always develops (byte-identical); only
    // BurnProvider opts in, gated on lands-in-play.
    virtual bool PreferHoldLandDrop(const GameState& s, int controller) const { return false; }

    // HoldDeferredDropForLethal -- after a deferred Treasure Hunt (DrawUntilNonland) resolves, HOLD the
    // still-open land drop entirely rather than developing it, because the lands now in hand are the marginal
    // Land's Edge ammunition for a lethal THIS turn. Generically the engine plays the deferred drop
    // (play_drawn_flood_keep_land), but with Land's Edge in play a land in HAND is worth `rate` damage this
    // turn; playing it as the drop removes it from the ammo pool and can drop the count below lethal -- and
    // the fire-count heuristic (LandsEdgeHeuristicFireCount) then HOLDS the rest, slipping the kill a full
    // turn (the s1 gi0 T4-vs-T3 shortfall: 10 lands in hand -> play one -> 9 -> no-longer-lethal -> hold ->
    // win T4 instead of T3). Return true only when the hand is ALREADY lethal ammo and developing the drop
    // would push it BELOW lethal (the marginal case), so the subsequent auto-fire discards them all for the
    // kill. DEFAULT false -> every other deck (and every non-marginal TH turn) develops the drop
    // byte-identically. The engine keeps the land-play MECHANISM and the open-drop precondition; only this
    // hold decision is provider-owned.
    virtual bool HoldDeferredDropForLethal(const GameState& s, int controller) const { return false; }

    // HoldDeferredDropForFurtherDig -- after a deferred Treasure Hunt (DrawUntilNonland) resolves with the
    // drop still open and NO flood-keep land revealed (PostDrawKeepLandName returned ""), HOLD the drop
    // instead of developing it when ANOTHER dig is affordable this turn. The generic fallback develops the
    // best normal land immediately (gi=881: an undeveloped drop meant a drawn land was discarded) -- correct
    // for a turn with ONE dig, wrong for a turn with more digs to come: the drop is the ONLY way to play a
    // Reliquary Tower, so spending it after dig 1 means a Reliquary revealed by dig 2 cannot be played and
    // the whole flood is discarded at cleanup (the s2 gi1 T4-vs-T5 shortfall). Holding costs nothing
    // structural: this same step runs again after the next dig and develops then if no keep land shows up.
    // Return true only when the hand is ALREADY flooding, no no-max-hand-size land is in play, and another
    // dig is payable from mana available WITHOUT the held land (so holding cannot starve the dig it is
    // waiting for). DEFAULT false -> every other deck develops byte-identically. The engine keeps the
    // open-drop precondition and the land-play mechanism; only the hold is here.
    virtual bool HoldDeferredDropForFurtherDig(const GameState& s, int controller) const { return false; }

    // NcLandDropTempoBonus -- NON-CLAIRVOYANT search tempo bonus (avg win-turns) for MAKING a land drop this
    // turn. The reshuffle-averaged NC search (TurnSolver::ReshuffleAvgChoosePlan) is mana-OPTIMISTIC: it
    // shuffles the true library away, so its mean future has normal land density and it undervalues a land
    // drop as screw-insurance -- it will DEFER the drop (a fetch-crack costs 1 life) and durdle in a
    // genuinely land-light game. This bonus prices developing mana back: it is subtracted from the averaged
    // win-turn of any land-drop plan before the min is taken, so it breaks decisions the objective considers
    // close WITHOUT overriding a real win-turn difference larger than the bonus. DEFAULT 0.0 -> inert (only
    // used inside the experimental MTG_NC_SEARCH path anyway). GenericProvider supplies the SAFE conservative
    // rule (small bonus, only while still building the mana base, and never when PreferHoldLandDrop wants to
    // bank/hold); archetypes that specifically benefit (mana- hungry, no land-as-resource mechanic) override
    // to be more aggressive. Land-pitch decks (Land's Edge / Seismic Assault) are protected by the mana-base
    // gate + PreferHoldLandDrop.
    virtual double NcLandDropTempoBonus(const GameState& s, int controller) const { (void)s; (void)controller; return 0.0; }

    // OpponentPlaysLands -- does this deck's goldfish opponent play lands? Decks whose spells target the
    // OPPONENT'S permanents for value (Hinata: Magma Opus taps them, the spread-damage / cost-
    // reduction targeting points at them) need a realistic opponent board. When true the engine
    // gives the passive opponent one land on each of the first three turns (a realistic floor:
    // most opponents have >=3 lands, and aggressive decks with fewer bring creatures = better
    // targets anyway). DEFAULT false -> every existing deck's opponent stays boardless-of-lands
    // (byte-identical); only HinataProvider opts in. NOT pure (defaulted) so no other provider
    // needs to implement it.
    virtual bool OpponentPlaysLands() const { return false; }

    // UseLethalShortCircuit -- opt IN to the board-lethal search short-circuit (Solve + EnumeratePlans):
    // when the current board's attack-all damage already kills the opponent this turn, skip the cast-subset
    // odometer and just attack. WIN-TURN-INVARIANT (winning this turn is the min win-turn), but it changes
    // WHICH winning plan is chosen -> the play log / digest differs. DEFAULT false so every deck's play stays
    // byte-identical; a provider opts in when the modest rollout speedup is worth re-accepting ITS play
    // digest (GoblinsProvider). Off-switch MTG_NO_LETHAL_CUT disables it even where opted in.
    virtual bool UseLethalShortCircuit() const { return false; }

    // DeferSacOutletPreCombat -- should a Goblin-style creature-sac OUTLET (Siege-Gang / Pashalik value
    // sacs, Skirk Prospector's sac-for-mana) be DROPPED from the PRE-COMBAT action enumeration and left
    // to the second (post-combat) main? Vs the passive goldfish opponent a VALUE sac is >= as good AFTER
    // attacking (you keep the attack), so deferring the value outlets is near-lossless while removing
    // them from the O(2^candidates) pre-combat cast-subset explosion on a wide Goblin board. Skirk's MANA
    // outlet is gated, not blanket-deferred: its pre-combat float only buys tempo the second main can't
    // recover when it funds a SAME-TURN attacker (needs haste), so the Goblins provider keeps it pre-combat
    // only when a Goblin haste lord is in play or castable from hand. `is_mana_outlet` distinguishes the two.
    // DEFAULT false -> every non-Goblins deck enumerates sac outlets pre-combat byte-identically. NOT pure
    // (defaulted) so no other provider needs to implement it. Measured quality-neutral (Goblins d3/400-game
    // 4.4375->4.4350) + 2.5-4.6x faster on the Skirk-amplified rollout outliers. See analysis-goblins.md.
    virtual bool DeferSacOutletPreCombat(const GameState& s, const Permanent& src,
                                         bool is_mana_outlet) const
    { (void)s; (void)src; (void)is_mana_outlet; return false; }

    // PayEchoToKeep -- when an echo obligation comes due and the cost is AFFORDABLE, PAY (keep the body,
    // return true) or DECLINE (sacrifice it, return false)? The engine owns the affordability gate and the
    // sacrifice/OnCreatureDies mechanism; only this pay-vs-decline JUDGEMENT is provider-owned so the
    // executor (AIEngine) and the rollout (TurnSolver) share ONE decision function -> lockstep by
    // construction. DEFAULT reproduces the historical fixed heuristic verbatim: a self-replacing body
    // (dies_watch_includes_self + a death token, e.g. Mogg War Marshal) DECLINES (the death token replaces
    // it, saving the mana); every other echo creature (Stingscourger) PAYS. So every deck is byte-identical
    // until a provider overrides -- GoblinsProvider adds the lethal/no-gas keep exceptions for Mogg.
    virtual bool PayEchoToKeep(const GameState& s, const Permanent& p) const
    {
        (void)s;
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { return true; }
        const CardParams& ep = d->params;
        const bool self_token = ep.dies_watch_includes_self && ep.dies_trigger_creates_tokens > 0;
        return !self_token;
    }

    // ShouldEmitUntapRitual -- emit the untap-RITUAL cast variant for an {X} untap spell (Reality Spasm)?
    // The variant floats mana for a same-turn payoff and only earns its keep with Hinata's
    // discount making the {X} free, so the solver must NOT branch on it otherwise. This is the
    // archetype GATE only -- the cost math and the ManaSourceCount stay engine-side. Was an inline
    // `HinataInPlay(state)` check in TurnSolver (audit B2); default false = byte-identical for every
    // non-Hinata deck, HinataProvider returns HinataInPlay(s).
    virtual bool ShouldEmitUntapRitual(const GameState& s) const { (void)s; return false; }

    // BranchSoulfireOwnTargets -- branch on Soulfire Eruption's OWN-creature target count (0..K)?
    // Own-targeting only pays off with Hinata's per-target discount (which can enable an
    // otherwise-unaffordable cast) plus a deeper dig; without it the K+1 variants are dead weight every
    // non-combo turn. The count itself (SoulfireOwnCreatureCount) stays an engine mechanic -- this is the
    // archetype gate only. Was an inline `HinataInPlay(state)` check in TurnSolver (audit B1); default false
    // = byte-identical (K collapses to 0), HinataProvider returns HinataInPlay(s).
    virtual bool BranchSoulfireOwnTargets(const GameState& s) const { (void)s; return false; }

    // EnumGroupCap -- enumeration BREADTH policy: the max number of card GROUPS the plan enumerator
    // keeps for a turn (groups beyond this, lowest by SituationalCardRank, drop out). A tractability
    // cap -- a deep dig can leave ~20 distinct nonland casts whose powerset dominates the whole
    // search -- so it is provider-OWNED policy now rather than a hardcoded solver constant (audit
    // A1): a combo deck that needs wider enumeration can raise it. Default 12 = the prior generic
    // value (byte-identical; inert for any hand with <= cap groups). MTG_SOLVE_GROUP_CAP /
    // MTG_NO_GROUP_CAP / MTG_UNPRUNED still override engine-side for A/B.
    virtual int EnumGroupCap() const { return 12; }

    // FetchSearchCap -- fetch BREADTH policy: how many of FetchCandidates' ordered targets the search
    // branches on (the list is best-first; lower ranks are strictly worse colour, a basic ranks
    // last). Provider-OWNED (audit A2) instead of a hardcoded solver constant. Default 2 = the prior
    // generic value (byte-identical). MTG_UNPRUNED still opens the full list engine-side.
    virtual int FetchSearchCap() const { return 2; }

    // TutorSearchWidth -- tutor BREADTH policy: how many of TutorCandidates' ordered targets the
    // post-dedup tutor axis actually scores, INCLUDING the provider's best (so 1 == the old
    // heuristic-only behaviour, byte-identical). Sibling of FetchSearchCap, and provider-owned for a
    // measured reason rather than a symmetry one: the axis is ADDITIVE (cost P+W, one rollout per
    // extra target), so unlike a multiplicative width it does not dilute the budget the same way --
    // and the per-deck optima genuinely DIVERGE. A deck whose tutor fetches a combo piece wants to
    // look deep; a deck whose tutor is a value grab is best served by its own top pick, because the
    // targets past rank ~2 are ones the provider already judged worse and the variants only spend
    // budget the rest of the turn wanted. Default 6 = the prior global constant.
    // MTG_TUTOR_WIDTH still overrides engine-side for A/B. See docs/design/searched-action-subdecisions.md.
    virtual int TutorSearchWidth() const { return 6; }

    // CleanupDiscardSearchWidth -- how many of CleanupDiscardCandidates' ranked cards the ROLLOUT's
    // end-of-turn cleanup branches over (1 == the ranked pick only, byte-identical to no branch).
    //
    // The EXECUTOR already searches this decision; the rollout did not, so every line the search
    // scored assumed the heuristic's shed. Provider-owned and DEFAULT 1 because the decision is
    // dead in most decks -- per 400 d0 games, five of nine suite decks never reach a cleanup
    // discard at all and three more are under 40 -- so a global width would buy plan variants that
    // pin an index nothing ever consumes. A deck that actually makes this decision opts in.
    virtual int CleanupDiscardSearchWidth() const { return 1; }

    // ManaSourceRank -- mana-source TAP ORDER: flexibility rank of a mana source (LOWER = tap earlier).
    // The greedy mana solver (AIEngine::TapForCost / TurnSolver::TapForCostDirect, scarcity path)
    // pays each pip from the lowest-ranked qualifying source, so the flexible sources stay up and the
    // exponential TapForCostBacktrack fallback is rarely entered. This is a QUALITY heuristic for a
    // sub-decision the search does NOT branch over (searching tap orderings is the very blowup we
    // avoid); tapping is always a single committed choice, so this only picks WHICH legal payment --
    // never whether one is found (the complete backtracker remains the fallback). Provider-owned so a
    // deck can override (e.g. a filter's float-colour preference). GenericProvider supplies the
    // default (basic/bounce 10, dual 20, filter 25, tri 30, rainbow 50, {C}-only manland 60). Not
    // pure-virtual-defaulted here because the ranking needs SpellEffects helpers unavailable in this
    // header; GenericProvider implements it and every archetype inherits that.
    virtual int ManaSourceRank(const GameState& s, const CardDefinition& def) const = 0;

    // OpponentLifegainUseful -- is making the OPPONENT gain life USEFUL to us right now? A
    // Grove-of-the-Burnwillows- style drip land (tap_opponent_lifegain) normally GIFTS the opponent life -- a
    // downside -- so the engine dodges the drip: a generic pip uses Grove's painless {C} mode and leftover
    // drip lands are NOT swept at end of main. A deck whose combo turns that gift into value flips this true,
    // and the two drip rules invert: Grove taps COLOURED (drips) even for a generic pip, and the sweep fires.
    // Anti-Lifegain returns true when a Tainted Remedy / Plague Drone is active (the gain is reversed into 1
    // damage); a Grove + Punishing Fire deck would return true too (the gain buys the Fire back). Default
    // false. NOTE: the rules-level lifegain->loss reversal in OpponentGainsLife stays keyed on RemedyActive
    // -- that is a FACT about the board, not a decision, so it is not routed through here.
    virtual bool OpponentLifegainUseful(const GameState& /*s*/, int /*controller*/) const { return false; }

    // KeepFloor -- keep-floor: an archetype override that can FORCE the mulligan keep decision for a
    // hand the exhaustive keep table (or static rule) would otherwise misjudge. Consulted in the play
    // path (AIEngine::HandleMulligan) BEFORE the table, so ForceKeep overrides a table mulligan and
    // ForceMulligan overrides a table keep; Undecided (the default) falls through unchanged. This is a
    // provider HEURISTIC (empirically-backed, not a rules fact) -- e.g. the Treasure Hunt archetype
    // force-keeps a castable-TH hand that also holds a flood payoff. Base returns Undecided so every
    // deck (and the analyzer's reference/static keep path, which never consults a provider) is
    // byte-identical; only an overriding archetype changes any decision.
    virtual KeepGuard KeepFloor(const std::vector<Card>& /*hand*/, int /*mulligan_count*/,
                                bool /*on_the_play*/) const { return KeepGuard::Undecided; }

    // UseAccelPrefixCollapse -- Dragonstorm acceleration-prefix collapse gate. When true, TurnSolver's Solve
    // / EnumeratePlans odometer enumerates only the K+1 CHEAPEST-FIRST PREFIXES of the ritual accelerants
    // (actions with ritual_floating_mana > 0: Rite of Flame, Pyretic/Desperate Ritual, Seething Song,
    // Irencrag Feat) instead of their full 2^K powerset (see GroupChoiceNonPrefixAccel). A self-funding
    // ritual chain is cheapest-first-optimal -- for any storm count j the cheapest j accelerants dominate
    // every other size-j subset (same storm count, >= mana) -- so the reachable (storm, mana) frontier is
    // preserved while the go-off hand's combinatorial straggler collapses to linear. This is a HEURISTIC (it
    // changes which action masks are enumerated -> NOT byte-identical), so it lives in the archetype provider
    // and is opened by MTG_UNPRUNED (UnprunedGate::AccelPrefix) for the standing pruned-vs-unpruned A/B,
    // exactly like EnumGroupCap / SituationalCardRank. Base returns false -> every non-Dragonstorm deck (and
    // Dragonstorm under MTG_UNPRUNED) stays byte-identical; only DragonstormProvider opts in. See
    // docs/design/dragonstorm-search-pruning.md (Step 2).
    virtual bool UseAccelPrefixCollapse() const { return false; }

    // UseSpliceCollapse -- Desperate Ritual SPLICE-count collapse gate. When true, TurnSolver's
    // CollectActions emits only TWO splice variants per same-named splice_onto_arcane copy -- the BARE cast
    // (k=0) and the position's MAX-CHAIN cast (k = N-1-pos, N = copies in hand) -- instead of the full
    // k=0..N-1 fan-out, and the odometer keeps only PREFIX selections of one family (bare-prefix OR max-chain
    // prefix) via SpliceCollapseViolated. Splicing keeps the revealed copies in hand and adds no storm, so
    // splice-then-cast is net-positive mana at the SAME eventual storm count -- the maximal splice chain
    // {N-1,...,N-m} dominates every other size-m assignment on mana (user's directive: "if you can afford to
    // splice you should; only search the bare line as a fallback when the spliced line can't be cast").
    // Collapses the go-off hand's ~N^N splice-count powerset (the residual atom after the accel / Lotus
    // prefix collapses) to the 2 dominant families. HEURISTIC (it drops the intermediate splice assignments
    // -> NOT byte-identical), so it lives in the archetype provider and is opened by MTG_UNPRUNED
    // (UnprunedGate::SpliceCollapse) for the standing A/B, exactly like AccelPrefix. Base returns false ->
    // every non-Dragonstorm deck (and Dragonstorm under MTG_UNPRUNED) enumerates the full splice fan-out
    // unchanged; only DragonstormProvider opts in.
    virtual bool UseSpliceCollapse() const { return false; }

    // WantsCastOrderingSearch -- cast-ORDERING search gate. When true, EnumeratePlansWithLand expands each
    // action set into the DISTINCT orderings of its non-sacrifice hand casts (deduped by end-of-phase state)
    // and the search scores each, committing the best via Plan::searched_order (the executor replays that
    // vector order -> lockstep). The archetype opt-in for the global MTG_SEARCH_ORDER knob. Dragonstorm's
    // combo turns leave a lot on the table under the fixed CastOrderRank (rituals@15/Irencrag@18/payoff@20)
    // -- a specific interleave (fund -> reducer -> discounted rest, or a Dragon hard-cast between rituals)
    // routinely beats the canonical bucket -- so letting the search FIND the order recovers it. Measured
    // (regression): d3 5.56->4.95, d5 5.36->4.82 (~0.55 turns) at ~+47% makespan. EXPENSIVE (applies each
    // tried ordering on a copy, k! capped at 120), so it lives in the archetype provider; base returns false
    // -> every non-Dragonstorm deck stays byte-identical. Also openable globally via MTG_SEARCH_ORDER /
    // MTG_UNPRUNED (UnprunedGate::SearchOrder) for the standing A/B.
    virtual bool WantsCastOrderingSearch() const { return false; }

    // PrunesAcceleratorWithoutPayoff -- payoff-prune gate (the ritual-guard's search-side analog; the user's
    // spec). A mana ritual is a ONE-TURN accelerant: its float empties at end of turn (identical to Hinata's
    // Reality Spasm untap). So a plan that casts a ritual but no PAYOFF -- a Dragon (creature), Dragonstorm
    // (tutor_to_battlefield), or Apex of Power (impulse_exile) -- burns the ritual for nothing (the mana has
    // no same-turn sink; storm count doesn't carry across turns). When true, both Solve::consider (leaf) and
    // EnumeratePlans (search branch list) DROP those accelerant-only subsets, focusing the search budget on
    // payoff lines. Deliberately NOT enabled for Hinata: there the ritual IS a useful mid-combo accelerant
    // (it powers a bigger cantrip/dig turn), so the same prune measured -0.05 tempo (see
    // docs/design/hinata-spasm-gate-rootcause.md). Dragonstorm has no such mana sink, so the prune should
    // net-help here. Base returns false -> byte-identical; only DragonstormProvider opts in.
    virtual bool PrunesAcceleratorWithoutPayoff() const { return false; }

    // CastCheapestFirstWithinTier -- within one CastOrderRank tier, cast same-tier MANA ACCELERANTS
    // cheapest-first by the action's ACTUAL cost. A ritual chain funds itself cheapest-first, so a dearer
    // accelerant attempted before a cheaper one may be unpayable and is then SILENTLY DROPPED
    // (CastSpellFromHand returns void) -- stranding the mana it would have produced and leaving the payoff
    // short (Dragonstorm d0 seed 8585 gi1578: led with Seething Song, floated 7, could not pay Dragonstorm's
    // 9, whole chain burned). Default ON (root). An earlier measurement had this default OFF "because at the
    // ROOT it regressed slivers/Hinata/Anti-Lifegain/Knights and cost 2 searched slowdowns" -- that
    // measurement predates the IsManaRitual restriction in CastOrderLess and was really measuring the
    // tie-break reordering CREATURES (Scourge/Lathliss ETB order), which cost is the wrong key for.
    // Restricted to accelerants it is inert for every deck that never holds two of them at once, so it is a
    // generic rule, not archetype tuning. A provider may still override to false;
    // MTG_LEGACY_CAST_TIER_ORDER=1 is the global hatch for a byte-identical A/B.
    virtual bool CastCheapestFirstWithinTier() const { return true; }

    // Float-colour collapse for "add N mana of ANY ONE colour" effects (HEURISTIC, provider-owned; NOT
    // byte-identical). These effects emit one cast/sac variant PER candidate colour, and with several Lotus
    // Blooms + Apex in play the per-colour fan-out is the top enumeration driver (branch-stats: up to
    // ~590k plans in a single node). For a red-primary archetype the off-colours are almost never worth it:
    //   * ImpulseFloatColorRedOnly: Apex of Power always floats RED -- its mono-red chain never needs
    //     another colour, and one colour can't fund a multicolour card by itself.
    //   * RestrictSacColorsToHasteAndRed: Lotus Bloom floats RED unless a HASTE creature castable THIS turn
    //     (in hand, which includes Apex-staged exile cards) demands an off-colour -- the only time an
    //     immediate off-colour Dragon (Karrthus/Kolaghan) is worth the sacrifice. Most turns => RED only.
    // Both open to the full candidate set under MTG_UNPRUNED(SacColor). Base returns false -> the old
    // library-scoped, all-colour behaviour, byte-identical for every other deck.
    virtual bool ImpulseFloatColorRedOnly()       const { return false; }
    virtual bool RestrictSacColorsToHasteAndRed() const { return false; }

    // ScaledCastVariants -- SCALED-CAST variants: for a spell whose cost depends on how much output it
    // commits, the candidate (opponent-face damage, cost) levels to branch on. This is the DIVIDED-damage
    // analogue of XCandidates: Magma Opus deals 4 damage divided among any number of targets, and committing
    // more of it to the opponent's FACE leaves fewer distinct spread/tap targets for Hinata's per-target
    // discount -- so more face costs more mana, exactly like paying more for a bigger {X}. The engine emits
    // one mutually-exclusive cast per returned variant and the plan enumerator picks per affordability +
    // value, so with several scaling spells in hand (a Crackle {X} + a Magma face) the SEARCH allocates the
    // spare mana across them (Crackle's X moves in 3-mana steps, Magma's face in 1-mana steps, so the
    // fine-grained face soaks up mana the coarse X cannot). The archetype owns the WHOLE model -- which
    // levels, and what each costs; every card-specific number lives in the provider -- while the engine keeps
    // only the emit/thread/resolve MECHANISM (the committed face rides to resolution and is dealt to the
    // opponent). Base returns {} -> the spell's normal single-line cast (byte-identical for every other
    // deck/card); only an archetype with a scaling card returns variants, and only when its model is enabled.
    // The search still picks by default; a provider may narrow the set as a heuristic (like XCandidates
    // does).
    virtual std::vector<ScaledCastVariant>
    ScaledCastVariants(const GameState& /*s*/, const CardDefinition& /*def*/) const { return {}; }

    // ---- Ported engine built-ins (2026-08-01 audit) ------------------------------------------
    // Each of the hooks below replaces a rule that was hard-coded in the engine with no provider
    // involvement at all -- found by auditing the g_play_* human-play choosers, which mark exactly
    // the points where a human must be able to override an engine default. Every base
    // implementation reproduces the historical rule EXACTLY, so adding them is byte-identical; the
    // point is ownership first, review and branching second (see
    // docs/design/engine-heuristics-to-providers.md).

    // CombatCheatCandidates -- Goblin Lackey: "whenever this deals combat damage to a player, you MAY
    // put a <subtype> permanent card from your hand onto the battlefield." WHICH card (or decline) is
    // a real choice, and it is NOT a cast: it is free, and it resolves during the combat-damage step,
    // so the put permanent is summoning-sick and cannot attack this turn. That makes it the same
    // KIND of decision as an Aether Vial deploy -- "which permanent do I want on board going
    // forward" -- rather than "what is the biggest thing I can sneak in".
    //
    // Returns hand indices in PREFERENCE order; index 0 is what a non-branching caller takes, so
    // returning ONE index decides it with no branch. Empty == decline the "may".
    //
    // The base implementation is the engine's historical rule -- highest MV, ties by power, then by
    // card number -- so a provider that does not override behaves exactly as before. Highest-MV is a
    // reasonable default and is usually right; it is offered FIRST precisely so that a search over
    // this axis inherits it as the tie-break winner (defect class 3: strict improvement means the
    // first-enumerated option owns every tie).
    virtual std::vector<int> CombatCheatCandidates(
        const GameState& s, int controller, const CardDefinition& source,
        const std::vector<int>& hand_indices) const;

    // EtbDigCandidates -- ETB "look at the top N, you may reveal a <subtype> card and put it into your
    // hand" (Acclaimed Contender). Returns indices INTO `examined` in PREFERENCE order, restricted to
    // `legal` (the subtype-matching looks); index 0 is what a non-branching caller takes. Empty ==
    // take nothing (it is a "may").
    //
    // The base implementation is the historical rule: the FIRST legal match in look order. That rule
    // is poor and is retained only for byte-identity -- look order is library order, i.e. shuffle
    // order, so which Knight you take is effectively random among the matches. It is the same defect
    // as "first land in hand order" in the cleanup-discard rule: a ranking that is really insertion
    // order. Providers should override; see docs/design/engine-heuristics-to-providers.md.
    virtual std::vector<int> EtbDigCandidates(
        const GameState& s, int controller, const std::vector<Card>& examined,
        const std::vector<int>& legal) const;

    // ReplicateCounts -- Hatchery Sliver replicate: how many token copies to pay for on cast.
    // Returns candidate counts in PREFERENCE order; index 0 is what a non-branching caller takes. A
    // NEGATIVE entry means "as many as the pool affords" (greedy max), which is the default.
    //
    // Provider-owned because greedy-max is a PER-DECK fact, not an engine truth. It is right for
    // slivers -- a deck of one-drops and lords, where an extra body is almost always the best use of
    // the mana -- and that is the deck that has the card today. It need not hold for a deck whose
    // replicate target competes with an expensive payoff. Unlike firebreathing (whose pool is a
    // by-value copy, making greedy-max provably dominant), replicate taps REAL sources:
    // MTG_REPLICATE_TRACE measured 59 of 189 replicate events squeezing at least one hand card out of
    // affordability, so the pool is genuinely contended.
    virtual std::vector<int> ReplicateCounts(const GameState& /*s*/,
                                             const CardDefinition& /*def*/) const
    {
        return std::vector<int>{ -1 };
    }

    // ---- Remaining engine built-ins, ported byte-identically -----------------------------------
    // Every hook below returns candidates in PREFERENCE order with index 0 equal to the historical
    // inline pick, so a non-branching caller is unchanged. Several of these base rules are known to
    // be weak (see the individual notes and engine-heuristics-to-providers.md); they are preserved
    // as-is here because the port must not move play. Fixing them is the next step, per rule.

    // LightPawsAuraCandidates -- Light-Paws, Emperor's Voice: which Aura the on-cast trigger fetches
    // onto the battlefield attached to her. `legal` is library indices already filtered to the legal
    // fetches (MV <= the cast Aura's, a name you do not already control, enchant restriction
    // satisfied); return them ranked, or {} to decline (it is a "may search").
    //
    // Provider-owned because this is a TUTOR, and every other tutor in the engine already routes
    // through TutorCandidates -- this one was the exception. The base rule ranks by the power the
    // Aura would REALIZE if attached now (scaling Auras counted against the live board), which is a
    // genuinely good rule; it is deck-agnostic and stays the default.
    virtual std::vector<int> LightPawsAuraCandidates(
        const GameState& s, int controller, const Permanent& lightpaws,
        const std::vector<int>& legal) const;

    // RetraceDiscardCandidates -- retrace (Throes of Chaos) casts from the graveyard by discarding a
    // LAND as an additional cost. Which land, as hand indices in preference order.
    //
    // The base rule is the historical FIRST land in hand order -- i.e. insertion order, the same
    // arbitrary-ranking defect as rule 1 of the cleanup discard. Lands are not fungible; this wants
    // a real ranking (ManaSourceRank is already provider-owned and is the obvious input).
    virtual std::vector<int> RetraceDiscardCandidates(
        const GameState& /*s*/, int /*controller*/,
        const std::vector<int>& hand_land_indices) const { return hand_land_indices; }

    // SacrificeLandCandidates -- "as an additional cost to cast this spell, sacrifice a land"
    // (Shard Volley). Battlefield indices in preference order.
    //
    // Base rule: the first TAPPED land (already spent this turn, so sacrificing it costs no mana
    // now), else the first land. The tapped-first half is sound; the fallback is hand-order
    // arbitrary again.
    virtual std::vector<int> SacrificeLandCandidates(
        const GameState& s, int controller, const std::vector<int>& land_indices) const;

    // BounceLandCandidates -- a Karoo bounce land (Izzet Boilerworks) returns one of your lands to
    // hand as it enters. Battlefield indices in preference order; never empty (the bounce is
    // mandatory, and the Karoo itself is the forced fallback when it is the only land).
    //
    // Base rule, lexicographic: never bounce another Karoo (replaying it just triggers another
    // bounce), prefer an already-TAPPED land (spent this turn -> no mana lost), prefer one that
    // re-enters UNTAPPED. That is a well-reasoned rule; it is provider-owned for ownership, not
    // because it looks wrong.
    virtual std::vector<int> BounceLandCandidates(
        const GameState& s, int controller, int self_index,
        const std::vector<int>& legal) const;

    // LegendKeepIndex -- legend rule (CR 704.5j): you control two or more legendary permanents with
    // the same name, so you CHOOSE one to keep and the rest go to the graveyard. `duplicates` is the
    // battlefield indices of one such same-name group, ascending; return the index to KEEP.
    //
    // The base rule keeps the OLDEST (lowest battlefield index). The rules make this the
    // controller's choice for a reason: the copies are not interchangeable once one carries an aura,
    // counters, or damage, or once one is summoning-sick and another is not.
    virtual int LegendKeepIndex(const GameState& /*s*/, int /*controller*/,
                                const std::vector<int>& duplicates) const
    {
        return duplicates.empty() ? -1 : duplicates.front();
    }

    // LandEntersUntapped -- a land that offers "pay a cost, or enter tapped": a shock land
    // (etb_pay_life_to_untap) or a reveal land (etb_untap_reveal_subtypes). `heuristic` carries the
    // engine's answer (shock: pay whenever mana is needed this turn and life allows; reveal: reveal
    // whenever able). Return true to enter untapped.
    //
    // Base returns `heuristic` unchanged, so this is byte-identical and applies uniformly to the
    // enumeration predicate as well as the real land drop -- the two MUST agree or the plan's mana
    // and the realised mana diverge.
    virtual bool LandEntersUntapped(const GameState& /*s*/, const CardDefinition& /*def*/,
                                    bool heuristic) const { return heuristic; }

    // BurnCreatureTargetCandidates -- a creature-targeting burn that carries a "when that creature
    // dies" rider (Searing Blood: 2 to a creature, then 3 to its controller if it dies). Opponent
    // battlefield indices in preference order.
    //
    // Base rule: prefer a creature this spell KILLS (EffectiveToughness <= damage) so the rider
    // fires, else the first opponent creature. Sound for a goldfish, where which creature dies is
    // irrelevant beyond enabling the rider -- revisit against a real opponent.
    virtual std::vector<int> BurnCreatureTargetCandidates(
        const GameState& s, int active, int damage,
        const std::vector<int>& opp_creatures) const;

    // LifegainRemovalCandidates -- controller-lifegain removal (Swords to Plowshares). Its rider
    // makes the EXILED creature's controller gain life equal to its power, which a Tainted Remedy /
    // Plague Drone turns into that much life LOSS -- so the base rule targets the opponent's
    // LARGEST-power creature.
    //
    // NOTE: the WHETHER-to-cast gate ("only while a Remedy enabler is in play") deliberately stays in
    // the engine helper, not here: it is a castability precondition shared in lockstep by the
    // enumeration gate, the rollout, and the executor, and moving it would let a provider desync
    // them. This hook owns only WHICH creature.
    virtual std::vector<int> LifegainRemovalCandidates(
        const GameState& s, int active, const std::vector<int>& opp_creatures) const;

    // OwnPumpTargetCandidates -- a spell that targets YOUR OWN creature (a pump / prowess enabler):
    // which one. Own battlefield indices in preference order. Base rule: highest effective power
    // among creatures that can attack.
    virtual std::vector<int> OwnPumpTargetCandidates(
        const GameState& s, int controller, const std::vector<int>& own_attackers) const;
};

// ---------------------------------------------------------------------------------------------------
// Payoff / ETB-value ordering primitive (shared, deck-agnostic). Given a multiset of card NAMES that
// will enter the battlefield together -- a mass-ETB put list (Dragonstorm) or a within-turn sequence
// of entering permanents -- return them reordered so an "on-other-ETB" trigger sees the MOST later
// entries: beneficial on-other-ETB sources lead, harmful ones trail. STABLE within each band, so the
// caller's own order among order-independent picks is preserved. Param-driven (CardParams), so a NEW
// deck gets correct payoff-ordering just by handing its selected set here, in any order. Names may
// repeat (multiplicity honoured). This is the value/kind-3 ordering; the LEGALITY kind (Daybreak
// Coronet "another aura", topological/K=1) is a SEPARATE mechanism in TurnSolver (aura sequencing).
// See DecisionProviders.cpp for the band rules + the harmful-trigger extension point, and
// docs/design/sequential-plan-evaluation.md.
std::vector<std::string> OrderEntriesByEtbValue(std::vector<std::string> names);
