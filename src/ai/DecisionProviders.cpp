#include "DecisionProviders.h"

#include "../core/SpellEffects.h"   // the shared free functions Generic forwards to
#include "../deck/DeckLoader.h"     // Decklist

// Stage 0: every GenericProvider hook forwards to the existing global free function, so
// behavior is byte-identical to pre-refactor. Later stages move the bodies in here. The
// `::` qualifier avoids recursing into the member of the same name.

std::vector<std::string>
GenericProvider::TutorCandidates(const GameState& s, int controller, const CardParams& pp) const
{
    return ::TutorCandidates(s, controller, pp);
}

std::vector<std::string>
GenericProvider::FetchCandidates(const GameState& s, int controller, const CardParams& fetch_pp) const
{
    return ::FetchCandidates(s, controller, fetch_pp);
}

bool GenericProvider::CanAutoFireAltPayload(const GameState& s, int controller,
                                            const CardDefinition& def) const
{
    return ::CanAutoFireAltPayload(s, controller, def);
}

bool GenericProvider::HasAnyDigSource(const GameState& s) const
{
    return ::HasAnyDigSource(s);
}

bool GenericProvider::ShouldConsiderDig(const GameState& s) const
{
    return ::ShouldConsiderDig(s);
}

std::string GenericProvider::SelectDigSource(const GameState& s, const ManaPool& pool,
                                             bool& out_is_sac) const
{
    return ::SelectDigSource(s, pool, out_is_sac);
}

int GenericProvider::LandsEdgeFireCount(const GameState& s, int rate) const
{
    return ::LandsEdgeHeuristicFireCount(s, rate);
}

bool GenericProvider::WantVialCharge(const GameState& s, const Permanent& vial) const
{
    return ::WantVialCharge(s, vial);
}

bool GenericProvider::ScryKeepOnTop(const GameState& s, const Card& top_card) const
{
    // Reproduces the inline keep heuristic shared by ScryTop/SurveilTop: keep nonland
    // spells always (combo pieces); keep a land only while it still helps -- a
    // DrawUntilNonland (Treasure Hunt) in hand wants land fuel, or fewer than two lands
    // are in play -- otherwise bottom/bin it to dig toward action.
    const CardDefinition* tdef = CardDatabase::Instance().LookupCached(top_card);
    bool is_land = tdef ? tdef->card.IsLand() : top_card.IsLand();
    if (!is_land) { return true; }

    const Player& ap = s.players[s.active_player_index];
    for (const Card& c : ap.hand)
    {
        const CardDefinition* cdef = CardDatabase::Instance().LookupCached(c);
        if (cdef && cdef->tmpl == CardTemplate::DrawUntilNonland) { return true; }
    }
    int lands_in_play = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index == s.active_player_index && p.card.IsLand()) { ++lands_in_play; }
    }
    return lands_in_play < 2;
}

bool GenericProvider::CastEnablerFirst(const GameState& /*s*/, const std::string& card_name) const
{
    // Reproduces the enabler-first partition: lifegain_to_loss cards (Tainted Remedy /
    // Plague Drone) cast + resolve before other spells so a same-turn payload sees the
    // enabler active. No-op for decks without such cards.
    return ::IsLifegainToLossCard(card_name);
}

bool GenericProvider::DiscardLandsFirst(const GameState& s) const
{
    // Reproduces the has_land_outlet scan: a Land's Edge land outlet (discard_land_damage)
    // in hand or in play makes lands ammunition -> shed a land before the highest-MV card.
    const Player& ap = s.players[s.active_player_index];
    for (const Card& c : ap.hand)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (def && def->params.discard_land_damage > 0) { return true; }
    }
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != s.active_player_index) { continue; }
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        if (def && def->params.discard_land_damage > 0) { return true; }
    }
    return false;
}

bool GenericProvider::ShouldEmitRiskyAltPayload(const GameState& s, int controller,
                                                const CardDefinition& def) const
{
    // Reproduces the inline gate: emit the risky alt only for a destroy-all-enchantments
    // payload while a Remedy is active (so the 6 face damage is real). The cheap
    // preconditions (alt_lifegain_cost>0 + Forest control) stay at the call site.
    return def.params.destroy_all_enchantments && ::RemedyActive(s, controller);
}

bool GenericProvider::ShouldStageSpectacleDraw(const GameState& /*s*/, int /*controller*/,
                                               const CardDefinition& draw_def) const
{
    // Reproduces the inline gate: a draw spell with a Spectacle cost can be staged behind
    // a cheap damage spell to unlock the cheaper cost.
    return draw_def.params.spectacle_cost.has_value();
}

namespace
{
    // Stateless, read-only -> a single shared const instance is thread-safe (same model
    // as CardDatabase). Process lifetime, so GameState's raw pointer stays valid.
    const GenericProvider g_generic;
}

const DecisionProvider& DefaultProvider()
{
    return g_generic;
}

const DecisionProvider& SelectDecisionProvider(const Decklist& /*deck*/)
{
    // Stage 0: all decks -> Generic (byte-identical). Archetype detection lands in
    // Stage 6 (scan deck.mainboard params, like GoldFishRunner::DeckUsesSecondMain).
    return g_generic;
}
