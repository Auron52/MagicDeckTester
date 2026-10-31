// The per-subset domain opening of the colour-exact gate (MTG_DOMAIN_OPEN_EXACT) --
// docs/design/mana-completeness-oracle.md 13.7. FiveColour s1100021 gi20 T3: Overgrown Tomb, Deathrite
// Shaman (no land in any graveyard, so it makes nothing), Island and Faeburrow Elder -- domain {W,B,G}, so
// Faeburrow taps for {W}{B}{G}. Nothing on the board makes {R}, and Jared Carthalion cannot widen the domain for his own cost,
// yet the gate opened every domain bundle to all five colours: lone Jared and {Deathrite, Jared} were
// offered and the executor dropped Jared (mana-oracle PHANTOM).
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <algorithm>
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
    s.turn_number         = 3;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Forest").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    Put(s, "Overgrown Tomb", 40);
    Put(s, "Deathrite Shaman", 15);
    Put(s, "Island", 22);
    Put(s, "Faeburrow Elder", 16);
    Hand(s, "Jared Carthalion", 23);
    Hand(s, "Deathrite Shaman", 14);
    return s;
}

// A plan casting exactly `names` (as a multiset of one each).
bool Offers(const std::vector<TurnSolver::Plan>& plans, const std::vector<std::string>& names)
{
    for (const TurnSolver::Plan& p : plans)
    {
        std::vector<std::string> casts;
        for (const Action& x : p.actions)
        { if (x.kind == Action::Kind::CastFromHand) { casts.push_back(x.card_name.str()); } }
        if (casts.size() != names.size()) { continue; }
        bool all = true;
        for (const std::string& n : names)
        { if (std::find(casts.begin(), casts.end(), n) == casts.end()) { all = false; break; } }
        if (all) { return true; }
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

TEST_CASE("Domain open exact: Jared cannot pay his own {R} off a {W,B,G} Faeburrow")
{
    EnsureCardsLoaded();
    const GameState s = Board();
    {
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
        CHECK_FALSE(Offers(plans, { "Jared Carthalion" }));
        CHECK_FALSE(Offers(plans, { "Jared Carthalion", "Deathrite Shaman" }));
        CHECK(Offers(plans, { "Deathrite Shaman" }));
    }
    // The lever off: the all-five bundle admits both (the PHANTOMs).
    ArmPin off(heurarm::DOMAIN_OPEN_EXACT, false);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    CHECK(Offers(plans, { "Jared Carthalion" }));
}
