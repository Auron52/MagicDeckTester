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
