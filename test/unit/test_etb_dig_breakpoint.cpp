// MTG_BP_ETB_DIG (2026-09-26, the Pirates D6 review): an ETB library dig must open a breakpoint so the
// dug card is castable in the phase that dug it. Before the fix ParamKeyedDrawClass CLAIMED
// etb_dig_count, so the general put-in-hand rule (site 10) stood down, while no param-keyed site ever
// armed -- the dug card waited a turn at every searched budget (Pirates s777011 gi11: T5, vs T4 for
// the Crewmate -> dig Buccaneer -> cast Buccaneer line).
#include <doctest/doctest.h>

#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>

namespace
{

void EnsureCardsEd()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefEd(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardEd(const std::string& name, int number)
{
    Card c = DefEd(name).card;
    c.m_number = number;
    return c;
}

void PutEd(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = CardEd(name, number);
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

bool OnBattlefield(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return true; } }
    return false;
}

}   // namespace

TEST_CASE("ETB dig is NOT claimed by a param-keyed class, so site 10 arms after it")
{
    EnsureCardsEd();
    GameState s;
    CHECK_FALSE(TurnSolver::ParamKeyedDrawClass(s, DefEd("Staunch Crewmate")));
}

TEST_CASE("Staunch Crewmate digs Daring Buccaneer and the leftover {R} casts it the SAME turn")
{
    EnsureCardsEd();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;   // no land drop left: the mana is exactly {U}{U}{R}
    PutEd(s, "Island", 10);
    PutEd(s, "Island", 11);
    PutEd(s, "Mountain", 12);
    s.players[0].hand.push_back(CardEd("Staunch Crewmate", 20));
    s.players[0].hand.push_back(CardEd("Dire Fleet Captain", 21));   // {B}{R}: uncastable, the reveal
    s.players[0].library.push_back(CardEd("Daring Buccaneer", 30));  // the only Pirate in the top 4
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(CardEd("Mountain", 40 + k)); }

    const TurnSolver::Plan plan = TurnSolver::Solve(s, /*is_pre_combat=*/true);
    TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);

    CHECK(OnBattlefield(s, "Staunch Crewmate"));
    CHECK(OnBattlefield(s, "Daring Buccaneer"));   // was: still in hand until next turn
}
