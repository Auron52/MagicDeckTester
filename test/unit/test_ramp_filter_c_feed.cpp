// The RAMP-FILTER {C} FEED relabel (MTG_RAMP_FILTER_C_FEED) -- docs/design/mana-completeness-oracle.md
// 13.5, the NivMizzetCombo "ramp filter fed by {C}" row. A fed ramp filter (Izzet Signet: "{1}, {T}: add
// {U}{R}") is credited "+1 wild", which assumes the unit that feeds its {1} was coloured. NivMizzetCombo
// s1100028 gi27 T6 (reduced): three Islands, two Mountains, Izzet Signet and Generator Servant ({T}, sac:
// {C}{C}). The model reads 6 coloured + {C}{C}; feeding one {C} into the Signet really yields 7 coloured +
// {C}, which pays Niv-Mizzet, Parun ({U}{U}{U}{R}{R}{R}) + Brainstorm ({U}).
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void Put(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

void Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

GameState Board()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Island").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    Put(s, "Island", 1);
    Put(s, "Island", 2);
    Put(s, "Island", 3);
    Put(s, "Mountain", 4);
    Put(s, "Mountain", 5);
    Put(s, "Izzet Signet", 6);
    Put(s, "Generator Servant", 7);
    Hand(s, "Niv-Mizzet, Parun", 10);
    Hand(s, "Brainstorm", 11);
    return s;
}

bool OffersBoth(const std::vector<TurnSolver::Plan>& plans)
{
    for (const TurnSolver::Plan& p : plans)
    {
        bool niv = false, bs = false;
        for (const Action& x : p.actions)
        {
            if (x.kind != Action::Kind::CastFromHand) { continue; }
            if (x.card_name.str() == "Niv-Mizzet, Parun") { niv = true; }
            if (x.card_name.str() == "Brainstorm") { bs = true; }
        }
        if (niv && bs) { return true; }
    }
    return false;
}

struct ArmPin
{
    heurarm::Slot slot;
    std::int8_t   prev;
    ArmPin(heurarm::Slot s_, bool on) : slot(s_), prev(heurarm::t_arm[s_]) { heurarm::t_arm[s_] = on ? 1 : 0; }
    ~ArmPin() { heurarm::t_arm[slot] = prev; }
};

}   // namespace

TEST_CASE("Ramp filter {C} feed: {Niv-Mizzet, Brainstorm} is offered when a {C} feeds Izzet Signet")
{
    EnsureCardsLoaded();
    const GameState s = Board();
    CHECK(OffersBoth(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true)));

    // The lever off: the Signet's feed is assumed coloured, so the pool reads 6 coloured and refuses it.
    ArmPin off(heurarm::RAMP_FILTER_C_FEED, false);
    CHECK_FALSE(OffersBoth(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true)));
}
