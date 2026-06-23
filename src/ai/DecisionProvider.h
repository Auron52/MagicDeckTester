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
};
