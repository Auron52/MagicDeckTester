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

#include "../core/GameState.h"
#include "../core/ManaPool.h"
#include "../cards/CardDatabase.h"
#include <string>
#include <vector>

class DecisionProvider
{
public:
    virtual ~DecisionProvider() = default;

    // Hook 1 -- tutor priority: ordered library card-name candidates for a tutor
    // (Idyllic / Enlightened). 1 = decided, >1 = search picks, {} = whiff.
    virtual std::vector<std::string>
    TutorCandidates(const GameState& s, int controller, const CardParams& pp) const = 0;

    // Hook 2 -- fetch priority: ordered land-name candidates for a fetchland.
    virtual std::vector<std::string>
    FetchCandidates(const GameState& s, int controller, const CardParams& fetch_pp) const = 0;

    // Hook 4 -- auto-fire safe alt payload: per-card predicate (the engine keeps the
    // deterministic re-scan loop; only the "is this free payload safe to fire" decision
    // is provider-owned).
    virtual bool CanAutoFireAltPayload(const GameState& s, int controller,
                                       const CardDefinition& def) const = 0;

    // Hook 6 -- dig gate + source (Treasure Hunt / Land's Edge cycling/sac-draw).
    virtual bool        HasAnyDigSource (const GameState& s) const = 0;
    virtual bool        ShouldConsiderDig(const GameState& s) const = 0;
    virtual std::string SelectDigSource (const GameState& s, const ManaPool& pool,
                                         bool& out_is_sac) const = 0;

    // Hook 7 -- how many lands to discard to a Land's Edge this activation.
    virtual int LandsEdgeFireCount(const GameState& s, int rate) const = 0;

    // Hook 8 -- whether to add an Aether Vial charge counter this upkeep.
    virtual bool WantVialCharge(const GameState& s, const Permanent& vial) const = 0;

    // Hook 9 -- scry/surveil per-card keep decision: keep `top_card` on top (true) or
    // bottom/bin it (false). The engine keeps the reorder/bin MECHANISM (ScryTop/
    // SurveilTop); only the keep DECISION is provider-owned.
    virtual bool ScryKeepOnTop(const GameState& s, const Card& top_card) const = 0;

    // Hook 9b -- Ponder-style reorder keep-vs-shuffle: a SET decision over the cards looked at
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

    // Hook 5 -- cast-sequencing: should this hand cast go in the ENABLER-FIRST pass (cast
    // + resolve before other spells, so a same-turn payload sees the enabler active)?
    // The engine keeps the multi-pass apply MECHANISM; only the partition is provider-owned.
    virtual bool CastEnablerFirst(const GameState& s, const std::string& card_name) const = 0;

    // Hook 10 -- discard-to-7 policy: when shedding to hand size, prefer discarding a LAND
    // first (true) over the highest-MV card (false). Used when a Land's Edge land outlet
    // makes lands ammunition. Required-piece protection stays engine-side.
    virtual bool DiscardLandsFirst(const GameState& s) const = 0;

    // Hook 3 -- whether to EMIT a risky alt-cost payload (Reverent Silence: free, but its
    // destroy-all-enchantments wipes the caster's own Aria/Remedy) as a searched action.
    // The engine keeps the alt-cost preconditions (alt_lifegain_cost>0 + Forest control)
    // and builds the Action; this is the wipe-vs-value gate. (The forthcoming antilife
    // refinement -- only when lethal or a Plague Drone / 2nd Remedy survives the wipe --
    // lands here.)
    virtual bool ShouldEmitRiskyAltPayload(const GameState& s, int controller,
                                           const CardDefinition& def) const = 0;

    // Hook 11 -- whether to enumerate spectacle-staging plan variants for a draw spell
    // (cast a cheap damage spell first to unlock its cheaper Spectacle cost). The engine
    // keeps the variant-building mechanism; this is the archetype gate.
    virtual bool ShouldStageSpectacleDraw(const GameState& s, int controller,
                                          const CardDefinition& draw_def) const = 0;

    // Hook 12 -- whether to CAST a "flood engine" card (Treasure Hunt's DrawUntilNonland,
    // and cascade/retrace cards like Throes of Chaos that can cascade into Treasure Hunt).
    // Firing the engine when the drawn lands cannot be used wastes them: they hit cleanup
    // discard with no Land's Edge to throw them. The engine asks this BEFORE emitting the
    // cast candidate (so the search and the bottoming rollouts both inherit the gate).
    // Generic = always cast (byte-identical); the Treasure-Hunt provider gates on having a
    // payoff this turn (Land's Edge in play, mana to cast Land's Edge after the engine, or
    // a no-max-hand-size land to keep the draw). `def` is the engine card being considered.
    virtual bool ShouldCastDrawEngine(const GameState& s, int controller,
                                      const CardDefinition& def) const = 0;

    // Hook 14 -- deck-specific extra damage toward THIS turn's lethal, BEYOND the generic
    // combat + direct-damage total the engine already sums. This is the Treasure Hunt /
    // Land's Edge model: lands in hand are Land's Edge ammunition, and a clairvoyant Treasure
    // Hunt cast this turn adds the run of lands on top of the library as further ammo. `casting`
    // lists the CardDefinitions this plan casts this turn, so the provider counts a Land's Edge
    // or Treasure Hunt being cast NOW (not only one already on the battlefield). The engine keeps
    // the generic win-check (projected attackers + direct damage + THIS addend >= opp life); only
    // the deck-specific addend is provider-owned. HasExtraLethalModel() is the cheap gate: when
    // false the engine skips building `casting` entirely (byte-identical fast path), so a deck
    // with no such model pays nothing. Generic = false / 0.
    virtual bool HasExtraLethalModel() const = 0;
    virtual int  ExtraLethalDamage(const GameState& s,
                                   const std::vector<const CardDefinition*>& casting) const = 0;

    // Hook 15 -- archetype-specific per-card VALUE for the candidate-ordering heuristic
    // (TurnSolver's EvalCard), for cards whose worth is a combo / clairvoyant assumption rather
    // than a generic single-card estimate. The Treasure Hunt provider values a Treasure Hunt
    // (clairvoyant count of lands on top of the library x Land's Edge rate) and a Land's Edge
    // (lands-in-hand x rate). `dmg_unit` is EvalCard's damage-equivalent unit. Returns true and
    // sets `out` when the provider owns this card's value; false -> EvalCard uses its generic
    // estimate (so the engine keeps every generic card type). Generic = false.
    virtual bool ArchetypeCardValue(const GameState& s, const CardDefinition& def,
                                    int dmg_unit, int& out) const = 0;

    // Hook 17 -- cast-order rank for a non-sacrifice hand cast (LOWER = cast earlier). The
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

    // Hook 18 -- candidate X values for an {X} spell (a branching-PRUNE heuristic). The engine
    // asks BEFORE emitting cast variants and emits one cast per returned value (the variants
    // share hand_index, so they are mutually exclusive in the plan), letting the search pick
    // among the narrowed set. `max_affordable` is the largest X the current mana can pay (spare
    // mana after the base cost). Generic proposes {max_affordable} (goldfish-optimal: an X burn
    // / X effect wants all available mana); MTG_UNPRUNED opens the full 1..max_affordable range
    // for the search to confirm. Empty -> the spell is not cast this turn. See analyze-deck 5f.
    virtual std::vector<int> XCandidates(const GameState& s, const CardDefinition& def,
                                         int max_affordable) const = 0;

    // Hook 16 -- combat: should this eligible creature be DECLARED as an attacker this turn?
    // The engine keeps combat eligibility (CanAttackFull: summoning sickness, tap state,
    // haste) and the damage MECHANISM; this is only the attack/hold DECISION over an
    // already-eligible attacker. Generic = true (attack with everything that can attack --
    // correct for a goldfish with no blockers). An archetype overrides to HOLD a creature
    // back (e.g. a creature whose {T} activated ability beats attacking). Honoured in
    // lockstep by the real declaration (AIEngine::DeclareAttackers) and every search-side
    // attack projection (PendingAttackDamage / prowess count / rollout combat), so an
    // override never makes the search predict an attack the executor won't make.
    virtual bool ShouldAttackWith(const GameState& s, const Permanent& attacker) const = 0;

    // Hook 13 -- which land to play AFTER a deferred draw-engine (Treasure Hunt) resolves and
    // the draw is known. Returns the NAME of a card in hand to play as the deferred land drop:
    // the Treasure-Hunt provider returns a drawn no-max-hand-size land (Reliquary Tower) when the
    // hand is flooding and no such land is already in play, so the whole draw is KEPT as Land's
    // Edge ammo (gi=65); "" means "play the best normal land via the engine's generic land-play".
    // The engine keeps the land-play MECHANISM (PlayLandByName / breakpoint land + recording) and
    // the open-land-drop precondition; only this card-choice is provider-owned. Generic = "".
    virtual std::string PostDrawKeepLandName(const GameState& s, int controller) const = 0;

    // Hook 19 -- SITUATIONAL card rank: "how much do I want THIS card on THIS turn" (HIGHER =
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

    // Hook 20 -- land-banking: among EQUAL-VALUE plans, prefer HOLDING the land drop (play no land)
    // over developing it. Burn banks spare lands once it has enough mana (its curve tops at MV 2) so
    // a future topdecked Searing Blaze has a land to play for its landfall (3-to-face instead of 1).
    // This only INVERTS the develop tiebreak in EnumeratePlansWithLand -- it never reorders plans of
    // DIFFERENT value, so on a turn that actually casts Blaze the land drop (which raises the plan's
    // value via landfall) still wins on value, not the tiebreak. Honoured identically in the search
    // and the rollout (both call EnumeratePlansWithLand). DEFAULT false -> every other deck always
    // develops (byte-identical); only BurnProvider opts in, gated on lands-in-play.
    virtual bool PreferHoldLandDrop(const GameState& s, int controller) const { return false; }

    // Hook 21 -- after a deferred Treasure Hunt (DrawUntilNonland) resolves, HOLD the still-open
    // land drop entirely rather than developing it, because the lands now in hand are the marginal
    // Land's Edge ammunition for a lethal THIS turn. Generically the engine plays the deferred drop
    // (play_drawn_flood_keep_land), but with Land's Edge in play a land in HAND is worth `rate`
    // damage this turn; playing it as the drop removes it from the ammo pool and can drop the count
    // below lethal -- and the fire-count heuristic (LandsEdgeHeuristicFireCount) then HOLDS the rest,
    // slipping the kill a full turn (the s1 gi0 T4-vs-T3 shortfall: 10 lands in hand -> play one ->
    // 9 -> no-longer-lethal -> hold -> win T4 instead of T3). Return true only when the hand is
    // ALREADY lethal ammo and developing the drop would push it BELOW lethal (the marginal case), so
    // the subsequent auto-fire discards them all for the kill. DEFAULT false -> every other deck (and
    // every non-marginal TH turn) develops the drop byte-identically. The engine keeps the land-play
    // MECHANISM and the open-drop precondition; only this hold decision is provider-owned.
    virtual bool HoldDeferredDropForLethal(const GameState& s, int controller) const { return false; }

    // Hook 22 -- NON-CLAIRVOYANT search tempo bonus (avg win-turns) for MAKING a land drop this turn.
    // The reshuffle-averaged NC search (TurnSolver::ReshuffleAvgChoosePlan) is mana-OPTIMISTIC: it
    // shuffles the true library away, so its mean future has normal land density and it undervalues a
    // land drop as screw-insurance -- it will DEFER the drop (a fetch-crack costs 1 life) and durdle in
    // a genuinely land-light game. This bonus prices developing mana back: it is subtracted from the
    // averaged win-turn of any land-drop plan before the min is taken, so it breaks decisions the
    // objective considers close WITHOUT overriding a real win-turn difference larger than the bonus.
    // DEFAULT 0.0 -> inert (only used inside the experimental MTG_NC_SEARCH path anyway). GenericProvider
    // supplies the SAFE conservative rule (small bonus, only while still building the mana base, and
    // never when PreferHoldLandDrop wants to bank/hold); archetypes that specifically benefit (mana-
    // hungry, no land-as-resource mechanic) override to be more aggressive. Land-pitch decks (Land's
    // Edge / Seismic Assault) are protected by the mana-base gate + PreferHoldLandDrop.
    virtual double NcLandDropTempoBonus(const GameState& s, int controller) const { (void)s; (void)controller; return 0.0; }

    // Hook 16 -- does this deck's goldfish opponent play lands? Decks whose spells target the
    // OPPONENT'S permanents for value (Hinata: Magma Opus taps them, the spread-damage / cost-
    // reduction targeting points at them) need a realistic opponent board. When true the engine
    // gives the passive opponent one land on each of the first three turns (a realistic floor:
    // most opponents have >=3 lands, and aggressive decks with fewer bring creatures = better
    // targets anyway). DEFAULT false -> every existing deck's opponent stays boardless-of-lands
    // (byte-identical); only HinataProvider opts in. NOT pure (defaulted) so no other provider
    // needs to implement it.
    virtual bool OpponentPlaysLands() const { return false; }

    // Hook 20 -- emit the untap-RITUAL cast variant for an {X} untap spell (Reality Spasm)?
    // The variant floats mana for a same-turn payoff and only earns its keep with Hinata's
    // discount making the {X} free, so the solver must NOT branch on it otherwise. This is the
    // archetype GATE only -- the cost math and the ManaSourceCount stay engine-side. Was an inline
    // `HinataInPlay(state)` check in TurnSolver (audit B2); default false = byte-identical for every
    // non-Hinata deck, HinataProvider returns HinataInPlay(s).
    virtual bool ShouldEmitUntapRitual(const GameState& s) const { (void)s; return false; }

    // Hook 21 -- branch on Soulfire Eruption's OWN-creature target count (0..K)? Own-targeting only
    // pays off with Hinata's per-target discount (which can enable an otherwise-unaffordable cast)
    // plus a deeper dig; without it the K+1 variants are dead weight every non-combo turn. The
    // count itself (SoulfireOwnCreatureCount) stays an engine mechanic -- this is the archetype gate
    // only. Was an inline `HinataInPlay(state)` check in TurnSolver (audit B1); default false =
    // byte-identical (K collapses to 0), HinataProvider returns HinataInPlay(s).
    virtual bool BranchSoulfireOwnTargets(const GameState& s) const { (void)s; return false; }

    // Hook 22 -- enumeration BREADTH policy: the max number of card GROUPS the plan enumerator
    // keeps for a turn (groups beyond this, lowest by SituationalCardRank, drop out). A tractability
    // cap -- a deep dig can leave ~20 distinct nonland casts whose powerset dominates the whole
    // search -- so it is provider-OWNED policy now rather than a hardcoded solver constant (audit
    // A1): a combo deck that needs wider enumeration can raise it. Default 12 = the prior generic
    // value (byte-identical; inert for any hand with <= cap groups). MTG_SOLVE_GROUP_CAP /
    // MTG_NO_GROUP_CAP / MTG_UNPRUNED still override engine-side for A/B.
    virtual int EnumGroupCap() const { return 12; }

    // Hook 23 -- fetch BREADTH policy: how many of FetchCandidates' ordered targets the search
    // branches on (the list is best-first; lower ranks are strictly worse colour, a basic ranks
    // last). Provider-OWNED (audit A2) instead of a hardcoded solver constant. Default 2 = the prior
    // generic value (byte-identical). MTG_UNPRUNED still opens the full list engine-side.
    virtual int FetchSearchCap() const { return 2; }

    // Hook 24 -- mana-source TAP ORDER: flexibility rank of a mana source (LOWER = tap earlier).
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

    // Hook 25 -- is making the OPPONENT gain life USEFUL to us right now? A Grove-of-the-Burnwillows-
    // style drip land (tap_opponent_lifegain) normally GIFTS the opponent life -- a downside -- so the
    // engine dodges the drip: a generic pip uses Grove's painless {C} mode and leftover drip lands are
    // NOT swept at end of main. A deck whose combo turns that gift into value flips this true, and the
    // two drip rules invert: Grove taps COLOURED (drips) even for a generic pip, and the sweep fires.
    // Anti-Lifegain returns true when a Tainted Remedy / Plague Drone is active (the gain is reversed
    // into 1 damage); a Grove + Punishing Fire deck would return true too (the gain buys the Fire back).
    // Default false. NOTE: the rules-level lifegain->loss reversal in OpponentGainsLife stays keyed on
    // RemedyActive -- that is a FACT about the board, not a decision, so it is not routed through here.
    virtual bool OpponentLifegainUseful(const GameState& /*s*/, int /*controller*/) const { return false; }
};
